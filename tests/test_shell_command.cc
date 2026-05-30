/**
 * test_shell_command.cc — Shell command execution tests
 *
 * Covers:
 *   - Basic shell run (Bash/Cmd/Powershell)
 *   - Shell with stdout/stderr capture
 *   - capture_run with shell
 *   - Shell with environment variables
 *   - Shell with working directory
 *   - Shell with stdin redirection
 *   - Shell exit code propagation
 *   - Shell stderr handling
 *   - Variadic shell run syntax
 *   - $bash / $cmd / $powershell named constants
 *   - detach_run with shell
 *   - Shell with newgroup
 */

#include <gtest/gtest.h>

#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::capture_run;
using subprocess::detach_run;
using subprocess::run;

// ===========================================================================
// Basic shell execution
// ===========================================================================

TEST(ShellCommandTest, BasicRunBash) {
#if defined(_WIN32)
  // On Windows, use cmd as default shell
  int ret = run(cmd, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run(cmd, "exit /b 42");
  ASSERT_EQ(ret, 42);
#else
  int ret = run(bash, "exit 0");
  ASSERT_EQ(ret, 0);

  ret = run(bash, "exit 42");
  ASSERT_EQ(ret, 42);
#endif
}

TEST(ShellCommandTest, BasicRunCmd) {
#if defined(_WIN32)
  int ret = run(cmd, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run(cmd, "exit /b 7");
  ASSERT_EQ(ret, 7);
#else
  GTEST_SKIP() << "cmd is Windows-only";
#endif
}

TEST(ShellCommandTest, BasicRunPowershell) {
#if defined(_WIN32)
  int ret = run(powershell, "exit 0");
  ASSERT_EQ(ret, 0);

  ret = run(powershell, "exit 3");
  ASSERT_EQ(ret, 3);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

// ===========================================================================
// Shell with stdout capture
// ===========================================================================

TEST(ShellCommandTest, StdoutCaptureBash) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "echo hello_shell", $stdout > out);
#else
  int ret = run(bash, "echo -n hello_shell", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
#if defined(_WIN32)
  ASSERT_EQ(out, "hello_shell\r\n");
#else
  ASSERT_EQ(out, "hello_shell");
#endif
}

TEST(ShellCommandTest, StdoutCapturePowershell) {
#if defined(_WIN32)
  buffer out;
  int ret = run(powershell, "Write-Host -NoNewline hello_ps", $stdout > out);
  ASSERT_EQ(ret, 0);
  // PowerShell's Write-Host -NoNewline goes to the console stream, not stdout.
  // Use Write-Output instead.
  out.clear();
  ret = run(powershell, "Write-Output hello_ps", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_ps\r\n");
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

// ===========================================================================
// Shell with stderr capture
// ===========================================================================

TEST(ShellCommandTest, StderrCaptureBash) {
  buffer err;
#if defined(_WIN32)
  int ret = run(cmd, "echo error_text >&2", $stderr > err);
#else
  int ret = run(bash, "echo -n error_text >&2", $stderr > err);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(err.empty());
}

TEST(ShellCommandTest, StderrCaptureCmd) {
#if defined(_WIN32)
  buffer err;
  int ret = run(cmd, "echo error_text >&2", $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(err.empty());
#else
  GTEST_SKIP() << "cmd is Windows-only";
#endif
}

// ===========================================================================
// capture_run with shell
// ===========================================================================

TEST(ShellCommandTest, CaptureRunBash) {
#if defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run(cmd, "(echo stdout_text & echo stderr_text >&2) & exit /b 5");
#else
  auto [exit_code, out, err] =
      capture_run(bash, "echo -n stdout_text; echo -n stderr_text >&2; exit 5");
#endif
  ASSERT_EQ(exit_code, 5);
#if defined(_WIN32)
  ASSERT_EQ(out, "stdout_text \r\n");
#else
  ASSERT_EQ(out, "stdout_text");
#endif
  ASSERT_FALSE(err.empty());
}

TEST(ShellCommandTest, CaptureRunPowershell) {
#if defined(_WIN32)
  auto [exit_code, out, err] = capture_run(
      powershell, "Write-Output ps_stdout; Write-Error ps_stderr; exit 9");
  ASSERT_EQ(exit_code, 9);
  ASSERT_EQ(out, "ps_stdout\r\n");
  ASSERT_FALSE(err.empty());
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

// ===========================================================================
// Shell with environment variables
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithEnv) {
  buffer out;
#if defined(_WIN32)
  int ret =
      run(cmd, "echo %SHELL_TEST_VAR%",
          $env = {{"SHELL_TEST_VAR", "env_value_from_shell"}}, $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "env_value_from_shell\r\n");
#else
  int ret =
      run(bash, "echo -n $SHELL_TEST_VAR",
          $env = {{"SHELL_TEST_VAR", "env_value_from_shell"}}, $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "env_value_from_shell");
#endif
}

TEST(ShellCommandTest, ShellRunWithEnvAppend) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "echo %PATH%",
                $env += {{"SHELL_APPEND_VAR", "appended_val"}}, $stdout > out);
#else
  int ret = run(bash, "echo -n $SHELL_APPEND_VAR",
                $env += {{"SHELL_APPEND_VAR", "appended_val"}}, $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(out.empty());
}

// ===========================================================================
// Shell with working directory
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithCwd) {
#if defined(_WIN32)
  buffer out;
  int ret = run(cmd, "cd", $cwd = "C:\\", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "C:\\\r\n");
#else
  buffer out;
  int ret = run(bash, "pwd", $cwd = "/tmp", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "/tmp\n");
#endif
}

// ===========================================================================
// Shell with stdin redirection
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithStdin) {
#if defined(_WIN32)
  buffer in("input_line_1\ninput_line_2\n");
  int ret = run(cmd, "findstr input_line_1", $stdin < in);
  ASSERT_EQ(ret, 0);
#else
  buffer in("input_line_1\ninput_line_2\n");
  int ret = run(bash, "grep -q input_line_1", $stdin < in);
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// Shell with newgroup
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithNewgroup) {
#if defined(_WIN32)
  int ret = run(cmd, "exit /b 0", $newgroup = true);
  ASSERT_EQ(ret, 0);
#else
  int ret = run(bash, "exit 0", $newgroup = true);
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// Shell with devnull redirections
// ===========================================================================

TEST(ShellCommandTest, ShellRunStdoutToDevnull) {
#if defined(_WIN32)
  int ret = run(cmd, "echo silenced & exit /b 0", $stdout > $devnull);
#else
  int ret = run(bash, "echo silenced; exit 0", $stdout > $devnull);
#endif
  ASSERT_EQ(ret, 0);
}

TEST(ShellCommandTest, ShellRunStderrToDevnull) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "echo visible & echo hidden >&2 & exit /b 0",
                $stdout > out, $stderr > $devnull);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "visible \r\n");
#else
  int ret = run(bash, "echo -n visible; echo hidden >&2; exit 0", $stdout > out,
                $stderr > $devnull);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "visible");
#endif
}

// ===========================================================================
// Shell variadic run syntax
// ===========================================================================

TEST(ShellCommandTest, VariadicShellRun) {
#if defined(_WIN32)
  int ret = run(cmd, "exit /b 55");
  ASSERT_EQ(ret, 55);
#else
  int ret = run(bash, "exit 55");
  ASSERT_EQ(ret, 55);
#endif
}

TEST(ShellCommandTest, VariadicShellCaptureRun) {
#if defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run(cmd, "echo variadic_capture & exit /b 88");
  ASSERT_EQ(exit_code, 88);
  ASSERT_EQ(out, "variadic_capture \r\n");
#else
  auto [exit_code, out, err] =
      capture_run(bash, "echo -n variadic_capture; exit 88");
  ASSERT_EQ(exit_code, 88);
  ASSERT_EQ(out, "variadic_capture");
#endif
}

// ===========================================================================
// Dollar-named shell constants ($bash, $cmd, $powershell)
// ===========================================================================

TEST(ShellCommandTest, DollarNamedShellConstants) {
#if defined(_WIN32)
  int ret = run($cmd, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run($powershell, "exit 0");
  ASSERT_EQ(ret, 0);
#else
  int ret = run($bash, "exit 0");
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// detach_run with shell
// ===========================================================================

TEST(ShellCommandTest, DetachRunShell) {
#if defined(_WIN32)
  bool result = detach_run(cmd, "exit /b 0");
  ASSERT_TRUE(result);
#else
  bool result = detach_run(bash, "exit 0");
  ASSERT_TRUE(result);
#endif
}

// ===========================================================================
// Shell with complex commands
// ===========================================================================

TEST(ShellCommandTest, ShellRunMultiLineCommand) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "(echo line1 & echo line2 & echo line3) & exit /b 0",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "line1 \r\nline2 \r\nline3\r\n");
#else
  int ret = run(bash, "echo -e 'line1\\nline2\\nline3'; exit 0", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "line1\nline2\nline3\n");
#endif
}

TEST(ShellCommandTest, ShellRunWithPipes) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "echo hello_pipe| findstr hello", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_pipe\r\n");
#else
  int ret = run(bash, "echo hello_pipe | grep hello", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_pipe\n");
#endif
}

// ===========================================================================
// Shell with both stdout and stderr captured simultaneously
// ===========================================================================

TEST(ShellCommandTest, ShellCaptureBothStdoutAndStderr) {
  buffer out, err;
#if defined(_WIN32)
  int ret = run(cmd, "(echo to_out & echo to_err >&2) & exit /b 0",
                $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "to_out \r\n");
  ASSERT_FALSE(err.empty());
#else
  int ret = run(bash, "echo -n to_out; echo -n to_err >&2; exit 0",
                $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "to_out");
  ASSERT_EQ(err, "to_err");
#endif
}

// ===========================================================================
// Shell with non-zero exit and stderr message
// ===========================================================================

TEST(ShellCommandTest, ShellRunNonZeroExitWithStderr) {
  buffer err;
#if defined(_WIN32)
  int ret = run(cmd, "echo failure_msg >&2 & exit /b 99", $stderr > err);
#else
  int ret = run(bash, "echo failure_msg >&2; exit 99", $stderr > err);
#endif
  ASSERT_EQ(ret, 99);
  ASSERT_FALSE(err.empty());
}

// ===========================================================================
// Shell with empty command output
// ===========================================================================

TEST(ShellCommandTest, ShellRunEmptyOutput) {
  buffer out;
#if defined(_WIN32)
  int ret = run(cmd, "exit /b 0", $stdout > out);
#else
  int ret = run(bash, "exit 0", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  // Output should be empty or contain only whitespace/newlines
  // (bash with no echo produces empty stdout)
}
