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
 *   - $shell / $bash / $powershell named constants
 *   - detach_run with shell
 *   - Shell with newgroup
 */

#include <gtest/gtest.h>

#include <string>

#include "./temp_file.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::detach_run;
using subprocess::dynamic_buffer;
using subprocess::run;

// ===========================================================================
// Basic shell execution
// ===========================================================================

TEST(ShellCommandTest, BasicRunBash) {
#if defined(_WIN32)
  // On Windows, use $shell as default shell
  int ret = run($shell, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run($shell, "exit /b 42");
  ASSERT_EQ(ret, 42);
#else
  int ret = run($shell, "exit 0");
  ASSERT_EQ(ret, 0);

  ret = run($shell, "exit 42");
  ASSERT_EQ(ret, 42);
#endif
}

TEST(ShellCommandTest, BasicRunCmd) {
#if defined(_WIN32)
  // $shell resolves to cmd.exe on Windows; test cmd-specific syntax
  int ret = run($shell, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run($shell, "exit /b 7");
  ASSERT_EQ(ret, 7);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, BasicRunBashExplicit) {
  // $bash should work on all platforms where bash is available
  int ret = run($bash, "exit 0");
  if (ret != 0) {
    GTEST_SKIP() << "bash not available on this platform";
  }
  ASSERT_EQ(ret, 0);

  ret = run($bash, "exit 33");
  ASSERT_EQ(ret, 33);
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
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "echo hello_shell", $stdout > out);
#else
  int ret = run($shell, "printf '%s' hello_shell", $stdout > out);
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
  dynamic_buffer out;
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

TEST(ShellCommandTest, StdoutCaptureBashExplicit) {
  dynamic_buffer out;
  int ret = run($bash, "echo -n hello_bash", $stdout > out);
  if (ret != 0) {
    GTEST_SKIP() << "bash not available on this platform";
  }
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_bash");
}

// ===========================================================================
// Shell with stderr capture
// ===========================================================================

TEST(ShellCommandTest, StderrCaptureBash) {
  dynamic_buffer err;
#if defined(_WIN32)
  int ret = run($shell, "echo error_text >&2", $stderr > err);
#else
  int ret = run($shell, "printf '%s' error_text >&2", $stderr > err);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(err.empty());
}

TEST(ShellCommandTest, StderrCaptureCmd) {
#if defined(_WIN32)
  dynamic_buffer err;
  int ret = run($shell, "echo error_text >&2", $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(err.empty());
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

// ===========================================================================
// capture_run with shell
// ===========================================================================

TEST(ShellCommandTest, CaptureRunBash) {
#if defined(_WIN32)
  auto [exit_code, out, err] = capture_run(
      $shell, "(echo stdout_text & echo stderr_text >&2) & exit /b 5");
#else
  auto [exit_code, out, err] = capture_run(
      $shell, "printf '%s' stdout_text; printf '%s' stderr_text >&2; exit 5");
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
  dynamic_buffer out;
#if defined(_WIN32)
  int ret =
      run($shell, "echo %SHELL_TEST_VAR%",
          $env = {{"SHELL_TEST_VAR", "env_value_from_shell"}}, $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "env_value_from_shell\r\n");
#else
  int ret =
      run($shell, "printf '%s' \"$SHELL_TEST_VAR\"",
          $env = {{"SHELL_TEST_VAR", "env_value_from_shell"}}, $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "env_value_from_shell");
#endif
}

TEST(ShellCommandTest, ShellRunWithEnvAppend) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "echo %PATH%",
                $env += {{"SHELL_APPEND_VAR", "appended_val"}}, $stdout > out);
#else
  int ret = run($shell, "printf '%s' \"$SHELL_APPEND_VAR\"",
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
  dynamic_buffer out;
  int ret = run($shell, "cd", $cwd = "C:\\", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "C:\\\r\n");
#else
  dynamic_buffer out;
  int ret = run($shell, "pwd", $cwd = "/", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "/\n");
#endif
}

// ===========================================================================
// Shell with stdin redirection
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithStdin) {
#if defined(_WIN32)
  dynamic_buffer in("input_line_1\ninput_line_2\n");
  int ret = run($shell, "findstr input_line_1", $stdin < in);
  ASSERT_EQ(ret, 0);
#else
  dynamic_buffer in("input_line_1\ninput_line_2\n");
  int ret = run($shell, "grep -q input_line_1", $stdin < in);
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// Shell with newgroup
// ===========================================================================

TEST(ShellCommandTest, ShellRunWithNewgroup) {
#if defined(_WIN32)
  int ret = run($shell, "exit /b 0", $newgroup = true);
  ASSERT_EQ(ret, 0);
#else
  int ret = run($shell, "exit 0", $newgroup = true);
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// Shell with devnull redirections
// ===========================================================================

TEST(ShellCommandTest, ShellRunStdoutToDevnull) {
#if defined(_WIN32)
  int ret = run($shell, "echo silenced & exit /b 0", $stdout > $devnull);
#else
  int ret = run($shell, "echo silenced; exit 0", $stdout > $devnull);
#endif
  ASSERT_EQ(ret, 0);
}

TEST(ShellCommandTest, ShellRunStderrToDevnull) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "echo visible & echo hidden >&2 & exit /b 0",
                $stdout > out, $stderr > $devnull);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "visible \r\n");
#else
  int ret = run($shell, "printf '%s' visible; echo hidden >&2; exit 0",
                $stdout > out, $stderr > $devnull);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "visible");
#endif
}

// ===========================================================================
// Shell variadic run syntax
// ===========================================================================

TEST(ShellCommandTest, VariadicShellRun) {
#if defined(_WIN32)
  int ret = run($shell, "exit /b 55");
  ASSERT_EQ(ret, 55);
#else
  int ret = run($shell, "exit 55");
  ASSERT_EQ(ret, 55);
#endif
}

TEST(ShellCommandTest, VariadicShellCaptureRun) {
#if defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run($shell, "echo variadic_capture & exit /b 88");
  ASSERT_EQ(exit_code, 88);
  ASSERT_EQ(out, "variadic_capture \r\n");
#else
  auto [exit_code, out, err] =
      capture_run($shell, "printf '%s' variadic_capture; exit 88");
  ASSERT_EQ(exit_code, 88);
  ASSERT_EQ(out, "variadic_capture");
#endif
}

// ===========================================================================
// Dollar-named shell constants ($shell, $bash, $powershell)
// ===========================================================================

TEST(ShellCommandTest, DollarNamedShellConstants) {
#if defined(_WIN32)
  int ret = run($shell, "exit /b 0");
  ASSERT_EQ(ret, 0);

  ret = run($powershell, "exit 0");
  ASSERT_EQ(ret, 0);
#else
  int ret = run($shell, "exit 0");
  ASSERT_EQ(ret, 0);
#endif
}

// ===========================================================================
// detach_run with shell
// ===========================================================================

TEST(ShellCommandTest, DetachRunShell) {
#if defined(_WIN32)
  bool result = detach_run($shell, "exit /b 0");
  ASSERT_TRUE(result);
#else
  bool result = detach_run($shell, "exit 0");
  ASSERT_TRUE(result);
#endif
}

// ===========================================================================
// Shell with complex commands
// ===========================================================================

TEST(ShellCommandTest, ShellRunMultiLineCommand) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "(echo line1 & echo line2 & echo line3) & exit /b 0",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "line1 \r\nline2 \r\nline3\r\n");
#else
  int ret =
      run($shell, "printf 'line1\\nline2\\nline3\\n'; exit 0", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "line1\nline2\nline3\n");
#endif
}

TEST(ShellCommandTest, ShellRunWithPipes) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "echo hello_pipe| findstr hello", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_pipe\r\n");
#else
  int ret = run($shell, "echo hello_pipe | grep hello", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello_pipe\n");
#endif
}

// ===========================================================================
// Shell with both stdout and stderr captured simultaneously
// ===========================================================================

TEST(ShellCommandTest, ShellCaptureBothStdoutAndStderr) {
  dynamic_buffer out, err;
#if defined(_WIN32)
  int ret = run($shell, "(echo to_out & echo to_err >&2) & exit /b 0",
                $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "to_out \r\n");
  ASSERT_FALSE(err.empty());
#else
  int ret = run($shell, "printf '%s' to_out; printf '%s' to_err >&2; exit 0",
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
  dynamic_buffer err;
#if defined(_WIN32)
  int ret = run($shell, "echo failure_msg >&2 & exit /b 99", $stderr > err);
#else
  int ret = run($shell, "echo failure_msg >&2; exit 99", $stderr > err);
#endif
  ASSERT_EQ(ret, 99);
  ASSERT_FALSE(err.empty());
}

// ===========================================================================
// Shell with empty command output
// ===========================================================================

TEST(ShellCommandTest, ShellRunEmptyOutput) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ret = run($shell, "exit /b 0", $stdout > out);
#else
  int ret = run($shell, "exit 0", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  // Output should be empty or contain only whitespace/newlines
  // ($shell with no echo produces empty stdout)
}

// Helper: skip if bash returned non-zero (likely not available)
static bool is_bash_available() {
  int ret = run(subprocess::named_arguments::bash, "exit 0");
  return ret == 0;
}

// ===========================================================================
// Complex shell commands — double quotes, special characters, functions
// ===========================================================================

// --- Bash: double quotes with variable expansion ---

TEST(ShellCommandTest, BashDoubleQuotesWithVarExpansion) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "FOO='hello world'; echo \"$FOO\"", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "hello world\n");
}

TEST(ShellCommandTest, BashEscapedDoubleQuotesInsideString) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "echo \"quoted string with spaces\"", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "quoted string with spaces\n");
}

TEST(ShellCommandTest, BashSingleQuotesLiteral) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  // Single quotes prevent expansion: $HOME is literal
  int ret = run($bash, "echo 'literal $HOME'", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "literal $HOME\n");
}

// --- Bash: function definition and call ---

TEST(ShellCommandTest, BashFunctionDefinitionSimple) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "myfunc() { echo -n 'called'; }; myfunc", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "called");
}

TEST(ShellCommandTest, BashFunctionWithArguments) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "greet() { echo -n \"Hello, $1!\"; }; greet World",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "Hello, World!");
}

TEST(ShellCommandTest, BashFunctionWithReturnValue) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  int ret = run($bash, "is_even() { return $(($1 % 2)); }; is_even 42");
  ASSERT_EQ(ret, 0);

  ret = run($bash, "is_even() { return $(($1 % 2)); }; is_even 7");
  ASSERT_EQ(ret, 1);
}

TEST(ShellCommandTest, BashFunctionCallingAnotherFunction) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash,
                "get_msg() { echo -n 'nested call'; };"
                " outer() { echo -n \"[$1]\"; };"
                " outer \"$(get_msg)\"",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "[nested call]");
}

// --- Bash: special characters ---

TEST(ShellCommandTest, BashCommandSubstitution) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "echo -n $(echo inner)", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "inner");
}

TEST(ShellCommandTest, BashArithmeticExpansion) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "echo -n $((3 * 7 + 1))", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "22");
}

TEST(ShellCommandTest, BashBackslashEscapes) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "echo -ne 'tab:\\t newline:\\n backslash:\\\\'",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "tab:\t newline:\n backslash:\\");
}

TEST(ShellCommandTest, BashPipeChainWithSpecialChars) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "echo -n 'a b c d e' | tr ' ' '|' | tr 'a-z' 'A-Z'",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "A|B|C|D|E");
}

TEST(ShellCommandTest, BashRedirectStderrToStdout) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  // Redirect stderr (fd 2) to stdout (fd 1) within the shell command itself.
  // The 2>&1 merges both streams before they reach separate pipes,
  // so all output appears on stdout.
  int ret = run($bash, "exec 2>&1; echo -n to_out; echo -n to_err >&2",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "to_outto_err");
}

// --- Bash: loops ---

TEST(ShellCommandTest, BashForLoop) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret =
      run($bash, "for i in 1 2 3; do echo -n \"$i \"; done", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "1 2 3 ");
}

TEST(ShellCommandTest, BashWhileLoop) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash,
                "i=0; while [ $i -lt 3 ]; do echo -n \"$i \"; i=$((i+1)); done",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "0 1 2 ");
}

// --- Bash: complex one-liners ---

TEST(ShellCommandTest, BashIfElseConditional) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "if [ 5 -gt 3 ]; then echo -n yes; else echo -n no; fi",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "yes");
}

TEST(ShellCommandTest, BashCaseStatement) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash,
                "x='two'; case $x in one) echo -n 1;; two) echo -n 2;; *) echo "
                "-n x;; esac",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "2");
}

TEST(ShellCommandTest, BashHereStringIntoGrep) {
  if (!is_bash_available()) {
    GTEST_SKIP() << "bash not available";
  }
  dynamic_buffer out;
  int ret = run($bash, "grep -o world <<< 'hello world'", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "world\n");
}

// ===========================================================================
// Complex CMD commands — special characters, variables, subroutines
// ===========================================================================

TEST(ShellCommandTest, CmdDoubleQuotesWithAmpersand) {
#if defined(_WIN32)
  dynamic_buffer out;
  // Double quotes protect the & from being interpreted as command separator
  int ret = run($shell, "echo \"hello & world\"", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "\"hello & world\"\r\n");
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdPercentVariableExpansion) {
#if defined(_WIN32)
  dynamic_buffer out;
  // In cmd /c mode, %FOO% is expanded at parse time (before set runs).
  // Use 'set FOO' (without value) to display the variable instead.
  int ret = run($shell, "set FOO=hello_cmd_var & set FOO", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("FOO=hello_cmd_var") != std::string::npos);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdIfElseConditional) {
#if defined(_WIN32)
  dynamic_buffer out;
  // cmd supports if/else blocks with parentheses
  int ret = run($shell, "if 1==1 (echo yes_branch) else (echo no_branch)",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("yes_branch") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("no_branch") == std::string::npos);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdForLoopCounting) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret = run($shell, "for /L %i in (1,1,3) do @echo %i", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "1\r\n2\r\n3\r\n");
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdErrorlevelPropagation) {
#if defined(_WIN32)
  int ret = run($shell, "(call) & exit /b 99");
  ASSERT_EQ(ret, 99);

  ret = run($shell, "ver >nul & exit /b %errorlevel%");
  ASSERT_EQ(ret, 0);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdVariableInNestedExpansion) {
#if defined(_WIN32)
  dynamic_buffer out;
  // In cmd /c, variables set earlier in the line are available via call echo
  // because call triggers a second parse pass.
  int ret = run($shell, "set X=inner_value & call echo %X%", $stdout > out);
  ASSERT_EQ(ret, 0);
  // %X% is expanded on the second parse triggered by 'call'
  ASSERT_TRUE(out.to_string().find("inner_value") != std::string::npos);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

TEST(ShellCommandTest, CmdConditionalExecution) {
#if defined(_WIN32)
  dynamic_buffer out;
  // && means execute next only if previous succeeded
  int ret = run($shell, "echo success_msg && echo also_ran", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("success_msg") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("also_ran") != std::string::npos);

  // || means execute next only if previous failed
  // 'find' with a non-matching string returns errorlevel 1
  out.clear();
  ret =
      run($shell, "echo test | findstr /c:nomatch >nul || echo previous_failed",
          $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("previous_failed") != std::string::npos);
#else
  GTEST_SKIP() << "cmd.exe is Windows-only";
#endif
}

// ===========================================================================
// Complex PowerShell commands — functions, script blocks, pipelines
// ===========================================================================

TEST(ShellCommandTest, PowerShellFunctionDefinition) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret = run(powershell,
                "function Test-Func { param($Name) \"Hello, $Name!\" }; "
                "Test-Func -Name World",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("Hello, World!") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellScriptBlock) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret = run(powershell, "& { param($x) $x * $x } 7", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("49") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellPipelineWithWhere) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret = run(powershell,
                "1,2,3,4,5 | Where-Object { $_ -gt 3 } | ForEach-Object { "
                "Write-Output $_ }",
                $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("4") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("5") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("3") == std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellHereString) {
#if defined(_WIN32)
  dynamic_buffer out;
  // PowerShell here-string: @' ... '@ must have content on separate lines.
  // Use embedded newlines (backtick-n) in a regular string for single-line
  // commands, or use a verbatim string with actual newlines.
  int ret =
      run(powershell,
          "$msg = \"double-quoted`nmulti-line`nstring\"; Write-Output $msg",
          $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("double-quoted") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("multi-line") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("string") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellErrorActionPreference) {
#if defined(_WIN32)
  dynamic_buffer out, err;
  int ret = run(powershell,
                "$ErrorActionPreference = 'Stop'; try { Get-Item "
                "nonexistent_path } catch { Write-Output 'caught' }",
                $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("caught") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellSpecialCharactersInString) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret =
      run(powershell,
          "Write-Output 'dollar$sign backtick`n newline ampersand& pipe|'",
          $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("dollar$sign") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("ampersand&") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("pipe|") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

TEST(ShellCommandTest, PowerShellCalculatedProperty) {
#if defined(_WIN32)
  dynamic_buffer out;
  int ret =
      run(powershell,
          "[PSCustomObject]@{Name='test';Value=42} | Select-Object Name,Value",
          $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("test") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("42") != std::string::npos);
#else
  GTEST_SKIP() << "powershell is Windows-only";
#endif
}

// ===========================================================================
// Cross-shell: mixed stdout/stderr with complex content
// ===========================================================================

TEST(ShellCommandTest, ComplexMixedStdoutStderr) {
#if defined(_WIN32)
  dynamic_buffer out, err;
  // cmd: odd numbers to stdout, even to stderr
  int ret =
      run($shell,
          "(echo 1 & echo 2 >&2 & echo 3 & echo 4 >&2 & echo 5) & exit /b 0",
          $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_TRUE(out.to_string().find("1") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("3") != std::string::npos);
  ASSERT_TRUE(out.to_string().find("5") != std::string::npos);
  ASSERT_FALSE(err.empty());
#else
  dynamic_buffer out, err;
  // bash: odd numbers to stdout, even to stderr
  int ret = run($shell,
                "for i in 1 2 3 4 5; do if [ $((i%2)) -eq 0 ]; then printf "
                "'%s' $i >&2; else printf '%s' $i; fi; done",
                $stdout > out, $stderr > err);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "135");
  ASSERT_EQ(err, "24");
#endif
}
