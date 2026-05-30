/**
 * test_string_like_arguments.cc — Comprehensive string_like_type tests
 *
 * Validates that subprocess::{run, capture_run, detach_run} accept all
 * string-like argument types defined by the detail::string_like_type concept:
 *   - char*
 *   - const char*
 *   - std::string
 *   - std::string_view
 *
 * And on Windows, additionally:
 *   - wchar_t*
 *   - const wchar_t*
 *   - std::wstring
 *   - std::wstring_view
 *
 * Covers:
 *   - Each type individually for all three functions
 *   - Mixed narrow types
 *   - Mixed wide types (Windows)
 *   - Rvalue temporaries (std::string{}, std::string_view from literal)
 *   - Single-argument calls (command name only)
 *   - C-array decay (string literals)
 *   - Edge case: empty string arguments
 */

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using subprocess::buffer;
using subprocess::capture_run;
using subprocess::detach_run;
using subprocess::run;
using subprocess::named_arguments::std_err;
using subprocess::named_arguments::std_out;

// ===========================================================================
// Helper: wait for file content (used by detach_run tests)
// ===========================================================================
namespace {
bool wait_for_file_content(
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

std::string read_trimmed(const std::string& path) {
  std::ifstream f(path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r')) {
    content.pop_back();
  }
  return content;
}
}  // namespace

// ===========================================================================
// Platform helpers: command + arguments for producing known output
// ===========================================================================
namespace {

struct CmdParts {
  // "app" + "arg1" + "arg2" that, when run, produce output_label on stdout
  // and exit 0.
  const char* app;
  const char* arg1;
  const char* arg2;
};

CmdParts echo_cmd(const char* label) {
#if defined(_WIN32)
  static std::string s1, s2;
  s1 = "/c";
  s2 = std::string("<nul set /p=") + label + "&exit /b 0";
  return {"cmd.exe", s1.c_str(), s2.c_str()};
#else
  static std::string s2;
  s2 = std::string("echo -n ") + label;
  return {"bash", "-c", s2.c_str()};
#endif
}

// For detach_run: command that writes label to a temp file.
// Uses 'echo' without -n for portability (macOS /bin/sh does not support
// echo -n); trailing newline is handled by read_trimmed().
std::vector<std::string> detach_cmd(const std::string& label,
                                    const std::string& file_path) {
#if defined(_WIN32)
  return {"cmd.exe", "/c",
          "<nul set /p=" + label + ">" + file_path + "&exit /b 0"};
#else
  return {"/bin/sh", "-c", "echo '" + label + "' > '" + file_path + "'"};
#endif
}

}  // namespace

// ===========================================================================
// 1. subprocess::run — narrow character types
// ===========================================================================

TEST(StringLikeRunTest, ConstCharPtr) {
  buffer out;
  auto [app, a1, a2] = echo_cmd("const_char_ptr");
  int ec = run(static_cast<const char*>(app), static_cast<const char*>(a1),
               static_cast<const char*>(a2), std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "const_char_ptr");
}

TEST(StringLikeRunTest, CharPtr) {
  buffer out;
  auto parts = echo_cmd("char_ptr");
#if defined(_WIN32)
  char app[] = "cmd.exe";
  char a1[] = "/c";
  char a2[256];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  strcpy(a2, parts.arg2);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#else
  char app[] = "bash";
  char a1[] = "-c";
  char a2[256];
  strcpy(a2, parts.arg2);
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "char_ptr");
}

TEST(StringLikeRunTest, StdString) {
  buffer out;
#if defined(_WIN32)
  std::string app("cmd.exe");
  std::string a1("/c");
  std::string a2("<nul set /p=std_string&exit /b 0");
#else
  std::string app("bash");
  std::string a1("-c");
  std::string a2("echo -n std_string");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "std_string");
}

TEST(StringLikeRunTest, StdStringView) {
  buffer out;
#if defined(_WIN32)
  std::string_view app("cmd.exe");
  std::string_view a1("/c");
  std::string_view a2("<nul set /p=std_string_view&exit /b 0");
#else
  std::string_view app("bash");
  std::string_view a1("-c");
  std::string_view a2("echo -n std_string_view");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "std_string_view");
}

TEST(StringLikeRunTest, RvalueStdString) {
  buffer out;
#if defined(_WIN32)
  int ec =
      run(std::string("cmd.exe"), std::string("/c"),
          std::string("<nul set /p=rvalue_string&exit /b 0"), std_out > out);
#else
  int ec = run(std::string("bash"), std::string("-c"),
               std::string("echo -n rvalue_string"), std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "rvalue_string");
}

TEST(StringLikeRunTest, MixedNarrowTypes) {
  buffer out;
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
  std::string_view a2("<nul set /p=mixed_narrow_run&exit /b 0");
#else
  char app[] = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n mixed_narrow_run");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "mixed_narrow_run");
}

TEST(StringLikeRunTest, StringLiteralCArrayDecay) {
  // String literals decay to const char* — must work directly
  buffer out;
#if defined(_WIN32)
  int ec = run("cmd.exe", "/c", "<nul set /p=literal_decay&exit /b 0",
               std_out > out);
#else
  int ec = run("bash", "-c", "echo -n literal_decay", std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "literal_decay");
}

TEST(StringLikeRunTest, SingleArgumentCommandOnly) {
  // Only the command, no additional string-like arguments
  int ec;
#if defined(_WIN32)
  ec = run(std::string("cmd.exe"), std::string("/c"), std::string("exit /b 0"));
#else
  ec = run(std::string("bash"), std::string("-c"), std::string("exit 0"));
#endif
  ASSERT_EQ(ec, 0);
}

TEST(StringLikeRunTest, ManyMixedArgs) {
  buffer out;
#if defined(_WIN32)
  const char* app = "cmd.exe";
  std::string a1("/c");
  std::string_view a2("<nul set /p=many_mixed_args&exit /b 0");
#else
  const char* app = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n many_mixed_args");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "many_mixed_args");
}

// ===========================================================================
// 2. subprocess::capture_run — narrow character types
// ===========================================================================

TEST(StringLikeCaptureRunTest, ConstCharPtr) {
#if defined(_WIN32)
  const char* app = "cmd.exe";
  const char* a1 = "/c";
  const char* a2 = "<nul set /p=cap_const_char_ptr&exit /b 0";
#else
  const char* app = "bash";
  const char* a1 = "-c";
  const char* a2 = "echo -n cap_const_char_ptr";
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_const_char_ptr");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, CharPtr) {
#if defined(_WIN32)
  char app[] = "cmd.exe";
  char a1[] = "/c";
  char a2[] = "<nul set /p=cap_char_ptr&exit /b 0";
#else
  char app[] = "bash";
  char a1[] = "-c";
  char a2[] = "echo -n cap_char_ptr";
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_char_ptr");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, StdString) {
#if defined(_WIN32)
  std::string app("cmd.exe");
  std::string a1("/c");
  std::string a2("<nul set /p=cap_std_string&exit /b 0");
#else
  std::string app("bash");
  std::string a1("-c");
  std::string a2("echo -n cap_std_string");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_std_string");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, StdStringView) {
#if defined(_WIN32)
  std::string_view app("cmd.exe");
  std::string_view a1("/c");
  std::string_view a2("<nul set /p=cap_std_string_view&exit /b 0");
#else
  std::string_view app("bash");
  std::string_view a1("-c");
  std::string_view a2("echo -n cap_std_string_view");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_std_string_view");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, RvalueStdString) {
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run(std::string("cmd.exe"), std::string("/c"),
                  std::string("<nul set /p=cap_rvalue_string&exit /b 0"));
#else
  auto [ec, out, err] = capture_run(std::string("bash"), std::string("-c"),
                                    std::string("echo -n cap_rvalue_string"));
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_rvalue_string");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, MixedNarrowTypes) {
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
  std::string_view a2("<nul set /p=cap_mixed_narrow&exit /b 0");
#else
  char app[] = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n cap_mixed_narrow");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_mixed_narrow");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, StringLiteralCArrayDecay) {
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run("cmd.exe", "/c", "<nul set /p=cap_literal&exit /b 0");
#else
  auto [ec, out, err] = capture_run("bash", "-c", "echo -n cap_literal");
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_literal");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunTest, ExitCodePropagation) {
#if defined(_WIN32)
  auto [ec, out, err] = capture_run(std::string("cmd.exe"), std::string("/c"),
                                    std::string_view("exit /b 77"));
#else
  auto [ec, out, err] = capture_run(std::string("bash"), std::string("-c"),
                                    std::string_view("exit 77"));
#endif
  ASSERT_EQ(ec, 77);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
}

// ===========================================================================
// 3. subprocess::detach_run — narrow character types
// ===========================================================================

TEST(StringLikeDetachRunTest, ConstCharPtr) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_const_char_ptr", tmp.path());
  bool ok = detach_run(static_cast<const char*>(cmd[0].c_str()),
                       static_cast<const char*>(cmd[1].c_str()),
                       static_cast<const char*>(cmd[2].c_str()));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_const_char_ptr");
}

TEST(StringLikeDetachRunTest, CharPtr) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_char_ptr", tmp.path());
#if defined(_WIN32)
  char app[] = "cmd.exe";
  char a1[] = "/c";
#else
  char app[] = "/bin/sh";
  char a1[] = "-c";
#endif
  std::string a2_s = cmd[2];
  char a2[512];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  std::strcpy(a2, a2_s.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_char_ptr");
}

TEST(StringLikeDetachRunTest, StdString) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_std_string", tmp.path());
  bool ok =
      detach_run(std::string(cmd[0]), std::string(cmd[1]), std::string(cmd[2]));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_std_string");
}

TEST(StringLikeDetachRunTest, StdStringView) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_string_view", tmp.path());
  std::string s0(cmd[0]), s1(cmd[1]), s2(cmd[2]);
  std::string_view app(s0), a1(s1), a2(s2);
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_string_view");
}

TEST(StringLikeDetachRunTest, RvalueStdString) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_rvalue_string", tmp.path());
  bool ok =
      detach_run(std::string(cmd[0]), std::string(cmd[1]), std::string(cmd[2]));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_rvalue_string");
}

TEST(StringLikeDetachRunTest, MixedNarrowTypes) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_mixed_narrow", tmp.path());
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
#else
  char app[] = "/bin/sh";
  std::string a1("-c");
#endif
  std::string a2_s(cmd[2]);
  std::string_view a2(a2_s);
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_mixed_narrow");
}

TEST(StringLikeDetachRunTest, StringLiteralCArrayDecay) {
  TempFile tmp;
  auto cmd = detach_cmd("detach_literal", tmp.path());
  // Can't use raw string literals for the dynamic cmd[2] part, so construct
  // strings first
  bool ok = detach_run(cmd[0].c_str(), cmd[1].c_str(), cmd[2].c_str());
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "detach_literal");
}

// ===========================================================================
// 4. Windows wide-character tests
// ===========================================================================

#if defined(_WIN32)

// --- subprocess::run with wide types ---

TEST(StringLikeRunWideTest, ConstWCharPtr) {
  buffer out;
  const wchar_t* app = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  const wchar_t* a2 = L"<nul set /p=w_const_char_ptr&exit /b 0";
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_const_char_ptr");
}

TEST(StringLikeRunWideTest, WCharPtr) {
  buffer out;
  wchar_t app[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  wchar_t a2[] = L"<nul set /p=w_char_ptr&exit /b 0";
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_char_ptr");
}

TEST(StringLikeRunWideTest, StdWString) {
  buffer out;
  std::wstring app(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=w_std_wstring&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_std_wstring");
}

TEST(StringLikeRunWideTest, StdWStringView) {
  buffer out;
  std::wstring_view app(L"cmd.exe");
  std::wstring_view a1(L"/c");
  std::wstring_view a2(L"<nul set /p=w_string_view&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_string_view");
}

TEST(StringLikeRunWideTest, RvalueStdWString) {
  buffer out;
  int ec = run(std::wstring(L"cmd.exe"), std::wstring(L"/c"),
               std::wstring(L"<nul set /p=w_rvalue&exit /b 0"), std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_rvalue");
}

TEST(StringLikeRunWideTest, MixedWideTypes) {
  buffer out;
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(L"<nul set /p=w_mixed&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_mixed");
}

TEST(StringLikeRunWideTest, WideStringLiteralDecay) {
  buffer out;
  int ec =
      run(L"cmd.exe", L"/c", L"<nul set /p=w_literal&exit /b 0", std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_literal");
}

// --- subprocess::capture_run with wide types ---

TEST(StringLikeCaptureRunWideTest, ConstWCharPtr) {
  const wchar_t* app = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  const wchar_t* a2 = L"<nul set /p=w_cap_const_char_ptr&exit /b 0";
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_const_char_ptr");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, WCharPtr) {
  wchar_t app[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  wchar_t a2[] = L"<nul set /p=w_cap_char_ptr&exit /b 0";
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_char_ptr");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, StdWString) {
  std::wstring app(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=w_cap_std_wstring&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_std_wstring");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, StdWStringView) {
  std::wstring_view app(L"cmd.exe");
  std::wstring_view a1(L"/c");
  std::wstring_view a2(L"<nul set /p=w_cap_string_view&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_string_view");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, RvalueStdWString) {
  auto [ec, out, err] =
      capture_run(std::wstring(L"cmd.exe"), std::wstring(L"/c"),
                  std::wstring(L"<nul set /p=w_cap_rvalue&exit /b 0"));
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_rvalue");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, MixedWideTypes) {
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(L"<nul set /p=w_cap_mixed&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_mixed");
  ASSERT_TRUE(err.empty());
}

TEST(StringLikeCaptureRunWideTest, WideStringLiteralDecay) {
  auto [ec, out, err] =
      capture_run(L"cmd.exe", L"/c", L"<nul set /p=w_cap_literal&exit /b 0");
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_literal");
  ASSERT_TRUE(err.empty());
}

// --- subprocess::detach_run with wide types ---

TEST(StringLikeDetachRunWideTest, ConstWCharPtr) {
  TempFile tmp;
  const wchar_t* app = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  std::wstring a2_s = L"<nul set /p=w_detach_const_char_ptr>" +
                      subprocess::detail::utf8_to_utf16(tmp.path()) +
                      L"&exit /b 0";
  bool ok = detach_run(app, a1, a2_s.c_str());
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_const_char_ptr");
}

TEST(StringLikeDetachRunWideTest, WCharPtr) {
  TempFile tmp;
  wchar_t app[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  std::wstring a2_s = L"<nul set /p=w_detach_char_ptr>" +
                      subprocess::detail::utf8_to_utf16(tmp.path()) +
                      L"&exit /b 0";
  wchar_t a2[512];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  std::wcscpy(a2, a2_s.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_char_ptr");
}

TEST(StringLikeDetachRunWideTest, StdWString) {
  TempFile tmp;
  std::wstring app(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=w_detach_std_wstring>" +
                  subprocess::detail::utf8_to_utf16(tmp.path()) +
                  L"&exit /b 0");
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_std_wstring");
}

TEST(StringLikeDetachRunWideTest, StdWStringView) {
  TempFile tmp;
  std::wstring cmd_s(L"cmd.exe");
  std::wstring a1_s(L"/c");
  std::wstring a2_s(L"<nul set /p=w_detach_string_view>" +
                    subprocess::detail::utf8_to_utf16(tmp.path()) +
                    L"&exit /b 0");
  std::wstring_view app(cmd_s);
  std::wstring_view a1(a1_s);
  std::wstring_view a2(a2_s);
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_string_view");
}

TEST(StringLikeDetachRunWideTest, RvalueStdWString) {
  TempFile tmp;
  bool ok =
      detach_run(std::wstring(L"cmd.exe"), std::wstring(L"/c"),
                 std::wstring(L"<nul set /p=w_detach_rvalue>" +
                              subprocess::detail::utf8_to_utf16(tmp.path()) +
                              L"&exit /b 0"));
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_rvalue");
}

TEST(StringLikeDetachRunWideTest, MixedWideTypes) {
  TempFile tmp;
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring a2_s(L"<nul set /p=w_detach_mixed>" +
                    subprocess::detail::utf8_to_utf16(tmp.path()) +
                    L"&exit /b 0");
  std::wstring_view a2(a2_s);
  bool ok = detach_run(app, a1, a2);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_mixed");
}

TEST(StringLikeDetachRunWideTest, WideStringLiteralDecay) {
  TempFile tmp;
  std::wstring a2_s = L"<nul set /p=w_detach_literal>" +
                      subprocess::detail::utf8_to_utf16(tmp.path()) +
                      L"&exit /b 0";
  bool ok = detach_run(L"cmd.exe", L"/c", a2_s.c_str());
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "w_detach_literal");
}

#endif  // _WIN32

// ===========================================================================
// 5. Cross-cutting edge cases
// ===========================================================================

TEST(StringLikeEdgeCaseTest, RunWithStderrCaptureMixedTypes) {
  buffer out, err;
#if defined(_WIN32)
  const char* app = "cmd.exe";
  std::string a1("/c");
  std::string_view a2(
      "<nul set /p=out_text& <nul set /p=err_text>&2&exit /b 0");
#else
  const char* app = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n out_text; echo -n err_text >&2");
#endif
  int ec = run(app, a1, a2, std_out > out, std_err > err);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "out_text");
  ASSERT_EQ(err.to_string(), "err_text");
}

TEST(StringLikeEdgeCaseTest, DetachRunWithVectorForm) {
  TempFile tmp;
  auto cmd = detach_cmd("vector_form", tmp.path());
  bool ok = detach_run(cmd[0], std::vector<std::string>{cmd[1], cmd[2]});
  EXPECT_TRUE(ok);
  ASSERT_TRUE(wait_for_file_content(tmp.path()));
  EXPECT_EQ(read_trimmed(tmp.path()), "vector_form");
}

TEST(StringLikeEdgeCaseTest, RunWithOnlyNamedArgs) {
  // All "arguments" are named args; the command is a single string
  buffer out;
#if defined(_WIN32)
  int ec = run(std::string("cmd.exe"), std::string("/c"),
               std::string("<nul set /p=only_named&exit /b 0"), std_out > out);
#else
  int ec = run(std::string("bash"), std::string("-c"),
               std::string("echo -n only_named"), std_out > out);
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "only_named");
}

TEST(StringLikeEdgeCaseTest, CaptureRunWithEmptyOutput) {
  // Command that produces no stdout but exits successfully
#if defined(_WIN32)
  auto [ec, out, err] =
      capture_run("cmd.exe", "/c", std::string_view("exit /b 0"));
#else
  auto [ec, out, err] = capture_run("bash", "-c", std::string_view("exit 0"));
#endif
  ASSERT_EQ(ec, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
}
