#include <chrono>

#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

using namespace std::chrono_literals;
using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::run;

// =============================================================================
// Helper macros to avoid trailing-comma issues with #if inside run() calls
// =============================================================================
#if defined(_WIN32)
#define CMD_TRUE TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")
#define CMD_SLEEP_10 \
  TEXT("cmd.exe"), TEXT("/c"), TEXT("ping 127.0.0.1 -n 11 > nul")
#define CMD_SLEEP_5 \
  TEXT("cmd.exe"), TEXT("/c"), TEXT("ping 127.0.0.1 -n 6 > nul")
#define CMD_SLEEP_20 \
  TEXT("cmd.exe"), TEXT("/c"), TEXT("ping 127.0.0.1 -n 21 > nul")
#define CMD_EXIT_42 TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 42")
#define CMD_ECHO_HELLO \
  TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=hello&exit /b 0")
#else
#define CMD_TRUE "true"
#define CMD_SLEEP_10 "sleep", "10"
#define CMD_SLEEP_5 "sleep", "5"
#define CMD_SLEEP_20 "sleep", "20"
#define CMD_EXIT_42 "bash", "-c", "exit 42"
#define CMD_ECHO_HELLO "/bin/echo", "-n", "hello"
#endif

// =============================================================================
// Tests for the timeout named argument
// =============================================================================

// 1. Quick command finishes well within timeout, exit code 0
TEST(TimeoutTest, QuickCommandWithinTimeout) {
  auto exit_code = run(CMD_TRUE, $timeout = std::chrono::seconds(5));
  ASSERT_EQ(exit_code, 0);
}

// 2. Command that sleeps longer than timeout gets killed
TEST(TimeoutTest, SleepExceedsTimeout) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run(CMD_SLEEP_10, $timeout = std::chrono::seconds(1));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  // Should finish well under 10 seconds (it was killed)
  EXPECT_LT(elapsed_ms, 5000);

  // On POSIX: 128 + SIGTERM(15) = 143
  // On Windows: TerminateProcess returns the exit code we passed (1)
#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// 3. Timeout using int (interpreted as seconds)
TEST(TimeoutTest, TimeoutWithIntSeconds) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run(CMD_SLEEP_10, $timeout = 1);  // int seconds

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 5000);
#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// 4. Timeout using non-dollar `timeout` named argument
TEST(TimeoutTest, TimeoutNonDollarNamedArg) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run(CMD_SLEEP_10, timeout = std::chrono::seconds(1));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 5000);
#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// 5. No timeout specified — command runs normally (default no timeout)
TEST(TimeoutTest, NoTimeoutRunsNormally) {
  auto exit_code = run(CMD_TRUE);
  ASSERT_EQ(exit_code, 0);
}

// 6. capture_run with timeout — killed process
TEST(TimeoutTest, CaptureRunWithTimeout) {
  auto start = std::chrono::steady_clock::now();

  auto [exit_code, out, err] =
      capture_run(CMD_SLEEP_10, $timeout = std::chrono::seconds(1));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 5000);
#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// 7. Timeout with sub-millisecond precision — a very short timeout
TEST(TimeoutTest, VeryShortTimeout) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run(CMD_SLEEP_5, $timeout = 100ms);

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  // Should finish well under 5 seconds
  EXPECT_LT(elapsed_ms, 4000);
#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// 8. Timeout with variadic run (no vector for command args)
TEST(TimeoutTest, VariadicRunWithTimeout) {
  auto exit_code = run(CMD_TRUE, $timeout = std::chrono::seconds(5));
  ASSERT_EQ(exit_code, 0);
}

// 9. Timeout does not affect commands that finish earlier than timeout
TEST(TimeoutTest, CommandFinishesWellBeforeTimeout) {
  auto exit_code = run(CMD_EXIT_42, $timeout = std::chrono::seconds(30));
  ASSERT_EQ(exit_code, 42);
}

// 10. Long timeout that is not hit — command completes successfully
TEST(TimeoutTest, LongTimeoutNotTriggered) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run(CMD_TRUE, $timeout = std::chrono::seconds(60));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_EQ(exit_code, 0);
  // Should finish almost instantly
  EXPECT_LT(elapsed_ms, 5000);
}

// 11. capture_run with timeout on a command that produces output before
// exceeding timeout
TEST(TimeoutTest, CaptureRunOutputWithTimeoutNotTriggered) {
  auto [exit_code, out, err] =
      capture_run(CMD_ECHO_HELLO, $timeout = std::chrono::seconds(10));

  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out.to_string(), "hello");
}

// 12. Using std::chrono::milliseconds directly
TEST(TimeoutTest, TimeoutInMilliseconds) {
  auto exit_code = run(CMD_SLEEP_20, $timeout = std::chrono::milliseconds(500));

#if !defined(_WIN32)
  EXPECT_EQ(exit_code, 143);
#else
  EXPECT_NE(exit_code, 0);
#endif
}

// =============================================================================
// Unix-only tests: verify that timeout kills the entire process *group*, not
// just the direct child process.  Before the fix a grandchild that inherited
// pipe write-ends could keep manage_pipe_io() blocked in poll() forever.
// =============================================================================
#if !defined(_WIN32)

// Spawn a shell that starts a background sleep (grandchild) and then sleeps
// itself.  Timeout must kill both; otherwise the background sleep keeps the
// stdout/stderr pipe(s) open and the test hangs.
TEST(TimeoutTest, ProcessTreeKilledOnTimeout) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run("sh", "-c", "sleep 10 & sleep 10; wait",
                       $timeout = std::chrono::milliseconds(500));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  // If the grandchild survived, poll() would block → elapsed >> 5 s.
  EXPECT_LT(elapsed_ms, 4000);
  // 128 + SIGTERM(15) = 143
  EXPECT_EQ(exit_code, 143);
}

// A process that explicitly ignores SIGTERM must eventually receive SIGKILL.
TEST(TimeoutTest, SigtermIgnoredFallsBackToSigkill) {
  auto start = std::chrono::steady_clock::now();

  auto exit_code = run("sh", "-c", "trap '' TERM; sleep 10",
                       $timeout = std::chrono::milliseconds(500));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 4000);
  // 128 + SIGKILL(9) = 137
  EXPECT_EQ(exit_code, 137);
}

// When no timeout is set, a sub-process tree should exit normally.
// This proves that setpgid() does not interfere with ordinary execution.
TEST(TimeoutTest, SubprocessTreeExitsNormallyWithoutTimeout) {
  auto exit_code = run("sh", "-c", "sleep 0.1 & sleep 0.1; wait");
  EXPECT_EQ(exit_code, 0);
}

// Same scenario as ProcessTreeKilledOnTimeout but with capture_run, which
// uses explicit pipe fds that a surviving grandchild would keep open.
TEST(TimeoutTest, DISABLED_ProcessTreeKilledWithCaptureRun) {
  auto start = std::chrono::steady_clock::now();

  auto [exit_code, out, err] =
      capture_run("sh", "-c", "sleep 10 & sleep 10 & sleep 10",
                  $timeout = std::chrono::milliseconds(500));

  auto elapsed = std::chrono::steady_clock::now() - start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  EXPECT_LT(elapsed_ms, 4000);
  EXPECT_EQ(exit_code, 143);
}

// A quick command with a timeout must not show side effects from setpgid.
TEST(TimeoutTest, QuickCommandWithProcessGroupNoSideEffect) {
  auto exit_code = run("sh", "-c", "true", $timeout = std::chrono::seconds(5));
  EXPECT_EQ(exit_code, 0);
}

#endif  // !_WIN32
