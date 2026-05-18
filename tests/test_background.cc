#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

using namespace std::chrono_literals;
using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::run;

#if defined(_WIN32)
#define CMD_TRUE TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")
#define CMD_EXIT_42 TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 42")
#define CMD_SLEEP_1 \
  TEXT("cmd.exe"), TEXT("/c"), TEXT("ping 127.0.0.1 -n 2 > nul")
#define CMD_CAT TEXT("cmd.exe"), TEXT("/c"), TEXT("more")
#else
#define CMD_TRUE "true"
#define CMD_EXIT_42 "bash", "-c", "exit 42"
#define CMD_SLEEP_1 "sleep", "1"
#define CMD_CAT "cat"
#endif

// =============================================================================
// Tests for the Background named argument
// =============================================================================

// 1. Explicit background = true runs successfully
TEST(BackgroundTest, ExplicitBackgroundTrue) {
  auto exit_code = run(CMD_TRUE, background = true);
  ASSERT_EQ(exit_code, 0);
}

// 2. Explicit background = false runs successfully
TEST(BackgroundTest, ExplicitBackgroundFalse) {
  auto exit_code = run(CMD_TRUE, background = false);
  ASSERT_EQ(exit_code, 0);
}

// 3. Background = true with exit code
TEST(BackgroundTest, BackgroundTrueWithExitCode) {
  auto exit_code = run(CMD_EXIT_42, background = true);
  ASSERT_EQ(exit_code, 42);
}

// 4. Background = false with exit code
TEST(BackgroundTest, BackgroundFalseWithExitCode) {
  auto exit_code = run(CMD_EXIT_42, background = false);
  ASSERT_EQ(exit_code, 42);
}

// 5. Background combined with timeout — kill process group via SIGTERM
//    Same scenario as ProcessTreeKilledOnTimeout but explicitly setting
//    background = true.
#if !defined(_WIN32)
TEST(BackgroundTest, BackgroundTrueTimeoutKillsProcessTree) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code =
      run("sh", "-c", "sleep 10 & sleep 10; wait", background = true,
          $timeout = std::chrono::milliseconds(500));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 4000);
  EXPECT_EQ(exit_code, 143);
}
#endif  // !_WIN32

// 6. Background = false skips process group creation — use timeout to
//    verify the direct child is killed but a grandchild holding a pipe open
//    would cause a hang. We just verify basic functioning here.
#if !defined(_WIN32)
TEST(BackgroundTest, BackgroundFalseNoProcessGroup) {
  auto exit_code = run("sh", "-c", "sleep 0.1", background = false,
                       $timeout = std::chrono::seconds(5));
  EXPECT_EQ(exit_code, 0);
}
#endif  // !_WIN32

// 7. Background with $background (dollar-style named argument)
TEST(BackgroundTest, DollarBackgroundSyntax) {
#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
  auto exit_code = run(CMD_TRUE, $background = true);
  ASSERT_EQ(exit_code, 0);
#else
  GTEST_SKIP()
      << "$background not available without USE_DOLLAR_NAMED_VARIABLES";
#endif
}

// =============================================================================
//  Background stdin → /dev/null tests
// =============================================================================

// capture_run always passes std_in < devnull, so the child's stdin is
// redirected to /dev/null regardless of whether background is explicitly
// set.  Reading from stdin should return EOF immediately.
#if !defined(_WIN32)
TEST(BackgroundTest, CaptureRunStdinIsDevNull) {
  // Try to read from stdin — should get immediate EOF (empty output)
  auto [exit_code, out, err] =
      capture_run("cat", $timeout = std::chrono::seconds(3));

  EXPECT_EQ(exit_code, 0);
  // 'cat' with /dev/null as stdin should produce no output
  EXPECT_TRUE(out.to_string().empty());
}

// When background = false, the library does NOT auto-redirect stdin to
// /dev/null.  The inherited stdin (test-runner pipe) may already be at EOF
// in CI, so we provide explicit input via a buffer and verify the child
// reads it correctly — proving that stdin is functional, not /dev/null.
TEST(BackgroundTest, BackgroundFalseStdinNotReplaced) {
  subprocess::buffer in("hello_stdin");
  subprocess::buffer out;
  auto exit_code = run("cat", background = false, std_in<in, std_out> out,
                       $timeout = std::chrono::seconds(3));

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(out.to_string(), "hello_stdin");
}

// With the removal of the implicit constructor-side stdin→/dev/null
// redirection, run() with background=true also does NOT auto-redirect
// stdin.  Provide explicit input via a buffer and verify the child reads
// it correctly — proving that stdin is functional, not /dev/null.
TEST(BackgroundTest, BackgroundTrueRunDoesNotAutoRedirectStdin) {
  subprocess::buffer in("hello_stdin_bg");
  subprocess::buffer out;
  auto exit_code = run("cat", background = true, std_in<in, std_out> out,
                       $timeout = std::chrono::seconds(3));

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(out.to_string(), "hello_stdin_bg");
}
#endif  // !_WIN32

// =============================================================================
//  pid() accessor tests
// =============================================================================

#if !defined(_WIN32)
TEST(BackgroundTest, PidAccessorReturnsValidPid) {
  subprocess::detail::subprocess proc({CMD_SLEEP_1});

  proc.async_run();
  auto pid = proc.pid();

  // PID should be positive (valid)
  EXPECT_GT(pid, 0);

  // Process should be running (kill with signal 0 checks existence)
  EXPECT_EQ(kill(pid, 0), 0);

  auto exit_code = proc.wait_for_exit();
  EXPECT_EQ(exit_code, 0);
}

TEST(BackgroundTest, PidAccessorInBackgroundMode) {
  subprocess::detail::subprocess proc({CMD_SLEEP_1}, background = true);

  proc.async_run();
  auto pid = proc.pid();

  EXPECT_GT(pid, 0);
  EXPECT_EQ(kill(pid, 0), 0);

  // In background mode the process should be in its own process group.
  // getpgid(pid) should equal pid (since the child called setpgid(0,0)).
  EXPECT_EQ(getpgid(pid), pid);

  auto exit_code = proc.wait_for_exit();
  EXPECT_EQ(exit_code, 0);
}

TEST(BackgroundTest, PidAccessorNoBackgroundNoProcessGroup) {
  subprocess::detail::subprocess proc({CMD_SLEEP_1}, background = false);

  proc.async_run();
  auto pid = proc.pid();

  EXPECT_GT(pid, 0);

  // Without background mode the process inherits our process group.
  auto pgid = getpgid(pid);
  auto my_pgid = getpgid(getpid());
  EXPECT_EQ(pgid, my_pgid);

  auto exit_code = proc.wait_for_exit();
  EXPECT_EQ(exit_code, 0);
}
#endif  // !_WIN32

// =============================================================================
//  is_atty utility tests
// =============================================================================

#if !defined(_WIN32)
TEST(BackgroundTest, IsAttyUtilityForRegularFile) {
  // Open a regular file and verify is_atty returns false.
  int fd = open("/dev/null", O_RDONLY);
  ASSERT_GE(fd, 0);
  EXPECT_FALSE(subprocess::detail::is_atty(fd));
  close(fd);
}

TEST(BackgroundTest, StdinIsAttyUtility) {
  // Just verify the utility functions exist and return a boolean.
  // The actual return value depends on the test environment.
  auto result = subprocess::detail::stdin_is_atty();
  EXPECT_TRUE(result == true ||
              result == false);  // just verify it compiles & returns bool
  static_assert(std::is_same_v<decltype(result), bool>);
}
#endif  // !_WIN32

// =============================================================================
//  capture_run always uses background = true and stdin → /dev/null
// =============================================================================

#if !defined(_WIN32)
TEST(BackgroundTest, CaptureRunUsesBackgroundMode) {
  // capture_run always passes background=true and std_in<devnull,
  // so the child process should be in its own process group and have
  // its stdin closed.  A background grandchild won't keep stdout/stderr
  // pipes open because the process group is cleaned up.
  auto [exit_code, out, err] = capture_run("sh", "-c", "echo hello; sleep 0.1");

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(out.to_string(), "hello\n");
}
#endif  // !_WIN32
