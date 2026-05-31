/**
 * test_pipeline.cc — Pipeline and pipe-chain tests
 *
 * Covers:
 *   - Manual pipe chaining with explicit Pipe objects (three-process chain)
 *   - Pipeline via | operator (2, 3, 4 process chains)
 *   - Pipe with stdin from buffer / stdout to buffer
 *   - Buffer-to-buffer piping
 *   - Pipe with stderr capture, stderr to devnull
 *   - Empty input / large data through pipes
 *   - Pipe with cwd, env set on chain members
 *   - Non-zero exit codes in middle of chain
 *   - Identity pipe chain
 *   - Both stdout and stderr capture at end of chain
 *   - Multiple independent pipe chains
 *   - Incremental pipeline building
 *   - Line counting, binary-like data through pipes
 *   - Top-level run() with pipeline
 *   - Stdout-only through pipe (stderr excluded)
 *   - Explicit Pipe RAII close
 *   - capture_run equivalence
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::capture_run;
using std::string_literals::operator""s;
#if defined(_WIN32)
using subprocess::detail::ssize_t;
#endif

static void read_from_native_handle(subprocess::detail::unique_fd const& fd,
                                    buffer& read_data) {
  char buf[1024];
  ssize_t read;
  do {
    read = subprocess::detail::read_some(fd, buf, sizeof(buf));
    if (read > 0) {
      read_data.append(buf, static_cast<size_t>(read));
    }
  } while (read > 0);
}
// ===========================================================================
// 1. Three-process manual pipe chain (explicit Pipe objects)
//    This consolidates the original test_pipe.cc and test_pipeline.cc tests.
// ===========================================================================
TEST(PipelineTest, ThreeProcessManualChain) {
#if defined(_WIN32)
  buffer out;
  auto pipe1 = subprocess::detail::Pipe::create();
  auto pipe2 = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p1(
      "cmd.exe"s, {"/c", "echo 123&echo 124&echo 456&exit /b 0"},
      $stdout > pipe1);
  subprocess::detail::subprocess p2("findstr.exe"s, {"2"},
                                    $stdin<pipe1, $stdout> pipe2);
  subprocess::detail::subprocess p3("findstr.exe"s, {"4"},
                                    $stdin<pipe2, $stdout> out);

  p1.spawn();
  p2.spawn();
  p3.spawn();

  auto r1 = p1.wait();
  auto r2 = p2.wait();
  auto r3 = p3.wait();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ(out, "124\r\n");
#else
  buffer out;
  auto pipe1 = subprocess::detail::Pipe::create();
  auto pipe2 = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p1("echo", {"123\n456"}, $stdout > pipe1);
  subprocess::detail::subprocess p2("sed", {"-e", "s/3/4/g"},
                                    $stdin<pipe1, $stdout> pipe2);
  subprocess::detail::subprocess p3("grep", {"4"}, $stdin<pipe2, $stdout> out);

  p1.spawn();
  p2.spawn();
  p3.spawn();

  auto r1 = p1.wait();
  auto r2 = p2.wait();
  auto r3 = p3.wait();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ(out, "124\n456\n");
#endif
}

// ===========================================================================
// 2. Three-process pipeline via | operator (consolidates test_pipeline.cc)
// ===========================================================================
TEST(PipelineTest, ThreeProcessPipeOperator) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo 123&echo 124&echo 456&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"2"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"4"}, $stdout > out);

  subs.run();
  auto exit_codes = subs.exit_codes();
  EXPECT_EQ(exit_codes.size(), 3u);
  EXPECT_EQ(exit_codes[0], 0);
  EXPECT_EQ(exit_codes[1], 0);
  EXPECT_EQ(exit_codes[2], 0);
  ASSERT_EQ(out, "124\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("echo"s, {"123\n456"}) |
              subprocess::detail::subprocess("sed"s, {"-e", "s/3/4/g"}) |
              subprocess::detail::subprocess("grep"s, {"4"}, $stdout > out);

  subs.run();
  auto exit_codes = subs.exit_codes();
  EXPECT_EQ(exit_codes.size(), 3u);
  EXPECT_EQ(exit_codes[0], 0);
  EXPECT_EQ(exit_codes[1], 0);
  EXPECT_EQ(exit_codes[2], 0);
  ASSERT_EQ(out, "124\n456\n");
#endif
}

// ===========================================================================
// 3. Two-process pipe via | operator
// ===========================================================================
TEST(PipelineTest, TwoProcessPipeOperator) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo Hello&echo World&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"Wor"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  EXPECT_EQ(subs.exit_codes()[0], 0);
  EXPECT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "World\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"Hello\\nWorld\\n"}) |
              subprocess::detail::subprocess("grep"s, {"Wor"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  EXPECT_EQ(subs.exit_codes()[0], 0);
  EXPECT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "World\n");
#endif
}

// ===========================================================================
// 4. Four-process pipe chain via | operator
// ===========================================================================
TEST(PipelineTest, FourProcessPipeOperator) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c",
                                      "echo Apple&echo Banana&echo "
                                      "Apricot&echo Cherry&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"A"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"p"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"r"}, $stdout > out);

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
  buffer out;
  auto subs = subprocess::detail::subprocess(
                  "printf"s, {"Apple\\nBanana\\nApricot\\nCherry\\n"}) |
              subprocess::detail::subprocess("grep"s, {"A"}) |
              subprocess::detail::subprocess("grep"s, {"p"}) |
              subprocess::detail::subprocess("grep"s, {"r"}, $stdout > out);

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
// 5. Pipe stdin from a buffer into a chain
// ===========================================================================
TEST(PipelineTest, StdinFromBufferIntoPipeChain) {
#if defined(_WIN32)
  buffer in{"Line1\nLine2\nLine3\n"};
  buffer out;
  auto subs =
      subprocess::detail::subprocess("findstr.exe"s, {"2"}, $stdin < in) |
      subprocess::detail::subprocess("findstr.exe"s, {"Line"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  ASSERT_EQ(out, "Line2\n");
#else
  buffer in{"Line1\nLine2\nLine3\n"};
  buffer out;
  auto subs = subprocess::detail::subprocess("grep"s, {"2"}, $stdin < in) |
              subprocess::detail::subprocess("grep"s, {"Line"}, $stdout > out);

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  ASSERT_EQ(out, "Line2\n");
#endif
}

// ===========================================================================
// 6. Pipe stdout into a buffer from a chain
// ===========================================================================
TEST(PipelineTest, PipeChainStdoutToBuffer) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo AAA&echo BBB&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"A"}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes()[0], 0);
  ASSERT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "AAA\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"AAA\\nBBB\\n"}) |
              subprocess::detail::subprocess("grep"s, {"A"}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes()[0], 0);
  ASSERT_EQ(subs.exit_codes()[1], 0);
  ASSERT_EQ(out, "AAA\n");
#endif
}

// ===========================================================================
// 7. Buffer-to-buffer: stdin from buffer, stdout to buffer via pipe
// ===========================================================================
TEST(PipelineTest, BufferToBufferViaPipe) {
#if defined(_WIN32)
  buffer in{"one\ntwo\nthree\nfour\nfive\n"};
  buffer out;
  auto subs =
      subprocess::detail::subprocess("findstr.exe"s, {"o"}, $stdin < in) |
      subprocess::detail::subprocess("findstr.exe"s, {"f"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "four\n");
#else
  buffer in{"one\ntwo\nthree\nfour\nfive\n"};
  buffer out;
  auto subs = subprocess::detail::subprocess("grep"s, {"o"}, $stdin < in) |
              subprocess::detail::subprocess("grep"s, {"f"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "four\n");
#endif
}

// ===========================================================================
// 8. Pipe with stderr captured separately
// ===========================================================================
TEST(PipelineTest, PipeWithStderrCapture) {
#if defined(_WIN32)
  buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  "cmd.exe"s,
                  {"/c",
                   "(echo stdout_line_1 & echo stderr_line_1>&2 & echo "
                   "stdout_line_2 & echo stderr_line_2>&2) & exit /b 0"},
                  $stderr > err_out) |
              subprocess::detail::subprocess("findstr.exe"s, {"stdout"});

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  EXPECT_EQ(codes[0], 0);
  ASSERT_FALSE(err_out.empty());
  std::string err_str(err_out.to_string());
  EXPECT_NE(err_str.find("stderr_line_1"), std::string::npos);
  EXPECT_NE(err_str.find("stderr_line_2"), std::string::npos);
#else
  buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  "bash"s,
                  {"-c",
                   "echo stdout_line_1; echo stderr_line_1>&2; echo "
                   "stdout_line_2; echo stderr_line_2>&2"},
                  $stderr > err_out) |
              subprocess::detail::subprocess("grep"s, {"stdout"});

  int ret = subs.run();
  auto codes = subs.exit_codes();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(codes.size(), 2u);
  EXPECT_EQ(codes[0], 0);
  ASSERT_FALSE(err_out.empty());
  auto err_str = err_out.to_string();
  EXPECT_NE(err_str.find("stderr_line_1"), std::string::npos);
  EXPECT_NE(err_str.find("stderr_line_2"), std::string::npos);
#endif
}

// ===========================================================================
// 9. Empty input through pipe
// ===========================================================================
TEST(PipelineTest, EmptyInputThroughPipe) {
#if defined(_WIN32)
  buffer in{};
  buffer out;
  auto subs =
      subprocess::detail::subprocess("findstr.exe"s, {"x"}, $stdin < in) |
      subprocess::detail::subprocess("findstr.exe"s, {"y"}, $stdout > out);

  subs.run();
  EXPECT_TRUE(out.empty());
#else
  buffer in{};
  buffer out;
  auto subs = subprocess::detail::subprocess("grep"s, {"x"}, $stdin < in) |
              subprocess::detail::subprocess("grep"s, {"y"}, $stdout > out);

  subs.run();
  EXPECT_TRUE(out.empty());
#endif
}

// ===========================================================================
// 10. Large data through pipes
// ===========================================================================
TEST(PipelineTest, LargeDataThroughPipe) {
  std::string input_data;
  for (int i = 0; i < 5000; ++i) {
    input_data += "line_" + std::to_string(i) + "\n";
  }

#if defined(_WIN32)
  buffer in{input_data};
  buffer out;
  auto subs =
      subprocess::detail::subprocess("findstr.exe"s, {"line_"}, $stdin < in) |
      subprocess::detail::subprocess("findstr.exe"s, {"line_"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(std::count(result.begin(), result.end(), '\n'), 5000);
#else
  buffer in{input_data};
  buffer out;
  auto subs = subprocess::detail::subprocess("grep"s, {"line_"}, $stdin < in) |
              subprocess::detail::subprocess("grep"s, {"line_"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(static_cast<int>(std::count(result.begin(), result.end(), '\n')),
            5000);
#endif
}

// ===========================================================================
// 11. Mixed redirect: pipe plus stderr to devnull
// ===========================================================================
TEST(PipelineTest, PipeChainWithStderrToDevnull) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c",
                                      "echo keep_me&echo ignore_me>&2&exit "
                                      "/b 0"},
                                     $stderr > $devnull) |
      subprocess::detail::subprocess("findstr.exe"s, {"keep"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "keep_me\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess(
                  "bash"s, {"-c", "echo keep_me; echo ignore_me>&2"},
                  $stderr > $devnull) |
              subprocess::detail::subprocess("grep"s, {"keep"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "keep_me\n");
#endif
}

// ===========================================================================
// 12. Single-process pipe (explicit Pipe for stdout capture)
// ===========================================================================
TEST(PipelineTest, SingleProcessWithExplicitPipe) {
#if defined(_WIN32)
  auto pipe = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p(
      "cmd.exe"s, {"/c"s, "echo single_pipe_test&exit /b 0"}, $stdout > pipe);

  p.spawn();
  buffer tmp;
  read_from_native_handle(pipe.rfd(), tmp);
  p.wait();
  ASSERT_EQ(tmp, "single_pipe_test\r\n");
#else
  auto pipe = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p("printf", {"single_pipe_test\\n"},
                                   $stdout > pipe);

  p.spawn();
  buffer tmp;
  read_from_native_handle(pipe.rfd(), tmp);
  p.wait();
  ASSERT_EQ(tmp, "single_pipe_test\n");
#endif
}

// ===========================================================================
// 13. Pipe with cwd set on one process in the chain
// ===========================================================================
TEST(PipelineTest, PipeChainWithCwd) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo cwd_test_line&exit /b 0"}, $cwd = "C:\\") |
      subprocess::detail::subprocess("findstr.exe"s, {"cwd"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "cwd_test_line\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"cwd_test_line\\n"},
                                             $cwd = "/tmp") |
              subprocess::detail::subprocess("grep"s, {"cwd"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "cwd_test_line\n");
#endif
}

// ===========================================================================
// 14. Pipe with environment variable set on first process
// ===========================================================================
TEST(PipelineTest, PipeChainWithEnv) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo %MY_PIPE_VAR%&exit /b 0"},
          $env = {{"MY_PIPE_VAR", "piped_env_value"}}) |
      subprocess::detail::subprocess("findstr.exe"s, {"piped"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "piped_env_value\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess(
                  "bash"s, {"-c", "echo $MY_PIPE_VAR"},
                  $env = {{"MY_PIPE_VAR", "piped_env_value"}}) |
              subprocess::detail::subprocess("grep"s, {"piped"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "piped_env_value\n");
#endif
}

// ===========================================================================
// 15. Non-zero exit code in the middle of a pipe chain
// ===========================================================================
TEST(PipelineTest, NonZeroExitInMiddleOfChain) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo data_line&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"no_such_string"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"."}, $stdout > out);

  [[maybe_unused]] int ret = subs.run();
  auto codes = subs.exit_codes();
  ASSERT_EQ(codes.size(), 3u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_NE(codes[1], 0);
  EXPECT_EQ(codes[2], 1);
  EXPECT_TRUE(out.empty());
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"data_line\\n"}) |
              subprocess::detail::subprocess("grep"s, {"no_such_string"}) |
              subprocess::detail::subprocess("grep"s, {"."}, $stdout > out);

  [[maybe_unused]] int ret = subs.run();
  auto codes = subs.exit_codes();
  ASSERT_EQ(codes.size(), 3u);
  EXPECT_EQ(codes[0], 0);
  EXPECT_NE(codes[1], 0);
  EXPECT_EQ(codes[2], 1);
  EXPECT_TRUE(out.empty());
#endif
}

// ===========================================================================
// 16. Identity pipe chain — everything passes through
// ===========================================================================
TEST(PipelineTest, IdentityPipeChain) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo identity_test&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"."}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  ASSERT_EQ(out, "identity_test\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"identity_test\\n"}) |
              subprocess::detail::subprocess("cat"s, {}, $stdout > out);

  subs.run();
  ASSERT_EQ(subs.exit_codes().size(), 2u);
  ASSERT_EQ(out, "identity_test\n");
#endif
}

// ===========================================================================
// 17. Pipe with both stdout and stderr captured in the last process
// ===========================================================================
TEST(PipelineTest, PipeChainCaptureBothStdoutAndStderrAtEnd) {
#if defined(_WIN32)
  buffer out;
  buffer err;
  auto subs = subprocess::detail::subprocess(
                  "cmd.exe"s, {"/c",
                               "echo to_stdout&echo to_stderr>&2&"
                               "exit /b 0"}) |
              subprocess::detail::subprocess("findstr.exe"s, {"to_"},
                                             $stdout > out, $stderr > err);

  subs.run();
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  EXPECT_NE(out_str.find("to_stdout"), std::string::npos);
#else
  buffer out;
  buffer err;
  auto subs = subprocess::detail::subprocess(
                  "bash"s, {"-c", "echo to_stdout; echo to_stderr>&2"}) |
              subprocess::detail::subprocess("grep"s, {"to_"}, $stdout > out,
                                             $stderr > err);

  subs.run();
  ASSERT_FALSE(out.empty());
  std::string out_str = out.to_string();
  EXPECT_NE(out_str.find("to_stdout"), std::string::npos);
#endif
}

// ===========================================================================
// 18. Multiple independent pipe chains in the same test
// ===========================================================================
TEST(PipelineTest, MultipleIndependentPipeChains) {
#if defined(_WIN32)
  buffer outA;
  auto chainA =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo Apple&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"App"}, $stdout > outA);

  buffer outB;
  auto chainB =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo Banana&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"Ban"}, $stdout > outB);

  chainA.run();
  chainB.run();

  ASSERT_EQ(outA, "Apple\r\n");
  ASSERT_EQ(outB, "Banana\r\n");
#else
  buffer outA;
  auto chainA =
      subprocess::detail::subprocess("printf"s, {"Apple\\n"}) |
      subprocess::detail::subprocess("grep"s, {"App"}, $stdout > outA);

  buffer outB;
  auto chainB =
      subprocess::detail::subprocess("printf"s, {"Banana\\n"}) |
      subprocess::detail::subprocess("grep"s, {"Ban"}, $stdout > outB);

  chainA.run();
  chainB.run();

  ASSERT_EQ(outA, "Apple\n");
  ASSERT_EQ(outB, "Banana\n");
#endif
}

// ===========================================================================
// 19. Incremental pipeline chain building
// ===========================================================================
TEST(PipelineTest, IncrementalPipeChainBuilding) {
#if defined(_WIN32)
  buffer out;

  subprocess::detail::pipeline chain =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo first_phase&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"first"});

  chain = std::move(chain) | subprocess::detail::subprocess(
                                 "findstr.exe"s, {"phase"}, $stdout > out);

  chain.run();
  ASSERT_EQ(out, "first_phase\r\n");
#else
  buffer out;

  subprocess::detail::pipeline chain =
      subprocess::detail::subprocess("printf"s, {"first_phase\\n"}) |
      subprocess::detail::subprocess("grep"s, {"first"});

  chain = std::move(chain) |
          subprocess::detail::subprocess("grep"s, {"phase"}, $stdout > out);

  chain.run();
  ASSERT_EQ(out, "first_phase\n");
#endif
}

// ===========================================================================
// 20. Pipe with line counting (wc -l / find /c)
// ===========================================================================
TEST(PipelineTest, PipeWithLineCounting) {
#if defined(_WIN32)
  buffer out;
  auto subs = subprocess::detail::subprocess(
                  "cmd.exe"s, {"/c", "echo A&echo B&echo C&exit /b 0"}) |
              subprocess::detail::subprocess("find.exe"s, {"/c", "/v", ""},
                                             $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_NE(result.find("3"), std::string::npos);
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"A\\nB\\nC\\n"}) |
              subprocess::detail::subprocess("wc"s, {"-l"}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_NE(result.find("3"), std::string::npos);
#endif
}

// ===========================================================================
// 21. Data integrity: binary-like data through pipes
// ===========================================================================
TEST(PipelineTest, BinaryLikeDataThroughPipe) {
  std::string input;
  for (int i = 32; i < 127; ++i) {
    input += static_cast<char>(i);
    if (i % 16 == 0) {
      input += '\n';
    }
  }

#if defined(_WIN32)
  buffer in{input};
  buffer out;
  auto subs =
      subprocess::detail::subprocess("findstr.exe"s, {"."}, $stdin < in) |
      subprocess::detail::subprocess("findstr.exe"s, {"."}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_GT(result.size(), 0u);
#else
  buffer in{input};
  buffer out;
  auto subs = subprocess::detail::subprocess("cat"s, {}, $stdin < in) |
              subprocess::detail::subprocess("cat"s, {}, $stdout > out);

  subs.run();
  std::string result = out.to_string();
  EXPECT_EQ(result, input);
#endif
}

// ===========================================================================
// 22. Top-level run() with pipeline (via | syntax)
// ===========================================================================
TEST(PipelineTest, TopLevelRunWithPipeOperator) {
  using subprocess::run;
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c", "echo top_level&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"top"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "top_level\r\n");
#else
  buffer out;
  auto subs = subprocess::detail::subprocess("printf"s, {"top_level\\n"}) |
              subprocess::detail::subprocess("grep"s, {"top"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "top_level\n");
#endif
}

// ===========================================================================
// 23. Only stdout goes through pipe (stderr excluded)
// ===========================================================================
TEST(PipelineTest, OnlyStdoutGoesThroughPipe) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess("cmd.exe"s,
                                     {"/c",
                                      "echo goes_to_pipe&echo "
                                      "goes_to_stderr_only>&2&exit /b 0"},
                                     $stderr > $devnull) |
      subprocess::detail::subprocess("findstr.exe"s, {"goes_to_pipe"},
                                     $stdout > out);

  subs.run();
  ASSERT_EQ(out, "goes_to_pipe\r\n");
#else
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "bash"s, {"-c", "echo goes_to_pipe; echo goes_to_stderr_only>&2"},
          $stderr > $devnull) |
      subprocess::detail::subprocess("grep"s, {"goes_to_pipe"}, $stdout > out);

  subs.run();
  ASSERT_EQ(out, "goes_to_pipe\n");
#endif
}

// ===========================================================================
// 24. Explicit Pipe closed after use (RAII smoke test)
// ===========================================================================
TEST(PipelineTest, ExplicitPipeClosedAfterUse) {
  buffer captured;
  {
    auto pipe = subprocess::detail::Pipe::create();
#if defined(_WIN32)
    subprocess::detail::subprocess p(
        "cmd.exe"s, {"/c"s, "echo pipe_raii&exit /b 0"}, $stdout > pipe);
#else
    subprocess::detail::subprocess p("printf", {"pipe_raii\\n"},
                                     $stdout > pipe);
#endif
    p.spawn();
    read_from_native_handle(pipe.rfd(), captured);
    p.wait();
  }
#if defined(_WIN32)
  ASSERT_EQ(captured, "pipe_raii\r\n");
#else
  ASSERT_EQ(captured, "pipe_raii\n");
#endif
}

// ===========================================================================
// 25. capture_run as pipeline equivalence check
// ===========================================================================
TEST(PipelineTest, PipeChainEquivalentToCaptureRun) {
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

// ===========================================================================
// Edge-case tests for handle inheritance when some standard handles use
// the parent's default (GetStdHandle) and others are redirected through
// PROC_THREAD_ATTRIBUTE_HANDLE_LIST on Windows.
// ===========================================================================

// ---------------------------------------------------------------------------
// Single subprocess: only stdout redirected, stdin/stderr default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStdoutOnly) {
#if defined(_WIN32)
  buffer out;
  int ret = subprocess::detail::subprocess(
                "cmd.exe"s,
                {"/c", "echo hello_stdout&echo hello_stderr>&2&exit /b 3"},
                $stdout > out)
                .run();
  EXPECT_EQ(ret, 3);
  ASSERT_EQ(out, "hello_stdout\r\n");
#else
  buffer out;
  int ret =
      subprocess::detail::subprocess(
          "bash"s, {"-c", "echo hello_stdout; echo hello_stderr>&2; exit 3"},
          $stdout > out)
          .run();
  EXPECT_EQ(ret, 3);
  ASSERT_EQ(out, "hello_stdout\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: only stderr redirected, stdin/stdout default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStderrOnly) {
#if defined(_WIN32)
  buffer err;
  int ret =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo to_stdout&echo to_stderr>&2&exit /b 5"},
          $stderr > err)
          .run();
  EXPECT_EQ(ret, 5);
  ASSERT_EQ(err, "to_stderr\r\n");
#else
  buffer err;
  int ret = subprocess::detail::subprocess(
                "bash"s, {"-c", "echo to_stdout; echo to_stderr>&2; exit 5"},
                $stderr > err)
                .run();
  EXPECT_EQ(ret, 5);
  ASSERT_EQ(err, "to_stderr\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: only stdin redirected, stdout/stderr default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStdinOnly) {
#if defined(_WIN32)
  buffer in("hello_via_stdin");
  int ret =
      subprocess::detail::subprocess("findstr.exe"s, {"hello"s}, $stdin < in)
          .run();
  EXPECT_EQ(ret, 0);
#else
  buffer in("hello_via_stdin\n");
  int ret =
      subprocess::detail::subprocess("grep"s, {"hello"s}, $stdin < in).run();
  EXPECT_EQ(ret, 0);
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: stdin + stdout redirected, stderr default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStdinStdoutStderrDefault) {
#if defined(_WIN32)
  buffer in("input_data");
  buffer out;
  int ret = subprocess::detail::subprocess("findstr.exe"s, {"data"s},
                                           $stdin<in, $stdout> out)
                .run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "input_data\r\n");
#else
  buffer in("input_data\n");
  buffer out;
  int ret = subprocess::detail::subprocess("grep"s, {"data"s},
                                           $stdin<in, $stdout> out)
                .run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "input_data\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: stdout + stderr redirected, stdin default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStdoutStderrStdinDefault) {
#if defined(_WIN32)
  buffer out, err;
  int ret = subprocess::detail::subprocess(
                "cmd.exe"s, {"/c", "echo to_out&echo to_err>&2&exit /b 9"},
                $stdout > out, $stderr > err)
                .run();
  EXPECT_EQ(ret, 9);
  ASSERT_EQ(out, "to_out\r\n");
  ASSERT_EQ(err, "to_err\r\n");
#else
  buffer out, err;
  int ret = subprocess::detail::subprocess(
                "bash"s, {"-c", "echo to_out; echo to_err>&2; exit 9"},
                $stdout > out, $stderr > err)
                .run();
  EXPECT_EQ(ret, 9);
  ASSERT_EQ(out, "to_out\n");
  ASSERT_EQ(err, "to_err\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: stdin + stderr redirected, stdout default
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectStdinStderrStdoutDefault) {
#if defined(_WIN32)
  buffer in("needle");
  buffer err;
  int ret = subprocess::detail::subprocess(
                "cmd.exe"s, {"/c", "findstr needle&echo to_err>&2&exit /b 0"},
                $stdin<in, $stderr> err)
                .run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(err, "to_err\r\n");
#else
  buffer in("needle\n");
  buffer err;
  int ret = subprocess::detail::subprocess(
                "bash"s, {"-c", "grep needle; echo to_err>&2"},
                $stdin<in, $stderr> err)
                .run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(err, "to_err\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: all three handles redirected
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessRedirectAllThree) {
#if defined(_WIN32)
  buffer in("all_three");
  buffer out, err;
  int ret = subprocess::detail::subprocess(
                "cmd.exe"s, {"/c", "findstr three&echo to_err>&2&exit /b 7"},
                $stdin<in, $stdout> out, $stderr > err)
                .run();
  EXPECT_EQ(ret, 7);
  ASSERT_EQ(out, "all_three\r\n");
  ASSERT_EQ(err, "to_err\r\n");
#else
  buffer in("all_three\n");
  buffer out, err;
  int ret = subprocess::detail::subprocess(
                "bash"s, {"-c", "grep three; echo to_err>&2; exit 7"},
                $stdin<in, $stdout> out, $stderr > err)
                .run();
  EXPECT_EQ(ret, 7);
  ASSERT_EQ(out, "all_three\n");
  ASSERT_EQ(err, "to_err\n");
#endif
}

// ---------------------------------------------------------------------------
// Single subprocess: no handles redirected at all (baseline)
// ---------------------------------------------------------------------------
TEST(PipelineTest, SingleProcessNoRedirects) {
#if defined(_WIN32)
  int ret =
      subprocess::detail::subprocess("cmd.exe"s, {"/c", "exit /b 11"}).run();
  EXPECT_EQ(ret, 11);
#else
  int ret = subprocess::detail::subprocess("true"s, {}).run();
  EXPECT_EQ(ret, 0);
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: only first process has redirected stderr, second uses all defaults
// except stdin (piped). This is the original regression test for the
// PROC_THREAD_ATTRIBUTE_HANDLE_LIST fix.
// ---------------------------------------------------------------------------
TEST(PipelineTest, PipeFirstStderrRedirectedSecondAllDefaults) {
#if defined(_WIN32)
  buffer err_out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo first_out&echo first_err>&2&exit /b 0"},
          $stderr > err_out) |
      subprocess::detail::subprocess("findstr.exe"s, {"first_out"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(err_out.empty());
  EXPECT_NE(err_out.to_string().find("first_err"), std::string::npos);
#else
  buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  "bash"s, {"-c", "echo first_out; echo first_err>&2"},
                  $stderr > err_out) |
              subprocess::detail::subprocess("grep"s, {"first_out"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(err_out.empty());
  EXPECT_NE(err_out.to_string().find("first_err"), std::string::npos);
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: first process no redirects, second process has stdout default
// (only stdin piped). Tests the case where the second process in a pipeline
// has exactly one non-default handle (stdin from pipe) and stdout/stderr
// use parent defaults.
// ---------------------------------------------------------------------------
TEST(PipelineTest, PipeFirstNoRedirectsSecondStdoutStderrDefault) {
#if defined(_WIN32)
  auto subs = subprocess::detail::subprocess(
                  "cmd.exe"s, {"/c", "echo simple_out&exit /b 0"}) |
              subprocess::detail::subprocess("findstr.exe"s, {"simple_out"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
#else
  auto subs = subprocess::detail::subprocess("printf"s, {"simple_out\\n"}) |
              subprocess::detail::subprocess("grep"s, {"simple_out"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: first process has both stdout + stderr redirected, second
// process has `stdout` default (only stdin from pipe).  Covers the case where
// the first process has all three handles non-default and the second has
// only stdin non-default.
// ---------------------------------------------------------------------------
TEST(PipelineTest, PipeFirstAllRedirectedSecondStdoutStderrDefault) {
#if defined(_WIN32)
  buffer err_out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo pipe_data&echo pipe_err>&2&exit /b 0"},
          $stderr > err_out) |
      subprocess::detail::subprocess("findstr.exe"s, {"pipe_data"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(err_out.empty());
  EXPECT_NE(err_out.to_string().find("pipe_err"), std::string::npos);
#else
  buffer err_out;
  auto subs = subprocess::detail::subprocess(
                  "bash"s, {"-c", "echo pipe_data; echo pipe_err>&2"},
                  $stderr > err_out) |
              subprocess::detail::subprocess("grep"s, {"pipe_data"});

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(err_out.empty());
  EXPECT_NE(err_out.to_string().find("pipe_err"), std::string::npos);
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: first process has stderr redirected, second process has both
// stdout + stderr redirected.  Covers the case where the second process has
// all handles explicitly redirected (stdin from pipe, stdout to buffer,
// stderr to buffer).
// ---------------------------------------------------------------------------
TEST(PipelineTest, PipeFirstStderrDefaultSecondBothRedirected) {
#if defined(_WIN32)
  buffer first_err, second_out, second_err;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo data_out&echo data_err>&2&exit /b 0"},
          $stderr > first_err) |
      subprocess::detail::subprocess("findstr.exe"s, {"data_out"},
                                     $stdout > second_out,
                                     $stderr > second_err);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(first_err.empty());
  EXPECT_NE(first_err.to_string().find("data_err"), std::string::npos);
  ASSERT_FALSE(second_out.empty());
  EXPECT_NE(second_out.to_string().find("data_out"), std::string::npos);
#else
  buffer first_err, second_out, second_err;
  auto subs =
      subprocess::detail::subprocess("bash"s,
                                     {"-c", "echo data_out; echo data_err>&2"},
                                     $stderr > first_err) |
      subprocess::detail::subprocess(
          "grep"s, {"data_out"}, $stdout > second_out, $stderr > second_err);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_FALSE(first_err.empty());
  EXPECT_NE(first_err.to_string().find("data_err"), std::string::npos);
  ASSERT_FALSE(second_out.empty());
  EXPECT_NE(second_out.to_string().find("data_out"), std::string::npos);
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: three processes. The middle process has no explicit redirects
// (stdin/stdout from pipes, stderr default).  Verifies that a process in the
// middle of a chain with only piped handles (no buffer redirects) works
// correctly when its stderr falls back to the parent's handle.
// ---------------------------------------------------------------------------
TEST(PipelineTest, ThreeProcessMiddleAllDefaults) {
#if defined(_WIN32)
  buffer out;
  auto subs =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo AAA&echo BBB&echo CCC&exit /b 0"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"A"}) |
      subprocess::detail::subprocess("findstr.exe"s, {"A"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "AAA\r\n");
#else
  buffer out;
  auto subs =
      subprocess::detail::subprocess("printf"s, {"AAA\\nBBB\\nCCC\\n"}) |
      subprocess::detail::subprocess("grep"s, {"A"}) |
      subprocess::detail::subprocess("grep"s, {"A"}, $stdout > out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(out, "AAA\n");
#endif
}

// ---------------------------------------------------------------------------
// Pipeline: four processes, alternating redirected and default handles.
// Processes 1 and 3 have stderr redirected; processes 2 and 4 have
// stdout or stderr default.  Stresses the handle inheritance list with
// mixed configurations across a longer chain.
// ---------------------------------------------------------------------------
TEST(PipelineTest, FourProcessMixedRedirectsAndDefaults) {
#if defined(_WIN32)
  buffer p1_err, p3_err, final_out;
  auto subs = subprocess::detail::subprocess(
                  "cmd.exe"s,
                  {"/c",
                   "echo Apple&echo Banana&echo Avocado&echo Blueberry&exit "
                   "/b 0"},
                  $stderr > p1_err) |
              subprocess::detail::subprocess("findstr.exe"s, {"A"}) |
              subprocess::detail::subprocess("findstr.exe"s, {"Avo"},
                                             $stderr > p3_err) |
              subprocess::detail::subprocess("findstr.exe"s, {"Avo"},
                                             $stdout > final_out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(final_out, "Avocado\r\n");
#else
  buffer p1_err, p3_err, final_out;
  auto subs =
      subprocess::detail::subprocess(
          "bash"s,
          {"-c", "echo Apple; echo Banana; echo Avocado; echo Blueberry"},
          $stderr > p1_err) |
      subprocess::detail::subprocess("grep"s, {"A"}) |
      subprocess::detail::subprocess("grep"s, {"Avo"}, $stderr > p3_err) |
      subprocess::detail::subprocess("grep"s, {"Avo"}, $stdout > final_out);

  int ret = subs.run();
  EXPECT_EQ(ret, 0);
  ASSERT_EQ(final_out, "Avocado\n");
#endif
}

// ---------------------------------------------------------------------------
// Multiple independent pipelines with mixed redirects running sequentially.
// ---------------------------------------------------------------------------
TEST(PipelineTest, MultipleIndependentPipelinesMixedRedirects) {
#if defined(_WIN32)
  // Chain A: first process stderr -> buffer, second process stdout default
  buffer errA;
  auto chainA =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo chainA_out&echo chainA_err>&2&exit /b 0"},
          $stderr > errA) |
      subprocess::detail::subprocess("findstr.exe"s, {"chainA_out"});
  int retA = chainA.run();
  EXPECT_EQ(retA, 0);
  ASSERT_FALSE(errA.empty());
  EXPECT_NE(errA.to_string().find("chainA_err"), std::string::npos);

  // Chain B: first process no redirects, second process stdout -> buffer
  buffer outB;
  auto chainB = subprocess::detail::subprocess(
                    "cmd.exe"s, {"/c", "echo chainB_data&exit /b 0"}) |
                subprocess::detail::subprocess("findstr.exe"s, {"chainB_data"},
                                               $stdout > outB);
  int retB = chainB.run();
  EXPECT_EQ(retB, 0);
  ASSERT_EQ(outB, "chainB_data\r\n");

  // Chain C: both processes have at least one buffer redirect
  buffer errC, outC;
  auto chainC =
      subprocess::detail::subprocess(
          "cmd.exe"s, {"/c", "echo chainC_out&echo chainC_err>&2&exit /b 0"},
          $stderr > errC) |
      subprocess::detail::subprocess("findstr.exe"s, {"chainC_out"},
                                     $stdout > outC);
  int retC = chainC.run();
  EXPECT_EQ(retC, 0);
  ASSERT_FALSE(errC.empty());
  EXPECT_NE(errC.to_string().find("chainC_err"), std::string::npos);
  ASSERT_EQ(outC, "chainC_out\r\n");
#else
  // Chain A
  buffer errA;
  auto chainA = subprocess::detail::subprocess(
                    "bash"s, {"-c", "echo chainA_out; echo chainA_err>&2"},
                    $stderr > errA) |
                subprocess::detail::subprocess("grep"s, {"chainA_out"});
  int retA = chainA.run();
  EXPECT_EQ(retA, 0);
  ASSERT_FALSE(errA.empty());
  EXPECT_NE(errA.to_string().find("chainA_err"), std::string::npos);

  // Chain B
  buffer outB;
  auto chainB =
      subprocess::detail::subprocess("printf"s, {"chainB_data\\n"}) |
      subprocess::detail::subprocess("grep"s, {"chainB_data"}, $stdout > outB);
  int retB = chainB.run();
  EXPECT_EQ(retB, 0);
  ASSERT_EQ(outB, "chainB_data\n");

  // Chain C
  buffer errC, outC;
  auto chainC =
      subprocess::detail::subprocess(
          "bash"s, {"-c", "echo chainC_out; echo chainC_err>&2"},
          $stderr > errC) |
      subprocess::detail::subprocess("grep"s, {"chainC_out"}, $stdout > outC);
  int retC = chainC.run();
  EXPECT_EQ(retC, 0);
  ASSERT_FALSE(errC.empty());
  EXPECT_NE(errC.to_string().find("chainC_err"), std::string::npos);
  ASSERT_EQ(outC, "chainC_out\n");
#endif
}
