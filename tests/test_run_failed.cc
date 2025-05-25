#include <gtest/gtest.h>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;

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
#if 0
  std::string content = R"(@echo off
echo 123
)";
  temp.write(content);
  ASSERT_EQ(127, run(temp.path()));
#endif
#else
  std::string content = R"(#!/bin/bash
echo 123
)";
  temp.write(content);
  ASSERT_EQ(127, run(temp.path()));
#endif
}
