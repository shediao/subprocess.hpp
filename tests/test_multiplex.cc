/**
 * test_multiplex.cc — Tests for detail::read_write_to_buffer_use_*()
 *
 * Covers the POSIX I/O multiplexing backends:
 *   - read_write_to_buffer_use_poll    (all POSIX)
 *
 * Each backend is tested with:
 *   - All handles nullopt (no-op)
 *   - Only stdin active  (write buffer → pipe)
 *   - Only stdout active (read pipe → buffer)
 *   - Only stderr active (read pipe → buffer)
 *   - All three active simultaneously
 *   - Empty stdin buffer + stdout
 *   - Larger data transfer (stdin + stdout)
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "subprocess/subprocess.hpp"

// The multiplexing backends are POSIX-only
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>

using subprocess::buffer;
using subprocess::detail::Buffer;
using subprocess::detail::INVALID_NATIVE_HANDLE_VALUE;
using subprocess::detail::StderrRedirector;
using subprocess::detail::StdinRedirector;
using subprocess::detail::StdoutRedirector;

static void read_write_to_buffer_use_poll(StdinRedirector& in,
                                          StdoutRedirector& out,
                                          StderrRedirector& err) {
  subprocess::detail::read_write_to_buffer_use_poll(
      in, out, err, INVALID_NATIVE_HANDLE_VALUE);
}

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Read everything from a fd (blocking) and append to |drained|.
// Used to verify data that was written *through* the pipe by the multiplexer
// (stdin side).
void drain_fd(int fd, buffer& drained) {
  char buf[256];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
    drained.append(buf, static_cast<size_t>(n));
  }
  ::close(fd);
}
void drain_fd(subprocess::detail::unique_fd const& fd, buffer& drained) {
  drain_fd(fd.get(), drained);
}

// Write |data| to a fd (blocking), then close it to signal EOF.
// Used to feed data *into* the pipe for the multiplexer to read
// (stdout / stderr side).
void feed_and_close(int fd, const buffer& data) {
  size_t written = 0;
  while (written < data.size()) {
    ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n > 0) {
      written += static_cast<size_t>(n);
    } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      std::this_thread::yield();
      continue;
    } else {
      break;
    }
  }
  ::close(fd);
}
void feed_and_close(subprocess::detail::unique_fd const& fd,
                    const buffer& data) {
  feed_and_close(fd.get(), data);
}

// Make a buffer pre-filled with |s|.
buffer make_buf(const std::string& s) {
  return buffer(s);  // string → string_view implicit conversion
}

// Generate a deterministic pattern of length |n| using alternating letters.
std::string make_pattern(size_t n, char base) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; ++i) {
    s[i] = static_cast<char>(base + static_cast<char>(i % 26));
  }
  return s;
}

}  // namespace

// ===========================================================================
// MultiplexTestBase — shared fixture
// ===========================================================================
class MultiplexTestBase : public ::testing::Test {
 protected:
  // Short sleep to give detached / background threads a moment to finish
  // after the multiplexer returns.
  static void yield_for_threads() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
};

// ===========================================================================
// read_write_to_buffer_use_poll  tests  (all POSIX)
// ===========================================================================
class MultiplexPollTest : public MultiplexTestBase {};

TEST_F(MultiplexPollTest, AllHandlesNullopt) {
  // Should return immediately without side-effects.
  StdinRedirector in;
  StdoutRedirector out;
  StderrRedirector err;
  read_write_to_buffer_use_poll(in, out, err);
  SUCCEED();
}

TEST_F(MultiplexPollTest, OnlyStdinActiveWritesData) {
  auto input_str = std::string("hello from stdin poll");
  buffer in_data = make_buf(input_str);
  StdinRedirector in_redir(in_data);
  StdoutRedirector out_redir;
  StderrRedirector err_redir;
  auto& in_buf = in_redir.get<Buffer>();

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  drain_thread.join();
  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(drained, input_str);
}

TEST_F(MultiplexPollTest, OnlyStdoutActiveReadsData) {
  auto output_str = std::string("hello from stdout poll");
  buffer out_data = make_buf(output_str);
  buffer out_buf_data;  // empty buffer, fills via redirector
  StdinRedirector in_redir;
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir;
  auto& out_buf = out_redir.get<Buffer>();

  std::thread feeder([&]() { feed_and_close(out_buf.wfd(), out_data); });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  feeder.join();
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf_data, output_str);
}

TEST_F(MultiplexPollTest, OnlyStderrActiveReadsData) {
  auto err_str = std::string("hello from stderr poll");
  buffer err_data = make_buf(err_str);
  buffer err_buf_data;  // empty buffer, fills via redirector
  StdinRedirector in_redir;
  StdoutRedirector out_redir;
  StderrRedirector err_redir(err_buf_data);
  auto& err_buf = err_redir.get<Buffer>();

  std::thread feeder([&]() { feed_and_close(err_buf.wfd(), err_data); });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  feeder.join();
  EXPECT_EQ(err_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err_buf_data, err_str);
}

TEST_F(MultiplexPollTest, AllThreeActive) {
  auto input_str = std::string("stdin-data-poll");
  auto output_str = std::string("stdout-data-poll");
  auto err_str = std::string("stderr-data-poll");

  buffer in_data = make_buf(input_str);
  buffer out_buf_data;
  buffer err_buf_data;
  StdinRedirector in_redir(in_data);
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir(err_buf_data);
  auto& in_buf = in_redir.get<Buffer>();
  auto& out_buf = out_redir.get<Buffer>();
  auto& err_buf = err_redir.get<Buffer>();

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });
  std::thread out_feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });
  std::thread err_feeder(
      [&]() { feed_and_close(err_buf.wfd(), make_buf(err_str)); });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  drain_thread.join();
  out_feeder.join();
  err_feeder.join();

  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);

  yield_for_threads();

  EXPECT_EQ(drained, input_str);
  EXPECT_EQ(out_buf_data, output_str);
  EXPECT_EQ(err_buf_data, err_str);
}

TEST_F(MultiplexPollTest, EmptyStdinBuffer) {
  auto output_str = std::string("only-stdout-poll");
  buffer in_buf_data;  // empty buffer for stdin
  buffer out_buf_data;
  StdinRedirector in_redir(in_buf_data);
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir;
  auto& in_buf = in_redir.get<Buffer>();
  auto& out_buf = out_redir.get<Buffer>();

  std::thread feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });

  // Pass an empty stdin Buffer (no data to write → immediately closes write
  // end).
  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  feeder.join();
  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_TRUE(in_buf_data.empty());
  EXPECT_EQ(out_buf_data, output_str);
}

TEST_F(MultiplexPollTest, LargerDataTransfer) {
  auto input_str = make_pattern(128 * 1024, 'A');  // 128 KiB
  auto output_str = make_pattern(64 * 1024, 'a');  //  64 KiB

  buffer in_data = make_buf(input_str);
  buffer out_buf_data;
  StdinRedirector in_redir(in_data);
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir;
  auto& in_buf = in_redir.get<Buffer>();
  auto& out_buf = out_redir.get<Buffer>();

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });
  std::thread feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  drain_thread.join();
  feeder.join();

  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);

  yield_for_threads();

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(drained, input_str);
  EXPECT_EQ(out_buf_data.size(), output_str.size());
  EXPECT_EQ(out_buf_data, output_str);
}

// ===========================================================================
// Full lifecycle: close_child_end + pump (simulates pump_pipe_data)
//
// These tests verify that after the complete I/O pump cycle (simulating
// close_child_end() followed by read_write_to_buffer_use_poll()), ALL
// pipe handles for ALL three buffers are properly closed.  The Redirector
// destructor previously printed cerr messages when handles were left open;
// these tests replace that runtime check with explicit assertions.
// ===========================================================================

TEST_F(MultiplexPollTest, FullLifecycleStdoutBothEndsClosed) {
  // stdout: child writes to wfd, parent reads from rfd.
  // Simulates close_child_end (close child write end) + pump (close parent
  // read end).
  auto output_str = std::string("lifecycle_stdout_poll");
  buffer out_buf_data;
  StdinRedirector in_redir;
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir;
  auto& out_buf = out_redir.get<Buffer>();

  // Write data into the pipe first, then close the write end (child side).
  auto out_data = make_buf(output_str);
  feed_and_close(out_buf.wfd(), out_data);
  out_buf.close_write();  // sync unique_fd state

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  // Both ends must be closed after the full cycle.
  ASSERT_FALSE(out_buf.rfd()) << "stdout parent read fd should be closed";
  ASSERT_FALSE(out_buf.wfd()) << "stdout child write fd should be closed";
  ASSERT_EQ(out_buf_data, output_str);
}

TEST_F(MultiplexPollTest, FullLifecycleStderrBothEndsClosed) {
  // stderr: child writes to wfd, parent reads from rfd.
  auto err_str = std::string("lifecycle_stderr_poll");
  buffer err_buf_data;
  StdinRedirector in_redir;
  StdoutRedirector out_redir;
  StderrRedirector err_redir(err_buf_data);
  auto& err_buf = err_redir.get<Buffer>();

  auto err_data = make_buf(err_str);
  feed_and_close(err_buf.wfd(), err_data);
  err_buf.close_write();  // sync unique_fd state

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  ASSERT_FALSE(err_buf.rfd()) << "stderr parent read fd should be closed";
  ASSERT_FALSE(err_buf.wfd()) << "stderr child write fd should be closed";
  ASSERT_EQ(err_buf_data, err_str);
}

TEST_F(MultiplexPollTest, FullLifecycleStdinBothEndsClosed) {
  // stdin: parent writes to wfd, child reads from rfd.
  auto input_str = std::string("lifecycle_stdin_poll");
  buffer in_data = make_buf(input_str);
  StdinRedirector in_redir(in_data);
  StdoutRedirector out_redir;
  StderrRedirector err_redir;
  auto& in_buf = in_redir.get<Buffer>();
  // NOTE: do NOT close read end before the drain thread starts —
  // in the real subprocess the child holds a dup'd copy of the read end
  // and close_child_end() only closes the parent's copy.

  buffer drained;
  std::thread drain_thread([&]() {
    drain_fd(in_buf.rfd(), drained);
    in_buf.close_read();  // child closes its end after reading
  });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);
  drain_thread.join();

  ASSERT_FALSE(in_buf.wfd()) << "stdin parent write fd should be closed";
  ASSERT_FALSE(in_buf.rfd()) << "stdin child read fd should be closed";
  ASSERT_EQ(drained, input_str);
}

TEST_F(MultiplexPollTest, FullLifecycleAllThreeAllHandlesClosed) {
  // stdin + stdout + stderr, all with close_child_end simulation.
  auto input_str = std::string("in_lifecycle_poll");
  auto output_str = std::string("out_lifecycle_poll");
  auto err_str = std::string("err_lifecycle_poll");

  buffer in_data = make_buf(input_str);
  buffer out_buf_data;
  buffer err_buf_data;
  StdinRedirector in_redir(in_data);
  StdoutRedirector out_redir(out_buf_data);
  StderrRedirector err_redir(err_buf_data);
  auto& in_buf = in_redir.get<Buffer>();
  auto& out_buf = out_redir.get<Buffer>();
  auto& err_buf = err_redir.get<Buffer>();

  // Feed stdout and stderr data, then close the write ends (child side).
  auto out_data = make_buf(output_str);
  feed_and_close(out_buf.wfd(), out_data);
  out_buf.close_write();  // sync unique_fd

  auto err_feeder_data = make_buf(err_str);
  feed_and_close(err_buf.wfd(), err_feeder_data);
  err_buf.close_write();  // sync unique_fd

  // Drain thread for stdin (simulates child reading).
  buffer drained;
  std::thread drain_thread([&]() {
    drain_fd(in_buf.rfd(), drained);
    in_buf.close_read();
  });

  read_write_to_buffer_use_poll(in_redir, out_redir, err_redir);

  drain_thread.join();

  // All six pipe handles must be closed.
  ASSERT_FALSE(in_buf.wfd()) << "stdin parent write fd";
  ASSERT_FALSE(in_buf.rfd()) << "stdin child read fd";
  ASSERT_FALSE(out_buf.rfd()) << "stdout parent read fd";
  ASSERT_FALSE(out_buf.wfd()) << "stdout child write fd";
  ASSERT_FALSE(err_buf.rfd()) << "stderr parent read fd";
  ASSERT_FALSE(err_buf.wfd()) << "stderr child write fd";

  yield_for_threads();

  ASSERT_EQ(drained, input_str);
  ASSERT_EQ(out_buf_data, output_str);
  ASSERT_EQ(err_buf_data, err_str);
}

#endif  // !defined(_WIN32)
