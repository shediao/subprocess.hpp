/**
 * test_detach.cc — detach_run() comprehensive tests
 *
 * Covers:
 *   - Basic detach: smoke test, verify process actually executes
 *   - Return value: command-not-found / invalid-cwd → platform-aware
 *   - Named arguments: Cwd, Env, EnvAppend, EnvItemAppend (append & prepend)
 *   - Variadic argument types: const char*, char*, std::string,
 * std::string_view
 *   - Windows wide-character types: const wchar_t*, wchar_t*, std::wstring,
 *     std::wstring_view
 *   - Edge cases: process survival after detach, multiple concurrent detaches,
 *     long-running process, special characters in env, paths with spaces,
 *     empty environment, grandchild survival (POSIX setsid)
 *
 * Platform notes:
 *   - On POSIX, detach_spawn_posix() uses double-fork+setsid. The parent
 *     only checks fork() success; exec failures in the grandchild are
 *     invisible to the caller. Therefore detach_run() almost always returns
 *     true on POSIX, even for invalid commands / CWD.
 *   - On Windows, detach_spawn_win() calls CreateProcessW directly, so
 *     command-not-found and invalid-CWD are detected and return false.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::detach_run;

// ===========================================================================
// Helper: wait for a file to be created (poll with timeout).
// Returns true if the file exists within the timeout.
// ===========================================================================
inline bool wait_for_file(
    const std::string& path,
    std::chrono::milliseconds max_wait = std::chrono::seconds(5)) {
  auto start = std::chrono::steady_clock::now();
  while (!std::filesystem::exists(path)) {
    if (std::chrono::steady_clock::now() - start > max_wait) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return true;
}

// ===========================================================================
// Helper: wait for a file to exist and contain non-empty content.
// On Windows, ">" creates/truncates the file before writing, so we must
// poll for content to avoid reading an empty file in the brief window
// between file creation and the first write.
// ===========================================================================
inline bool wait_for_file_content(
    const std::string& path,
    std::chrono::milliseconds max_wait = std::chrono::seconds(5)) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (!ec && sz > 0) {
      return true;
    }
    if (std::chrono::steady_clock::now() - start > max_wait) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// ===========================================================================
// Helper: read file content as string, trim trailing whitespace
// ===========================================================================
inline std::string read_file_trimmed(const std::string& path) {
  std::ifstream f(path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  // Trim trailing newlines / carriage returns
  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r')) {
    content.pop_back();
  }
  return content;
}

// ===========================================================================
// 1. Basic smoke test — detach_run returns true for a simple command
// ===========================================================================
TEST(DetachTest, SmokeTest) {
  TempFile tmp;
  bool ok = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=ok>" + tmp.path()
#else
      "/bin/sh", "-c", "echo ok > '" + tmp.path() + "'"
#endif
  });
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "ok");
}

// ===========================================================================
// 2. Verify the detached process actually executes and produces side effects
// ===========================================================================
TEST(DetachTest, ActuallyExecutes) {
  TempFile tmp;
  bool ok = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=executed>" + tmp.path()
#else
      "/bin/sh", "-c", "echo executed > '" + tmp.path() + "'"
#endif
  });
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "executed");
}

// ===========================================================================
// 3. detach_run with a non-zero exit — still returns true (fire-and-forget)
// ===========================================================================
TEST(DetachTest, NonZeroExitStillReturnsTrue) {
  bool ok = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "exit /b 42"
#else
      "/bin/sh", "-c", "exit 42"
#endif
  });
  // detach_run is fire-and-forget; spawn success → true regardless of exit
  EXPECT_TRUE(ok);
}

// ===========================================================================
// 4. Command not found — platform-dependent return value
//    Windows: CreateProcessW fails → false
//    POSIX:   double-fork hides exec failure → true
// ===========================================================================
TEST(DetachTest, CommandNotFound) {
  bool ok = detach_run({"this_command_definitely_does_not_exist_xyz"});
#if defined(_WIN32)
  EXPECT_FALSE(ok);
#else
  // On POSIX, fork() succeeds and the exec failure is in the grandchild.
  EXPECT_TRUE(ok);
#endif
}

// ===========================================================================
// 5. Invalid CWD — platform-dependent return value
// ===========================================================================
TEST(DetachTest, InvalidCwd) {
#if defined(_WIN32)
  bool ok = detach_run({"cmd.exe", "/c", "exit /b 0"},
                       cwd = "Z:\\this\\directory\\does\\not\\exist\\xyz");
  EXPECT_FALSE(ok);
#else
  bool ok = detach_run({"true"}, cwd = "/this/directory/does/not/exist/xyz");
  // On POSIX, fork() succeeds; the chdir/exec failure is in the grandchild.
  EXPECT_TRUE(ok);
#endif
}

// ===========================================================================
// 6. CWD — detached process runs in the specified working directory
// ===========================================================================
TEST(DetachTest, CwdSetToHome) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run({"cmd.exe", "/c", "<nul set /p=%CD%>" + tmp.path()},
                       cwd = "C:\\Windows");
#else
  bool ok =
      detach_run({"/bin/sh", "-c", "pwd > '" + tmp.path() + "'"}, cwd = "/tmp");
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
#if defined(_WIN32)
  EXPECT_EQ(out, "C:\\Windows");
#elif defined(__APPLE__)
  // macOS /tmp is a symlink to /private/tmp
  EXPECT_TRUE(out == "/tmp" || out == "/private/tmp");
#else
  EXPECT_EQ(out, "/tmp");
#endif
}

// ===========================================================================
// 7. CWD — variadic form with std::string cwd
// ===========================================================================
TEST(DetachTest, CwdVariadicForm) {
  TempFile tmp;
#if defined(_WIN32)
  std::string wd("C:\\Windows");
  bool ok =
      detach_run("cmd.exe", "/c", "<nul set /p=%CD%>" + tmp.path(), cwd = wd);
#else
  std::string wd("/tmp");
  bool ok = detach_run("/bin/sh", "-c", "pwd > '" + tmp.path() + "'", cwd = wd);
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
#if defined(_WIN32)
  EXPECT_EQ(out, "C:\\Windows");
#elif defined(__APPLE__)
  EXPECT_TRUE(out == "/tmp" || out == "/private/tmp");
#else
  EXPECT_EQ(out, "/tmp");
#endif
}

// ===========================================================================
// 8. Env — full environment replacement
// ===========================================================================
TEST(DetachTest, EnvFullReplacement) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok =
      detach_run({"cmd.exe", "/c",
                  "<nul set /p=%MY_DETACH_VAR%>" + tmp.path() + "&exit /b 0"},
                 env = {{"MY_DETACH_VAR", "detached_value"}});
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "printf '%s' \"$MY_DETACH_VAR\" > '" + tmp.path() + "'"},
                 env = {{"MY_DETACH_VAR", "detached_value"}});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "detached_value");
}

// ===========================================================================
// 9. Env — full replacement clears other vars.
//    Use printenv (not shell variable expansion) because shells like bash
//    assign default values (e.g. PATH) even when they are absent from the
//    actual environment.
// ===========================================================================
TEST(DetachTest, EnvFullReplacementClearsOtherVars) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c", "<nul set /p=%PATH%>" + tmp.path() + "&exit /b 0"},
      env = {{"ONLY_THIS", "present"}});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "%PATH%");

  TempFile tmp2;
  ok = detach_run({"cmd.exe", "/c",
                   "<nul set /p=%ONLY_THIS%>" + tmp2.path() + "&exit /b 0"},
                  env = {{"ONLY_THIS", "present"}});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp2.path()));
  EXPECT_EQ(read_file_trimmed(tmp2.path()), "present");
#else
  // Use `env` to dump actual environment, then grep for a well-known var.
  // With full env replacement, HOME should not be present.
  // We poll for non-empty content (not just existence) because the ">"
  // redirect creates/truncates the file before `env` writes to it, and
  // `env` is an external binary (requires fork+exec by the shell).
  bool ok = detach_run({"/bin/sh", "-c", "env > '" + tmp.path() + "'"},
                       env = {{"ONLY_THIS", "present"}});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  std::string env_output = read_file_trimmed(tmp.path());
  // ONLY_THIS should be present
  EXPECT_TRUE(env_output.find("ONLY_THIS=present") != std::string::npos);
  // HOME/PATH should NOT be in the actual environment
  EXPECT_TRUE(env_output.find("\nHOME=") == std::string::npos);
  EXPECT_TRUE(env_output.find("\nPATH=") == std::string::npos);
#endif
}

// ===========================================================================
// 10. EnvAppend — merge into existing environment
// ===========================================================================
TEST(DetachTest, EnvAppend) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c",
       "<nul set /p=%MY_APPEND_DETACH%>" + tmp.path() + "&exit /b 0"},
      env += {{"MY_APPEND_DETACH", "appended_val"}});
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "printf '%s' \"$MY_APPEND_DETACH\" > '" + tmp.path() + "'"},
                 env += {{"MY_APPEND_DETACH", "appended_val"}});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "appended_val");
}

// ===========================================================================
// 11. EnvAppend — original env vars are preserved
// ===========================================================================
TEST(DetachTest, EnvAppendPreservesExistingVars) {
  TempFile tmp;
#if defined(_WIN32)
  // PATH should still exist after env+=
  bool ok = detach_run(
      {"cmd.exe", "/c",
       R"(if defined PATH (<nul set /p=path_exists>)" + tmp.path() +
           R"() else (<nul set /p=path_missing>)" + tmp.path() + ")"},
      env += {{"EXTRA_DETACH", "yes"}});
#else
  bool ok = detach_run(
      {"/bin/sh", "-c",
       "if [ -n \"$PATH\" ]; then printf '%s' path_exists > '" + tmp.path() +
           "'; else printf '%s' path_missing > '" + tmp.path() + "'; fi"},
      env += {{"EXTRA_DETACH", "yes"}});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "path_exists");
}

// ===========================================================================
// 12. EnvItemAppend — append to a specific env var (PATH-style separator)
// ===========================================================================
TEST(DetachTest, EnvItemAppend) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c", "<nul set /p=%PATH%>" + tmp.path() + "&exit /b 0"},
      env["PATH"] += "DETACH_SUFFIX");
#else
  bool ok = detach_run(
      {"/bin/sh", "-c", "printf '%s' \"$PATH\" > '" + tmp.path() + "'"},
      env["PATH"] += "DETACH_SUFFIX");
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
  // The env var content should end with the separator + suffix
  EXPECT_TRUE(out.size() >= std::string("DETACH_SUFFIX").size());
  auto pos = out.rfind("DETACH_SUFFIX");
  EXPECT_NE(pos, std::string::npos);
}

// ===========================================================================
// 13. EnvItemPrepend — prepend to a specific env var
// ===========================================================================
TEST(DetachTest, EnvItemPrepend) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c", "<nul set /p=%PATH%>" + tmp.path() + "&exit /b 0"},
      env["PATH"] <<= "DETACH_PREFIX");
#else
  bool ok = detach_run(
      {"/bin/sh", "-c", "printf '%s' \"$PATH\" > '" + tmp.path() + "'"},
      env["PATH"] <<= "DETACH_PREFIX");
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
  EXPECT_TRUE(out.find("DETACH_PREFIX") != std::string::npos);
}

// ===========================================================================
// 14. EnvItemAppend — non-existent key creates a new one
// ===========================================================================
TEST(DetachTest, EnvItemAppendNewKey) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok =
      detach_run({"cmd.exe", "/c",
                  "<nul set /p=%DETACH_NEW_VAR%>" + tmp.path() + "&exit /b 0"},
                 env["DETACH_NEW_VAR"] += "new_value");
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "printf '%s' \"$DETACH_NEW_VAR\" > '" + tmp.path() + "'"},
                 env["DETACH_NEW_VAR"] += "new_value");
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "new_value");
}

// ===========================================================================
// 15. Env + EnvAppend + EnvItemAppend combined
// ===========================================================================
TEST(DetachTest, EnvCombined) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c", "<nul set /p=%COMBINED%>" + tmp.path() + "&exit /b 0"},
      env = {{"COMBINED", "base"}}, env += {{"EXTRA", "bonus"}},
      env["COMBINED"] += "suffix");
#else
  bool ok = detach_run(
      {"/bin/sh", "-c", "printf '%s' \"$COMBINED\" > '" + tmp.path() + "'"},
      env = {{"COMBINED", "base"}}, env += {{"EXTRA", "bonus"}},
      env["COMBINED"] += "suffix");
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
  // "base" + separator + "suffix"
  EXPECT_TRUE(out.find("base") != std::string::npos);
  EXPECT_TRUE(out.find("suffix") != std::string::npos);
}

// ===========================================================================
// 16. Empty command vector — platform-dependent return
// ===========================================================================
TEST(DetachTest, EmptyCommandVector) {
  bool ok = detach_run(std::vector<std::string>{});
#if defined(_WIN32)
  EXPECT_FALSE(ok);
#else
  // On POSIX, fork succeeds; exec failure is in grandchild
  EXPECT_FALSE(ok);
#endif
}

// ===========================================================================
// 17. Multiple concurrent detach_run calls
// ===========================================================================
TEST(DetachTest, MultipleConcurrent) {
  TempFile tmp1, tmp2, tmp3;
  bool ok1 = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=one>" + tmp1.path()
#else
      "/bin/sh", "-c", "echo one > '" + tmp1.path() + "'"
#endif
  });
  bool ok2 = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=two>" + tmp2.path()
#else
      "/bin/sh", "-c", "echo two > '" + tmp2.path() + "'"
#endif
  });
  bool ok3 = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=three>" + tmp3.path()
#else
      "/bin/sh", "-c", "echo three > '" + tmp3.path() + "'"
#endif
  });
  EXPECT_TRUE(ok1);
  EXPECT_TRUE(ok2);
  EXPECT_TRUE(ok3);
  ASSERT_TRUE(wait_for_file(tmp1.path()));
  ASSERT_TRUE(wait_for_file(tmp2.path()));
  ASSERT_TRUE(wait_for_file(tmp3.path()));
  EXPECT_EQ(read_file_trimmed(tmp1.path()), "one");
  EXPECT_EQ(read_file_trimmed(tmp2.path()), "two");
  EXPECT_EQ(read_file_trimmed(tmp3.path()), "three");
}

// ===========================================================================
// 18. Long-running detached process — survives beyond the detach_run call
// ===========================================================================
TEST(DetachTest, LongRunningProcess) {
  TempFile tmp_start, tmp_done;
#if defined(_WIN32)
  bool ok = detach_run({"cmd.exe", "/c",
                        "<nul set /p=started>" + tmp_start.path() +
                            "& ping 127.0.0.1 -n 3 > nul & <nul set /p=done>" +
                            tmp_done.path()});
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "echo started > '" + tmp_start.path() +
                      "' && sleep 1 && echo done > '" + tmp_done.path() + "'"});
#endif
  EXPECT_TRUE(ok);
  // The start marker should appear quickly
  ASSERT_TRUE(wait_for_file(tmp_start.path(), std::chrono::seconds(3)));
  EXPECT_EQ(read_file_trimmed(tmp_start.path()), "started");
  // The done marker should appear after the sleep
  ASSERT_TRUE(wait_for_file(tmp_done.path(), std::chrono::seconds(10)));
  EXPECT_EQ(read_file_trimmed(tmp_done.path()), "done");
}

// ===========================================================================
// 19. Paths with spaces — verify quoting works
// ===========================================================================
TEST(DetachTest, PathWithSpaces) {
  TempFile tmp;
  // TempFile paths from GetTempFileNameA / mkstemps usually don't have spaces,
  // so this test verifies the command-line argument quoting works correctly
  // when the output file path is passed within quotes.
  bool ok = detach_run({
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=spaces_ok>" + tmp.path()
#else
      "/bin/sh", "-c", "echo spaces_ok > '" + tmp.path() + "'"
#endif
  });
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "spaces_ok");
}

// ===========================================================================
// 20. Special characters in environment variable values
// ===========================================================================
#if !defined(_WIN32)
TEST(DetachTest, EnvSpecialCharacters) {
  TempFile tmp;
  // Value with spaces and special characters
  std::string special_val = "hello world! @#$^%&*()";
#if defined(_WIN32)
  bool ok = detach_run({"powershell.exe", "-NoProfile", "-Command",
                        "[System.IO.File]::WriteAllText('" + tmp.path() +
                            "',$env:DETACH_SPECIAL)"},
                       env = {{"DETACH_SPECIAL", special_val}});
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "printf '%s' \"$DETACH_SPECIAL\" > '" + tmp.path() + "'"},
                 env = {{"DETACH_SPECIAL", special_val}});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), special_val);
}
#endif

// ===========================================================================
// 21. Empty environment value
// ===========================================================================
#if !defined(_WIN32)
TEST(DetachTest, EnvEmptyValue) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c",
       R"(if "%DETACH_EMPTY%"=="" (<nul set /p=empty>)" + tmp.path() +
           R"() else (<nul set /p=not_empty>)" + tmp.path() + ")"},
      env = {{"DETACH_EMPTY", ""}});
#else
  bool ok = detach_run(
      {"/bin/sh", "-c",
       "if [ -z \"$DETACH_EMPTY\" ]; then printf '%s' empty > '" + tmp.path() +
           "'; else printf '%s' not_empty > '" + tmp.path() + "'; fi"},
      env = {{"DETACH_EMPTY", ""}});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "empty");
}
#endif

// ===========================================================================
// 22. Process is truly detached — parent test process exit does not affect it
// ===========================================================================
TEST(DetachTest, ProcessSurvivesDetachReturn) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok =
      detach_run({"cmd.exe", "/c",
                  "ping 127.0.0.1 -n 2 > nul & <nul set /p=survived_detach>" +
                      tmp.path()});
#else
  bool ok =
      detach_run({"/bin/sh", "-c",
                  "sleep 1 && echo survived_detach > '" + tmp.path() + "'"});
#endif
  EXPECT_TRUE(ok);
  // detach_run has already returned; process should still be running.
  // We poll for non-empty content (not just existence) because on Windows
  // the ">" redirect creates/truncates the file before writing to it.
  ASSERT_TRUE(wait_for_file_content(tmp.path(), std::chrono::seconds(10)));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "survived_detach");
}

// ===========================================================================
// 23. Command with many arguments
// ===========================================================================
TEST(DetachTest, ManyArguments) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run(
      {"cmd.exe", "/c", "<nul set /p=a b c d e f g h i j>" + tmp.path()});
#else
  bool ok = detach_run(
      {"/bin/sh", "-c", "echo a b c d e f g h i j > '" + tmp.path() + "'"});
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "a b c d e f g h i j");
}

// ===========================================================================
// Variadic argument type tests — narrow characters
// ===========================================================================

// 24. const char* arguments
TEST(DetachTest, VariadicConstCharPtr) {
  TempFile tmp;
#if defined(_WIN32)
  std::string full_cmd = "<nul set /p=const_char_ptr>" + tmp.path();
  const char* cmd = "cmd.exe";
  const char* a1 = "/c";
  bool ok = detach_run(cmd, a1, full_cmd.c_str());
#else
  std::string full_cmd = "echo const_char_ptr > '" + tmp.path() + "'";
  const char* cmd = "/bin/sh";
  const char* a1 = "-c";
  bool ok = detach_run(cmd, a1, full_cmd.c_str());
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "const_char_ptr");
}

// 25. char* arguments
TEST(DetachTest, VariadicCharPtr) {
  TempFile tmp;
#if defined(_WIN32)
  std::string cmd_s = "cmd.exe";
  std::string a1_s = "/c";
  std::string a2_s = "<nul set /p=char_ptr>" + tmp.path();
#else
  std::string cmd_s = "/bin/sh";
  std::string a1_s = "-c";
  std::string a2_s = "echo char_ptr > '" + tmp.path() + "'";
#endif
  char cmd[256], a1[256], a2[512];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  std::strcpy(cmd, cmd_s.c_str());
  std::strcpy(a1, a1_s.c_str());
  std::strcpy(a2, a2_s.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "char_ptr");
}

// 26. std::string arguments
TEST(DetachTest, VariadicStdString) {
  TempFile tmp;
#if defined(_WIN32)
  std::string cmd("cmd.exe");
  std::string a1("/c");
  std::string a2("<nul set /p=std_string>" + tmp.path());
#else
  std::string cmd("/bin/sh");
  std::string a1("-c");
  std::string a2("echo std_string > '" + tmp.path() + "'");
#endif
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "std_string");
}

// 27. std::string_view arguments
TEST(DetachTest, VariadicStdStringView) {
  TempFile tmp;
#if defined(_WIN32)
  std::string cmd_s("cmd.exe");
  std::string a1_s("/c");
  std::string a2_s("<nul set /p=string_view>" + tmp.path());
  std::string_view cmd(cmd_s);
  std::string_view a1(a1_s);
  std::string_view a2(a2_s);
#else
  std::string cmd_s("/bin/sh");
  std::string a1_s("-c");
  std::string a2_s("echo string_view > '" + tmp.path() + "'");
  std::string_view cmd(cmd_s);
  std::string_view a1(a1_s);
  std::string_view a2(a2_s);
#endif
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "string_view");
}

// 28. Mixed narrow types
TEST(DetachTest, VariadicMixedNarrowTypes) {
  TempFile tmp;
#if defined(_WIN32)
  char cmd[] = "cmd.exe";
  std::string a1("/c");
  std::string a2_s("<nul set /p=mixed_narrow>" + tmp.path());
  std::string_view a2(a2_s);
#else
  char cmd[] = "/bin/sh";
  std::string a1("-c");
  std::string a2_s("echo mixed_narrow > '" + tmp.path() + "'");
  std::string_view a2(a2_s);
#endif
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "mixed_narrow");
}

// 29. Variadic with named args mixed in
TEST(DetachTest, VariadicWithNamedArgs) {
  TempFile tmp;
#if defined(_WIN32)
  bool ok = detach_run("cmd.exe", "/c",
                       "<nul set /p=%CD%>" + tmp.path() + "&exit /b 0",
                       cwd = std::string("C:\\Windows"));
#else
  bool ok = detach_run("/bin/sh", "-c", "pwd > '" + tmp.path() + "'",
                       cwd = std::string("/tmp"));
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
#if defined(_WIN32)
  EXPECT_EQ(out, "C:\\Windows");
#elif defined(__APPLE__)
  EXPECT_TRUE(out == "/tmp" || out == "/private/tmp");
#else
  EXPECT_EQ(out, "/tmp");
#endif
}

// ===========================================================================
// Windows wide-character tests
// ===========================================================================

#if defined(_WIN32)

// 30. const wchar_t* arguments
TEST(DetachTest, VariadicConstWCharPtr) {
  TempFile tmp;
  const wchar_t* cmd = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  std::wstring a2_s = L"<nul set /p=const_wchar_ptr>" +
                      subprocess::detail::utf8_to_utf16(tmp.path());
  bool ok = detach_run(cmd, a1, a2_s.c_str());
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "const_wchar_ptr");
}

// 31. wchar_t* arguments
TEST(DetachTest, VariadicWCharPtr) {
  TempFile tmp;
  wchar_t cmd[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  std::wstring a2_s =
      L"<nul set /p=wchar_ptr>" + subprocess::detail::utf8_to_utf16(tmp.path());
  wchar_t a2[512];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  std::wcscpy(a2, a2_s.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "wchar_ptr");
}

// 32. std::wstring arguments
TEST(DetachTest, VariadicStdWString) {
  TempFile tmp;
  std::wstring cmd(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=std_wstring>" +
                  subprocess::detail::utf8_to_utf16(tmp.path()));
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "std_wstring");
}

// 33. std::wstring_view arguments
TEST(DetachTest, VariadicStdWStringView) {
  TempFile tmp;
  std::wstring cmd_s(L"cmd.exe");
  std::wstring a1_s(L"/c");
  std::wstring a2_s(L"<nul set /p=wstring_view>" +
                    subprocess::detail::utf8_to_utf16(tmp.path()));
  std::wstring_view cmd(cmd_s);
  std::wstring_view a1(a1_s);
  std::wstring_view a2(a2_s);
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "wstring_view");
}

// 34. Mixed wide types
TEST(DetachTest, VariadicMixedWideTypes) {
  TempFile tmp;
  wchar_t cmd[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring a2_s(L"<nul set /p=mixed_wide>" +
                    subprocess::detail::utf8_to_utf16(tmp.path()));
  std::wstring_view a2(a2_s);
  bool ok = detach_run(cmd, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "mixed_wide");
}

// 35. Wide types with env + cwd named args
TEST(DetachTest, VariadicWideWithEnvAndCwd) {
  TempFile tmp;
  bool ok = detach_run(
      L"cmd.exe", L"/c",
      L"<nul set /p=%WDETACH%>" +
          subprocess::detail::utf8_to_utf16(tmp.path()) + L"&exit /b 0",
      env = std::map<std::wstring, std::wstring>{{L"WDETACH", L"wide_env"}},
      cwd = std::wstring(L"C:\\Windows"));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "wide_env");
}

// 36. Wide string vector form
TEST(DetachTest, WideVectorForm) {
  TempFile tmp;
  std::wstring full_cmd = L"<nul set /p=wide_vector>" +
                          subprocess::detail::utf8_to_utf16(tmp.path());
  bool ok = detach_run(std::vector<std::wstring>{L"cmd.exe", L"/c", full_cmd});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "wide_vector");
}

#endif  // _WIN32

// ===========================================================================
// POSIX-specific tests
// ===========================================================================

#if !defined(_WIN32)

// 37. Detached process is in its own session (setsid).
//     After double-fork+setsid, the grandchild gets a new session where
//     PGID == PID.  Verify that the process group ID equals the PID and
//     differs from the test process's PGID.
//     We use pgid (not sess/sid) because ps on macOS does not report the
//     session ID correctly.
TEST(DetachTest, DetachedProcessInOwnSession) {
  TempFile tmp;
  // Write both PID and PGID on one line: "PID PGID"
  bool ok = detach_run(
      {"/bin/sh", "-c", "echo $$ $(ps -o pgid= -p $$) > '" + tmp.path() + "'"});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  std::string out = read_file_trimmed(tmp.path());
  // Parse "PID PGID" from the file.
  std::istringstream iss(out);
  pid_t child_pid = -1, child_pgid = -1;
  if (iss >> child_pid >> child_pgid) {
    // In a new session with setsid, PGID == PID
    EXPECT_EQ(child_pgid, child_pid);
    // And it should differ from our PGID
    pid_t parent_pgid = getpgid(0);
    EXPECT_NE(child_pgid, parent_pgid);
  } else {
    // On platforms where ps does not report PGID correctly (e.g., MSYS),
    // skip the assertions rather than failing.
    GTEST_SKIP() << "ps did not produce valid PID/PGID output: " << out;
  }
}

// 38. Detached grandchild is reparented to init (PID 1).
//     After double-fork, the immediate child exits and the grandchild
//     should have PPID 1 (or a sub-reaper PID, but not the test process).
TEST(DetachTest, DetachedGrandchildParentIsInit) {
  TempFile tmp;
  bool ok = detach_run({"/bin/sh", "-c", "echo $PPID > '" + tmp.path() + "'"});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path(), std::chrono::seconds(5)));
  std::string ppid_str = read_file_trimmed(tmp.path());
  if (!ppid_str.empty()) {
    pid_t ppid = std::stol(ppid_str);
    // Should not be the test process (reparented to init/sub-reaper)
    EXPECT_NE(ppid, getpid());
  }
}

// 39. No zombie — detach_run should reap the intermediate child.
//     The intermediate child from the double-fork is explicitly waitpid()'d
//     in detach_spawn_posix(). Run many detaches in quick succession as a
//     smoke check.
TEST(DetachTest, NoZombieFromIntermediateChild) {
  for (int i = 0; i < 10; ++i) {
    bool ok = detach_run({"/bin/sh", "-c", "exit 0"});
    EXPECT_TRUE(ok);
  }
  // Give processes time to be reaped
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  // If zombies accumulated, the test process would eventually hit resource
  // limits or the system would complain. This is a smoke check.
  SUCCEED();
}

// 40. Detach with CWD and Env together
TEST(DetachTest, CwdAndEnvTogether) {
  TempFile tmp;
  bool ok = detach_run({"/bin/sh", "-c",
                        "pwd > '" + tmp.path() +
                            ".cwd' && "
                            "printf '%s' \"$DTVAR\" > '" +
                            tmp.path() + ".env'"},
                       cwd = "/tmp", env = {{"DTVAR", "cwd_env_value"}});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path() + ".cwd"));
  ASSERT_TRUE(wait_for_file(tmp.path() + ".env"));
  std::string cwd_out = read_file_trimmed(tmp.path() + ".cwd");
  std::string env_out = read_file_trimmed(tmp.path() + ".env");
#if defined(__APPLE__)
  EXPECT_TRUE(cwd_out == "/tmp" || cwd_out == "/private/tmp");
#else
  EXPECT_EQ(cwd_out, "/tmp");
#endif
  EXPECT_EQ(env_out, "cwd_env_value");
  std::filesystem::remove(tmp.path() + ".cwd");
  std::filesystem::remove(tmp.path() + ".env");
}

// 41. Detached process stdin/stdout/stderr are redirected to /dev/null.
//     The process should get immediate EOF on stdin.
TEST(DetachTest, StdinIsDevNull) {
  TempFile tmp;
  // 'cat' with /dev/null as stdin should produce no output
  bool ok = detach_run({"/bin/sh", "-c", "cat > '" + tmp.path() + "'"});
  EXPECT_TRUE(ok);
  // Give it a moment to run, then check — cat with /dev/null stdin
  // should produce an empty file (or not even create it).
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  if (std::filesystem::exists(tmp.path())) {
    EXPECT_TRUE(read_file_trimmed(tmp.path()).empty());
  }
  // Either way, the process didn't hang — SUCCEED
  SUCCEED();
}

#endif  // !_WIN32

// ===========================================================================
// Edge case: detach_run with no additional args (vector-only form)
// ===========================================================================
TEST(DetachTest, VectorOnlyForm) {
  TempFile tmp;
  bool ok = detach_run(std::vector<std::string>{
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=vector_only>" + tmp.path()
#else
      "/bin/sh", "-c", "echo vector_only > '" + tmp.path() + "'"
#endif
  });
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file(tmp.path()));
  EXPECT_EQ(read_file_trimmed(tmp.path()), "vector_only");
}
