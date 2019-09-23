/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include <string>
#include <tuple>
#include <vector>

#include <android-base/logging.h>
#include <gtest/gtest.h>

#include "Color.h"
#include "NanoTime.h"
#include "Test.h"

namespace android {
namespace gtest_extras {

Test::Test(std::tuple<std::string, std::string>& test, size_t index, size_t run_index, int fd)
    : suite_name_(std::get<0>(test)),
      test_name_(std::get<1>(test)),
      name_(suite_name_ + test_name_),
      test_index_(index),
      run_index_(run_index),
      fd_(fd),
      start_ns_(NanoTime()) {}

void Test::Stop() {
  end_ns_ = NanoTime();
}

void Test::CloseFd() {
  fd_.reset();
}

void Test::Print() {
  ColoredPrintf(COLOR_GREEN, "[ RUN      ]");
  printf(" %s\n", name_.c_str());
  printf("%s", output_.c_str());

  switch (result_) {
    case TEST_PASS:
    case TEST_XFAIL:
      ColoredPrintf(COLOR_GREEN, "[       OK ]");
      break;
    case TEST_SKIPPED:
      ColoredPrintf(COLOR_GREEN, "[  SKIPPED ]");
      break;
    default:
      ColoredPrintf(COLOR_RED, "[  FAILED  ]");
      break;
  }
  printf(" %s", name_.c_str());
  if (::testing::GTEST_FLAG(print_time)) {
    printf(" (%" PRId64 " ms)", RunTimeNs() / kNsPerMs);
  }
  printf("\n");
  fflush(stdout);
}

bool Test::Read() {
  char buffer[2048];
  ssize_t bytes = TEMP_FAILURE_RETRY(read(fd_, buffer, sizeof(buffer) - 1));
  if (bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Reading would block. Since this is not an error keep going.
      return true;
    }
    PLOG(FATAL) << "Unexpected failure from read";
    return false;
  }

  if (bytes == 0) {
    return false;
  }
  buffer[bytes] = '\0';
  output_ += buffer;
  return true;
}

void Test::ReadUntilClosed() {
  uint64_t start_ns = NanoTime();
  while (fd_ != -1) {
    if (!Read()) {
      CloseFd();
      break;
    }
    if (NanoTime() - start_ns > 2 * kNsPerS) {
      printf("Reading of done process did not finish after 2 seconds.\n");
      CloseFd();
      break;
    }
  }
}

void Test::SetResultFromOutput() {
  result_ = TEST_PASS;

  // Need to parse the output to determine if this test was skipped.
  // Format of a skipped test:
  //   <filename>:(<line_number>) Failure in test <testname>
  //   Skipped
  //   <Skip Message>

  // There can be multiple skip messages, so remove all of them.
  size_t start_index = 0;
  while (true) {
    size_t skipped_index = output_.find("\nSkipped\n", start_index);
    if (skipped_index == std::string::npos) {
      return;
    }
    if (skipped_index == 0) {
      // The output starts with Skipped, so skip over it and keep looking.
      start_index = skipped_index + 9;
      continue;
    }
    // Look backwards for start of line before "Skipped" message.
    size_t failure_line_start = output_.rfind('\n', skipped_index - 1);
    if (failure_line_start == std::string::npos) {
      failure_line_start = 0;
    }
    skipped_index += 9;
    size_t failure_index = output_.find(" Failure in test ", failure_line_start);
    if (failure_index == std::string::npos || failure_index > skipped_index) {
      // Could still be another skipped message matching the pattern after
      // this one.
      start_index = skipped_index - 1;
      continue;
    }
    start_index = 0;
    result_ = TEST_SKIPPED;
    if (failure_line_start != 0) {
      output_ = output_.substr(0, failure_line_start + 1) + output_.substr(skipped_index);
    } else {
      output_ = output_.substr(skipped_index);
    }
  }
}

}  // namespace gtest_extras
}  // namespace android
