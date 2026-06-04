/**
 * test_error_handling.cc — Error handling tests
 *
 * Covers:
 *   - Invalid CWD: child process detects chdir failure → _Exit(126)
 *   - execv/execve failure → _Exit(127)
 *   - Error message propagation to stderr
 *   - Invalid CWD takes precedence over exec failure
 *   - Valid CWD still works (sanity check)
 *   - Command not found in PATH
 *   - No permission on executable
 *   - POSIX spawn path: die() in parent, run() catches, returns 127
 *   - POSIX spawn: valid CWD, capture stdout, regular commands
 *
 * Tests covering both fork/exec and posix_spawn code paths.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::capture_run;
using subprocess::run;

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// ===========================================================================
// Platform-agnostic tests
// ===========================================================================

#if defined(_WIN32)

TEST(ErrorHandlingTest, InvalidCwdReturnsNonZero) {
  auto exit_code = run("cmd.exe", "/c", "exit /b 0",
                       cwd = "Z:\\this\\directory\\does\\not\\exist\\xyz");
  ASSERT_NE(exit_code, 0);
}

TEST(ErrorHandlingTest, CommandNotFoundReturns127) {
  auto exit_code = run("this_command_not_found_in_paths_xyz.exe");
  ASSERT_EQ(exit_code, 127);
}

TEST(ErrorHandlingTest, NonExistentExecutableReturns127) {
  auto exit_code = run("C:\\path\\to\\this_command_not_exists.exe");
  ASSERT_EQ(exit_code, 127);
}

#endif  // _WIN32

// ===========================================================================
// POSIX-specific fork/exec error handling
// ===========================================================================

#if !defined(_WIN32)

// Invalid CWD via run() — chdir fails → _Exit(126)
TEST(ErrorHandlingTest, InvalidCwdReturns126) {
  auto exit_code = run("true", cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 126);
}

// Invalid CWD via capture_run()
TEST(ErrorHandlingTest, InvalidCwdWithCaptureRunReturns126) {
  auto [exit_code, out, err] =
      capture_run("true", cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 126);
}

// Invalid CWD prints "chdir failed" to stderr
TEST(ErrorHandlingTest, InvalidCwdPrintsChdirFailedToStderr) {
  buffer stderr_buf;
  auto exit_code = run("true", cwd = "/this/directory/does/not/exist/xyz",
                       std_err > stderr_buf);
  ASSERT_EQ(exit_code, 126);
  EXPECT_NE(stderr_buf.to_string().find("chdir:"), std::string::npos);
}

// execv failure — non-existent executable by absolute path
TEST(ErrorHandlingTest, ExecvFailureReturns127) {
  auto exit_code = run("/path/to/this_command_not_exists");
  ASSERT_EQ(exit_code, 127);
}

// execv failure prints to stderr
TEST(ErrorHandlingTest, ExecvFailurePrintsToStderr) {
  buffer stderr_buf;
  auto exit_code =
      run("/path/to/this_command_not_exists", std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  EXPECT_NE(stderr_buf.to_string().find("execv("), std::string::npos);
}

// execve failure with explicit environment
TEST(ErrorHandlingTest, ExecveFailureReturns127) {
  auto exit_code =
      run("/path/to/this_command_not_exists",
          env = std::map<std::string, std::string>{{"FOO", "BAR"}});
  ASSERT_EQ(exit_code, 127);
}

// execve failure prints to stderr
TEST(ErrorHandlingTest, ExecveFailurePrintsToStderr) {
  buffer stderr_buf;
  auto exit_code = run("/path/to/this_command_not_exists",
                       env = std::map<std::string, std::string>{{"FOO", "BAR"}},
                       std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  EXPECT_NE(stderr_buf.to_string().find("execve("), std::string::npos);
}

// Invalid CWD takes precedence over exec failure (chdir checked first)
TEST(ErrorHandlingTest, InvalidCwdTakesPrecedenceOverExecFailure) {
  auto exit_code = run("/path/to/this_command_not_exists",
                       cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 126);
}

// Valid CWD sanity check
TEST(ErrorHandlingTest, ValidCwdStillWorks) {
  auto exit_code = run("true", cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
}

// Command not found in PATH
TEST(ErrorHandlingTest, CommandNotFoundInPathReturns127) {
  auto exit_code = run("this_command_not_found_in_paths");
  ASSERT_EQ(exit_code, 127);
}

// No permission on executable
TEST(ErrorHandlingTest, NoPermissionOnExecutableReturns127) {
  auto tmp_path =
      "/tmp/subprocess_test_noexec_" + std::to_string(::getpid()) + ".sh";
  {
    std::ofstream f(tmp_path);
    f << "#!/bin/sh\nexit 0\n";
  }
  ::chmod(tmp_path.c_str(), 0644);
  auto exit_code = run(tmp_path);
  ::unlink(tmp_path.c_str());
#if defined(__MSYS__) || defined(__CYGWIN__)
  ASSERT_EQ(exit_code, 0);
#else
  ASSERT_EQ(exit_code, 127);
#endif
}

#endif  // !_WIN32

// ===========================================================================
// POSIX spawn error handling (only when exceptions + posix_spawn + addchdir)
// ===========================================================================

#if !defined(_WIN32) && SUBPROCESS_HAS_EXCEPTIONS &&       \
    (defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP) || \
     defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR))

// When posix_spawn is active, an invalid CWD is detected in the parent via
// die() → run() catches the exception → returns 127.
TEST(PosixSpawnErrorHandlingTest, InvalidCwdReturns127) {
  auto exit_code = run("true", cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 127);
}

TEST(PosixSpawnErrorHandlingTest, InvalidCwdWithCaptureRunReturns127) {
  auto [exit_code, out, err] =
      capture_run("true", cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 127);
}

// On the posix_spawn path, the error is printed to the parent's stderr,
// so the child stderr buffer is empty.
TEST(PosixSpawnErrorHandlingTest, InvalidCwdChildStderrIsEmpty) {
  buffer stderr_buf;
  auto exit_code = run("true", cwd = "/this/directory/does/not/exist/xyz",
                       std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  EXPECT_TRUE(stderr_buf.empty());
}

TEST(PosixSpawnErrorHandlingTest, ValidCwdStillWorks) {
  auto exit_code = run("true", cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
}

TEST(PosixSpawnErrorHandlingTest, RegularCommandStillWorks) {
  auto exit_code = run("true");
  ASSERT_EQ(exit_code, 0);
}

TEST(PosixSpawnErrorHandlingTest, CaptureStdoutWorks) {
  buffer out;
  auto exit_code = run("/bin/echo", "-n", "hello", std_out > out);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out.to_string(), "hello");
}

#endif  // !_WIN32 && SUBPROCESS_HAS_EXCEPTIONS && posix_spawn addchdir
