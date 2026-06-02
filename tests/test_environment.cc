/**
 * test_environment.cc — Environment variables and current working directory
 *
 * Covers:
 *   - $cwd / cwd — set working directory for the child process
 *   - $env = {{...}} — replace environment (Env)
 *   - $env += {{...}} — append to environment (EnvAppend)
 *   - $env["KEY"] += "val" — append value to a specific env var (EnvItemAppend,
 * PATH-style)
 *   - $env["KEY"] <<= "val" — prepend value to a specific env var
 *   - cwd + relative file access
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "./utils.h"
#include "environment/environment.hpp"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::run;

static std::optional<std::string> home() {
#if defined(_WIN32)
  auto user_profile = env::get("USERPROFILE");
  if (user_profile.has_value() && !user_profile->empty()) {
    return user_profile;
  }
  auto home_drive = env::get("HOMEDRIVE");
  auto homepath = env::get("HOMEPATH");
  if (home_drive.has_value() && homepath.has_value() &&
      !home_drive.value().empty() && !homepath.value().empty()) {
    return home_drive.value() + homepath.value();
  }
#else
  auto home_dir = env::get("HOME");
  if (home_dir.has_value() && !home_dir->empty()) {
    return home_dir;
  }

  // If HOME is not set, fall back to getpwuid.
  // This is a more reliable method for finding the home directory.
  uid_t uid = getuid();
  struct passwd* pw = getpwuid(uid);
  if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0') {
    return std::string(pw->pw_dir);
  }
#endif
  return std::nullopt;
}

// ===========================================================================
// CWD — current working directory
// ===========================================================================

TEST(EnvironmentTest, CwdSetToHome) {
  buffer out;
  auto home_dir = home();

#if !defined(_WIN32)
  run("/bin/pwd", $stdout > out, $cwd = home_dir.value());
#else
  run("cmd.exe", "/c", "echo %CD%", $stdout > out, $cwd = home_dir.value());
#endif

  ASSERT_FALSE(out.empty());

  auto out_span = out.span();
  auto it = std::find_if(rbegin(out_span), rend(out_span),
                         [](char c) { return c != '\n' && c != '\r'; });
  if (it != rend(out_span)) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }

  ASSERT_TRUE(std::equal(out_span.begin(), out_span.end(), home_dir->begin(),
                         home_dir->end()));
}

TEST(EnvironmentTest, CwdSetToHomeVariadic) {
  buffer out;
  auto home_dir = home();

#if !defined(_WIN32)
  run("/bin/pwd", $stdout > out, $cwd = home_dir.value());
#else
  run("cmd.exe", "/c", "echo %CD%", $stdout > out, $cwd = home_dir.value());
#endif

  ASSERT_FALSE(out.empty());

  auto out_span = out.span();
  auto it = std::find_if(rbegin(out_span), rend(out_span),
                         [](char c) { return c != '\n' && c != '\r'; });
  if (it != rend(out_span)) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }

  ASSERT_TRUE(std::equal(out_span.begin(), out_span.end(), home_dir->begin(),
                         home_dir->end()));
}

TEST(EnvironmentTest, CwdSetToTmpAndPwd) {
  buffer stdout_buf;
#if defined(_WIN32)
  int exit_code =
      run("cmd.exe", "/c", "cd", cwd = "C:\\Windows", std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  std::string output = stdout_buf.to_string();
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

TEST(EnvironmentTest, CwdSetAndReadRelativeFile) {
  TempFile temp_file;
  const std::string temp_dir_path =
      std::filesystem::path(temp_file.path()).parent_path().string();
  const std::string relative_file_name =
      std::filesystem::path(temp_file.path()).filename().string();
  temp_file.write(std::string{"Relative Content"});

  buffer stdout_buf;
#if defined(_WIN32)
  int exit_code = run("cmd.exe", "/c", "type " + relative_file_name,
                      cwd = temp_dir_path, std_out > stdout_buf);
#else
  int exit_code = run("/bin/cat", relative_file_name, cwd = temp_dir_path,
                      std_out > stdout_buf);
#endif

  ASSERT_EQ(exit_code, 0);
  std::string output = stdout_buf.to_string();
#if defined(_WIN32)
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  if (!output.empty() && output.back() == '\r') {
    output.pop_back();
  }
#endif
  ASSERT_EQ(output, "Relative Content");
}

// ===========================================================================
// Environment — full replacement (Env)
// ===========================================================================

TEST(EnvironmentTest, EnvOverrideCheckValue) {
  buffer stdout_buf;
#if defined(_WIN32)
  int exit_code =
      run("cmd.exe", "/c", "<nul set /p=%MY_TEST_VAR%&exit /b 0",
          subprocess::named_arguments::env = {{"MY_TEST_VAR", "is_set"}},
          std_out > stdout_buf);
#else
  int exit_code =
      run("bash", "-c", "echo -n $MY_TEST_VAR",
          subprocess::named_arguments::env = {{"MY_TEST_VAR", "is_set"}},
          std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "is_set");
}

TEST(EnvironmentTest, EnvOverrideClearsOtherVars) {
  buffer stdout_buf;
#if defined(_WIN32)
  int exit_code = run(
      "cmd.exe", "/c",
      R"(if "%ONLY_VAR%"=="visible" (<nul set /p=isolated& exit /b 0) else (<nul set /p=not_isolated& exit /b 0))",
      subprocess::named_arguments::env = {{"ONLY_VAR", "visible"}},
      std_out > stdout_buf);
#else
  int exit_code =
      run("bash", "-c",
          R"(
if [ "$ONLY_VAR" = "visible" ]; then
  echo -n isolated;
else
  echo -n not_isolated;
fi
)",
          subprocess::named_arguments::env = {{"ONLY_VAR", "visible"}},
          std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "isolated");
}

TEST(EnvironmentTest, EnvVectorForm) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("/usr/bin/printenv", "env1",
                 subprocess::named_arguments::env = {{"env1", "value1"}},
                 std_out > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "value1\n");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 subprocess::named_arguments::env = {{"env1", "value1"}},
                 std_out > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "value1");
#endif
}

// ===========================================================================
// Environment — append (EnvAppend)
// ===========================================================================

TEST(EnvironmentTest, EnvAppendCheckValue) {
  buffer stdout_buf;
#if defined(_WIN32)
  int exit_code =
      run("cmd.exe", "/c",
          "<nul set /p=%MY_APPEND_VAR%&if defined PATH (<nul set "
          "/p=_haspath)&exit /b 0",
          subprocess::named_arguments::env += {{"MY_APPEND_VAR", "appended"}},
          std_out > stdout_buf);
#else
  int exit_code =
      run("bash", "-c",
          "echo -n $MY_APPEND_VAR; if [ -n \"$PATH\" ]; then echo -n "
          "_haspath; fi",
          subprocess::named_arguments::env += {{"MY_APPEND_VAR", "appended"}},
          std_out > stdout_buf);
#endif
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "appended_haspath");
}

TEST(EnvironmentTest, EnvAppendDollarForm) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $env1", $env += {{"env1", "value1"}},
                 $stdout > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 $env += {{"env1", "value1"}}, $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "value1");
}

// ===========================================================================
// Environment — item append (EnvItemAppend: $env["KEY"] += "val")
// ===========================================================================

TEST(EnvironmentTest, EnvItemAppendPath) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $PATH", $env["PATH"] += "XXXXXXXXX",
                 $stdout > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%PATH%&exit /b 0",
                 $env["PATH"] += "XXXXXXXXX", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_GT(out.size(), 10);

  auto subspan = out.span().subspan(out.size() - 9);
  ASSERT_TRUE(std::all_of(subspan.begin(), subspan.end(),
                          [](unsigned char c) { return c == 'X'; }));
}

// ===========================================================================
// Environment — item prepend (EnvItemAppend: $env["KEY"] <<= "val")
// ===========================================================================

// Basic prepend: verify the prepended value is present and some original
// content follows (original weak test; kept for regression coverage).
TEST(EnvironmentTest, EnvItemPrependPath) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $PATH", $env["PATH"] <<= "XXXXXXXXX",
                 $stdout > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%PATH%&exit /b 0",
                 $env["PATH"] <<= "XXXXXXXXX", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_GT(out.size(), 10);
  ASSERT_NE(out, "XXXXXXXXX");

  // Verify format: the prepended value must appear BEFORE the separator,
  // i.e. "XXXXXXXXX:" not ":XXXXXXXXX".  This catches the bug where the
  // two insert() calls were in the wrong order, producing ":VALUEORIGINAL"
  // instead of "VALUE:ORIGINAL".
  auto pos = std::string(out.to_string()).find("XXXXXXXXX");
  ASSERT_NE(pos, std::string::npos);
  // The prepended value must NOT be preceded by the path separator.
  char sep =
#if defined(_WIN32)
      ';';
#else
      ':';
#endif
  ASSERT_FALSE(pos > 0 && out.to_string()[pos - 1] == sep)
      << "Prepend format is wrong: separator before value means old bug "
         "(insert() order was swapped)";
}

// Verify prepend produces VALUE:ORIGINAL format using a known base value
// set via Env.  This isolates the prepend logic from the ambient PATH.
TEST(EnvironmentTest, EnvItemPrependWithKnownBaseValue) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $TEST_PREPEND_VAR",
                 $env = {{"TEST_PREPEND_VAR", "original"}},
                 $env["TEST_PREPEND_VAR"] <<= "prepended", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "prepended:original");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%TEST_PREPEND_VAR%&exit /b 0",
                 $env = {{"TEST_PREPEND_VAR", "original"}},
                 $env["TEST_PREPEND_VAR"] <<= "prepended", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "prepended;original");
#endif
}

// Verify prepend does NOT insert a leading separator before the value.
// The buggy code (separator-first insert) produced ":VALUEORIGINAL".
TEST(EnvironmentTest, EnvItemPrependNoLeadingSeparator) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $TEST_NOLSEP",
                 $env = {{"TEST_NOLSEP", "base"}},
                 $env["TEST_NOLSEP"] <<= "head", $stdout > out);
  ASSERT_EQ(ret, 0);
  // Must not start with ':' on Unix or ';' on Windows
  ASSERT_FALSE(out.empty());
  EXPECT_NE(out.to_string()[0], ':') << "Result starts with separator — "
                                        "this is the EnvItemPrepend bug";
  EXPECT_EQ(out, "head:base");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%TEST_NOLSEP%&exit /b 0",
                 $env = {{"TEST_NOLSEP", "base"}},
                 $env["TEST_NOLSEP"] <<= "head", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_FALSE(out.empty());
  EXPECT_NE(out.to_string()[0], ';') << "Result starts with separator — "
                                        "this is the EnvItemPrepend bug";
  EXPECT_EQ(out, "head;base");
#endif
}

// Verify prepend into a non-existent key creates the key with just the
// prepended value (no separator).
TEST(EnvironmentTest, EnvItemPrependNewKey) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $TEST_PREPEND_NEW",
                 $env["TEST_PREPEND_NEW"] <<= "only_me", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "only_me");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%TEST_PREPEND_NEW%&exit /b 0",
                 $env["TEST_PREPEND_NEW"] <<= "only_me", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "only_me");
#endif
}

// Verify prepend + append interactions: prepend then append produces the
// correct sequence VALUE:original:suffix.
TEST(EnvironmentTest, EnvItemPrependThenAppend) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $TEST_COMBO",
                 $env = {{"TEST_COMBO", "mid"}}, $env["TEST_COMBO"] <<= "head",
                 $env["TEST_COMBO"] += "tail", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "head:mid:tail");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%TEST_COMBO%&exit /b 0",
                 $env = {{"TEST_COMBO", "mid"}}, $env["TEST_COMBO"] <<= "head",
                 $env["TEST_COMBO"] += "tail", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "head;mid;tail");
#endif
}

// Verify prepend with empty base value: should be just "VALUE" (no leading
// or trailing separator).
TEST(EnvironmentTest, EnvItemPrependEmptyBase) {
  buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $TEST_EMPTY_BASE",
                 $env = {{"TEST_EMPTY_BASE", ""}},
                 $env["TEST_EMPTY_BASE"] <<= "pre", $stdout > out);
  ASSERT_EQ(ret, 0);
  // Behaviour: insert separator then value → "pre:"
  ASSERT_EQ(out, "pre:");
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%TEST_EMPTY_BASE%&exit /b 0",
                 $env = {{"TEST_EMPTY_BASE", ""}},
                 $env["TEST_EMPTY_BASE"] <<= "pre", $stdout > out);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out, "pre;");
#endif
}
