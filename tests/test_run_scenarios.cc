#include <string>
#include <vector>

#include "./utils.h"
#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

using namespace std::string_literals;
using namespace subprocess;
using namespace subprocess::named_arguments;

#if !defined(_WIN32)
// Helper function to write file contents (for setting up append tests)
static bool writeFileContents(const std::string& path,
                              const std::string& content) {
  std::ofstream outfile(path);
  if (!outfile.is_open()) {
    return false;
  }
  outfile << content;
  outfile.close();
  return true;
}

// Helper function to remove a file
static void removeFile(const std::string& path) { std::remove(path.c_str()); }
#endif

namespace {

class RunFunctionTest : public ::testing::Test {
 protected:
  // Per-test-suite set-up.
  // Called before the first test in this test suite.
  static void SetUpTestSuite() {
    // Ensure /tmp exists and is writable if tests rely on it heavily.
    // For these tests, we assume /tmp is standard and available.
  }

  // Per-test-suite tear-down.
  // Called after the last test in this test suite.
  static void TearDownTestSuite() {}

  // Per-test set-up.
  // Called before each test.
  void SetUp() override {}

  // Per-test tear-down.
  // Called after each test.
  void TearDown() override {
    // General cleanup if any test forgets, though tests should manage their own
    // files. removeFile("/tmp/test_run_stdout_overwrite.txt"); // Example,
    // better to make names unique
  }
};

// 1. Test simple command success
TEST_F(RunFunctionTest, RunTrueCommand) {
  int exit_code = run({
#if defined(_WIN32)
      TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")
#else
      "true"
#endif
  });
  ASSERT_EQ(exit_code, 0);
}

// 2. Test simple command failure
TEST_F(RunFunctionTest, RunFalseCommand) {
  int exit_code = run({"false"});
  ASSERT_NE(exit_code, 0);  // Typically 1 for "false"
}

// 3. Test non-existent command
TEST_F(RunFunctionTest, RunNonExistentCommand) {
  int exit_code = run({"this_command_does_not_exist_123xyz"});
  ASSERT_NE(exit_code,
            0);  // Often 127, but depends on shell/OS and subprocess lib
}

// 4. Test capture stdout
TEST_F(RunFunctionTest, CaptureStdoutBasic) {
  subprocess::buffer stdout_buf;
  int exit_code = run(
#if defined(_WIN32)
      {
        TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=Hello Stdout&exit /b 0")
      }
#else
      {"/bin/echo", "-n", "Hello Stdout"}
#endif
      ,
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "Hello Stdout");
}

// 5. Test capture stderr
TEST_F(RunFunctionTest, CaptureStderrBasic) {
  subprocess::buffer stderr_buf;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"),
          TEXT("<nul set /p=Hello Stderr>&2&exit /b 0")
#else
          "/bin/bash", "-c", "echo -n 'Hello Stderr' >&2"
#endif
      },
      std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stderr_buf.to_string(), "Hello Stderr");
}

// 6. Test capture both stdout and stderr
TEST_F(RunFunctionTest, CaptureBothStdoutAndStderr) {
  subprocess::buffer stdout_buf;
  subprocess::buffer stderr_buf;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"),
          TEXT("<nul set /p=Out& <nul set /p=Err>&2&exit /b 0")
#else
          "/bin/bash", "-c", "echo -n Out; echo -n Err >&2"
#endif
      },
      std_out > stdout_buf, std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "Out");
  ASSERT_EQ(stderr_buf.to_string(), "Err");
}

// 7. Test capture empty stdout (e.g., from "true" command)
TEST_F(RunFunctionTest, CaptureEmptyStdoutFromTrue) {
  subprocess::buffer stdout_buf;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")
#else
          "true"
#endif
      },
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stdout_buf.empty());
}

// 8. Test capture empty stderr (e.g., from "true" command)
TEST_F(RunFunctionTest, CaptureEmptyStderrFromTrue) {
  subprocess::buffer stderr_buf;
  int exit_code = run(
#if defined(_WIN32)
      TEXT("cmd.exe"), TEXT("/c"), TEXT("exit /b 0")
#else
      "true"
#endif
                                       ,
      std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stderr_buf.empty());
}

// 9. Test redirect stdout to file (overwrite)
TEST_F(RunFunctionTest, RedirectStdoutToFileOverwrite) {
  TempFile temp_file;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"),
          TEXT("<nul set /p=Overwrite Content&exit /b 0")
#else
          "/bin/echo", "-n", "Overwrite Content"
#endif
      },
      std_out > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Overwrite Content");
}

// 10. Test redirect stderr to file (overwrite)
TEST_F(RunFunctionTest, RedirectStderrToFileOverwrite) {
  TempFile temp_file;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"),
          TEXT("<nul set /p=Error Overwrite>&2&exit /b 0")
#else
          "/bin/bash", "-c", "echo -n 'Error Overwrite' >&2"
#endif
      },
      std_err > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Error Overwrite");
}

// 11. Test redirect stdout to file (append)
TEST_F(RunFunctionTest, RedirectStdoutToFileAppend) {
  TempFile temp_file;
  ASSERT_TRUE(temp_file.write("Initial\n"s));
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=Appended&exit /b 0")
#else
          "/bin/echo", "-n", "Appended"
#endif
      },
      std_out >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Initial\nAppended");
}

// 12. Test redirect stderr to file (append)
TEST_F(RunFunctionTest, RedirectStderrToFileAppend) {
  TempFile temp_file;
  ASSERT_TRUE(temp_file.write("InitialError\n"s));
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"),
          TEXT("<nul set /p=AppendedError>&2&exit /b 0")
#else
          "/bin/bash", "-c", "echo -n 'AppendedError' >&2"
#endif
      },
      std_err >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "InitialError\nAppendedError");
}

// 13. Test set environment variables (override) - check specific var
TEST_F(RunFunctionTest, EnvOverrideCheckValue) {
  subprocess::buffer stdout_buf;
  // Assuming OverrideEnv is the way to specify overriding environment map
#if defined(_WIN32)
  int exit_code = run(
      TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=%MY_TEST_VAR%&exit /b 0"),
      env = {{TEXT("MY_TEST_VAR"), TEXT("is_set")}}, std_out > stdout_buf);
#else
  int exit_code = run("/bin/bash", "-c", "echo -n $MY_TEST_VAR",
                      env = {{"MY_TEST_VAR", "is_set"}}, std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "is_set");
}

// 14. Test append environment variables - check new var and PATH presence
TEST_F(RunFunctionTest, EnvAppendCheckValueAndPath) {
  subprocess::buffer stdout_buf;
  // Assuming AppendEnv is the way to specify appending environment map
  // This test checks if the appended var is set AND common vars like PATH still
  // exist.
#if defined(_WIN32)
  int exit_code =
      run({TEXT("cmd.exe"), TEXT("/c"),
           TEXT("<nul set /p=%MY_APPEND_VAR%&if defined PATH (<nul set "
                "/p=_haspath)&exit /b 0")},
          env += {{"MY_APPEND_VAR", "appended"}}, std_out > stdout_buf);
#else
  int exit_code =
      run({"/bin/bash", "-c",
           "echo -n $MY_APPEND_VAR; if [ -n \"$PATH\" ]; then echo -n "
           "_haspath; fi"},
          env += {{"MY_APPEND_VAR", "appended"}}, std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "appended_haspath");
}

// 15. Test set environment variables (override) - check it clears other common
// vars like PATH
TEST_F(RunFunctionTest, EnvOverrideClearsPath) {
  subprocess::buffer stdout_buf;
#if defined(_WIN32)
  int exit_code = run(
      {"cmd.exe", "/c",
       R"(if "%ONLY_VAR%"=="visible" (<nul set /p=isolated& exit /b 0) else (<nul set /p=not_isolated& exit /b 0))"},
      env = {{"ONLY_VAR", "visible"}}, std_out > stdout_buf);
#else
  int exit_code = run({"/bin/bash", "-c",
                       R"(
if [ "$ONLY_VAR" = "visible" ]; then
  echo -n isolated;
else
  echo -n not_isolated;
fi
)"},
                      env = {{"ONLY_VAR", "visible"}}, std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "isolated");
}

// 16. Test set current working directory (cwd)
TEST_F(RunFunctionTest, CwdSetToTmpAndPwd) {
  subprocess::buffer stdout_buf;
#ifdef _WIN32
  // On Windows, pwd might not exist or /tmp might not be standard.
  // This test might need adjustment for Windows. For now, POSIX assumed.
  // A cross-platform way to get temp dir would be better if this were truly
  // generic. For example, creating a temp dir and cd-ing into it. For now,
  // let\'s use a command that outputs CWD in a platform-agnostic way if
  // possible, or skip/adapt for Windows. `cmd /c cd` on windows prints current
  // directory.
  int exit_code = run("cmd.exe", "/c", "cd", cwd = "C:\\Windows",
                      std_out > stdout_buf);  // Example for Windows
  ASSERT_EQ(exit_code, 0);
  // Output format of `cmd /c cd` is "C:\Windows", no trailing newline typically
  std::string output = stdout_buf.to_string();
  // Remove potential trailing \r\n
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  if (!output.empty() && output.back() == '\r') {
    output.pop_back();
  }
  ASSERT_EQ(output, "C:\\Windows");
#else
  int exit_code = run("/bin/pwd", cwd = "/tmp", std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
#if defined(__APPLE__)
  ASSERT_EQ(stdout_buf.to_string(), "/private/tmp\n");
#else
  ASSERT_EQ(stdout_buf.to_string(), "/tmp\n");
#endif
#endif
}

// 17. Test set cwd and read a relative file from that CWD
TEST_F(RunFunctionTest, CwdSetAndReadRelativeFile) {
  TempFile temp_file;
  const std::string temp_dir_path =
      std::filesystem::path(temp_file.path()).parent_path().string();
  const std::string relative_file_name =
      std::filesystem::path(temp_file.path()).filename().string();
  ASSERT_TRUE(temp_file.write("Relative Content"s));

  subprocess::buffer stdout_buf;
// On Windows, use `type` instead of `cat`.
#ifdef _WIN32
  int exit_code = run({"cmd.exe", "/c", "type " + relative_file_name},
                      cwd = temp_dir_path, std_out > stdout_buf);
#else
  int exit_code = run({"/bin/cat", relative_file_name}, cwd = temp_dir_path,
                      std_out > stdout_buf);
#endif

  ASSERT_EQ(exit_code, 0);
  std::string output = stdout_buf.to_string();
#ifdef _WIN32  // `type` command might add \r\n
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  if (!output.empty() && output.back() == '\r') {
    output.pop_back();
  }
#endif
  ASSERT_EQ(output, "Relative Content");
}

// 18. Test command with multiple arguments, including one with spaces
TEST_F(RunFunctionTest, CommandWithMultipleArgumentsComplex) {
  subprocess::buffer stdout_buf;
  int exit_code = run(
      {
#if defined(_WIN32)
          TEXT("cmd.exe"), TEXT("/c"), TEXT("echo one two words three")
#else
          "/bin/echo", "one", "two words", "three"
#endif
      },
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
#if defined(_WIN32)
  ASSERT_EQ(stdout_buf.to_string(), "one two words three\r\n");
#else
  ASSERT_EQ(stdout_buf.to_string(), "one two words three\n");
#endif
}

// 19. Test command name from system PATH (using a common command)
TEST_F(RunFunctionTest, CommandNameFromSystemPathMkdirHelp) {
  subprocess::buffer stdout_buf;
// `mkdir --help` usually exits 0 and prints to stdout.
// On Windows `mkdir /?`
#ifdef _WIN32
  int exit_code = run(TEXT("cmd.exe"), TEXT("/c"), TEXT("echo true&exit /b 0"),
                      std_out > stdout_buf,
                      std_err > stdout_buf);  // some help output to stderr
#else
  int exit_code = run("echo", "true", std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(stdout_buf.empty());  // Check that it produced some output
}

// 20. Test running a shell script, checking stdout, stderr, and exit code
TEST_F(RunFunctionTest, RunShellScriptFull) {
#ifdef _WIN32
  // Shell scripts (.sh) are not directly executable on Windows without WSL or
  // similar. This test would need to be adapted to a .bat or .ps1 script. For
  // now, skipping on Windows or using a simple .bat example.
  TempFile temp_file("", "-test_run_script.bat");
  ASSERT_TRUE(temp_file.write(
      "@echo off\necho script_out\necho script_err 1>&2\nexit /b 5"s));

  subprocess::buffer stdout_buf;
  subprocess::buffer stderr_buf;
  // cmd /c script.bat to run
  int exit_code = run({"cmd.exe", "/c", temp_file.path()}, std_out > stdout_buf,
                      std_err > stderr_buf);

  ASSERT_EQ(exit_code, 5);
  // Output from echo in batch includes \r\n
  ASSERT_EQ(stdout_buf.to_string(), "script_out\r\n");
  ASSERT_EQ(
      stderr_buf.to_string(),
      "script_err \r\n");  // Note: stderr from echo might have trailing space
#else
  const std::string script_path = "/tmp/test_run_script.sh";
  removeFile(script_path);  // Ensure clean state
  ASSERT_TRUE(writeFileContents(script_path,
                                "#!/bin/bash\n"
                                "echo -n 'script_out'\n"
                                "echo -n 'script_err' >&2\n"
                                "exit 5"));

  // Make the script executable
  int chmod_exit_code = run({"/bin/chmod", "+x", script_path});
  ASSERT_EQ(chmod_exit_code, 0);

  subprocess::buffer stdout_buf;
  subprocess::buffer stderr_buf;
  int script_exit_code =
      run({script_path}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(script_exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out");
  ASSERT_EQ(stderr_buf.to_string(), "script_err");
  removeFile(script_path);
#endif
}

}  // namespace
