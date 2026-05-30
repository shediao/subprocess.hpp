/**
 * test_basic.cc — Basic subprocess::run() functionality tests
 *
 * Covers:
 *   - Simple command execution (true/false)
 *   - Exit code propagation (0..126, signal-induced codes)
 *   - Command-not-found / no-permission error paths
 *   - Multi-argument commands
 *   - Commands resolved from system PATH
 *   - Trivial smoke test (stdout-to-fd-2)
 */

#include <gtest/gtest.h>

#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

// ===========================================================================
// Trivial smoke test — verifies the header compiles and basic run() works
// ===========================================================================
TEST(BasicTest, SmokeTest) {
  int exit_code = run(
#if defined(_WIN32)
      "cmd.exe", "/c", "exit /b 0"
#else
      "true"
#endif
  );
  ASSERT_EQ(exit_code, 0);
}

// ===========================================================================
// Simple command execution — true / false
// ===========================================================================
TEST(BasicTest, RunTrueCommand) {
  int exit_code = run(
#if defined(_WIN32)
      "cmd.exe", "/c", "exit /b 0"
#else
      "true"
#endif
  );
  ASSERT_EQ(exit_code, 0);
}

TEST(BasicTest, RunFalseCommand) {
#if !defined(_WIN32)
  int exit_code = run("false");
  ASSERT_NE(exit_code, 0);  // Typically 1
#else
  int exit_code = run("cmd.exe", "/c", "exit /b 1");
  ASSERT_EQ(exit_code, 1);
#endif
}

// ===========================================================================
// Exit code propagation — full range 0..126, plus signal-induced codes
// ===========================================================================
TEST(BasicTest, ExitCodeRange) {
#if !defined(_WIN32)
  ASSERT_EQ(0, run("true"));
  ASSERT_EQ(1, run("false"));

  for (int i = 0; i < 127; i++) {
    ASSERT_EQ(i, run("bash", "-c", "exit " + std::to_string(i)));
  }

  // Signal-induced exit codes: 128 + signal number
  for (int i : {4, 9, 15}) {
    ASSERT_EQ(128 + i, run("bash", "-c", "kill -" + std::to_string(i) + " $$"));
  }
#else
  for (int i = 0; i < 127; i++) {
    ASSERT_EQ(i, run("cmd.exe", "/c", "exit /b " + std::to_string(i)));
  }
#endif
}

// ===========================================================================
// Command not found / does not exist
// ===========================================================================
TEST(BasicTest, CommandNotFound) {
  ASSERT_EQ(127, run("this_command_not_found_in_paths"));
}

TEST(BasicTest, CommandNotExistsAbsolutePath) {
#if defined(_WIN32)
  ASSERT_EQ(127, run(R"(C:\path\to\this_command_not_exists.exe)"));
#else
  ASSERT_EQ(127, run("/path/to/this_command_not_exists"));
#endif
}

TEST(BasicTest, NonExistentCommandVector) {
  int exit_code = run("this_command_does_not_exist_123xyz");
  ASSERT_NE(exit_code, 0);
}

// ===========================================================================
// No permission on executable
// ===========================================================================
TEST(BasicTest, NoPermission) {
  TempFile temp;
#if defined(_WIN32)
  std::string content = R"(@echo off
exit /b 0
)";
  temp.write(content);
  ASSERT_EQ(127, run(temp.path()));
#else
  std::string content = R"(#!/usr/bin/env bash
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

// ===========================================================================
// Multi-argument commands
// ===========================================================================
TEST(BasicTest, CommandWithMultipleArguments) {
  subprocess::buffer stdout_buf;
  int exit_code = run(
#if defined(_WIN32)
      "cmd.exe", "/c", "echo one two words three"
#else
      "/bin/echo", "one", "two words", "three"
#endif
      ,
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
#if defined(_WIN32)
  ASSERT_EQ(stdout_buf.to_string(), "one two words three\r\n");
#else
  ASSERT_EQ(stdout_buf.to_string(), "one two words three\n");
#endif
}

// ===========================================================================
// Command resolved from system PATH
// ===========================================================================
TEST(BasicTest, CommandFromSystemPath) {
  subprocess::buffer stdout_buf;
#if defined(_WIN32)
  int exit_code = run("cmd.exe", "/c", "echo true", std_out > stdout_buf);
#else
  int exit_code = run("echo", "true", std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(stdout_buf.empty());
}
