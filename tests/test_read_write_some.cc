/**
 * test_read_write_some.cc — Unit tests for read_some / write_some APIs
 *
 * Covers:
 *   - detail::read_some  / detail::write_some  (low-level free functions)
 *   - detail::read_exact / detail::write_all
 *   - Pipe::read_some    / Pipe::write_some
 *   - Pipe::read_exact   / Pipe::write_all
 *   - File::read_some    / File::write_some
 *   - File::read_exact   / File::write_all
 *   - FileHandler::read_some  / FileHandler::write_some
 *   - FileHandler::read_exact / FileHandler::write_all
 *   - Buffer::read_some  / Buffer::write_some
 *
 * Edge cases:
 *   - size == 0              → returns 0
 *   - invalid / closed handle → returns -1
 *   - EOF / broken pipe       → returns 0
 *   - partial read / write
 *   - empty dynamic_buffer write_some → returns 0 (EOF sentinel)
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "./temp_file.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;

// Shorter aliases for the types under test.
using subprocess::detail::Buffer;
using subprocess::detail::dynamic_buffer;
using subprocess::detail::File;
using subprocess::detail::FileHandler;
using subprocess::detail::Pipe;
using subprocess::detail::StderrRedirector;
using subprocess::detail::StdinRedirector;
using subprocess::detail::StdoutRedirector;
using subprocess::detail::unique_fd;
#if defined(_WIN32)
using subprocess::detail::ssize_t;
#endif

// ===========================================================================
// detail::read_some / detail::write_some — low-level free functions
// ===========================================================================

TEST(ReadWriteSomeTest, DetailReadSomeSizeZero) {
  // read_some with size == 0 must return 0 immediately.
  char buf[16];
  auto pipe = Pipe::create();
  auto n = subprocess::detail::read_some(pipe.rfd(), buf, 0);
  ASSERT_EQ(n, 0);
}

TEST(ReadWriteSomeTest, DetailWriteSomeSizeZero) {
  // write_some with size == 0 should return 0.
  const char data[] = "hello";
  auto pipe = Pipe::create();
  auto n = subprocess::detail::write_some(pipe.wfd(), data, 0);
  ASSERT_EQ(n, 0);
}

TEST(ReadWriteSomeTest, DetailReadSomeInvalidHandle) {
  char buf[16];
  auto n = subprocess::detail::read_some(unique_fd{}, buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, DetailWriteSomeInvalidHandle) {
  const char data[] = "hello";
  auto n = subprocess::detail::write_some(unique_fd{}, data, sizeof(data));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, DetailReadWriteSomeBasic) {
  // End-to-end: write_some into a pipe, read_some from it.
  auto pipe = Pipe::create();
  const char input[] = "hello_read_write_some";
  const auto input_len = strlen(input);

  auto w = subprocess::detail::write_some(pipe.wfd(), input, input_len);
  ASSERT_GT(w, 0);

  char output[64] = {};
  auto r = subprocess::detail::read_some(pipe.rfd(), output, sizeof(output));
  ASSERT_GT(r, 0);
  ASSERT_EQ(r, w);
  ASSERT_EQ(std::string(output, static_cast<size_t>(r)), std::string(input));
}

TEST(ReadWriteSomeTest, DetailReadExactWriteAll) {
  auto pipe = Pipe::create();
  const char input[] = "exact_all_test_data";
  const auto input_len = strlen(input);

  bool ok = subprocess::detail::write_all(pipe.wfd(), input, input_len);
  ASSERT_TRUE(ok);

  char output[64] = {};
  ok = subprocess::detail::read_exact(pipe.rfd(), output, input_len);
  ASSERT_TRUE(ok);
  ASSERT_EQ(std::string(output, input_len), std::string(input));
}

TEST(ReadWriteSomeTest, DetailReadExactTooShort) {
  // read_exact should fail when not enough data is available and pipe is
  // closed.
  auto pipe = Pipe::create();
  const char input[] = "short";
  const auto input_len = strlen(input);

  ASSERT_TRUE(subprocess::detail::write_all(pipe.wfd(), input, input_len));
  pipe.close_write();  // signal EOF

  char output[64] = {};
  // Request more bytes than available → must fail.
  bool ok = subprocess::detail::read_exact(pipe.rfd(), output, input_len + 10);
  ASSERT_FALSE(ok);
}

// ===========================================================================
// Pipe::read_some / Pipe::write_some
// ===========================================================================

TEST(ReadWriteSomeTest, PipeReadWriteBasic) {
  auto pipe = Pipe::create();
  const char msg[] = "pipe_rw_basic";
  const auto len = strlen(msg);

  auto w = pipe.write_some(msg, len);
  ASSERT_GT(w, 0);

  char buf[64] = {};
  auto r = pipe.read_some(buf, sizeof(buf));
  ASSERT_GT(r, 0);
  ASSERT_EQ(r, w);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

TEST(ReadWriteSomeTest, PipeReadClosed) {
  auto pipe = Pipe::create();
  pipe.close_read();
  char buf[16];
  auto n = pipe.read_some(buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, PipeWriteClosed) {
  auto pipe = Pipe::create();
  pipe.close_write();
  const char msg[] = "nope";
  auto n = pipe.write_some(msg, strlen(msg));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, PipeReadExactWriteAll) {
  auto pipe = Pipe::create();
  const char msg[] = "pipe_exact_all";
  const auto len = strlen(msg);

  ASSERT_TRUE(pipe.write_all(msg, len));

  char buf[64] = {};
  ASSERT_TRUE(pipe.read_exact(buf, len));
  ASSERT_EQ(std::string(buf, len), std::string(msg));
}

TEST(ReadWriteSomeTest, PipeReadEOF) {
  // read_some on a pipe whose write end has been closed should return 0 (EOF).
  auto pipe = Pipe::create();
  pipe.close_write();
  char buf[16];
  auto n = pipe.read_some(buf, sizeof(buf));
  ASSERT_EQ(n, 0);
}

// ===========================================================================
// File::read_some / File::write_some
// ===========================================================================

TEST(ReadWriteSomeTest, FileReadWrite) {
  TempFile tmp;
  const char content[] = "file_content_123";

  // Write via File::write_some
  {
    File f(tmp.path(), File::OpenType::WriteTruncate);
    auto w = f.write_some(content, strlen(content));
    ASSERT_GT(w, 0);
    ASSERT_EQ(static_cast<size_t>(w), strlen(content));
  }

  // Read back via File::read_some
  {
    File f(tmp.path(), File::OpenType::ReadOnly);
    char buf[64] = {};
    auto r = f.read_some(buf, sizeof(buf));
    ASSERT_GT(r, 0);
    ASSERT_EQ(static_cast<size_t>(r), strlen(content));
    ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(content));
  }
}

TEST(ReadWriteSomeTest, FileWriteAllReadExact) {
  TempFile tmp;
  const char content[] = "file_exact_all_data";

  {
    File f(tmp.path(), File::OpenType::WriteTruncate);
    ASSERT_TRUE(f.write_all(content, strlen(content)));
  }

  {
    File f(tmp.path(), File::OpenType::ReadOnly);
    char buf[64] = {};
    ASSERT_TRUE(f.read_exact(buf, strlen(content)));
    ASSERT_EQ(std::string(buf, strlen(content)), std::string(content));
  }
}

TEST(ReadWriteSomeTest, FileReadClosed) {
  // File that was never opened has an invalid handle.
  File f("/dev/null", File::OpenType::ReadOnly);
  f.close();
  char buf[16];
  auto n = f.read_some(buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, FileWriteClosed) {
  // File that was never opened has an invalid handle.
  File f("/dev/null", File::OpenType::ReadOnly);
  auto n = f.write_some("x", 1);
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, FileReadSomePartial) {
  // read_some may return fewer bytes than requested; test that it returns >0.
  TempFile tmp;
  const std::string content(4096, 'A');

  {
    File f(tmp.path(), File::OpenType::WriteTruncate);
    ASSERT_TRUE(f.write_all(content.data(), content.size()));
  }

  {
    File f(tmp.path(), File::OpenType::ReadOnly);
    char buf[128];
    auto r = f.read_some(buf, sizeof(buf));
    ASSERT_GT(r, 0);
    ASSERT_LE(static_cast<size_t>(r), sizeof(buf));
    ASSERT_EQ(std::string(buf, static_cast<size_t>(r)),
              content.substr(0, static_cast<size_t>(r)));
  }
}

// ===========================================================================
// FileHandler::read_some / FileHandler::write_some
// ===========================================================================

TEST(ReadWriteSomeTest, FileHandlerReadWrite) {
  TempFile tmp;
  const char content[] = "fh_content";

  // Write using File, then read using FileHandler
  {
    File f(tmp.path(), File::OpenType::WriteTruncate);
    ASSERT_TRUE(f.write_all(content, strlen(content)));
  }

  {
    File f(tmp.path(), File::OpenType::ReadOnly);
    FileHandler fh(f.fd().release());  // wrap the native handle

    char buf[64] = {};
    auto r = fh.read_some(buf, sizeof(buf));
    ASSERT_GT(r, 0);
    ASSERT_EQ(static_cast<size_t>(r), strlen(content));
    ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(content));

    // FileHandler does NOT own the handle — it has no destructor that closes
    // it. File owns the handle and will close it in ~File().
    // Calling fh.close() here would cause a double-close in ~File().
  }
}

TEST(ReadWriteSomeTest, FileHandlerWriteAllReadExact) {
  TempFile tmp;

  {
    File f(tmp.path(), File::OpenType::WriteTruncate);
    FileHandler fh(f.fd().release());
    const char data[] = "fh_exact";
    ASSERT_TRUE(fh.write_all(data, strlen(data)));
  }

  {
    File f(tmp.path(), File::OpenType::ReadOnly);
    FileHandler fh(f.fd().release());
    char buf[64] = {};
    ASSERT_TRUE(fh.read_exact(buf, strlen("fh_exact")));
    ASSERT_EQ(std::string(buf, strlen("fh_exact")), "fh_exact");
  }
}

TEST(ReadWriteSomeTest, FileHandlerReadClosed) {
  // Construct FileHandler directly with an invalid handle.
  FileHandler fh(subprocess::detail::INVALID_NATIVE_HANDLE_VALUE);
  char buf[16];
  auto n = fh.read_some(buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, FileHandlerWriteClosed) {
  // Construct FileHandler directly with an invalid handle.
  FileHandler fh(subprocess::detail::INVALID_NATIVE_HANDLE_VALUE);
  auto n = fh.write_some("x", 1);
  ASSERT_EQ(n, -1);
}

// ===========================================================================
// Buffer::read_some / Buffer::write_some
// ===========================================================================

TEST(ReadWriteSomeTest, BufferReadSome) {
  // Write data to the Buffer's pipe and then read it out via read_some().
  Buffer buf;
  const char input[] = "buf_read_some_test";
  const auto len = strlen(input);

  // Push data into the buffer's pipe from outside.
  auto w = subprocess::detail::write_some(buf.wfd(), input, len);
  ASSERT_EQ(static_cast<size_t>(w), len);

  // Now let the Buffer consume it.
  auto r = buf.read_some();
  ASSERT_EQ(r, w);

  // The data should now be inside buf.get<dynamic_buffer>().
  ASSERT_EQ(buf.get<dynamic_buffer>(), std::string_view(input, len));
}

TEST(ReadWriteSomeTest, BufferWriteSome) {
  // Create a Buffer backed by an external dynamic_buffer with known content,
  // then write_some() it out and read from the pipe.
  std::string_view data_view{"buf_write_some_test"};
  subprocess::const_buffer data{data_view};
  Buffer buf(data);

  // Write everything out.
  while (!buf.empty()) {
    auto w = buf.write_some();
    ASSERT_GE(w, 0);
    if (w == 0) {
      break;
    }
  }

  // Read everything back from the pipe.
  char out[64] = {};
  auto total_read = subprocess::detail::read_some(buf.rfd(), out, sizeof(out));
  ASSERT_GT(total_read, 0);
  ASSERT_EQ(static_cast<size_t>(total_read), data_view.size());
  ASSERT_EQ(std::string(out, static_cast<size_t>(total_read)), data_view);
  ASSERT_TRUE(buf.empty());
}

TEST(ReadWriteSomeTest, BufferWriteSomeEOF) {
  // After all data has been written, write_some() should return 0 (EOF).
  subprocess::dynamic_buffer data{"eof_test"};
  Buffer buf(data);

  // Write everything out.
  while (!buf.empty()) {
    auto w = buf.write_some();
    ASSERT_GE(w, 0);
    if (w == 0) {
      break;
    }
  }

  // Now the dynamic_buffer should be empty and write_some should return 0.
  ASSERT_TRUE(buf.empty());
  auto w = buf.write_some();
  ASSERT_EQ(w, 0);

  // Call again — still 0.
  w = buf.write_some();
  ASSERT_EQ(w, 0);
}

TEST(ReadWriteSomeTest, BufferReadSomeEOF) {
  // When the pipe's write end is closed, read_some() should return 0 (EOF).
  Buffer buf;
  buf.close_write();  // close the write end of the internal pipe

  auto r = buf.read_some();
  ASSERT_EQ(r, 0);
}

TEST(ReadWriteSomeTest, BufferReadSomeMultipleChunks) {
  // Push data in multiple chunks; buf.get<dynamic_buffer>() accumulates
  // everything.
  Buffer buf;
  const char chunk1[] = "chunk_one_";
  const char chunk2[] = "chunk_two_";
  const char chunk3[] = "chunk_three";

  subprocess::detail::write_some(buf.wfd(), chunk1, strlen(chunk1));
  subprocess::detail::write_some(buf.wfd(), chunk2, strlen(chunk2));
  subprocess::detail::write_some(buf.wfd(), chunk3, strlen(chunk3));
  buf.close_write();  // EOF

  // Read all available data; read_some returns 0 at EOF.
  auto r = buf.read_some();
  while (r > 0) {
    r = buf.read_some();
  }
  ASSERT_EQ(r, 0);

  std::string expected = std::string(chunk1) + chunk2 + chunk3;
  ASSERT_EQ(buf.get<dynamic_buffer>(), expected);
}

// ===========================================================================
// Edge-case: zero-byte dynamic_buffer contents
// ===========================================================================

TEST(ReadWriteSomeTest, EmptyBufferWriteSome) {
  subprocess::dynamic_buffer empty_data;
  Buffer buf(empty_data);

  ASSERT_TRUE(buf.empty());
  auto w = buf.write_some();
  ASSERT_EQ(w, 0);
}

// ===========================================================================
// Large data: verify write_all / read_exact consistency
// ===========================================================================

TEST(ReadWriteSomeTest, PipeLargeDataWriteAllReadExact) {
  auto pipe = Pipe::create();
  std::string large(64 * 1024, 'X');  // 64 KiB
  for (size_t i = 0; i < large.size(); ++i) {
    large[i] = static_cast<char>((i % 95) + 32);  // printable ASCII
  }
  // Drain the pipe from a dedicated thread so the write never blocks.
  std::string out(large.size(), '\0');
  bool read_ok = false;
  std::thread reader_thread(
      [&]() { read_ok = pipe.read_exact(out.data(), out.size()); });

  ASSERT_TRUE(pipe.write_all(large.data(), large.size()));

  reader_thread.join();
  ASSERT_TRUE(read_ok);
  ASSERT_EQ(out, large);
}

// ===========================================================================
// read_write_to_buffer_with_threads — verify pipe fds are closed afterward
//
// Guards against regressions where close_read() / close_write() are
// accidentally removed from the I/O pump function.
// ===========================================================================

TEST(ReadWriteSomeTest, PumpThreadsClosesReadFdsForOutputBuffers) {
  // Simulate stdout and stderr buffers that have data written to their
  // pipes.  After the pump runs, the read ends must be closed.
  subprocess::dynamic_buffer out_buf_data, err_buf_data;
  StdinRedirector in;
  StdoutRedirector out{out_buf_data};
  StderrRedirector err{err_buf_data};
  auto& out_buf = out.get<Buffer>();
  auto& err_buf = err.get<Buffer>();

  // Write data into each buffer's pipe, then close the write end to signal
  // EOF — exactly what happens when the child process exits.
  const char out_data[] = "hello_stdout";
  subprocess::detail::write_some(out_buf.wfd(), out_data, strlen(out_data));
  out_buf.close_write();

  const char err_data[] = "hello_stderr";
  subprocess::detail::write_some(err_buf.wfd(), err_data, strlen(err_data));
  err_buf.close_write();

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  // After the pump returns, read fds must be closed.
  ASSERT_FALSE(out_buf.rfd()) << "stdout read fd should be closed after pump";
  ASSERT_FALSE(err_buf.rfd()) << "stderr read fd should be closed after pump";

  // Data should have been consumed correctly.
  ASSERT_EQ(out_buf.get<dynamic_buffer>(), std::string_view(out_data));
  ASSERT_EQ(err_buf.get<dynamic_buffer>(), std::string_view(err_data));
}

TEST(ReadWriteSomeTest, PumpThreadsClosesReadFdForEmptyOutputBuffer) {
  // Even when there is no data (EOF immediately), the read fd must be closed.
  subprocess::dynamic_buffer out_buf_data;
  StdinRedirector in;
  StdoutRedirector out{out_buf_data};
  StderrRedirector err;
  auto& out_buf = out.get<Buffer>();
  out_buf.close_write();  // EOF before any data

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  ASSERT_FALSE(out_buf.rfd())
      << "read fd should be closed even for empty output";
  ASSERT_TRUE(out_buf.get<dynamic_buffer>().empty());
}

TEST(ReadWriteSomeTest, PumpThreadsClosesWriteFdForInputBuffer) {
  // The stdin path must close the write end of the pipe after writing all
  // data.  (close_write() was already present before the close_read() fix;
  // this guards against future regressions.)
  std::string_view data_view{"stdin_data_for_pump"};
  subprocess::dynamic_buffer data{data_view};
  StdinRedirector in{data};
  StdoutRedirector out;
  StderrRedirector err;
  auto& in_buf = in.get<Buffer>();

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  ASSERT_FALSE(in_buf.wfd()) << "stdin write fd should be closed after pump";

  // Read back what was written through the pipe.
  char read_buf[64] = {};
  auto n =
      subprocess::detail::read_some(in_buf.rfd(), read_buf, sizeof(read_buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(read_buf, static_cast<size_t>(n)), data_view);
  ASSERT_TRUE(data.empty());
}
TEST(ReadWriteSomeTest, PumpThreadsClosesAllFdsForFullDuplex) {
  // Full scenario: stdin + stdout + stderr.
  subprocess::dynamic_buffer in_data{"pump_full_duplex"};
  subprocess::dynamic_buffer out_buf_data, err_buf_data;
  StdinRedirector in{in_data};
  StdoutRedirector out{out_buf_data};
  StderrRedirector err{err_buf_data};
  auto& in_buf = in.get<Buffer>();
  auto& out_buf = out.get<Buffer>();
  auto& err_buf = err.get<Buffer>();

  const char out_data[] = "full_duplex_out";
  subprocess::detail::write_some(out_buf.wfd(), out_data, strlen(out_data));
  out_buf.close_write();

  const char err_data[] = "full_duplex_err";
  subprocess::detail::write_some(err_buf.wfd(), err_data, strlen(err_data));
  err_buf.close_write();

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  ASSERT_FALSE(in_buf.wfd()) << "stdin write fd should be closed";
  ASSERT_FALSE(out_buf.rfd()) << "stdout read fd should be closed";
  ASSERT_FALSE(err_buf.rfd()) << "stderr read fd should be closed";

  ASSERT_EQ(out_buf.get<dynamic_buffer>(), std::string_view(out_data));
  ASSERT_EQ(err_buf.get<dynamic_buffer>(), std::string_view(err_data));
}
//
// In the real subprocess flow, close_child_end() closes the child-facing
// pipe end, then read_write_to_buffer_*() closes the parent-facing end.
// After both, ALL pipe handles must be invalid — the Redirector destructor
// previously verified this with cerr messages; now we verify it in tests.
// ===========================================================================

TEST(ReadWriteSomeTest, FullLifecycleAllHandlesClosedForStdoutBuffer) {
  // stdout: child writes to wfd, parent reads from rfd
  subprocess::dynamic_buffer out_buf_data;
  StdinRedirector in;
  StdoutRedirector out{out_buf_data};
  StderrRedirector err;
  auto& out_buf = out.get<Buffer>();
  const char out_data[] = "lifecycle_stdout";
  subprocess::detail::write_some(out_buf.wfd(), out_data, strlen(out_data));
  out_buf.close_write();  // simulate child exit (close_child_end equivalent)

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  ASSERT_FALSE(out_buf.rfd()) << "stdout parent read fd should be closed";
  ASSERT_FALSE(out_buf.wfd()) << "stdout child write fd should be closed";
  ASSERT_EQ(out_buf.get<dynamic_buffer>(), std::string_view(out_data));
}

TEST(ReadWriteSomeTest, FullLifecycleAllHandlesClosedForStderrBuffer) {
  // stderr: child writes to wfd, parent reads from rfd
  subprocess::dynamic_buffer err_buf_data;
  StdinRedirector in;
  StdoutRedirector out;
  StderrRedirector err{err_buf_data};
  auto& err_buf = err.get<Buffer>();
  const char err_data[] = "lifecycle_stderr";
  subprocess::detail::write_some(err_buf.wfd(), err_data, strlen(err_data));
  err_buf.close_write();  // simulate child exit

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  ASSERT_FALSE(err_buf.rfd()) << "stderr parent read fd should be closed";
  ASSERT_FALSE(err_buf.wfd()) << "stderr child write fd should be closed";
  ASSERT_EQ(err_buf.get<dynamic_buffer>(), std::string_view(err_data));
}

TEST(ReadWriteSomeTest, FullLifecycleAllHandlesClosedForStdinBuffer) {
  // stdin: parent writes to wfd, child reads from rfd.
  // close_child_end() closes the parent's copy of the child read end,
  // but the child still has its dup'd copy — simulate with a drain thread.
  std::string_view data_view{"lifecycle_stdin"};
  subprocess::dynamic_buffer data{data_view};
  StdinRedirector in{data};
  StdoutRedirector out;
  StderrRedirector err;
  auto& in_buf = in.get<Buffer>();

  subprocess::dynamic_buffer drained;
  std::thread drain_thread([&]() {
    char buf[256];
    ssize_t n;
    while ((n = subprocess::detail::read_some(in_buf.rfd(), buf, sizeof(buf))) >
           0) {
      drained.append(buf, static_cast<size_t>(n));
    }
    in_buf.close_read();  // child closes its end after reading
  });

  // Parent closes its copy of the child read end (simulating close_child_end).
  // The child's drain thread still holds the dup'd read end.
  // NOTE: we skip close_read() here because the drain thread needs it.

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  drain_thread.join();

  ASSERT_FALSE(in_buf.wfd()) << "stdin parent write fd should be closed";
  ASSERT_FALSE(in_buf.rfd()) << "stdin child read fd should be closed";
  ASSERT_EQ(drained, data_view);
  ASSERT_TRUE(data.empty());
}

TEST(ReadWriteSomeTest, FullLifecycleAllThreeBuffersAllHandlesClosed) {
  // stdin + stdout + stderr, all with close_child_end simulation.
  std::string_view data_view{"lifecycle_in"};
  subprocess::dynamic_buffer in_data{data_view};
  subprocess::dynamic_buffer out_buf_data, err_buf_data;
  StdinRedirector in{in_data};
  StdoutRedirector out{out_buf_data};
  StderrRedirector err{err_buf_data};
  auto& in_buf = in.get<Buffer>();
  auto& out_buf = out.get<Buffer>();
  auto& err_buf = err.get<Buffer>();

  const char out_data[] = "lifecycle_out";
  subprocess::detail::write_some(out_buf.wfd(), out_data, strlen(out_data));
  out_buf.close_write();

  const char err_data[] = "lifecycle_err";
  subprocess::detail::write_some(err_buf.wfd(), err_data, strlen(err_data));
  err_buf.close_write();

  // Drain thread simulates the child reading from stdin.
  subprocess::dynamic_buffer drained;
  std::thread drain_thread([&]() {
    char buf[256];
    ssize_t n;
    while ((n = subprocess::detail::read_some(in_buf.rfd(), buf, sizeof(buf))) >
           0) {
      drained.append(buf, static_cast<size_t>(n));
    }
    in_buf.close_read();
  });

  auto _pump =
      subprocess::detail::read_write_to_buffer_with_threads(in, out, err);
  for (auto& t : _pump) {
    t.join();
  }

  drain_thread.join();

  // All pipe handles must be closed — both parent and child ends.
  ASSERT_FALSE(in_buf.wfd()) << "stdin parent write fd";
  ASSERT_FALSE(in_buf.rfd()) << "stdin child read fd";
  ASSERT_FALSE(out_buf.rfd()) << "stdout parent read fd";
  ASSERT_FALSE(out_buf.wfd()) << "stdout child write fd";
  ASSERT_FALSE(err_buf.rfd()) << "stderr parent read fd";
  ASSERT_FALSE(err_buf.wfd()) << "stderr child write fd";

  ASSERT_TRUE(in_data.empty());
  ASSERT_EQ(drained, data_view);
  ASSERT_EQ(out_buf.get<dynamic_buffer>(), std::string_view(out_data));
  ASSERT_EQ(err_buf.get<dynamic_buffer>(), std::string_view(err_data));
}
