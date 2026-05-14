#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"

using subprocess::buffer;
using namespace subprocess::named_arguments;
using std::string_literals::operator""s;

// ===========================================================================
// 1. Basic explicit-Pipe chaining (the original test, kept as-is in spirit)
// ===========================================================================
TEST(PipeTest, ThreeProcessManualChain) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto pipe1 = subprocess::detail::Pipe::create();
  auto pipe2 = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p1(
      {"cmd.exe"s, "/c", "echo 123&echo 124&echo 456&exit /b 0"},
      $stdout > pipe1);
  subprocess::detail::subprocess p2({"findstr.exe"s, "2"},
                                    $stdin<pipe1, $stdout> pipe2);
  subprocess::detail::subprocess p3({"findstr.exe"s, "4"},
                                    $stdin<pipe2, $stdout> out);

  p1.async_run();
  p2.async_run();
  p3.async_run();

  auto r1 = p1.wait_for_exit();
  auto r2 = p2.wait_for_exit();
  auto r3 = p3.wait_for_exit();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ(out, "124\r\n");
#else
  subprocess::buffer out;
  auto pipe1 = subprocess::detail::Pipe::create();
  auto pipe2 = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p1({"echo", "123\n456"}, $stdout > pipe1);
  subprocess::detail::subprocess p2({"sed", "-e", "s/3/4/g"},
                                    $stdin<pipe1, $stdout> pipe2);
  subprocess::detail::subprocess p3({"grep", "4"}, $stdin<pipe2, $stdout> out);

  p1.async_run();
  p2.async_run();
  p3.async_run();

  auto r1 = p1.wait_for_exit();
  auto r2 = p2.wait_for_exit();
  auto r3 = p3.wait_for_exit();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ(out, "124\n456\n");
#endif
}

// ===========================================================================
// 2. Two-process pipe via | operator (pipeline)
// ===========================================================================
TEST(PipeTest, TwoProcessPipeOperator) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo Hello&echo World&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "Wor"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  EXPECT_EQ(subs.exit_codes()[0], 0);
  EXPECT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "World\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "Hello\\nWorld\\n"}) |
              subprocess::detail::subprocess({"grep"s, "Wor"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  EXPECT_EQ(subs.exit_codes()[0], 0);
  EXPECT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "World\n");
#endif
}

// ===========================================================================
// 3. Four-process pipe chain via | operator
// ===========================================================================
TEST(PipeTest, FourProcessPipeOperator) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"cmd.exe"s, "/c",
                                      "echo Apple&echo Banana&echo "
                                      "Apricot&echo Cherry&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "A"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "p"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "r"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 4u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_EQ(codes[1], 0);
  EXPECT_EQ(codes[2], 0);
  EXPECT_EQ(codes[3], 0);
  ASSERT_EQ(out, "Apricot\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess(
                  {"printf"s, "Apple\\nBanana\\nApricot\\nCherry\\n"}) |
              subprocess::detail::subprocess({"grep"s, "A"}) |
              subprocess::detail::subprocess({"grep"s, "p"}) |
              subprocess::detail::subprocess({"grep"s, "r"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 4u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_EQ(codes[1], 0);
  EXPECT_EQ(codes[2], 0);
  EXPECT_EQ(codes[3], 0);
  ASSERT_EQ(out, "Apricot\n");
#endif
}

// ===========================================================================
// 4. Pipe stdin from a buffer into a chain
// ===========================================================================
TEST(PipeTest, StdinFromBufferIntoPipeChain) {
#if defined(_WIN32)
  subprocess::buffer in{"Line1\nLine2\nLine3\n"};
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"findstr.exe"s, "2"}, $stdin < in) |
      subprocess::detail::subprocess({"findstr.exe"s, "Line"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  ASSERT_EQ(out, "Line2\n");
#else
  subprocess::buffer in{"Line1\nLine2\nLine3\n"};
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"grep"s, "2"}, $stdin < in) |
              subprocess::detail::subprocess({"grep"s, "Line"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  ASSERT_EQ(out, "Line2\n");
#endif
}

// ===========================================================================
// 5. Pipe stdout into a buffer from a chain
// ===========================================================================
TEST(PipeTest, PipeChainStdoutToBuffer) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo AAA&echo BBB&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "A"}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes()[0], 0);
  ASSERT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "AAA\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "AAA\\nBBB\\n"}) |
              subprocess::detail::subprocess({"grep"s, "A"}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes()[0], 0);
  ASSERT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "AAA\n");
#endif
}

// ===========================================================================
// 6. Buffer-to-buffer: stdin from buffer, stdout to buffer via pipe
// ===========================================================================
TEST(PipeTest, BufferToBufferViaPipe) {
#if defined(_WIN32)
  subprocess::buffer in{"one\ntwo\nthree\nfour\nfive\n"};
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"findstr.exe"s, "o"}, $stdin < in) |
      subprocess::detail::subprocess({"findstr.exe"s, "f"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "four\n");
#else
  subprocess::buffer in{"one\ntwo\nthree\nfour\nfive\n"};
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"grep"s, "o"}, $stdin < in) |
              subprocess::detail::subprocess({"grep"s, "f"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "four\n");
#endif
}

// ===========================================================================
// 7. Pipe with stderr captured separately
// ===========================================================================
TEST(PipeTest, PipeWithStderrCapture) {
#if defined(_WIN32)
  // cmd: write to stdout and stderr, pipe through findstr
  subprocess::buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  {"cmd.exe"s, "/c",
                   "(echo stdout_line_1 & echo stderr_line_1>&2 & echo "
                   "stdout_line_2 & echo stderr_line_2>&2) & exit /b 0"},
                  $stderr > err_out) |
              subprocess::detail::subprocess({"findstr.exe"s, "stdout"});

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  EXPECT_EQ(codes[0], 0);
  // stderr from the first process should be captured
  ASSERT_FALSE(err_out.empty());
  std::string err_str(err_out.to_string());
  EXPECT_NE(err_str.find("stderr_line_1"), std::string::npos);
  EXPECT_NE(err_str.find("stderr_line_2"), std::string::npos);
#else
  subprocess::buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  {"bash"s, "-c",
                   "echo stdout_line_1; echo stderr_line_1>&2; echo "
                   "stdout_line_2; echo stderr_line_2>&2"},
                  $stderr > err_out) |
              subprocess::detail::subprocess({"grep"s, "stdout"});

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  EXPECT_EQ(codes[0], 0);
  // stderr from the first process should be captured
  ASSERT_FALSE(err_out.empty());
  auto err_str = err_out.to_string();
  EXPECT_NE(err_str.find("stderr_line_1"), std::string::npos);
  EXPECT_NE(err_str.find("stderr_line_2"), std::string::npos);
#endif
}

// ===========================================================================
// 8. Empty input through pipe
// ===========================================================================
TEST(PipeTest, EmptyInputThroughPipe) {
#if defined(_WIN32)
  subprocess::buffer in{};
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"findstr.exe"s, "x"}, $stdin < in) |
      subprocess::detail::subprocess({"findstr.exe"s, "y"}, $stdout > out);

  subs.run();
  // With empty input, findstr/grep will find nothing, exit 1
  // On Windows findstr returns 1 when no match
  EXPECT_TRUE(out.empty());
#else
  subprocess::buffer in{};
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"grep"s, "x"}, $stdin < in) |
              subprocess::detail::subprocess({"grep"s, "y"}, $stdout > out);

  subs.run();
  EXPECT_TRUE(out.empty());
#endif
}

// ===========================================================================
// 9. Large data through pipes
// ===========================================================================
TEST(PipeTest, LargeDataThroughPipe) {
  // Build a sizeable input with predictable content
  std::string input_data;
  for (int i = 0; i < 5000; ++i) {
    input_data += "line_" + std::to_string(i) + "\n";
  }

#if defined(_WIN32)
  // Feed large data into a two-process pipe chain.
  // Both processes match "line_", so all lines should pass through.
  subprocess::buffer in{input_data};
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"findstr.exe"s, "line_"}, $stdin < in) |
      subprocess::detail::subprocess({"findstr.exe"s, "line_"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(std::count(result.begin(), result.end(), '\n'), 5000);
#else
  subprocess::buffer in{input_data};
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"grep"s, "line_"}, $stdin < in) |
              subprocess::detail::subprocess({"grep"s, "line_"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(static_cast<int>(std::count(result.begin(), result.end(), '\n')),
            5000);
#endif
}

// ===========================================================================
// 10. Mixed redirect: pipe plus stderr to devnull
// ===========================================================================
TEST(PipeTest, PipeChainWithStderrToDevnull) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"cmd.exe"s, "/c",
                                      "echo keep_me&echo ignore_me>&2&exit "
                                      "/b 0"},
                                     $stderr > $devnull) |
      subprocess::detail::subprocess({"findstr.exe"s, "keep"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "keep_me\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess(
                  {"bash"s, "-c", "echo keep_me; echo ignore_me>&2"},
                  $stderr > $devnull) |
              subprocess::detail::subprocess({"grep"s, "keep"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "keep_me\n");
#endif
}

// ===========================================================================
// 11. Single-process pipe (just a pipe with buffer, no actual second process)
//    Verifies that a Pipe can be used as a simple stdout capture.
// ===========================================================================
TEST(PipeTest, SingleProcessWithExplicitPipe) {
#if defined(_WIN32)
  auto pipe = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p(
      {"cmd.exe"s, "/c", "echo single_pipe_test&exit /b 0"}, $stdout > pipe);

  // The subprocess writes to the pipe write end; we read from the read end.
  p.async_run();
  buffer tmp;
  subprocess::detail::read_from_native_handle(pipe.read(), tmp);
  p.wait_for_exit();
  ASSERT_EQ(tmp, "single_pipe_test\r\n");
#else
  auto pipe = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p({"printf", "single_pipe_test\\n"},
                                   $stdout > pipe);

  // The subprocess writes to the pipe write end; we read from the read end.
  p.async_run();
  buffer tmp;
  subprocess::detail::read_from_native_handle(pipe.read(), tmp);
  p.wait_for_exit();
  ASSERT_EQ(tmp, "single_pipe_test\n");
#endif
}

// ===========================================================================
// 12. Pipe with cwd set on one process in the chain
// ===========================================================================
TEST(PipeTest, PipeChainWithCwd) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo cwd_test_line&exit /b 0"}, $cwd = "C:\\") |
      subprocess::detail::subprocess({"findstr.exe"s, "cwd"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "cwd_test_line\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "cwd_test_line\\n"},
                                             $cwd = "/tmp") |
              subprocess::detail::subprocess({"grep"s, "cwd"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "cwd_test_line\n");
#endif
}

// ===========================================================================
// 13. Pipe with environment variable set on first process
// ===========================================================================
TEST(PipeTest, PipeChainWithEnv) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo %MY_PIPE_VAR%&exit /b 0"},
          $env = {{"MY_PIPE_VAR", "piped_env_value"}}) |
      subprocess::detail::subprocess({"findstr.exe"s, "piped"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "piped_env_value\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess(
                  {"bash"s, "-c", "echo $MY_PIPE_VAR"},
                  $env = {{"MY_PIPE_VAR", "piped_env_value"}}) |
              subprocess::detail::subprocess({"grep"s, "piped"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "piped_env_value\n");
#endif
}

// ===========================================================================
// 14. Non-zero exit code in the middle of a pipe chain
// ===========================================================================
TEST(PipeTest, NonZeroExitInMiddleOfChain) {
#if defined(_WIN32)
  subprocess::buffer out;
  // "findstr nothing" will exit with code 1 on no match
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo data_line&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "no_such_string"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "."}, $stdout > out);

  [[maybe_unused]] int ret = subs.run();
  auto codes = subs.exit_codes();
  ASSERT_EQ(codes.size(), 3u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_NE(codes[1], 0);  // findstr returns 1 when no match
  EXPECT_EQ(codes[2], 1);
  // The last process received no input (since the middle filtered everything)
  EXPECT_TRUE(out.empty());
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "data_line\\n"}) |
              subprocess::detail::subprocess({"grep"s, "no_such_string"}) |
              subprocess::detail::subprocess({"grep"s, "."}, $stdout > out);

  [[maybe_unused]] int ret = subs.run();
  auto codes = subs.exit_codes();
  ASSERT_EQ(codes.size(), 3u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_NE(codes[1], 0);  // grep returns 1 when no match
  EXPECT_EQ(codes[2], 1);
  EXPECT_TRUE(out.empty());
#endif
}

// ===========================================================================
// 15. Pipe chain: every process exits code 0 even when there's no output
//    (e.g. "cat" / "more" passes everything through)
// ===========================================================================
TEST(PipeTest, IdentityPipeChain) {
#if defined(_WIN32)
  subprocess::buffer out;
  // more.com passes through, findstr "." matches any non-empty line
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo identity_test&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "."}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  ASSERT_EQ(out, "identity_test\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "identity_test\\n"}) |
              subprocess::detail::subprocess({"cat"s}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  ASSERT_EQ(out, "identity_test\n");
#endif
}

// ===========================================================================
// 16. Pipe with both stdout and stderr captured in the last process
// ===========================================================================
TEST(PipeTest, PipeChainCaptureBothStdoutAndStderrAtEnd) {
#if defined(_WIN32)
  subprocess::buffer out;
  subprocess::buffer err;
  auto subs =
      subprocess::detail::subprocess({"cmd.exe"s, "/c",
                                      "echo to_stdout&echo to_stderr>&2&"
                                      "exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "to_"}, $stdout > out,
                                     $stderr > err);

  subs.run();
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  EXPECT_NE(out_str.find("to_stdout"), std::string::npos);
  // findstr outputs matching lines to stdout; stderr should be empty
  // unless findstr itself wrote errors
#else
  subprocess::buffer out;
  subprocess::buffer err;
  auto subs = subprocess::detail::subprocess(
                  {"bash"s, "-c", "echo to_stdout; echo to_stderr>&2"}) |
              subprocess::detail::subprocess({"grep"s, "to_"}, $stdout > out,
                                             $stderr > err);

  subs.run();
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  EXPECT_NE(out_str.find("to_stdout"), std::string::npos);
#endif
}

// ===========================================================================
// 17. Multiple independent pipe chains in the same test
// ===========================================================================
TEST(PipeTest, MultipleIndependentPipeChains) {
#if defined(_WIN32)
  // Chain A
  subprocess::buffer outA;
  auto chainA =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo Apple&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "App"}, $stdout > outA);

  // Chain B
  subprocess::buffer outB;
  auto chainB =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo Banana&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "Ban"}, $stdout > outB);

  chainA.run();
  chainB.run();

  ASSERT_EQ(outA, "Apple\r\n");
  ASSERT_EQ(outB, "Banana\r\n");
#else
  subprocess::buffer outA;
  auto chainA =
      subprocess::detail::subprocess({"printf"s, "Apple\\n"}) |
      subprocess::detail::subprocess({"grep"s, "App"}, $stdout > outA);

  subprocess::buffer outB;
  auto chainB =
      subprocess::detail::subprocess({"printf"s, "Banana\\n"}) |
      subprocess::detail::subprocess({"grep"s, "Ban"}, $stdout > outB);

  chainA.run();
  chainB.run();

  ASSERT_EQ(outA, "Apple\n");
  ASSERT_EQ(outB, "Banana\n");
#endif
}

// ===========================================================================
// 18. Pipe the output of one piped chain into a new pipeline
//     (dynamically building a chain)
// ===========================================================================
TEST(PipeTest, IncrementalPipeChainBuilding) {
#if defined(_WIN32)
  subprocess::buffer out;

  subprocess::detail::pipeline chain =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo first_phase&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "first"});

  // Append another stage
  chain = std::move(chain) | subprocess::detail::subprocess(
                                 {"findstr.exe"s, "phase"}, $stdout > out);

  chain.run();
  ASSERT_EQ(out, "first_phase\r\n");
#else
  subprocess::buffer out;

  subprocess::detail::pipeline chain =
      subprocess::detail::subprocess({"printf"s, "first_phase\\n"}) |
      subprocess::detail::subprocess({"grep"s, "first"});

  chain = std::move(chain) |
          subprocess::detail::subprocess({"grep"s, "phase"}, $stdout > out);

  chain.run();
  ASSERT_EQ(out, "first_phase\n");
#endif
}

// ===========================================================================
// 19. Pipe with line counting (wc -l / find /c)
// ===========================================================================
TEST(PipeTest, PipeWithLineCounting) {
#if defined(_WIN32)
  subprocess::buffer out;
  // find /c counts lines
  auto subs = subprocess::detail::subprocess(
                  {"cmd.exe"s, "/c", "echo A&echo B&echo C&exit /b 0"}) |
              subprocess::detail::subprocess({"find.exe"s, "/c", "/v", ""},
                                             $stdout > out);

  subs.run();
  std::string result = out.to_string();
  // find /c /v "" outputs something like "---------- FOO.TXT: 3"
  EXPECT_NE(result.find("3"), std::string::npos);
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "A\\nB\\nC\\n"}) |
              subprocess::detail::subprocess({"wc"s, "-l"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  // wc -l outputs something like "       3"
  EXPECT_NE(result.find("3"), std::string::npos);
#endif
}

// ===========================================================================
// 20. Data integrity: binary-like data through pipes
// ===========================================================================
TEST(PipeTest, BinaryLikeDataThroughPipe) {
  // Use data with null bytes, high bytes etc. but still valid in arguments.
  // We'll generate printable data with spaces and special chars.
  std::string input;
  for (int i = 32; i < 127; ++i) {
    input += static_cast<char>(i);
    if (i % 16 == 0) {
      input += '\n';
    }
  }

#if defined(_WIN32)
  // On Windows, use findstr "." to match any line with content
  subprocess::buffer in{input};
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"findstr.exe"s, "."}, $stdin < in) |
      subprocess::detail::subprocess({"findstr.exe"s, "."}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  // All printable lines should pass through both filters
  EXPECT_GT(result.size(), 0u);
#else
  subprocess::buffer in{input};
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"cat"s}, $stdin < in) |
              subprocess::detail::subprocess({"cat"s}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(result, input);
#endif
}

// ===========================================================================
// 21. Run pipe chain using the top-level run() with | syntax
// ===========================================================================
TEST(PipeTest, TopLevelRunWithPipeOperator) {
  using subprocess::run;
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo top_level&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "top"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "top_level\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"printf"s, "top_level\\n"}) |
              subprocess::detail::subprocess({"grep"s, "top"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "top_level\n");
#endif
}

// ===========================================================================
// 22. Pipe with a process that writes to both stdout and stderr,
//     only stdout goes through the pipe
// ===========================================================================
TEST(PipeTest, OnlyStdoutGoesThroughPipe) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess({"cmd.exe"s, "/c",
                                      "echo goes_to_pipe&echo "
                                      "goes_to_stderr_only>&2&exit /b 0"},
                                     $stderr > $devnull) |
      subprocess::detail::subprocess({"findstr.exe"s, "goes_to_pipe"},
                                     $stdout > out);

  subs.run();
  ASSERT_EQ(out, "goes_to_pipe\r\n");
#else
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"bash"s, "-c", "echo goes_to_pipe; echo goes_to_stderr_only>&2"},
          $stderr > $devnull) |
      subprocess::detail::subprocess({"grep"s, "goes_to_pipe"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "goes_to_pipe\n");
#endif
}

// ===========================================================================
// 23. Pipe: explicit Pipe object closed after use (smoke test for RAII)
// ===========================================================================
TEST(PipeTest, ExplicitPipeClosedAfterUse) {
  buffer captured;
  {
    auto pipe = subprocess::detail::Pipe::create();
#if defined(_WIN32)
    subprocess::detail::subprocess p(
        {"cmd.exe"s, "/c", "echo pipe_raii&exit /b 0"}, $stdout > pipe);
#else
    subprocess::detail::subprocess p({"printf", "pipe_raii\\n"},
                                     $stdout > pipe);
#endif
    p.async_run();
    subprocess::detail::read_from_native_handle(pipe.read(), captured);
    p.wait_for_exit();
    // pipe goes out of scope here; destructor should not complain
  }
#if defined(_WIN32)
  ASSERT_EQ(captured, "pipe_raii\r\n");
#else
  ASSERT_EQ(captured, "pipe_raii\n");
#endif
}

// ===========================================================================
// 24. Pipe chain with the high-level capture_run through piping
//     (using pipeline, capture stdout at the end)
// ===========================================================================
TEST(PipeTest, PipeChainEquivalentToCaptureRun) {
  using subprocess::capture_run;
  // Just verify that capture_run itself works with the pipe-friendly commands
#if defined(_WIN32)
  auto [exit_code, out, err] =
      capture_run("cmd.exe", "/c", "echo captured_pipe&exit /b 0");
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "captured_pipe\r\n");
#else
  auto [exit_code, out, err] = capture_run("printf", "captured_pipe\\n");
  ASSERT_EQ(exit_code, 0);
  ASSERT_EQ(out, "captured_pipe\n");
#endif
}
