/**
 * Tests for subprocess::run and subprocess::capture_run with various
 * string-like argument types:
 *   - char*
 *   - const char*
 *   - std::string
 *   - std::string_view
 *   - mixed combinations of the above
 *
 * On Windows, additionally:
 *   - wchar_t*
 *   - const wchar_t*
 *   - std::wstring
 *   - std::wstring_view
 *   - mixed combinations of the above
 */

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <tuple>

#include "subprocess/subprocess.hpp"

using namespace subprocess;
using namespace subprocess::named_arguments;

// ============================================================================
// Narrow character tests (char*, const char*, std::string, std::string_view)
// These work on all platforms.
// ============================================================================

class RunVariadicTypesTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// ---------------------------------------------------------------------------
// subprocess::run — narrow character types
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, RunConstCharPtr) {
  subprocess::buffer out;
#if defined(_WIN32)
  const char* app = "cmd.exe";
  const char* a1 = "/c";
  const char* a2 = "<nul set /p=hello_from_const_char_ptr&exit /b 0";
#else
  const char* app = "bash";
  const char* a1 = "-c";
  const char* a2 = "echo -n hello_from_const_char_ptr";
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_from_const_char_ptr");
}

TEST_F(RunVariadicTypesTest, RunCharPtr) {
  subprocess::buffer out;
#if defined(_WIN32)
  char app[] = "cmd.exe";
  char a1[] = "/c";
  char a2[] = "<nul set /p=hello_from_char_ptr&exit /b 0";
#else
  char app[] = "bash";
  char a1[] = "-c";
  char a2[] = "echo -n hello_from_char_ptr";
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_from_char_ptr");
}

TEST_F(RunVariadicTypesTest, RunStdString) {
  subprocess::buffer out;
#if defined(_WIN32)
  std::string app("cmd.exe");
  std::string a1("/c");
  std::string a2("<nul set /p=hello_from_std_string&exit /b 0");
#else
  std::string app("bash");
  std::string a1("-c");
  std::string a2("echo -n hello_from_std_string");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_from_std_string");
}

TEST_F(RunVariadicTypesTest, RunStdStringView) {
  subprocess::buffer out;
#if defined(_WIN32)
  std::string_view app("cmd.exe");
  std::string_view a1("/c");
  std::string_view a2("<nul set /p=hello_from_string_view&exit /b 0");
#else
  std::string_view app("bash");
  std::string_view a1("-c");
  std::string_view a2("echo -n hello_from_string_view");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_from_string_view");
}

TEST_F(RunVariadicTypesTest, RunMixedNarrowTypes) {
  subprocess::buffer out;
  // Mix char*, std::string, std::string_view, const char* in one call
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
  std::string_view a2("<nul set /p=hello_mixed_narrow&exit /b 0");
#else
  char app[] = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n hello_mixed_narrow");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_mixed_narrow");
}

// ---------------------------------------------------------------------------
// subprocess::capture_run — narrow character types
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, CaptureRunConstCharPtr) {
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

TEST_F(RunVariadicTypesTest, CaptureRunCharPtr) {
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

TEST_F(RunVariadicTypesTest, CaptureRunStdString) {
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

TEST_F(RunVariadicTypesTest, CaptureRunStdStringView) {
#if defined(_WIN32)
  std::string_view app("cmd.exe");
  std::string_view a1("/c");
  std::string_view a2("<nul set /p=cap_string_view&exit /b 0");
#else
  std::string_view app("bash");
  std::string_view a1("-c");
  std::string_view a2("echo -n cap_string_view");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_string_view");
  ASSERT_TRUE(err.empty());
}

TEST_F(RunVariadicTypesTest, CaptureRunMixedNarrowTypes) {
  // Mix char*, std::string, std::string_view in one capture_run call
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

// ---------------------------------------------------------------------------
// Mixed narrow types with stderr capture
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, RunMixedNarrowWithStderr) {
  subprocess::buffer out;
  subprocess::buffer err;
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
  std::string_view a2(
      "<nul set /p=stdout_text& <nul set /p=stderr_text>&2&exit /b 0");
#else
  char app[] = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n stdout_text; echo -n stderr_text >&2");
#endif
  int ec = run(app, a1, a2, std_out > out, std_err > err);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "stdout_text");
  ASSERT_EQ(err.to_string(), "stderr_text");
}

TEST_F(RunVariadicTypesTest, CaptureRunMixedNarrowWithStderr) {
#if defined(_WIN32)
  char app[] = "cmd.exe";
  std::string a1("/c");
  std::string_view a2(
      "<nul set /p=cap_stdout& <nul set /p=cap_stderr>&2&exit /b 0");
#else
  char app[] = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n cap_stdout; echo -n cap_stderr >&2");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_stdout");
  ASSERT_EQ(err.to_string(), "cap_stderr");
}

// ============================================================================
// Windows wide character tests
//   (wchar_t*, const wchar_t*, std::wstring, std::wstring_view)
// ============================================================================

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// subprocess::run — wide character types
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, RunConstWCharPtr) {
  subprocess::buffer out;
  const wchar_t* app = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  const wchar_t* a2 = L"<nul set /p=hello_const_wchar_ptr&exit /b 0";
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_const_wchar_ptr");
}

TEST_F(RunVariadicTypesTest, RunWCharPtr) {
  subprocess::buffer out;
  wchar_t app[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  wchar_t a2[] = L"<nul set /p=hello_wchar_ptr&exit /b 0";
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_wchar_ptr");
}

TEST_F(RunVariadicTypesTest, RunStdWString) {
  subprocess::buffer out;
  std::wstring app(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=hello_std_wstring&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_std_wstring");
}

TEST_F(RunVariadicTypesTest, RunStdWStringView) {
  subprocess::buffer out;
  std::wstring_view app(L"cmd.exe");
  std::wstring_view a1(L"/c");
  std::wstring_view a2(L"<nul set /p=hello_wstring_view&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_wstring_view");
}

TEST_F(RunVariadicTypesTest, RunMixedWideTypes) {
  subprocess::buffer out;
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(L"<nul set /p=hello_wide_mixed&exit /b 0");
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "hello_wide_mixed");
}

// ---------------------------------------------------------------------------
// subprocess::capture_run — wide character types
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, CaptureRunConstWCharPtr) {
  const wchar_t* app = L"cmd.exe";
  const wchar_t* a1 = L"/c";
  const wchar_t* a2 = L"<nul set /p=cap_const_wchar_ptr&exit /b 0";
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_const_wchar_ptr");
  ASSERT_TRUE(err.empty());
}

TEST_F(RunVariadicTypesTest, CaptureRunWCharPtr) {
  wchar_t app[] = L"cmd.exe";
  wchar_t a1[] = L"/c";
  wchar_t a2[] = L"<nul set /p=cap_wchar_ptr&exit /b 0";
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_wchar_ptr");
  ASSERT_TRUE(err.empty());
}

TEST_F(RunVariadicTypesTest, CaptureRunStdWString) {
  std::wstring app(L"cmd.exe");
  std::wstring a1(L"/c");
  std::wstring a2(L"<nul set /p=cap_std_wstring&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_std_wstring");
  ASSERT_TRUE(err.empty());
}

TEST_F(RunVariadicTypesTest, CaptureRunStdWStringView) {
  std::wstring_view app(L"cmd.exe");
  std::wstring_view a1(L"/c");
  std::wstring_view a2(L"<nul set /p=cap_wstring_view&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_wstring_view");
  ASSERT_TRUE(err.empty());
}

TEST_F(RunVariadicTypesTest, CaptureRunMixedWideTypes) {
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(L"<nul set /p=cap_wide_mixed&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "cap_wide_mixed");
  ASSERT_TRUE(err.empty());
}

// ---------------------------------------------------------------------------
// Wide types with stderr capture
// ---------------------------------------------------------------------------

TEST_F(RunVariadicTypesTest, RunMixedWideWithStderr) {
  subprocess::buffer out;
  subprocess::buffer err;
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(
      L"<nul set /p=w_stdout& <nul set /p=w_stderr>&2&exit /b 0");
  int ec = run(app, a1, a2, std_out > out, std_err > err);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_stdout");
  ASSERT_EQ(err.to_string(), "w_stderr");
}

TEST_F(RunVariadicTypesTest, CaptureRunMixedWideWithStderr) {
  wchar_t app[] = L"cmd.exe";
  std::wstring a1(L"/c");
  std::wstring_view a2(
      L"<nul set /p=w_cap_stdout& <nul set /p=w_cap_stderr>&2&exit /b 0");
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 0);
  ASSERT_EQ(out.to_string(), "w_cap_stdout");
  ASSERT_EQ(err.to_string(), "w_cap_stderr");
}

#endif  // _WIN32

// ============================================================================
// Edge case: run / capture_run with exit code from variadic args
// ============================================================================

TEST_F(RunVariadicTypesTest, RunNonZeroExitCode) {
  const char* app =
#if defined(_WIN32)
      "cmd.exe";
  const char* a1 = "/c";
  const char* a2 = "exit /b 42";
#else
      "bash";
  const char* a1 = "-c";
  const char* a2 = "exit 42";
#endif
  int ec = run(app, a1, a2);
  ASSERT_EQ(ec, 42);
}

TEST_F(RunVariadicTypesTest, CaptureRunNonZeroExitCode) {
#if defined(_WIN32)
  std::string app("cmd.exe");
  std::string a1("/c");
  std::string_view a2("exit /b 99");
#else
  std::string app("bash");
  std::string a1("-c");
  std::string_view a2("exit 99");
#endif
  auto [ec, out, err] = capture_run(app, a1, a2);
  ASSERT_EQ(ec, 99);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
}

// ============================================================================
// Edge case: many arguments of mixed types
// ============================================================================

TEST_F(RunVariadicTypesTest, RunManyMixedArgs) {
  subprocess::buffer out;
#if defined(_WIN32)
  const char* app = "cmd.exe";
  std::string a1("/c");
  // Use echo to test multiple args
  std::string_view a2("echo one two three four five&exit /b 0");
#else
  const char* app = "bash";
  std::string a1("-c");
  std::string_view a2("echo -n 'one two three four five'");
#endif
  int ec = run(app, a1, a2, std_out > out);
  ASSERT_EQ(ec, 0);
#if defined(_WIN32)
  ASSERT_EQ(out.to_string(), "one two three four five\r\n");
#else
  ASSERT_EQ(out.to_string(), "one two three four five");
#endif
}
