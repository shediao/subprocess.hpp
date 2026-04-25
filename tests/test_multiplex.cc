#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "subprocess/subprocess.hpp"

// The multiplex_using_* functions are POSIX-only
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>

using subprocess::detail::INVALID_NATIVE_HANDLE_VALUE;
using subprocess::detail::NativeHandle;

namespace {

// ---------------------------------------------------------------------------
// PipePair: RAII helper managing a pair of pipe file descriptors.
//           Provides release_*() to transfer ownership of a single end.
// ---------------------------------------------------------------------------
struct PipePair {
  NativeHandle read_fd = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle write_fd = INVALID_NATIVE_HANDLE_VALUE;

  static PipePair create() {
    int fds[2];
    if (::pipe(fds) != 0) {
      throw std::runtime_error("pipe() failed");
    }
    return {fds[0], fds[1]};
  }

  // Release ownership of the read end and return its fd.
  NativeHandle release_read() {
    auto fd = read_fd;
    read_fd = INVALID_NATIVE_HANDLE_VALUE;
    return fd;
  }
  // Release ownership of the write end and return its fd.
  NativeHandle release_write() {
    auto fd = write_fd;
    write_fd = INVALID_NATIVE_HANDLE_VALUE;
    return fd;
  }

  void close_read() {
    if (read_fd != INVALID_NATIVE_HANDLE_VALUE) {
      ::close(read_fd);
      read_fd = INVALID_NATIVE_HANDLE_VALUE;
    }
  }
  void close_write() {
    if (write_fd != INVALID_NATIVE_HANDLE_VALUE) {
      ::close(write_fd);
      write_fd = INVALID_NATIVE_HANDLE_VALUE;
    }
  }

  ~PipePair() {
    close_read();
    close_write();
  }

  PipePair(int read_fd, int write_fd) noexcept {
    this->read_fd = read_fd;
    this->write_fd = write_fd;
  }
  PipePair(PipePair&& other) noexcept
      : read_fd(other.read_fd), write_fd(other.write_fd) {
    other.read_fd = INVALID_NATIVE_HANDLE_VALUE;
    other.write_fd = INVALID_NATIVE_HANDLE_VALUE;
  }
  PipePair& operator=(PipePair&& other) noexcept {
    if (this != &other) {
      close_read();
      close_write();
      read_fd = other.read_fd;
      write_fd = other.write_fd;
      other.read_fd = INVALID_NATIVE_HANDLE_VALUE;
      other.write_fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    return *this;
  }
  PipePair(const PipePair&) = delete;
  PipePair& operator=(const PipePair&) = delete;
};

}  // namespace

// ===========================================================================
// MultiplexTestBase: fixture for all multiplex_using_* tests
// ===========================================================================
class MultiplexTestBase : public ::testing::Test {
 protected:
  // Start a drain-thread that takes *ownership* of the read-end fd.
  // The thread reads until EOF, appends data to |drained|, then closes the fd.
  static void start_stdin_drain_thread(NativeHandle read_fd,
                                       std::vector<char>& drained) {
    std::thread([read_fd, &drained]() {
      char buf[256];
      ssize_t n;
      while ((n = ::read(read_fd, buf, sizeof(buf))) > 0) {
        drained.insert(drained.end(), buf, buf + n);
      }
      ::close(read_fd);
    }).detach();
  }

  // Start a feeder-thread that takes *ownership* of the write-end fd.
  // The thread writes |data|, then closes the fd to signal EOF.
  static void start_stdout_stderr_feeder_thread(NativeHandle write_fd,
                                                const std::vector<char>& data) {
    std::thread([write_fd, data]() {
      size_t written = 0;
      while (written < data.size()) {
        ssize_t n =
            ::write(write_fd, data.data() + written, data.size() - written);
        if (n > 0) {
          written += static_cast<size_t>(n);
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          std::this_thread::yield();
          continue;
        } else {
          break;
        }
      }
      ::close(write_fd);
    }).detach();
  }
};

// ===========================================================================
// multiplex_using_poll  tests
// ===========================================================================
class MultiplexPollTest : public MultiplexTestBase {};

TEST_F(MultiplexPollTest, AllHandlesInvalid) {
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_TRUE(in_buf.empty());
  EXPECT_TRUE(out_buf.empty());
  EXPECT_TRUE(err_buf.empty());
}

TEST_F(MultiplexPollTest, OnlyStdinActiveWritesData) {
  auto pp = PipePair::create();
  NativeHandle in = pp.release_write();  // ownership transferred to |in|
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str = "hello from stdin poll";
  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp.release_read(), drained);

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  // EXPECT_TRUE(in_buf.empty());

  // Give the drain thread a moment to finish.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
}

TEST_F(MultiplexPollTest, OnlyStdoutActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp.release_read();  // ownership transferred to |out|
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "hello from stdout poll";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), out_data);

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexPollTest, OnlyStderrActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = pp.release_read();  // ownership transferred to |err|

  std::string err_str = "hello from stderr poll";
  std::vector<char> err_data(err_str.begin(), err_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), err_data);

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexPollTest, AllThreeActive) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();
  auto pp_err = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = pp_err.release_read();

  std::string input_str = "stdin-data";
  std::string output_str = "stdout-data-poll";
  std::string err_str = "stderr-data-poll";

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});
  start_stdout_stderr_feeder_thread(pp_err.release_write(),
                                    {err_str.begin(), err_str.end()});

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);

  // Give background threads a brief moment to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexPollTest, EmptyStdinBuffer) {
  auto pp_out = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "only-stdout";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;  // empty
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp_out.release_write(), out_data);

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
  EXPECT_TRUE(in_buf.empty());
}

TEST_F(MultiplexPollTest, LargerDataTransfer) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  // Data larger than typical pipe buffer (64 KiB on Linux)
  std::string input_str(128 * 1024, 'A');
  for (size_t i = 0; i < input_str.size(); ++i) {
    input_str[i] = static_cast<char>('A' + (i % 26));
  }
  std::string output_str(64 * 1024, 'B');
  for (size_t i = 0; i < output_str.size(); ++i) {
    output_str[i] = static_cast<char>('a' + (i % 26));
  }

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});

  subprocess::detail::multiplex_using_poll(in, in_buf, out, out_buf, err,
                                           err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(out_buf.size(), output_str.size());
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

// ===========================================================================
// multiplex_using_select  tests
// ===========================================================================
class MultiplexSelectTest : public MultiplexTestBase {};

TEST_F(MultiplexSelectTest, AllHandlesInvalid) {
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
}

TEST_F(MultiplexSelectTest, OnlyStdinActiveWritesData) {
  auto pp = PipePair::create();
  NativeHandle in = pp.release_write();
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str = "hello from stdin select";
  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp.release_read(), drained);

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
}

TEST_F(MultiplexSelectTest, OnlyStdoutActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "hello from stdout select";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), out_data);

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexSelectTest, OnlyStderrActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = pp.release_read();

  std::string err_str = "hello from stderr select";
  std::vector<char> err_data(err_str.begin(), err_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), err_data);

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexSelectTest, AllThreeActive) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();
  auto pp_err = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = pp_err.release_read();

  std::string input_str = "stdin-select";
  std::string output_str = "stdout-select";
  std::string err_str = "stderr-select";

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});
  start_stdout_stderr_feeder_thread(pp_err.release_write(),
                                    {err_str.begin(), err_str.end()});

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexSelectTest, EmptyStdinBuffer) {
  auto pp_out = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "only-stdout-select";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp_out.release_write(), out_data);

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexSelectTest, LargerDataTransfer) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str(96 * 1024, 'X');
  for (size_t i = 0; i < input_str.size(); ++i) {
    input_str[i] = static_cast<char>('A' + (i % 26));
  }
  std::string output_str(48 * 1024, 'Y');
  for (size_t i = 0; i < output_str.size(); ++i) {
    output_str[i] = static_cast<char>('a' + (i % 26));
  }

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});

  subprocess::detail::multiplex_using_select(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(out_buf.size(), output_str.size());
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

// ===========================================================================
// multiplex_using_epoll  tests  (Linux only)
// ===========================================================================
#if defined(__linux__)
class MultiplexEpollTest : public MultiplexTestBase {};

TEST_F(MultiplexEpollTest, AllHandlesInvalid) {
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_TRUE(out_buf.empty());
  EXPECT_TRUE(err_buf.empty());
}

TEST_F(MultiplexEpollTest, OnlyStdinActiveWritesData) {
  auto pp = PipePair::create();
  NativeHandle in = pp.release_write();
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str = "hello from stdin epoll";
  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp.release_read(), drained);

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
}

TEST_F(MultiplexEpollTest, OnlyStdoutActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "hello from stdout epoll";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), out_data);

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexEpollTest, OnlyStderrActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = pp.release_read();

  std::string err_str = "hello from stderr epoll";
  std::vector<char> err_data(err_str.begin(), err_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), err_data);

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexEpollTest, AllThreeActive) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();
  auto pp_err = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = pp_err.release_read();

  std::string input_str = "stdin-epoll";
  std::string output_str = "stdout-epoll";
  std::string err_str = "stderr-epoll";

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});
  start_stdout_stderr_feeder_thread(pp_err.release_write(),
                                    {err_str.begin(), err_str.end()});

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexEpollTest, EmptyStdinBuffer) {
  auto pp_out = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "only-stdout-epoll";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp_out.release_write(), out_data);

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexEpollTest, LargerDataTransfer) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str(128 * 1024, 'Z');
  for (size_t i = 0; i < input_str.size(); ++i) {
    input_str[i] = static_cast<char>('A' + (i % 26));
  }
  std::string output_str(64 * 1024, 'Q');
  for (size_t i = 0; i < output_str.size(); ++i) {
    output_str[i] = static_cast<char>('a' + (i % 26));
  }

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});

  subprocess::detail::multiplex_using_epoll(in, in_buf, out, out_buf, err,
                                            err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(out_buf.size(), output_str.size());
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}
#endif  // defined(__linux__)

// ===========================================================================
// multiplex_using_kqueue  tests  (macOS / BSD only)
// ===========================================================================
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
class MultiplexKqueueTest : public MultiplexTestBase {};

TEST_F(MultiplexKqueueTest, AllHandlesInvalid) {
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_TRUE(out_buf.empty());
  EXPECT_TRUE(err_buf.empty());
}

TEST_F(MultiplexKqueueTest, OnlyStdinActiveWritesData) {
  auto pp = PipePair::create();
  NativeHandle in = pp.release_write();
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str = "hello from stdin kqueue";
  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp.release_read(), drained);

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
}

TEST_F(MultiplexKqueueTest, OnlyStdoutActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "hello from stdout kqueue";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), out_data);

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexKqueueTest, OnlyStderrActiveReadsData) {
  auto pp = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle err = pp.release_read();

  std::string err_str = "hello from stderr kqueue";
  std::vector<char> err_data(err_str.begin(), err_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp.release_write(), err_data);

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexKqueueTest, AllThreeActive) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();
  auto pp_err = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = pp_err.release_read();

  std::string input_str = "stdin-kqueue";
  std::string output_str = "stdout-kqueue";
  std::string err_str = "stderr-kqueue";

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});
  start_stdout_stderr_feeder_thread(pp_err.release_write(),
                                    {err_str.begin(), err_str.end()});

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(err, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
  EXPECT_EQ(std::string(err_buf.begin(), err_buf.end()), err_str);
}

TEST_F(MultiplexKqueueTest, EmptyStdinBuffer) {
  auto pp_out = PipePair::create();
  NativeHandle in = INVALID_NATIVE_HANDLE_VALUE;
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string output_str = "only-stdout-kqueue";
  std::vector<char> out_data(output_str.begin(), output_str.end());
  std::vector<char> in_buf;
  std::vector<char> out_buf;
  std::vector<char> err_buf;

  start_stdout_stderr_feeder_thread(pp_out.release_write(), out_data);

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}

TEST_F(MultiplexKqueueTest, LargerDataTransfer) {
  auto pp_in = PipePair::create();
  auto pp_out = PipePair::create();

  NativeHandle in = pp_in.release_write();
  NativeHandle out = pp_out.release_read();
  NativeHandle err = INVALID_NATIVE_HANDLE_VALUE;

  std::string input_str(128 * 1024, 'K');
  for (size_t i = 0; i < input_str.size(); ++i) {
    input_str[i] = static_cast<char>('A' + (i % 26));
  }
  std::string output_str(64 * 1024, 'J');
  for (size_t i = 0; i < output_str.size(); ++i) {
    output_str[i] = static_cast<char>('a' + (i % 26));
  }

  std::vector<char> in_buf(input_str.begin(), input_str.end());
  std::vector<char> out_buf;
  std::vector<char> err_buf;
  std::vector<char> drained;

  start_stdin_drain_thread(pp_in.release_read(), drained);
  start_stdout_stderr_feeder_thread(pp_out.release_write(),
                                    {output_str.begin(), output_str.end()});

  subprocess::detail::multiplex_using_kqueue(in, in_buf, out, out_buf, err,
                                             err_buf);

  EXPECT_EQ(in, INVALID_NATIVE_HANDLE_VALUE);
  EXPECT_EQ(out, INVALID_NATIVE_HANDLE_VALUE);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  EXPECT_EQ(drained.size(), input_str.size());
  EXPECT_EQ(std::string(drained.begin(), drained.end()), input_str);
  EXPECT_EQ(out_buf.size(), output_str.size());
  EXPECT_EQ(std::string(out_buf.begin(), out_buf.end()), output_str);
}
#endif  // defined(__APPLE__) || defined(__FreeBSD__) || ...

#endif  // !defined(_WIN32)
