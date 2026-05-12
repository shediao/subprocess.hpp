// This test file explicitly enables the POSIX spawn code path to exercise
// the exception-catching logic in subprocess::run().  When
// SUBPROCESS_USE_POSIX_SPAWN is defined, an invalid CWD causes
// add_posix_spawn_file_actions() to call die(), which throws std::runtime_error
// (when exceptions are enabled).  The run() method now catches this and
// returns 127 instead of letting the exception propagate.
//
// NOTE: These tests are only meaningful when exceptions are enabled
// (SUBPROCESS_HAS_EXCEPTIONS == 1).  With exceptions disabled, die() calls
// abort(), which would crash the test process.

#ifndef SUBPROCESS_USE_POSIX_SPAWN
#define SUBPROCESS_USE_POSIX_SPAWN 1
#endif

#include <gtest/gtest.h>

#include <string>

// =============================================================================
// These tests only run when exceptions are enabled and on non-Windows.
// With -fno-exceptions, die() would abort() the test process.
// =============================================================================

#if !defined(_WIN32) && SUBPROCESS_HAS_EXCEPTIONS &&       \
    (defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP) || \
     defined(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR))

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;

// ---------------------------------------------------------------------------
// 1.  Invalid CWD with posix_spawn — die() throws, run() catches, returns 127.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, InvalidCwdReturns127) {
  auto exit_code =
      subprocess::run({"true"}, subprocess::named_arguments::cwd =
                                    "/this/directory/does/not/exist/xyz");
  // With posix_spawn, the invalid cwd is detected in the parent before
  // spawning.  die() throws → run() catches → returns 127.
  ASSERT_EQ(exit_code, 127);
}

// ---------------------------------------------------------------------------
// 2.  Invalid CWD with capture_run() on posix_spawn path.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, InvalidCwdWithCaptureRunReturns127) {
  auto [exit_code, out, err] = subprocess::capture_run(
      {"true"},
      subprocess::named_arguments::cwd = "/this/directory/does/not/exist/xyz");
  ASSERT_EQ(exit_code, 127);
}

// ---------------------------------------------------------------------------
// 3.  Invalid CWD — on the posix_spawn path the error is printed to the
//     *parent* stderr by print_error() (called from the catch block in
//     run()), not to the child's stderr.  So the child stderr buffer will
//     be empty.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, InvalidCwdChildStderrIsEmpty) {
  subprocess::buffer stderr_buf;
  auto exit_code = subprocess::run(
      {"true"},
      subprocess::named_arguments::cwd = "/this/directory/does/not/exist/xyz",
      subprocess::named_arguments::std_err > stderr_buf);
  ASSERT_EQ(exit_code, 127);
  // The error message goes to the parent's stderr, not the child's.
  EXPECT_TRUE(stderr_buf.empty());
}

// ---------------------------------------------------------------------------
// 4.  Valid CWD still works with posix_spawn path.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, ValidCwdStillWorks) {
  auto exit_code =
      subprocess::run({"true"}, subprocess::named_arguments::cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
}

// ---------------------------------------------------------------------------
// 5.  Regular command execution (no CWD) still works.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, RegularCommandStillWorks) {
  auto exit_code = subprocess::run({"true"});
  ASSERT_EQ(exit_code, 0);
}

// ---------------------------------------------------------------------------
// 6.  Capture stdout from a command that succeeds — sanity check that
//     posix_spawn correctly wires up pipes.
// ---------------------------------------------------------------------------
TEST(PosixSpawnErrorHandlingTest, CaptureStdoutWorks) {
  subprocess::buffer out;
  auto exit_code = subprocess::run({"/bin/echo", "-n", "hello"},
                                   subprocess::named_arguments::std_out > out);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out.to_string(), "hello");
}

#endif  // !_WIN32 && SUBPROCESS_HAS_EXCEPTIONS
