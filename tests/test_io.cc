/**
 * test_io.cc — I/O redirection and capture tests
 *
 * Covers:
 *   - run() with stdout/stderr capture to buffer
 *   - capture_run() (stdout+stderr+exit_code in one call)
 *   - stdin from buffer
 *   - File redirection: overwrite (>), append (>>) for stdout and stderr
 *   - /dev/null (or NUL) redirection
 *   - Large data capture (100MB+)
 *   - Empty stdout/stderr capture
 */

#include <gtest/gtest.h>

#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::capture_run;
using subprocess::run;

// ===========================================================================
// Stdout / stderr capture via run() + named arguments
// ===========================================================================

TEST(IOTest, CaptureStdout) {
  buffer out;
  buffer err;

#if !defined(_WIN32)
  run("bash", "-c", "echo -n 123; echo -n '345' >&2", std_out > out,
      std_err > err);
#else
  run("cmd.exe", "/c", "<nul set /p=123& <nul set /p=345>&2\r\n", std_out > out,
      std_err > err);
#endif
  ASSERT_EQ(out, "123");
  ASSERT_EQ(err, "345");

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run("bash", "-c", "echo -n 123", std_out > out, std_err > err);
#else
  run("cmd.exe", "/c", "<nul set /p=123", std_out > out, std_err > err);
#endif
  ASSERT_EQ(out, "123");
  ASSERT_TRUE(err.empty());

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run("bash", "-c", "echo -n '123' >&2", std_out > out, std_err > err);
#else
  run("cmd.exe", "/c", "<nul set /p=123>&2", std_out > out, std_err > err);
#endif
  ASSERT_TRUE(out.empty());
  ASSERT_FALSE(err.empty());
  ASSERT_EQ(err, "123");
}

TEST(IOTest, CaptureStdoutBasic) {
  buffer stdout_buf;
  int exit_code = run(
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Hello Stdout&exit /b 0"
#else
      "/bin/echo", "-n", "Hello Stdout"
#endif
      ,
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "Hello Stdout");
}

TEST(IOTest, CaptureStderrBasic) {
  buffer stderr_buf;
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Hello Stderr>&2&exit /b 0"
#else
      "bash", "-c", "echo -n 'Hello Stderr' >&2"
#endif
      ,
      std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stderr_buf.to_string(), "Hello Stderr");
}

TEST(IOTest, CaptureBothStdoutAndStderr) {
  buffer stdout_buf;
  buffer stderr_buf;
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Out& <nul set /p=Err>&2&exit /b 0"
#else
      "bash", "-c", "echo -n Out; echo -n Err >&2"
#endif
      ,
      std_out > stdout_buf, std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(stdout_buf.to_string(), "Out");
  ASSERT_EQ(stderr_buf.to_string(), "Err");
}

TEST(IOTest, CaptureEmptyStdout) {
  buffer stdout_buf;
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "exit /b 0"
#else
      "true"
#endif
      ,
      std_out > stdout_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stdout_buf.empty());
}

TEST(IOTest, CaptureEmptyStderr) {
  buffer stderr_buf;
  int exit_code = run(
#if defined(_WIN32)
      "cmd.exe", "/c", "exit /b 0"
#else
      "true"
#endif
      ,
      std_err > stderr_buf);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(stderr_buf.empty());
}

// ===========================================================================
// capture_run() — combined stdout + stderr + exit code
// ===========================================================================

TEST(IOTest, CaptureRunBasic) {
  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("bash", "-c", "echo -n 123; echo -n '345' >&2");
#else
        capture_run("cmd.exe", "/c", "<nul set /p=123& <nul set /p=345>&2\r\n");
#endif
    ASSERT_EQ(out, "123");
    ASSERT_EQ(err, "345");
  }

  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("bash", "-c", "echo -n 123");
#else
        capture_run("cmd.exe", "/c", "<nul set /p=123");
#endif
    ASSERT_EQ(out, "123");
    ASSERT_TRUE(err.empty());
  }

  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("bash", "-c", "echo -n '123' >&2");
#else
        capture_run("cmd.exe", "/c", "<nul set /p=123>&2");
#endif
    ASSERT_TRUE(out.empty());
    ASSERT_FALSE(err.empty());
    ASSERT_EQ(err, "123");
  }
}

TEST(IOTest, CaptureRunLargeData) {
#if !defined(_WIN32)
  {
    auto [exit_code, out, err] =
        capture_run("head", "-c", "419430400", "/dev/zero");
    ASSERT_EQ(out.size(), (100 * 4 * 1024 * 1024));
  }
#else
  {
    auto [exit_code, out, err] =
        capture_run("powershell", "-NoProfile", "-c", "'A'*400MB");
    ASSERT_EQ(exit_code, 0) << err.data();
    ASSERT_EQ(out.size(), (400 * 1024 * 1024 + 2));  // \r\n
  }
#endif
}

// ===========================================================================
// Stdin from buffer
// ===========================================================================

TEST(IOTest, StdinFromBuffer) {
  buffer in{"123"};
  buffer out;
#if !defined(_WIN32)
  run("/bin/cat", "-", std_in<in, std_out> out);
  ASSERT_EQ(in, out);
#else
  run("more.com", std_in<in, std_out> out);
  auto out_span = out.span();
  auto it = std::find_if(out_span.rbegin(), out_span.rend(),
                         [](char c) { return c != '\r' && c != '\n'; });
  if (it != out_span.rend()) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }
  ASSERT_TRUE(
      std::equal(in.begin(), in.end(), out_span.begin(), out_span.end()));
#endif
}

// ===========================================================================
// File redirection: overwrite (>)
// ===========================================================================

TEST(IOTest, RedirectStdoutToFileOverwrite) {
  TempFile tmp_file;
  buffer content{"123"};
#if !defined(_WIN32)
  run("echo", "-n", content.to_string(), std_out > tmp_file.path());
#else
  run("cmd.exe", "/c", "<nul set /p=" + content.to_string(),
      std_out > tmp_file.path());
#endif
  ASSERT_EQ(content.to_string(), tmp_file.content_str());
}

TEST(IOTest, RedirectStderrToFileOverwrite) {
  TempFile tmp_file;
  buffer content{"123"};
#if !defined(_WIN32)
  run("bash", "-c", "echo -n " + content.to_string() + " >&2",
      std_err > tmp_file.path());
#else
  run("cmd.exe", "/c", "<nul set /p=" + content.to_string() + ">&2",
      std_err > tmp_file.path());
#endif
  ASSERT_EQ(content.to_string(), tmp_file.content_str());
}

TEST(IOTest, RedirectStdoutToFileOverwrite2) {
  TempFile temp_file;
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Overwrite Content&exit /b 0"
#else
      "/bin/echo", "-n", "Overwrite Content"
#endif
      ,
      std_out > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Overwrite Content");
}

TEST(IOTest, RedirectStderrToFileOverwrite2) {
  TempFile temp_file;
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Error Overwrite>&2&exit /b 0"
#else
      "bash", "-c", "echo -n 'Error Overwrite' >&2"
#endif
      ,
      std_err > temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Error Overwrite");
}

// ===========================================================================
// File redirection: append (>>)
// ===========================================================================

TEST(IOTest, RedirectStdoutToFileAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string{"000"});
  buffer content{"123"};
#if !defined(_WIN32)
  run("echo", "-n", content.to_string(), std_out >> tmp_file.path());
#else
  run("cmd.exe", "/c", "<nul set /p=" + content.to_string(),
      std_out >> tmp_file.path());
#endif
  ASSERT_EQ("000123", tmp_file.content_str());
}

TEST(IOTest, RedirectStderrToFileAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string{"999"});
  buffer content{"123"};
#if !defined(_WIN32)
  run("bash", "-c", "echo -n " + content.to_string() + " >&2",
      std_err >> tmp_file.path());
#else
  run("cmd.exe", "/c", "<nul set /p=" + content.to_string() + ">&2",
      std_err >> tmp_file.path());
#endif
  ASSERT_EQ("999123", tmp_file.content_str());
}

TEST(IOTest, RedirectStdoutToFileAppend2) {
  TempFile temp_file;
  temp_file.write(std::string{"Initial\n"});
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=Appended&exit /b 0"
#else
      "/bin/echo", "-n", "Appended"
#endif
      ,
      std_out >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "Initial\nAppended");
}

TEST(IOTest, RedirectStderrToFileAppend2) {
  TempFile temp_file;
  temp_file.write(std::string{"InitialError\n"});
  int exit_code = run(

#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=AppendedError>&2&exit /b 0"
#else
      "bash", "-c", "echo -n 'AppendedError' >&2"
#endif
      ,
      std_err >> temp_file.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(temp_file.content_str(), "InitialError\nAppendedError");
}

// ===========================================================================
// /dev/null (or NUL) redirection
// ===========================================================================

TEST(IOTest, Devnull) {
  auto ret =
#if defined(_WIN32)
      run("cmd.exe", "/c", "echo 123", $stdout > $devnull);
#else
      run("/bin/echo", "123", $stdout > $devnull);
#endif
  ASSERT_EQ(0, ret);
}
