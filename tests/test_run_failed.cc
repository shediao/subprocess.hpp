#include <gtest/gtest.h>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

TEST(SubprocessTest, CommandNotFound) {
  ASSERT_EQ(127, run("this_command_not_found_in_paths"));
}

TEST(SubprocessTest, CommandNotExists) {
#if defined(_WIN32)
  ASSERT_EQ(127, run(R"(C:\path\to\this_command_not_exists.exe)"));
#else
  ASSERT_EQ(127, run("/path/to/this_command_not_exists"));
#endif
}

TEST(SubprocessTest, NoPermission) {
  TempFile temp;
#if defined(_WIN32)
  std::string content = R"(@echo off
exit /b 0
)";
  temp.write(content);
  ASSERT_EQ(127, run(temp.path()));
#else
  std::string content = R"(#!/bin/bash
exit 0
)";
  temp.write(content);
#if defined(__CYGWIN__) || defined(__MSYS__)
  ASSERT_EQ(0, run(temp.path()));
#else
  ASSERT_EQ(127, run(temp.path()));
#endif
#endif
}
