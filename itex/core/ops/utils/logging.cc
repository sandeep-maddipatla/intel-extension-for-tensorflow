/* Copyright (c) 2021-2022 Intel Corporation

Copyright 2015 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "itex/core/ops/utils/logging.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>

#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/sysinfo.h"
#include "itex/core/ops/utils/env_time.h"
#include "itex/core/ops/utils/macros.h"

namespace itex {
namespace internal {
namespace {

int ParseInteger(const char* str, size_t size) {
  // Ideally we would use env_var / safe_strto64, but it is
  // hard to use here without pulling in a lot of dependencies,
  // so we use std:istringstream instead
  string integer_str(str, size);
  int level = 0;
  level = std::stoi(integer_str);
  return level;
}

// Parse log level (int64) from environment variable (char*)
int64 LogLevelStrToInt(const char* tf_env_var_val) {
  if (tf_env_var_val == nullptr) {
    return 0;
  }
  return ParseInteger(tf_env_var_val, strlen(tf_env_var_val));
}

// Using StringPiece breaks Windows build.
struct StringData {
  struct Hasher {
    size_t operator()(const StringData& sdata) const {
      // For dependency reasons, we cannot use hash.h here. Use DBJHash instead.
      size_t hash = 5381;
      const char* data = sdata.data;
      for (const char* top = data + sdata.size; data < top; ++data) {
        hash = ((hash << 5) + hash) + (*data);
      }
      return hash;
    }
  };

  StringData() = default;
  StringData(const char* data, size_t size) : data(data), size(size) {}

  bool operator==(const StringData& rhs) const {
    return size == rhs.size && memcmp(data, rhs.data, size) == 0;
  }

  const char* data = nullptr;
  size_t size = 0;
};

using VmoduleMap = std::unordered_map<StringData, int, StringData::Hasher>;

// Returns a mapping from module name to ITEX_VLOG level, derived from the
// TF_CPP_VMODULE environment variable; ownership is transferred to the caller.
VmoduleMap* VmodulesMapFromEnv() {
  // The value of the env var is supposed to be of the form:
  //    "foo=1,bar=2,baz=3"
  const char* env = getenv("TF_CPP_VMODULE");
  if (env == nullptr) {
    // If there is no TF_CPP_VMODULE configuration (most common case), return
    // nullptr so that the ShouldVlogModule() API can fast bail out of it.
    return nullptr;
  }
  // The memory returned by getenv() can be invalidated by following getenv() or
  // setenv() calls. And since we keep references to it in the VmoduleMap in
  // form of StringData objects, make a copy of it.
  const char* env_data = strdup(env);
  ITEX_CHECK(env_data != nullptr);
  char* original_addr = const_cast<char*>(env_data);
  VmoduleMap* result = new VmoduleMap();
  while (true) {
    const char* eq = strchr(env_data, '=');
    if (eq == nullptr) {
      break;
    }
    const char* after_eq = eq + 1;

    // Comma either points at the next comma delimiter, or at a null terminator.
    // We check that the integer we parse ends at this delimiter.
    const char* comma = strchr(after_eq, ',');
    const char* new_env_data;
    if (comma == nullptr) {
      comma = strchr(after_eq, '\0');
      new_env_data = comma;
    } else {
      new_env_data = comma + 1;
    }
    (*result)[StringData(env_data, eq - env_data)] =
        ParseInteger(after_eq, comma - after_eq);
    env_data = new_env_data;
  }

  // Reference is not created if the orignal ptr address is not changed, then
  // it can be deleted safely. Otherwise the reference is stored in VMODULE.
  if (env_data == original_addr) {
    free(original_addr);
  }

  return result;
}

bool EmitThreadIdFromEnv() {
  const char* tf_env_var_val = getenv("TF_CPP_LOG_THREAD_ID");
  return tf_env_var_val == nullptr
             ? false
             : ParseInteger(tf_env_var_val, strlen(tf_env_var_val)) != 0;
}

}  // namespace

int64 MinLogLevelFromEnv() {
  // We don't want to print logs during fuzzing as that would slow fuzzing down
  // by almost 2x. So, if we are in fuzzing mode (not just running a test), we
  // return a value so that nothing is actually printed. Since ITEX_LOG uses >=
  // (see ~LogMessage in this file) to see if log messages need to be printed,
  // the value we're interested on to disable printing is the maximum severity.
  // See also http://llvm.org/docs/LibFuzzer.html#fuzzer-friendly-build-mode
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  return itex::NUM_SEVERITIES;
#else
  const char* itex_env_var_val = getenv("ITEX_CPP_MIN_LOG_LEVEL");
  return LogLevelStrToInt(itex_env_var_val);
#endif
}

int64 MinIssueLogLevel() {
  return std::max(MinLogLevelFromEnv(), static_cast<int64>(itex::ERROR));
}

int64 MinVLogLevelFromEnv() {
  // We don't want to print logs during fuzzing as that would slow fuzzing down
  // by almost 2x. So, if we are in fuzzing mode (not just running a test), we
  // return a value so that nothing is actually printed. Since ITEX_VLOG uses <=
  // (see ITEX_VLOG_IS_ON in logging.h) to see if log messages need to be
  // printed, the value we're interested on to disable printing is 0. See also
  // http://llvm.org/docs/LibFuzzer.html#fuzzer-friendly-build-mode
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  return 0;
#else
  // Follow below priority to log ITEX verbose:
  // 1. Check `ITEX_VERBOSE` first
  // 2. Check TF setting `TF_CPP_MAX_VLOG_LEVEL` if #1 is not set
  const char* itex_env_var_val = getenv("ITEX_VERBOSE");
  if (itex_env_var_val != nullptr) return LogLevelStrToInt(itex_env_var_val);

  itex_env_var_val = getenv("TF_CPP_MAX_VLOG_LEVEL");
  return LogLevelStrToInt(itex_env_var_val);
#endif
}

LogMessage::LogMessage(const char* fname, int line, int severity)
    : fname_(fname), line_(line), severity_(severity) {}

LogMessage& LogMessage::AtLocation(const char* fname, int line) {
  fname_ = fname;
  line_ = line;
  return *this;
}

LogMessage::~LogMessage() {
  // Read the min log level once during the first call to logging.
  // TODO(itex): Temporarily ignore TF min log limitation, will fix later
  //             after figured out where the limiation is.
  static int64 min_log_level = MinLogLevelFromEnv();
  if (severity_ >= min_log_level) {
    GenerateLogMessage();
  }
}

void LogMessage::GenerateLogMessage() {
  static bool log_thread_id = EmitThreadIdFromEnv();
  uint64 now_micros = EnvTime::NowMicros();
  time_t now_seconds = static_cast<time_t>(now_micros / 1000000);
  int32 micros_remainder = static_cast<int32>(now_micros % 1000000);
  constexpr size_t kTimeBufferSize = 30;
  char time_buffer[kTimeBufferSize];
  strftime(time_buffer, kTimeBufferSize, "%Y-%m-%d %H:%M:%S",
           localtime(&now_seconds));
  const size_t tid_buffer_size = 10;
  char tid_buffer[tid_buffer_size] = "";
  if (log_thread_id) {
    ITEX_CHECK(snprintf(tid_buffer, sizeof(tid_buffer), " %7u",
                        absl::base_internal::GetTID()) >= 0)
        << "Encoding error occurs";
  }
  // TODO(jeff,sanjay): Replace this with something that logs through the env.
  fprintf(stderr, "%s.%06d: %c%s %s:%d] %s\n", time_buffer, micros_remainder,
          "IWEF"[severity_], tid_buffer, fname_, line_, str().c_str());
  IssueLink();
}

void LogMessage::IssueLink() {
  static int64 issue_log_level = MinIssueLogLevel();

  if (severity_ >= issue_log_level) {
    fprintf(stderr,
            "If you need help, create an issue at "
            "https://github.com/intel/intel-extension-for-tensorflow/issues\n");
  }
}

int64 LogMessage::MinVLogLevel() {
  static int64 min_vlog_level = MinVLogLevelFromEnv();
  return min_vlog_level;
}

bool LogMessage::VmoduleActivated(const char* fname, int level) {
  if (level <= MinVLogLevel()) {
    return true;
  }
  static VmoduleMap* vmodules = VmodulesMapFromEnv();
  if (ITEX_PREDICT_TRUE(vmodules == nullptr)) {
    return false;
  }
  const char* last_slash = strrchr(fname, '/');
  const char* module_start = last_slash == nullptr ? fname : last_slash + 1;
  const char* dot_after = strchr(module_start, '.');
  const char* module_limit =
      dot_after == nullptr ? strchr(fname, '\0') : dot_after;
  StringData module(module_start, module_limit - module_start);
  auto it = vmodules->find(module);
  return it != vmodules->end() && it->second >= level;
}

LogMessageFatal::LogMessageFatal(const char* file, int line)
    : LogMessage(file, line, FATAL) {}
LogMessageFatal::~LogMessageFatal() {
  // abort() ensures we don't return (we promised we would not via
  // ATTRIBUTE_NORETURN).
  GenerateLogMessage();
  abort();
}

void LogString(const char* fname, int line, int severity,
               const string& message) {
  LogMessage(fname, line, severity) << message;
}

template <>
void MakeCheckOpValueString(std::ostream* os, const char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "char value " << static_cast<int16>(v);
  }
}

template <>
void MakeCheckOpValueString(
    std::ostream* os, const signed char& v) {  // NOLINT(runtime/references)
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "signed char value " << static_cast<int16>(v);
  }
}

template <>
void MakeCheckOpValueString(
    std::ostream* os, const unsigned char& v) {  // NOLINT(runtime/references)
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "unsigned char value " << static_cast<uint16>(v);
  }
}

#if LANG_CXX11
template <>
void MakeCheckOpValueString(std::ostream* os, const std::nullptr_t& v) {
  (*os) << "nullptr";
}
#endif

CheckOpMessageBuilder::CheckOpMessageBuilder(const char* exprtext)
    : stream_(new std::ostringstream) {
  *stream_ << "Check failed: " << exprtext << " (";
}

CheckOpMessageBuilder::~CheckOpMessageBuilder() { delete stream_; }

std::ostream* CheckOpMessageBuilder::ForVar2() {
  *stream_ << " vs. ";
  return stream_;
}

string* CheckOpMessageBuilder::NewString() {
  *stream_ << ")";
  return new string(stream_->str());
}

namespace {
// The following code behaves like AtomicStatsCounter::LossyAdd() for
// speed since it is fine to lose occasional updates.
// Returns old value of *counter.
uint32 LossyIncrement(std::atomic<uint32>* counter) {
  const uint32 value = counter->load(std::memory_order_relaxed);
  counter->store(value + 1, std::memory_order_relaxed);
  return value;
}
}  // namespace

bool LogEveryNState::ShouldLog(int n) {
  return n != 0 && (LossyIncrement(&counter_) % n) == 0;
}

bool LogFirstNState::ShouldLog(int n) {
  const int counter_value =
      static_cast<int>(counter_.load(std::memory_order_relaxed));
  if (counter_value < n) {
    counter_.store(counter_value + 1, std::memory_order_relaxed);
    return true;
  }
  return false;
}

bool LogEveryPow2State::ShouldLog(int ignored) {
  const uint32 new_value = LossyIncrement(&counter_) + 1;
  return (new_value & (new_value - 1)) == 0;
}

bool LogEveryNSecState::ShouldLog(double seconds) {
  LossyIncrement(&counter_);
  const int64 now_cycles = absl::base_internal::CycleClock::Now();
  int64 next_cycles = next_log_time_cycles_.load(std::memory_order_relaxed);
  do {
    if (now_cycles <= next_cycles) return false;
  } while (!next_log_time_cycles_.compare_exchange_weak(
      next_cycles,
      now_cycles + seconds * absl::base_internal::CycleClock::Frequency(),
      std::memory_order_relaxed, std::memory_order_relaxed));
  return true;
}

}  // namespace internal
}  // namespace itex
