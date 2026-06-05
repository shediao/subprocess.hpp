#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"
#include "temp_file.h"

using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::const_buffer;
using subprocess::dynamic_buffer;

[[maybe_unused]]
static void write_all_and_close(subprocess::detail::unique_fd& fd,
                                dynamic_buffer const& write_data) {
  auto write_span = write_data.span();
  subprocess::detail::write_all(fd, write_span.data(), write_span.size());
  fd.close();
}

[[maybe_unused]]
static void write_all_and_close(subprocess::detail::unique_fd& fd,
                                const_buffer write_data) {
  subprocess::detail::write_all(fd, write_data.data(), write_data.size());
  fd.close();
}

// ===========================================================================
// 1. Default behavior (backward compatibility): no stdin arg → devnull
//    Programs that read stdin should get EOF immediately.
// ===========================================================================
TEST(CaptureStdinTest, DefaultStdinIsDevnull) {
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat");
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com");
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 2. Explicit std_in < devnull — same as default
// ===========================================================================
TEST(CaptureStdinTest, ExplicitStdinDevnull) {
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < devnull);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < devnull);
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 3. stdin from File — two-argument form
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileTwoArgForm) {
  TempFile tf("capstdin", ".txt");
  std::string expected = "hello_from_file\n";
  tf.write(expected);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, expected);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "hello_from_file\r\n");
#endif
}

// ===========================================================================
// 4. stdin from Buffer — two-argument form
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferTwoArgForm) {
  const_buffer in{"buffer_content\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "buffer_content\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("buffer_content"), std::string::npos);
#endif
}

// ===========================================================================
// 5. stdin from Pipe — two-argument form
// ===========================================================================
TEST(CaptureStdinTest, StdinFromPipeTwoArgForm) {
  auto pipe = subprocess::detail::Pipe::create();
  std::string pipe_data = "pipe_data_123\n";
  write_all_and_close(pipe.wfd(), const_buffer{pipe_data});

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, pipe_data);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 6. Variadic form: stdin from File
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileVariadicForm) {
  TempFile tf("capstdin_var", ".txt");
  std::string expected = "variadic_file_content\nline2\n";
  tf.write(expected);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, expected);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 7. Variadic form: stdin from Buffer
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferVariadicForm) {
  dynamic_buffer in{"variadic_buffer_data\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "variadic_buffer_data\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("variadic_buffer_data"), std::string::npos);
#endif
}

// ===========================================================================
// 8. Variadic form: stdin from Pipe
// ===========================================================================
TEST(CaptureStdinTest, StdinFromPipeVariadicForm) {
  auto pipe = subprocess::detail::Pipe::create();
  std::string pipe_data = "variadic_pipe_data\n";
  write_all_and_close(pipe.wfd(), const_buffer{pipe_data});

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, pipe_data);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 9. stdin from File combined with other named arguments (env)
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileWithEnvVariadicForm) {
  TempFile tf("capstdin_env", ".txt");
  std::string file_content = "content_with_env\n";
  tf.write(file_content);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run(
      "cat", std_in < tf.path(), env = {{"MY_TEST_VAR", "test_value"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, file_content);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path(),
                  env = {{"MY_TEST_VAR", "test_value"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 10. stdin from Buffer combined with env and cwd (variadic form)
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferWithEnvAndCwdVariadicForm) {
  dynamic_buffer in{"data_with_env_cwd\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("cat", std_in < in, env = {{"FOO", "bar"}}, cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "data_with_env_cwd\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run(
      "more.com", std_in < in, env = {{"FOO", "bar"}}, cwd = "C:\\");
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 11. stdin from Buffer — two-arg form with env
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferWithEnvTwoArgForm) {
  dynamic_buffer in{"two_arg_with_env\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("cat", std_in < in, env = {{"MYVAR", "myval"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "two_arg_with_env\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("more.com", std_in < in, env = {{"MYVAR", "myval"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 12. Empty stdin from File
// ===========================================================================
TEST(CaptureStdinTest, EmptyStdinFromFile) {
  TempFile tf("empty_stdin", ".txt");
  {
    std::ofstream out(tf.path());
    out.close();
  }

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 13. Empty stdin from Buffer
// ===========================================================================
TEST(CaptureStdinTest, EmptyStdinFromBuffer) {
  dynamic_buffer in{};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 14. Empty stdin from Pipe
// ===========================================================================
TEST(CaptureStdinTest, EmptyStdinFromPipe) {
  auto pipe = subprocess::detail::Pipe::create();
  pipe.wfd().close();

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 15. Large data via stdin from Buffer
// ===========================================================================
TEST(CaptureStdinTest, LargeDataStdinFromBuffer) {
  std::string large_data;
  for (int i = 0; i < 10000; ++i) {
    large_data += "line_" + std::to_string(i) + "\n";
  }
  dynamic_buffer in{large_data};

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("wc", "-l", std_in < in);
  ASSERT_EQ(exit_code, 0);
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("10000"), std::string::npos);
#else
  auto [exit_code, out, err] =
      capture_run("find.exe", "/c", "/v", "", std_in < in);
  ASSERT_EQ(exit_code, 0);
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("10000"), std::string::npos);
#endif
}

// ===========================================================================
// 16. Large data via stdin from File
// ===========================================================================
TEST(CaptureStdinTest, LargeDataStdinFromFile) {
  TempFile tf("large_stdin", ".txt");
  std::string large_data;
  for (int i = 0; i < 5000; ++i) {
    large_data += "file_line_" + std::to_string(i) + "\n";
  }
  tf.write(large_data);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("wc", "-l", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("5000"), std::string::npos);
#else
  auto [exit_code, out, err] =
      capture_run("find.exe", "/c", "/v", "", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("5000"), std::string::npos);
#endif
}

// ===========================================================================
// 17. Multiple lines via stdin from Buffer
// ===========================================================================
TEST(CaptureStdinTest, MultiLineStdinFromBuffer) {
  dynamic_buffer in{"first line\nsecond line\nthird line\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "first line\nsecond line\nthird line\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 18. stdin overrides default devnull (override semantics with Buffer)
// ===========================================================================
TEST(CaptureStdinTest, StdinOverrideDefaultDevnullWithBuffer) {
  dynamic_buffer in{"overridden_stdin\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "overridden_stdin\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("overridden_stdin"), std::string::npos);
#endif
}

// ===========================================================================
// 19. stdin overrides default devnull (override semantics with File)
// ===========================================================================
TEST(CaptureStdinTest, StdinOverrideDefaultDevnullWithFile) {
  TempFile tf("override_devnull", ".txt");
  std::string content = "file_overrides_devnull\n";
  tf.write(content);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, content);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 20. stdin redirect with command that writes to stderr
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferWithStderrOutput) {
  dynamic_buffer in{"input_data\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("bash", "-c", "cat; echo 'to_stderr' >&2", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "input_data\n");
  ASSERT_EQ(err, "to_stderr\n");
#else
  auto [exit_code, out, err] =
      capture_run("cmd.exe", "/c", "more.com & echo to_stderr>&2", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(err.empty());
#endif
}

// ===========================================================================
// 21. stdin from Pipe with special characters
// ===========================================================================
TEST(CaptureStdinTest, StdinFromPipeSpecialChars) {
  auto pipe = subprocess::detail::Pipe::create();
  std::string special_data = "spaces  and\ttabs\n!@#$%^&*()\n";
  write_all_and_close(pipe.wfd(), const_buffer{special_data});

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, special_data);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 22. stdin from File with binary content (including null bytes)
// ===========================================================================
TEST(CaptureStdinTest, BinaryDataStdinFromFile) {
  TempFile tf("binary_stdin", ".bin");
  std::vector<char> binary_data = {'H', 'e', 'l', 'l', 'o', '\0',
                                   'W', 'o', 'r', 'l', 'd', '\n'};
  tf.write(binary_data);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("wc", "-c", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("12"), std::string::npos);
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", "Hello", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 23. stdin from File with no trailing newline
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileNoTrailingNewline) {
  TempFile tf("no_newline", ".txt");
  std::string content = "no_trailing_newline";
  tf.write(content);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, content);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 24. stdin from Buffer with no trailing newline
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferNoTrailingNewline) {
  dynamic_buffer in{"no_newline_here"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "no_newline_here");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 25. stdin from Buffer — variadic form with multiple string args
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferVariadicMultiArgs) {
  dynamic_buffer in{"multi_arg_test\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("grep", "multi", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "multi_arg_test\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("findstr.exe", "multi", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 26. Two-arg form: stdin from File with multiple named args
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileTwoArgFormMultiNamedArgs) {
  TempFile tf("cap_two_arg", ".txt");
  std::string content = "two_arg_multi\n";
  tf.write(content);

#if !defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("bash", "-c", "cat; echo $MYVAR", std_in < tf.path(),
                  env = {{"MYVAR", "hello"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "two_arg_multi\nhello\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("cmd.exe", "/c", "more.com&echo %MYVAR%", std_in < tf.path(),
                  env = {{"MYVAR", "hello"}});
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 27. Verify exit code is correctly propagated when using stdin
// ===========================================================================
TEST(CaptureStdinTest, ExitCodePropagationWithStdin) {
  dynamic_buffer in{"success\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("grep", "success", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "success\n");

  auto [exit_code2, out2, err2] = capture_run("grep", "no_match", std_in < in);
  ASSERT_NE(exit_code2, 0);
  ASSERT_TRUE(out2.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", "success", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());

  auto [exit_code2, out2, err2] =
      capture_run("findstr.exe", "no_match", std_in < in);
  ASSERT_NE(exit_code2, 0);
#endif
}

// ===========================================================================
// 28. stdin from File with a program that ignores stdin
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileProgramIgnoresStdin) {
  TempFile tf("ignore_stdin", ".txt");
  tf.write(std::string{"unused_data\n"});

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("true", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_TRUE(out.empty());
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("cmd.exe", "/c", "exit /b 0", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
#endif
}

// ===========================================================================
// 29. stdin from Buffer with a program that reads partially
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferProgramReadsPartially) {
  dynamic_buffer in{"line1\nline2\nline3\nline4\nline5\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("head", "-n", "2", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "line1\nline2\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("findstr.exe", "line1", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 30. Pipe stdin: data written before process starts
// ===========================================================================
TEST(CaptureStdinTest, StdinFromPipeWriteBeforeProcessStart) {
  auto pipe = subprocess::detail::Pipe::create();
  std::string expected = "pipe_streaming_data\n";
  write_all_and_close(pipe.wfd(), const_buffer{expected});

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, expected);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < pipe);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 31. stdin from Buffer with UTF-8 content
// ===========================================================================
TEST(CaptureStdinTest, StdinFromBufferUnicodeContent) {
  dynamic_buffer in{"caf\xc3\xa9\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "caf\xc3\xa9\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] = capture_run("more.com", std_in < in);
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 32. stdin from File with unicode content
// ===========================================================================
TEST(CaptureStdinTest, StdinFromFileUnicodeContent) {
  TempFile tf("unicode_stdin", ".txt");
  std::string content = "こんにちは\n";
  tf.write(content);

#if !defined(_WIN32)
  auto [exit_code, out, err] = capture_run("cat", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, content);
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("findstr.exe", ".", std_in < tf.path());
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
#endif
}

// ===========================================================================
// 33. Combination: variadic form with stdin, env, cwd, and multiple args
// ===========================================================================
TEST(CaptureStdinTest, StdinWithAllNamedArgsVariadicForm) {
  dynamic_buffer in{"all_args_test\n"};
#if !defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("bash", "-c", "cat; echo $ALL_ARGS_VAR", std_in < in,
                  env = {{"ALL_ARGS_VAR", "works"}}, cwd = "/tmp");
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "all_args_test\nworks\n");
  ASSERT_TRUE(err.empty());
#else
  auto [exit_code, out, err] =
      capture_run("cmd.exe", "/c", "more.com & echo %ALL_ARGS_VAR%",
                  std_in < in, env = {{"ALL_ARGS_VAR", "works"}}, cwd = "C:\\");
  ASSERT_EQ(exit_code, 0);
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  ASSERT_NE(out_str.find("works"), std::string::npos);
#endif
}
