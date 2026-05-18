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
  subprocess::detail::read_write_to_buffer_use_poll(std::nullopt, std::nullopt,
                                                    std::nullopt);
  SUCCEED();
}

TEST_F(MultiplexPollTest, OnlyStdinActiveWritesData) {
  auto input_str = std::string("hello from stdin poll");
  buffer in_data = make_buf(input_str);
  Buffer in_buf(in_data);  // wraps in_data, owns the pipe

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });

  subprocess::detail::read_write_to_buffer_use_poll(std::ref(in_buf),
                                                    std::nullopt, std::nullopt);

  drain_thread.join();
  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(drained, input_str);
}

TEST_F(MultiplexPollTest, OnlyStdoutActiveReadsData) {
  auto output_str = std::string("hello from stdout poll");
  buffer out_data = make_buf(output_str);
  Buffer out_buf;  // empty internal buffer

  std::thread feeder([&]() { feed_and_close(out_buf.wfd(), out_data); });

  subprocess::detail::read_write_to_buffer_use_poll(
      std::nullopt, std::ref(out_buf), std::nullopt);

  feeder.join();
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.buf(), output_str);
}

TEST_F(MultiplexPollTest, OnlyStderrActiveReadsData) {
  auto err_str = std::string("hello from stderr poll");
  buffer err_data = make_buf(err_str);
  Buffer err_buf;

  std::thread feeder([&]() { feed_and_close(err_buf.wfd(), err_data); });

  subprocess::detail::read_write_to_buffer_use_poll(std::nullopt, std::nullopt,
                                                    std::ref(err_buf));

  feeder.join();
  EXPECT_EQ(err_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err_buf.buf(), err_str);
}

TEST_F(MultiplexPollTest, AllThreeActive) {
  auto input_str = std::string("stdin-data-poll");
  auto output_str = std::string("stdout-data-poll");
  auto err_str = std::string("stderr-data-poll");

  buffer in_data = make_buf(input_str);
  Buffer in_buf(in_data);
  Buffer out_buf;
  Buffer err_buf;

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });
  std::thread out_feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });
  std::thread err_feeder(
      [&]() { feed_and_close(err_buf.wfd(), make_buf(err_str)); });

  subprocess::detail::read_write_to_buffer_use_poll(
      std::ref(in_buf), std::ref(out_buf), std::ref(err_buf));

  drain_thread.join();
  out_feeder.join();
  err_feeder.join();

  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);

  yield_for_threads();

  EXPECT_EQ(drained, input_str);
  EXPECT_EQ(out_buf.buf(), output_str);
  EXPECT_EQ(err_buf.buf(), err_str);
}

TEST_F(MultiplexPollTest, EmptyStdinBuffer) {
  auto output_str = std::string("only-stdout-poll");
  Buffer out_buf;

  std::thread feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });

  // Pass an empty stdin Buffer (no data to write → immediately closes write
  // end).
  Buffer in_buf;  // default-constructed: empty buffer
  subprocess::detail::read_write_to_buffer_use_poll(
      std::ref(in_buf), std::ref(out_buf), std::nullopt);

  feeder.join();
  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_TRUE(in_buf.buf().empty());
  EXPECT_EQ(out_buf.buf(), output_str);
}

TEST_F(MultiplexPollTest, LargerDataTransfer) {
  auto input_str = make_pattern(128 * 1024, 'A');  // 128 KiB
  auto output_str = make_pattern(64 * 1024, 'a');  //  64 KiB

  buffer in_data = make_buf(input_str);
  Buffer in_buf(in_data);
  Buffer out_buf;

  buffer drained;
  std::thread drain_thread([&]() { drain_fd(in_buf.rfd(), drained); });
  std::thread feeder(
      [&]() { feed_and_close(out_buf.wfd(), make_buf(output_str)); });

  subprocess::detail::read_write_to_buffer_use_poll(
      std::ref(in_buf), std::ref(out_buf), std::nullopt);

  drain_thread.join();
  feeder.join();

  EXPECT_EQ(in_buf.wfd(), INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out_buf.rfd(), INVALID_NATIVE_HANDLE_VALUE);

  yield_for_threads();

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(drained, input_str);
  EXPECT_EQ(out_buf.buf().size(), output_str.size());
  EXPECT_EQ(out_buf.buf(), output_str);
}

#endif  // !defined(_WIN32)
