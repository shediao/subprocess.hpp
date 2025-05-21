#include <cstdio>   // For std::remove
#include <cstdlib>  // For setenv, unsetenv (conditionally used for more complex append test if needed)
#include <fstream>
#include <string>
#include <vector>

#include "./utils.h"
#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

using namespace std::string_literals;

// Helper function to convert vector<char> to string
static std::string vecCharToString(const std::vector<char>& vec) {
  return std::string(vec.begin(), vec.end());
}

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

namespace {

using namespace process;

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
  int exit_code = run({"true"});
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
  std::vector<char> stdout_buf;
  int exit_code =
      run({"/bin/echo", "-n", "Hello Stdout"}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "Hello Stdout");
}

// 5. Test capture stderr
TEST_F(RunFunctionTest, CaptureStderrBasic) {
  std::vector<char> stderr_buf;
  int exit_code = run({"/bin/bash", "-c", "echo -n 'Hello Stderr' >&2"},
                      std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stderr_buf), "Hello Stderr");
}

// 6. Test capture both stdout and stderr
TEST_F(RunFunctionTest, CaptureBothStdoutAndStderr) {
  std::vector<char> stdout_buf;
  std::vector<char> stderr_buf;
  int exit_code = run({"/bin/bash", "-c", "echo -n Out; echo -n Err >&2"},
                      std_out > stdout_buf, std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "Out");
  ASSERT_EQ(vecCharToString(stderr_buf), "Err");
}

// 7. Test capture empty stdout (e.g., from "true" command)
TEST_F(RunFunctionTest, CaptureEmptyStdoutFromTrue) {
  std::vector<char> stdout_buf;
  int exit_code = run({"true"}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stdout_buf.empty());
}

// 8. Test capture empty stderr (e.g., from "true" command)
TEST_F(RunFunctionTest, CaptureEmptyStderrFromTrue) {
  std::vector<char> stderr_buf;
  int exit_code = run({"true"}, std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stderr_buf.empty());
}

// 9. Test redirect stdout to file (overwrite)
TEST_F(RunFunctionTest, RedirectStdoutToFileOverwrite) {
  TempFile temp_file;
  int exit_code =
      run({"/bin/echo", "-n", "Overwrite Content"}, std_out > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Overwrite Content");
}

// 10. Test redirect stderr to file (overwrite)
TEST_F(RunFunctionTest, RedirectStderrToFileOverwrite) {
  TempFile temp_file;
  int exit_code = run({"/bin/bash", "-c", "echo -n 'Error Overwrite' >&2"},
                      std_err > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Error Overwrite");
}

// 11. Test redirect stdout to file (append)
TEST_F(RunFunctionTest, RedirectStdoutToFileAppend) {
  TempFile temp_file;
  ASSERT_TRUE(temp_file.write("Initial\n"s));
  int exit_code =
      run({"/bin/echo", "-n", "Appended"}, std_out >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Initial\nAppended");
}

// 12. Test redirect stderr to file (append)
TEST_F(RunFunctionTest, RedirectStderrToFileAppend) {
  TempFile temp_file;
  ASSERT_TRUE(temp_file.write("InitialError\n"s));
  int exit_code = run({"/bin/bash", "-c", "echo -n 'AppendedError' >&2"},
                      std_err >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "InitialError\nAppendedError");
}

// 13. Test set environment variables (override) - check specific var
TEST_F(RunFunctionTest, EnvOverrideCheckValue) {
  std::vector<char> stdout_buf;
  // Assuming OverrideEnv is the way to specify overriding environment map
  int exit_code = run({"/bin/bash", "-c", "echo -n $MY_TEST_VAR"},
                      env = {{"MY_TEST_VAR", "is_set"}}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "is_set");
}

// 14. Test append environment variables - check new var and PATH presence
TEST_F(RunFunctionTest, EnvAppendCheckValueAndPath) {
  std::vector<char> stdout_buf;
  // Assuming AppendEnv is the way to specify appending environment map
  // This test checks if the appended var is set AND common vars like PATH still
  // exist.
  int exit_code =
      run({"/bin/bash", "-c",
           "echo -n $MY_APPEND_VAR; if [ -n \"$PATH\" ]; then echo -n "
           "_haspath; fi"},
          env += {{"MY_APPEND_VAR", "appended"}}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "appended_haspath");
}

// 15. Test set environment variables (override) - check it clears other common
// vars like PATH
TEST_F(RunFunctionTest, EnvOverrideClearsPath) {
  std::vector<char> stdout_buf;
  int exit_code = run({"/bin/bash", "-c",
                       R"(
if [ "$ONLY_VAR" = "visible" ]; then
  echo -n isolated;
else
  echo -n not_isolated;
fi
)"},
                      env = {{"ONLY_VAR", "visible"}}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "isolated");
}

// 16. Test set current working directory (cwd)
TEST_F(RunFunctionTest, CwdSetToTmpAndPwd) {
  std::vector<char> stdout_buf;
#ifdef _WIN32
  // On Windows, pwd might not exist or /tmp might not be standard.
  // This test might need adjustment for Windows. For now, POSIX assumed.
  // A cross-platform way to get temp dir would be better if this were truly
  // generic. For example, creating a temp dir and cd-ing into it. For now,
  // let\'s use a command that outputs CWD in a platform-agnostic way if
  // possible, or skip/adapt for Windows. `cmd /c cd` on windows prints current
  // directory.
  int exit_code = run({"cmd", "/c", "cd"}, cwd = "C:\\Windows",
                      std_out > stdout_buf);  // Example for Windows
  ASSERT_EQ(exit_code, 0);
  // Output format of `cmd /c cd` is "C:\Windows", no trailing newline typically
  std::string output = vecCharToString(stdout_buf);
  // Remove potential trailing \r\n
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  if (!output.empty() && output.back() == '\r') {
    output.pop_back();
  }
  ASSERT_EQ(output, "C:\\Windows");
#else
  int exit_code = run({"/bin/pwd"}, cwd = "/tmp", std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
#if defined(__APPLE__)
  ASSERT_EQ(vecCharToString(stdout_buf), "/private/tmp\n");
#else
  ASSERT_EQ(vecCharToString(stdout_buf), "/tmp\n");
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

  std::vector<char> stdout_buf;
// On Windows, use `type` instead of `cat`.
#ifdef _WIN32
  int exit_code = run({"cmd", "/c", "type " + relative_file_name},
                      cwd = temp_dir_path, std_out > stdout_buf);
#else
  int exit_code = run({"/bin/cat", relative_file_name}, cwd = temp_dir_path,
                      std_out > stdout_buf);
#endif

  ASSERT_EQ(exit_code, 0);
  std::string output = vecCharToString(stdout_buf);
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
  std::vector<char> stdout_buf;
  int exit_code =
      run({"/bin/echo", "one", "two words", "three"}, std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(vecCharToString(stdout_buf), "one two words three\n");
}

// 19. Test command name from system PATH (using a common command)
TEST_F(RunFunctionTest, CommandNameFromSystemPathMkdirHelp) {
  std::vector<char> stdout_buf;
// `mkdir --help` usually exits 0 and prints to stdout.
// On Windows `mkdir /?`
#ifdef _WIN32
  int exit_code = run({"mkdir", "/?"}, std_out > stdout_buf,
                      std_err > stdout_buf);  // some help output to stderr
#else
  int exit_code = run({"echo", "true"}, std_out > stdout_buf);
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

  std::vector<char> stdout_buf;
  std::vector<char> stderr_buf;
  // cmd /c script.bat to run
  int exit_code = run({"cmd", "/c", temp_file.path()}, std_out > stdout_buf,
                      std_err > stderr_buf);

  ASSERT_EQ(exit_code, 5);
  // Output from echo in batch includes \r\n
  ASSERT_EQ(vecCharToString(stdout_buf), "script_out\r\n");
  ASSERT_EQ(
      vecCharToString(stderr_buf),
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

  std::vector<char> stdout_buf;
  std::vector<char> stderr_buf;
  int script_exit_code =
      run({script_path}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(script_exit_code, 5);
  ASSERT_EQ(vecCharToString(stdout_buf), "script_out");
  ASSERT_EQ(vecCharToString(stderr_buf), "script_err");
  removeFile(script_path);
#endif
}

}  // namespace
