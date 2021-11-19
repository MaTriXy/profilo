/**
 * Copyright 2004-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ThreadTimer.h"

#include <errno.h>
#include <signal.h>

#include <fb/log.h>
#include <profilo/util/common.h>
#include <random>
#include <stdexcept>
#include <system_error>

namespace facebook {
namespace profilo {
namespace profiler {

namespace {
constexpr auto kNanosecondsInMicrosecond = 1000;
constexpr auto kMicrosecondsInSecond = 1000 * 1000;
constexpr auto kMicrosecondsInMillisecond = 1000;

// --- thread-specific timer wrappers

// getCpuClockIdFromTid - obtain the thread-specific clock_id for a kernel tid.
//
// Notes:
// Reasons we can't rely on bionic variants of:
// - clock_getcpuclockid():
//   On 5.x and older: it is not available.
//   On 6.x and newer: it fails with ESRCH. Under the hood, this happens
//   because clock_getcpuclockid() only succeeds if the clock_id THREAD bit is
//   set (non main threads).
// - pthread_getcpuclockid():
//   We don't have pthread_ids when discovering existing threads via /proc.
//   On OS 4.1-4.3, due to a bug in Bionic, value generated by
//   pthread_getcpuclockid() is wrong
//
// So we have to roll our own implementation based on pthread_getcpuclockid and
// clock_getcpuclockid.
clockid_t getCpuClockIdFromTid(pid_t tid) {
  clockid_t result;
  result = ~static_cast<clockid_t>(tid) << 3;

  // Bits 0 and 1: (0 = CPUCLOCK_PROF, 1 = CPUCLOCK_VIRT, 2 = CPUCLOCK_SCHED)
  // result |= 0; // CPUCLOCK_PROF also seems to work
  // result |= 1; // CPUCLOCK_VIRT also seems to work
  result |= 2; // CPUCLOCK_SCHED, per pthread_getcpuclockid

  // Bit 2: (1 = THREAD, 0 = PROCESS)
  // result |= (0 << 2); // clock_getcpuclockid() sets this to 0, but it fails
  result |= (1 << 2); // pthread_getcpuclockid() sets this to 1
  return result;
}

bool createThreadTimer(
    pid_t ktid,
    timer_t* timerId,
    bool wallClockModeEnabled) {
  clockid_t clockid =
      wallClockModeEnabled ? CLOCK_MONOTONIC : getCpuClockIdFromTid(ktid);
  struct sigevent sigev;
  sigev.sigev_notify = SIGEV_THREAD_ID;
  sigev.sigev_signo = SIGPROF;
  sigev._sigev_un._tid = ktid; /* ID of kernel thread to signal */
  sigev.sigev_value.sival_int = ThreadTimer::encodeType(
      wallClockModeEnabled ? ThreadTimer::Type::WallTime
                           : ThreadTimer::Type::CpuTime);
  if (timer_create(clockid, &sigev, timerId) != 0) {
    return false;
  }
  return true;
}

bool startThreadTimer(timer_t timerId, int samplingRateMs) {
  itimerval tv = getInitialItimerval(samplingRateMs);
  struct itimerspec itimer;
  itimer.it_interval.tv_sec = tv.it_interval.tv_sec;
  itimer.it_interval.tv_nsec =
      tv.it_interval.tv_usec * kNanosecondsInMicrosecond;
  itimer.it_value.tv_sec = tv.it_value.tv_sec;
  itimer.it_value.tv_nsec = tv.it_value.tv_usec * kNanosecondsInMicrosecond;
  if (timer_settime(timerId, 0, &itimer, NULL) != 0) {
    return false;
  }
  return true;
}

void deleteThreadTimer(timer_t timerId) {
  if (timer_delete(timerId) == -1) {
    FBLOGV("INFO: Cannot delete profiling timer: %s", strerror(errno));
  }
}
} // namespace

// get timer repeat interval and initial offset
itimerval getInitialItimerval(int samplingRateMs) {
  auto sampleRateMicros = samplingRateMs * kMicrosecondsInMillisecond;
  // Generate random initial delay. Used to calculate the initial trace delay
  // to avoid sampling bias.
  // Narrowing cast is acceptable, the lower bits should have
  // all the entropy anyway.
  std::mt19937 randGenerator(static_cast<uint32_t>(monotonicTime()));
  std::uniform_int_distribution<> randDistribution(1, sampleRateMicros);
  auto sampleStartDelayMicros = randDistribution(randGenerator);

  int32_t sampleStartDelaySeconds = 0;
  int32_t sampleRateSeconds = 0;

  if (sampleStartDelayMicros >= kMicrosecondsInSecond) {
    sampleStartDelaySeconds = sampleStartDelayMicros / kMicrosecondsInSecond;
    sampleStartDelayMicros = sampleStartDelayMicros % kMicrosecondsInSecond;
  }

  if (sampleRateMicros >= kMicrosecondsInSecond) {
    sampleRateSeconds = sampleRateMicros / kMicrosecondsInSecond;
    sampleRateMicros = sampleRateMicros % kMicrosecondsInSecond;
  }

  itimerval tv{};
  tv.it_value.tv_sec = sampleStartDelaySeconds;
  tv.it_value.tv_usec = sampleStartDelayMicros;
  tv.it_interval.tv_sec = sampleRateSeconds;
  tv.it_interval.tv_usec = sampleRateMicros;
  return tv;
}

ThreadTimer::ThreadTimer(int32_t tid, int samplingRateMs, Type timerType)
    : tid_(tid), samplingRateMs_(samplingRateMs), timerType_(timerType) {
  if (!createThreadTimer(tid_, &timerId_, timerType == Type::WallTime)) {
    // e.g. tid died
    throw std::system_error(errno, std::system_category(), "createThreadTimer");
  }
  if (!startThreadTimer(timerId_, samplingRateMs_)) {
    // e.g. tid died
    throw std::system_error(errno, std::system_category(), "startThreadTimer");
  }
}

ThreadTimer::~ThreadTimer() {
  if (timerId_ == INVALID_TIMER_ID) {
    // Expected when creating new ThreadTimer objects
    return;
  }
  deleteThreadTimer(timerId_);
}

long ThreadTimer::typeSeed = 0;

ThreadTimer::Type ThreadTimer::decodeType(long salted) {
  auto type = static_cast<Type>(salted ^ typeSeed);
  if (type != Type::CpuTime && type != Type::WallTime) {
    throw std::runtime_error("invalid timer type");
  }
  return type;
}

long ThreadTimer::encodeType(Type type) {
  while (typeSeed == 0) {
    typeSeed = rand();
  }
  return static_cast<long>(type) ^ typeSeed;
}

} // namespace profiler
} // namespace profilo
} // namespace facebook
