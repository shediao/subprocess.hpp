/**
 * test_executable_resolution.cc — Tests for executable path resolution
 *
 * Covers:
 *   - PATHEXT resolution (Windows: running command without extension)
 *   - Command with extension found in PATH
 *   - Command with relative path (directory separator)
 *   - Command not found error handling
 */

#include <gtest/gtest.h>

#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

// ===========================================================================
// PATHEXT resolution — command without extension
// ===========================================================================
TEST(ExecutableResolutionTest, CommandWithoutExtension) {
#if defined(_WIN32)
  // "cmd" without .exe — must be resolved via PATHEXT
  int exit_code = run("cmd", "/c", "exit /b 42");
  ASSERT_EQ(exit_code, 42);

  // "findstr" without .exe — also PATHEXT resolution
  exit_code = run("findstr", "/?", "findstr");
  // findstr /? returns 0 for help, 1 for no match; both mean it was found
  ASSERT_TRUE(exit_code == 0 || exit_code == 1);
#else
  // On Unix, "true" is typically at /usr/bin/true or /bin/true
  int exit_code = run("true");
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// Command with extension in PATH
// ===========================================================================
TEST(ExecutableResolutionTest, CommandWithExtension) {
#if defined(_WIN32)
  int exit_code = run("cmd.exe", "/c", "exit /b 7");
  ASSERT_EQ(exit_code, 7);
#else
  // "echo" is a shell built-in but also exists as /bin/echo
  subprocess::buffer out;
  int exit_code = run("echo", "hello", std_out > out);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// Command with relative path (directory separator)
// ===========================================================================
TEST(ExecutableResolutionTest, CommandWithRelativePath) {
#if defined(_WIN32)
  // Create a temporary batch file and run it with a relative path
  TempFile temp("test_bat_", ".bat");
  std::string content = "@echo off\r\nexit /b 99\r\n";
  temp.write(content);

  // Use the full path to the temp file
  int exit_code = run(temp.path());
  ASSERT_EQ(exit_code, 99);

  // Also test with forward slashes
  std::string path_with_fwd = temp.path();
  for (auto& c : path_with_fwd) {
    if (c == '\\') {
      c = '/';
    }
  }
  exit_code = run(path_with_fwd);
  ASSERT_EQ(exit_code, 99);
#else
  // Create a temporary shell script and run it with a relative path
  TempFile temp("test_sh_", ".sh");
  std::string content = "#!/bin/sh\nexit 99\n";
  temp.write(content);

  // Make it executable
  std::string chmod_cmd = "chmod +x " + temp.path();
  int rc = system(chmod_cmd.c_str());
  (void)rc;

  int exit_code = run(temp.path());
  ASSERT_EQ(exit_code, 99);
#endif
}

// ===========================================================================
// Command not found — returns 127
// ===========================================================================
TEST(ExecutableResolutionTest, CommandNotFoundReturns127) {
  int exit_code = run("this_command_does_not_exist_anywhere_xyz");
  ASSERT_EQ(exit_code, 127);
}

// ===========================================================================
// Command not found with extension
// ===========================================================================
TEST(ExecutableResolutionTest, CommandNotFoundWithExtension) {
  int exit_code = run("nonexistent_cmd_xyz.exe");
  ASSERT_EQ(exit_code, 127);
}

// ===========================================================================
// Command with absolute path not found
// ===========================================================================
TEST(ExecutableResolutionTest, AbsolutePathNotFound) {
#if defined(_WIN32)
  int exit_code = run("C:\\nonexistent\\path\\to\\command.exe");
  ASSERT_EQ(exit_code, 127);
#else
  int exit_code = run("/nonexistent/path/to/command");
  ASSERT_EQ(exit_code, 127);
#endif
}

// ===========================================================================
// Captured run with PATH resolution
// ===========================================================================
TEST(ExecutableResolutionTest, CapturedRunWithPathResolution) {
#if defined(_WIN32)
  auto [exit_code, out, err] =
      subprocess::capture_run("cmd", "/c", "echo captured");
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#else
  auto [exit_code, out, err] = subprocess::capture_run("echo", "captured");
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}
