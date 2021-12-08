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

#pragma once

#include <string.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <type_traits>

namespace facebook {
namespace profilo {

int64_t monotonicTime();

int32_t threadID();

// Returns 0 if value was not found, and 1 if value <= 1, actual value otherwise
int32_t systemClockTickIntervalMs();

// Determines the kernel jiffy value and returns it's value in microseconds.
// Returns -1 if unable to determine the actual value.
int32_t cpuClockResolutionMicros();

std::string get_system_property(const char* key);

// Given a path, create the directory specified by it, along with all
// intermediate directories
void mkdirs(char const* dir);

// Custom parse for unsinged long values, ignores minus sign and skips blank
// spaces in front. Such narrowly specialized method is faster than the standard
// strtoull.
uint64_t parse_ull(char* str, char** end);

} // namespace profilo
} // namespace facebook
