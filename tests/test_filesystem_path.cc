/**
 * test_filesystem_path.cc — std::filesystem::path as string_like_type tests
 *
 * Validates that subprocess::{run, capture_run, detach_run} accept
 * std::filesystem::path arguments via the detail::string_like_type concept.
 *
 * std::filesystem::path::value_type is char on Unix and wchar_t on Windows,
 * so the library internally converts to the correct NativeString type.
 *
 * Covers:
 *   - run() with filesystem::path arguments
 *   - capture_run() with filesystem::path arguments
 *   - detach_run() with filesystem::path arguments
 *   - Mixed filesystem::path with other string-like types
 *   - Rvalue filesystem::path
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "./temp_file.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::detach_run;
using subprocess::dynamic_buffer;
using subprocess::run;

namespace fs = std::filesystem;

// ===========================================================================
// Platform helpers
// ===========================================================================
namespace {

#if defined(_WIN32)
// On Windows, filesystem::path::value_type is wchar_t, but we always use
// the narrow-string public API (the library converts internally).
// For run()/capture_run(), we use cmd.exe with a narrow command string.
fs::path echo_cmd_path(const char* label) {
  (void)label;
  return fs::path("cmd.exe");
}

std::vector<fs::path> echo_cmd_args(const char* label) {
  return {fs::path("/c"),
          fs::path(std::string("<nul set /p=") + label + "&exit /b 0")};
}

std::vector<fs::path> detach_cmd_args(const std::string& label,
                                      const std::string& file_path) {
  return {fs::path("cmd.exe"), fs::path("/c"),
          fs::path("<nul set /p=" + label + ">" + file_path + "&exit /b 0")};
}
#else
fs::path echo_cmd_path(const char* /*label*/) { return fs::path("bash"); }

std::vector<fs::path> echo_cmd_args(const char* label) {
  return {fs::path("-c"), fs::path(std::string("echo -n ") + label)};
}

std::vector<fs::path> detach_cmd_args(const std::string& label,
                                      const std::string& file_path) {
  return {fs::path("/bin/sh"), fs::path("-c"),
          fs::path("echo '" + label + "' > '" + file_path + "'")};
}
#endif

}  // namespace

// ===========================================================================
// 1. subprocess::run — filesystem::path arguments
// ===========================================================================

TEST(FilesystemPathRunTest, PathAppAndArgs) {
  dynamic_buffer out;
  auto app = echo_cmd_path("fs_path_run");
  auto args = echo_cmd_args("fs_path_run");
  int ec = run(app, args[0], args[1], std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_run");
}

TEST(FilesystemPathRunTest, PathAppOnly) {
  // Only the app name as filesystem::path, args as string literals
  dynamic_buffer out;
#if defined(_WIN32)
  fs::path app("cmd.exe");
  int ec =
      run(app, "/c", "<nul set /p=fs_path_app_only&exit /b 0", std_out > out);
#else
  fs::path app("bash");
  int ec = run(app, "-c", "echo -n fs_path_app_only", std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_app_only");
}

TEST(FilesystemPathRunTest, PathArgsOnly) {
  // App as string, first arg as filesystem::path
  dynamic_buffer out;
#if defined(_WIN32)
  fs::path a1("/c");
  fs::path a2("<nul set /p=fs_path_args_only&exit /b 0");
  int ec = run("cmd.exe", a1, a2, std_out > out);
#else
  fs::path a1("-c");
  fs::path a2("echo -n fs_path_args_only");
  int ec = run("bash", a1, a2, std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_args_only");
}

TEST(FilesystemPathRunTest, RvaluePath) {
  dynamic_buffer out;
#if defined(_WIN32)
  int ec = run(fs::path("cmd.exe"), fs::path("/c"),
               fs::path("<nul set /p=fs_path_rvalue&exit /b 0"), std_out > out);
#else
  int ec = run(fs::path("bash"), fs::path("-c"),
               fs::path("echo -n fs_path_rvalue"), std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_rvalue");
}

TEST(FilesystemPathRunTest, MixedPathAndStringTypes) {
  dynamic_buffer out;
#if defined(_WIN32)
  fs::path app("cmd.exe");
  std::string a1("/c");
  fs::path a2("<nul set /p=fs_path_mixed&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
#else
  fs::path app("bash");
  std::string a1("-c");
  fs::path a2("echo -n fs_path_mixed");
  int ec = run(app, a1, a2, std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_mixed");
}

TEST(FilesystemPathRunTest, MixedPathAndStringView) {
  dynamic_buffer out;
#if defined(_WIN32)
  std::string_view app("cmd.exe");
  fs::path a1("/c");
  std::string_view a2("<nul set /p=fs_path_mixed_sv&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
#else
  std::string_view app("bash");
  fs::path a1("-c");
  std::string_view a2("echo -n fs_path_mixed_sv");
  int ec = run(app, a1, a2, std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_path_mixed_sv");
}

// ===========================================================================
// 2. subprocess::capture_run — filesystem::path arguments
// ===========================================================================

TEST(FilesystemPathCaptureRunTest, PathAppAndArgs) {
  auto app = echo_cmd_path("fs_cap_path");
  auto args = echo_cmd_args("fs_cap_path");
  auto [ec, out, err] = capture_run(app, args[0], args[1]);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_cap_path");
  ASSERT_TRUE(err.empty());
}

TEST(FilesystemPathCaptureRunTest, PathAppOnly) {
#if defined(_WIN32)
  fs::path app("cmd.exe");
  auto [ec, out, err] =
      capture_run(app, "/c", "<nul set /p=fs_cap_app_only&exit /b 0");
#else
  fs::path app("bash");
  auto [ec, out, err] = capture_run(app, "-c", "echo -n fs_cap_app_only");
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_cap_app_only");
  ASSERT_TRUE(err.empty());
}

TEST(FilesystemPathCaptureRunTest, PathArgsOnly) {
#if defined(_WIN32)
  fs::path a1("/c");
  fs::path a2("<nul set /p=fs_cap_args_only&exit /b 0");
  auto [ec, out, err] = capture_run("cmd.exe", a1, a2);
#else
  fs::path a1("-c");
  fs::path a2("echo -n fs_cap_args_only");
  auto [ec, out, err] = capture_run("bash", a1, a2);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_cap_args_only");
  ASSERT_TRUE(err.empty());
}

TEST(FilesystemPathCaptureRunTest, RvaluePath) {
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run(fs::path("cmd.exe"), fs::path("/c"),
                  fs::path("<nul set /p=fs_cap_rvalue&exit /b 0"));
#else
  auto [ec, out, err] = capture_run(fs::path("bash"), fs::path("-c"),
                                    fs::path("echo -n fs_cap_rvalue"));
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_cap_rvalue");
  ASSERT_TRUE(err.empty());
}

TEST(FilesystemPathCaptureRunTest, ExitCodePropagation) {
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run(fs::path("cmd.exe"), fs::path("/c"), fs::path("exit /b 77"));
#else
  auto [ec, out, err] =
      capture_run(fs::path("bash"), fs::path("-c"), fs::path("exit 77"));
#endif
  ASSERT_EQ(ec, 77);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
}

// ===========================================================================
// 3. subprocess::detach_run — filesystem::path arguments
// ===========================================================================

TEST(FilesystemPathDetachRunTest, PathAppAndArgs) {
  TempFile tmp;
  auto args = detach_cmd_args("fs_detach_path", tmp.path());
  bool ok = detach_run(args[0], args[1], args[2]);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(tmp.wait_for_file());
  EXPECT_EQ(tmp.read_trimmed(), "fs_detach_path");
}

TEST(FilesystemPathDetachRunTest, PathAppOnly) {
  TempFile tmp;
#if defined(_WIN32)
  fs::path app("cmd.exe");
  fs::path a1("/c");
  std::string a2_s =
      "<nul set /p=fs_detach_app_only>" + tmp.path() + "&exit /b 0";
  bool ok = detach_run(app, a1, a2_s);
#else
  fs::path app("/bin/sh");
  fs::path a1("-c");
  std::string a2_s = "echo 'fs_detach_app_only' > '" + tmp.path() + "'";
  bool ok = detach_run(app, a1, a2_s);
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(tmp.wait_for_file());
  EXPECT_EQ(tmp.read_trimmed(), "fs_detach_app_only");
}

TEST(FilesystemPathDetachRunTest, RvaluePath) {
  TempFile tmp;
  auto args = detach_cmd_args("fs_detach_rvalue", tmp.path());
  bool ok = detach_run(fs::path(args[0]), fs::path(args[1]), fs::path(args[2]));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(tmp.wait_for_file());
  EXPECT_EQ(tmp.read_trimmed(), "fs_detach_rvalue");
}

TEST(FilesystemPathDetachRunTest, MixedPathAndString) {
  TempFile tmp;
#if defined(_WIN32)
  fs::path app("cmd.exe");
  std::string a1("/c");
  fs::path a2("<nul set /p=fs_detach_mixed>" + tmp.path() + "&exit /b 0");
  bool ok = detach_run(app, a1, a2);
#else
  fs::path app("/bin/sh");
  std::string a1("-c");
  fs::path a2("echo 'fs_detach_mixed' > '" + tmp.path() + "'");
  bool ok = detach_run(app, a1, a2);
#endif
  EXPECT_TRUE(ok);
  ASSERT_TRUE(tmp.wait_for_file());
  EXPECT_EQ(tmp.read_trimmed(), "fs_detach_mixed");
}

// ===========================================================================
// 4. Edge cases
// ===========================================================================

TEST(FilesystemPathEdgeCaseTest, RunWithStderrCapture) {
  dynamic_buffer out, err;
#if defined(_WIN32)
  fs::path app("cmd.exe");
  fs::path a1("/c");
  fs::path a2("<nul set /p=fs_out& <nul set /p=fs_err>&2&exit /b 0");
#else
  fs::path app("bash");
  fs::path a1("-c");
  fs::path a2("echo -n fs_out; echo -n fs_err >&2");
#endif
  int ec = run(app, a1, a2, std_out > out, std_err > err);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "fs_out");
  ASSERT_EQ(err.to_string(), "fs_err");
}

TEST(FilesystemPathEdgeCaseTest, CaptureRunWithEmptyOutput) {
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run(fs::path("cmd.exe"), fs::path("/c"), fs::path("exit /b 0"));
#else
  auto [ec, out, err] =
      capture_run(fs::path("bash"), fs::path("-c"), fs::path("exit 0"));
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
}
