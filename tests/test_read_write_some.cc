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
 *   - empty buffer write_some → returns 0 (EOF sentinel)
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;

// Shorter aliases for the types under test.
using subprocess::detail::Buffer;
using subprocess::detail::File;
using subprocess::detail::FileHandler;
using subprocess::detail::Pipe;
using subprocess::detail::unique_fd;

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
    File f(tmp.path());
    f.open_for_write();
    auto w = f.write_some(content, strlen(content));
    ASSERT_GT(w, 0);
    ASSERT_EQ(static_cast<size_t>(w), strlen(content));
  }

  // Read back via File::read_some
  {
    File f(tmp.path());
    f.open_for_read();
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
    File f(tmp.path());
    f.open_for_write();
    ASSERT_TRUE(f.write_all(content, strlen(content)));
  }

  {
    File f(tmp.path());
    f.open_for_read();
    char buf[64] = {};
    ASSERT_TRUE(f.read_exact(buf, strlen(content)));
    ASSERT_EQ(std::string(buf, strlen(content)), std::string(content));
  }
}

TEST(ReadWriteSomeTest, FileReadClosed) {
  // File that was never opened has an invalid handle.
  File f("/dev/null");
  char buf[16];
  auto n = f.read_some(buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, FileWriteClosed) {
  // File that was never opened has an invalid handle.
  File f("/dev/null");
  auto n = f.write_some("x", 1);
  ASSERT_EQ(n, -1);
}

TEST(ReadWriteSomeTest, FileReadSomePartial) {
  // read_some may return fewer bytes than requested; test that it returns >0.
  TempFile tmp;
  const std::string content(4096, 'A');

  {
    File f(tmp.path());
    f.open_for_write();
    ASSERT_TRUE(f.write_all(content.data(), content.size()));
  }

  {
    File f(tmp.path());
    f.open_for_read();
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
    File f(tmp.path());
    f.open_for_write();
    ASSERT_TRUE(f.write_all(content, strlen(content)));
  }

  {
    File f(tmp.path());
    f.open_for_read();
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
    File f(tmp.path());
    f.open_for_write();
    FileHandler fh(f.fd().release());
    const char data[] = "fh_exact";
    ASSERT_TRUE(fh.write_all(data, strlen(data)));
  }

  {
    File f(tmp.path());
    f.open_for_read();
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

  // The data should now be inside buf.buf().
  ASSERT_EQ(buf.buf(), std::string_view(input, len));
}

TEST(ReadWriteSomeTest, BufferWriteSome) {
  // Create a Buffer backed by an external buffer with known content, then
  // write_some() it out and read from the pipe.
  subprocess::buffer data{"buf_write_some_test"};
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
  ASSERT_EQ(static_cast<size_t>(total_read), data.size());
  ASSERT_EQ(std::string(out, static_cast<size_t>(total_read)),
            data.to_string());
}

TEST(ReadWriteSomeTest, BufferWriteSomeEOF) {
  // After all data has been written, write_some() should return 0 (EOF).
  subprocess::buffer data{"eof_test"};
  Buffer buf(data);

  // Write everything out.
  while (!buf.empty()) {
    auto w = buf.write_some();
    ASSERT_GE(w, 0);
    if (w == 0) {
      break;
    }
  }

  // Now the buffer should be empty and write_some should return 0.
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
  // Push data in multiple chunks; buf.buf() accumulates everything.
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
  ASSERT_EQ(buf.buf(), expected);
}

// ===========================================================================
// Edge-case: zero-byte buffer contents
// ===========================================================================

TEST(ReadWriteSomeTest, EmptyBufferWriteSome) {
  subprocess::buffer empty_data;
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

  ASSERT_TRUE(pipe.write_all(large.data(), large.size()));

  std::string out(large.size(), '\0');
  ASSERT_TRUE(pipe.read_exact(out.data(), out.size()));
  ASSERT_EQ(out, large);
}
