#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;

// =============================================================================
// Tests for the new error-handling behaviour introduced with the print_error()
// refactoring and exception-safe run().
// =============================================================================

#if !defined(_WIN32)

// ---------------------------------------------------------------------------
// 1.  Invalid CWD via run() — child process detects chdir failure, calls
//     _Exit(126).  Previously the child would call die() which either aborted
//     (no-exceptions build) or threw (exceptions build — undefined behaviour
//     in a forked child).
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, InvalidCwdReturns126) {
  auto exit_code =
      subprocess::run({"true"}, subprocess::named_arguments::cwd =
                                    "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 126);
}

// ---------------------------------------------------------------------------
// 2.  Invalid CWD via capture_run() — same as above but through the
//     capture_run wrapper.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, InvalidCwdWithCaptureRunReturns126) {
  auto [exit_code, out, err] = subprocess::capture_run(
      {"true"},
      subprocess::named_arguments::cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 126);
}

// ---------------------------------------------------------------------------
// 3.  Invalid CWD — verify the child process prints a "chdir failed" message
//     to stderr.  The new print_error() function writes this before _Exit().
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, InvalidCwdPrintsChdirFailedToStderr) {
  subprocess::buffer stderr_buf;
  auto exit_code = subprocess::run(
      {"true"},
      subprocess::named_arguments::cwd = "/this/directory/does/not/exist/xyz",
      subprocess::named_arguments::std_err > stderr_buf);
  ASSERT_EQ(exit_code, 126);
  // The error message should contain "chdir failed"
  EXPECT_NE(stderr_buf.to_string().find("chdir failed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 4.  execv failure — run a non-existent executable (by absolute path).
//     The child process should call _Exit(127) after print_error().
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, ExecvFailureReturns127) {
  auto exit_code = subprocess::run({"/path/to/this_command_not_exists"});
  ASSERT_EQ(exit_code, 127);
}

// ---------------------------------------------------------------------------
// 5.  execv failure with stderr capture — verify stderr contains "execv"
//     and "failed".
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, ExecvFailurePrintsToStderr) {
  subprocess::buffer stderr_buf;
  auto exit_code =
      subprocess::run({"/path/to/this_command_not_exists"},
                      subprocess::named_arguments::std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  // The child prints "execv(...) failed: ..." to stderr
  EXPECT_NE(stderr_buf.to_string().find("execv"), std::string::npos);
  EXPECT_NE(stderr_buf.to_string().find("failed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 6.  execve failure — run with an explicit environment and a non-existent
//     executable.  The child should still exit with 127.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, ExecveFailureReturns127) {
  auto exit_code =
      subprocess::run({"/path/to/this_command_not_exists"},
                      subprocess::named_arguments::env =
                          std::map<std::string, std::string>{{"FOO", "BAR"}});
  ASSERT_EQ(exit_code, 127);
}

// ---------------------------------------------------------------------------
// 7.  execve failure prints to stderr — verify "execve" appears.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, ExecveFailurePrintsToStderr) {
  subprocess::buffer stderr_buf;
  auto exit_code =
      subprocess::run({"/path/to/this_command_not_exists"},
                      subprocess::named_arguments::env =
                          std::map<std::string, std::string>{{"FOO", "BAR"}},
                      subprocess::named_arguments::std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  EXPECT_NE(stderr_buf.to_string().find("execve"), std::string::npos);
  EXPECT_NE(stderr_buf.to_string().find("failed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 8.  Both chdir failure AND exec failure cannot happen in the same process,
//     but we can verify that a non-existent command with an invalid CWD
//     still exits with 126 (chdir is checked first).
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, InvalidCwdTakesPrecedenceOverExecFailure) {
  auto exit_code = subprocess::run(
      {"/path/to/this_command_not_exists"},
      subprocess::named_arguments::cwd = "/this/directory/does/not/exist/xyz");
  // chdir is attempted before exec, so 126 is expected
  ASSERT_EQ(exit_code, 126);
}

// ---------------------------------------------------------------------------
// 9.  Valid CWD works correctly — a sanity check that the feature isn't
//     broken for normal usage.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, ValidCwdStillWorks) {
  auto exit_code =
      subprocess::run({"true"}, subprocess::named_arguments::cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
}

// ---------------------------------------------------------------------------
// 10. Command not found in PATH (no '/' in name) — should still return 127
//     from the execv failure path in the child.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, CommandNotFoundInPathReturns127) {
  auto exit_code = subprocess::run({"this_command_not_found_in_paths"});
  ASSERT_EQ(exit_code, 127);
}

// ---------------------------------------------------------------------------
// 11. No permission on executable — the exec call fails with EACCES.
//     Should exit with 127.
// ---------------------------------------------------------------------------
TEST(ErrorHandlingTest, NoPermissionOnExecutableReturns127) {
  // Create a temporary file without execute permission
  auto tmp_path =
      "/tmp/subprocess_test_noexec_" + std::to_string(::getpid()) + ".sh";
  {
    std::ofstream f(tmp_path);
    f << "#!/bin/sh\nexit 0\n";
  }
  // Ensure no execute permission
  ::chmod(tmp_path.c_str(), 0644);
  auto exit_code = subprocess::run({tmp_path});
  ::unlink(tmp_path.c_str());
  // Without execute permission, execv fails with EACCES → _Exit(127)
  ASSERT_EQ(exit_code, 127);
}

#endif  // !_WIN32

// =============================================================================
// Windows-specific tests
// =============================================================================

#if defined(_WIN32)

TEST(ErrorHandlingTest, InvalidCwdReturnsNonZero) {
  auto exit_code =
      subprocess::run({TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")},
                      subprocess::named_arguments::cwd =
                          TEXT("Z:\\this\\directory\\does\\not\\exist\\xyz"));
  // On Windows, a non-existent cwd causes CreateProcessW to fail, which
  // currently prints to stderr and leaves the process handle invalid.
  // The exit code will be non-zero.
  ASSERT_NE(exit_code, 0);
}

TEST(ErrorHandlingTest, CommandNotFoundReturns127) {
  auto exit_code =
      subprocess::run({TEXT("this_command_not_found_in_paths_xyz.exe")});
  ASSERT_EQ(exit_code, 127);
}

TEST(ErrorHandlingTest, NonExistentExecutableReturns127) {
  auto exit_code =
      subprocess::run({TEXT("C:\\path\\to\\this_command_not_exists.exe")});
  ASSERT_EQ(exit_code, 127);
}

#endif  // _WIN32
