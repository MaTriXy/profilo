/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_BASE_SYSTRACE_H_
#define ART_RUNTIME_BASE_SYSTRACE_H_

#define ATRACE_TAG ATRACE_TAG_DALVIK
//#include <cutils/trace.h>
#include <museum/7.0.0/external/libcxx/string>
//#include <utils/Trace.h>

namespace facebook { namespace museum { namespace MUSEUM_VERSION { namespace art {

class ScopedTrace {
 public:
  explicit ScopedTrace(const char* name) {
    //ATRACE_BEGIN(name);
  }

  explicit ScopedTrace(const std::string& name) : ScopedTrace(name.c_str()) {}

  ~ScopedTrace() {
    //ATRACE_END();
  }
};

} } } } // namespace facebook::museum::MUSEUM_VERSION::art

#endif  // ART_RUNTIME_BASE_SYSTRACE_H_
