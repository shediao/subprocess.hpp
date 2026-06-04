/**
 * test_pipe_reader_writer.cc — Unit tests for PipeReader / PipeWriter
 *
 * Covers:
 *   - PipeReader/PipeWriter construction from Pipe
 *   - Round-trip write/read
 *   - EOF detection (read returns 0 when write end closed)
 *   - Error handling (read/write on closed handles)
 *   - shared_ptr<pipe_pair> lifetime (read/write after Pipe destroyed)
 *   - Move semantics
 *   - Polymorphic use via Readable* / Writable*
 *   - Virtual destructor safety
 *   - Large data transfer
 *   - Multiple readers sharing the same pipe
 *   - Zero-size operations
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>

#include "./temp_file.h"
#include "subprocess/subprocess.hpp"

using subprocess::detail::Pipe;
using subprocess::detail::PipeReader;
using subprocess::detail::PipeWriter;
using subprocess::detail::Readable;
using subprocess::detail::Writable;
#if defined(_WIN32)
using subprocess::detail::ssize_t;
#endif

// ===========================================================================
// Construction & basic API
// ===========================================================================

TEST(PipeReaderWriterTest, PipeReaderConstructFromPipe) {
  auto pipe = Pipe::create();
  PipeReader reader(pipe);
  SUCCEED();
}

TEST(PipeReaderWriterTest, PipeWriterConstructFromPipe) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  SUCCEED();
}

TEST(PipeReaderWriterTest, PipeReaderDefaultConstructThenThrowOnRead) {
  // Default construction is allowed (pair_ is null).
  PipeReader reader;
  // Using a default-constructed reader must throw.
  char buf[16];
  EXPECT_THROW(reader.read(buf, sizeof(buf)), std::runtime_error);
}

TEST(PipeReaderWriterTest, PipeWriterDefaultConstructThenThrowOnWrite) {
  // Default construction is allowed (pair_ is null).
  PipeWriter writer;
  // Using a default-constructed writer must throw.
  const char msg[] = "x";
  EXPECT_THROW(writer.write(msg, 1), std::runtime_error);
}

TEST(PipeReaderWriterTest, PipeReaderCannotCopy) {
  static_assert(!std::is_copy_constructible_v<PipeReader>);
  static_assert(!std::is_copy_assignable_v<PipeReader>);
}

TEST(PipeReaderWriterTest, PipeWriterCannotCopy) {
  static_assert(!std::is_copy_constructible_v<PipeWriter>);
  static_assert(!std::is_copy_assignable_v<PipeWriter>);
}

// ===========================================================================
// Basic read / write round-trip
// ===========================================================================

TEST(PipeReaderWriterTest, WriteThenReadRoundTrip) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  PipeReader reader(pipe);

  const char msg[] = "hello_pipe_reader_writer";
  const auto len = strlen(msg);

  auto w = writer.write(msg, len);
  ASSERT_GT(w, 0);
  ASSERT_EQ(static_cast<size_t>(w), len);

  // Close write end so read won't block; then read exactly len bytes.
  pipe.close_write();

  char buf[64] = {};
  auto r = reader.read(buf, sizeof(buf));
  ASSERT_GT(r, 0);
  ASSERT_EQ(static_cast<size_t>(r), len);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

TEST(PipeReaderWriterTest, WriteMultipleThenReadAll) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  PipeReader reader(pipe);

  const char chunk1[] = "chunk_one_";
  const char chunk2[] = "chunk_two_";
  const char chunk3[] = "chunk_three";

  auto w1 = writer.write(chunk1, strlen(chunk1));
  auto w2 = writer.write(chunk2, strlen(chunk2));
  auto w3 = writer.write(chunk3, strlen(chunk3));
  ASSERT_GT(w1, 0);
  ASSERT_GT(w2, 0);
  ASSERT_GT(w3, 0);

  // Signal EOF before the read loop.
  pipe.close_write();

  std::string expected = std::string(chunk1) + chunk2 + chunk3;
  std::string result;

  char buf[16];
  ssize_t n;
  while ((n = reader.read(buf, sizeof(buf))) > 0) {
    result.append(buf, static_cast<size_t>(n));
  }
  ASSERT_EQ(result, expected);
}

// ===========================================================================
// EOF: reading when the write end is closed
// ===========================================================================

TEST(PipeReaderWriterTest, ReadEOFWhenWriteEndClosed) {
  auto pipe = Pipe::create();
  PipeReader reader(pipe);

  {
    PipeWriter writer(pipe);
    const char msg[] = "eof_test";
    writer.write(msg, strlen(msg));
  }

  pipe.close_write();

  char buf[64] = {};
  auto r1 = reader.read(buf, sizeof(buf));
  ASSERT_GT(r1, 0);

  auto r2 = reader.read(buf, sizeof(buf));
  ASSERT_EQ(r2, 0);
}

// ===========================================================================
// Error handling: read/write on closed handles
// ===========================================================================

TEST(PipeReaderWriterTest, ReadFromClosedReadEndReturnsMinusOne) {
  auto pipe = Pipe::create();
  PipeReader reader(pipe);
  pipe.close_read();

  char buf[16];
  auto n = reader.read(buf, sizeof(buf));
  ASSERT_EQ(n, -1);
}

TEST(PipeReaderWriterTest, WriteToClosedWriteEndReturnsMinusOne) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  pipe.close_write();

  const char msg[] = "nope";
  auto n = writer.write(msg, strlen(msg));
  ASSERT_EQ(n, -1);
}

// ===========================================================================
// Lifetime: PipeReader/PipeWriter keep pipe_pair alive after Pipe destroyed
// ===========================================================================

TEST(PipeReaderWriterTest, ReadAfterPipeDestroyed) {
  // PipeReader shares ownership via shared_ptr<pipe_pair>.  After the Pipe
  // and PipeWriter are destroyed, the underlying fds remain open because the
  // reader still holds a reference.  We close_write() explicitly to make the
  // read non-blocking.
  PipeReader reader = [&]() {
    auto pipe = Pipe::create();
    PipeWriter writer(pipe);
    const char msg[] = "survive_destruction";
    writer.write(msg, strlen(msg));
    pipe.close_write();
    return PipeReader(pipe);
  }();  // pipe and writer destroyed.

  char buf[64] = {};
  auto n = reader.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)),
            std::string("survive_destruction"));

  // After draining, EOF is signalled.
  auto eof = reader.read(buf, sizeof(buf));
  ASSERT_EQ(eof, 0);
}

TEST(PipeReaderWriterTest, WriteAfterPipeDestroyed) {
  // PipeWriter keeps the write fd alive even after the Pipe is gone.
  PipeWriter writer = [&]() {
    auto pipe = Pipe::create();
    // Keep the read end open so the pipe buffer doesn't raise SIGPIPE.
    auto reader = PipeReader(pipe);
    auto w = PipeWriter(pipe);
    // Return the writer; pipe and reader are destroyed, but the pipe_pair
    // lives on because writer (and the moved-out reader handle via
    // shared_ptr) keep it alive.
    return w;
  }();

  // Write should succeed — the underlying fd is still valid.
  const char msg[] = "write_after_destroy";
  auto written = writer.write(msg, strlen(msg));
  ASSERT_GT(written, 0);
  ASSERT_EQ(static_cast<size_t>(written), strlen(msg));
}

// ===========================================================================
// Move semantics
// ===========================================================================

TEST(PipeReaderWriterTest, PipeReaderMoveConstruct) {
  auto pipe = Pipe::create();
  PipeReader reader1(pipe);

  PipeWriter writer(pipe);
  const char msg[] = "move_construct_test";
  writer.write(msg, strlen(msg));
  pipe.close_write();

  PipeReader reader2(std::move(reader1));

  char buf[64] = {};
  auto n = reader2.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)), std::string(msg));
}

TEST(PipeReaderWriterTest, PipeReaderMoveAssign) {
  auto pipe1 = Pipe::create();
  auto pipe2 = Pipe::create();
  PipeReader reader1(pipe1);
  PipeReader reader2(pipe2);

  {
    PipeWriter writer(pipe1);
    const char msg[] = "move_assign_test";
    writer.write(msg, strlen(msg));
  }
  pipe1.close_write();

  reader2 = std::move(reader1);

  char buf[64] = {};
  auto n = reader2.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)),
            std::string("move_assign_test"));
}

TEST(PipeReaderWriterTest, PipeWriterMoveConstruct) {
  auto pipe = Pipe::create();
  PipeWriter writer1(pipe);

  PipeWriter writer2(std::move(writer1));

  PipeReader reader(pipe);
  const char msg[] = "writer_move_construct";
  auto w = writer2.write(msg, strlen(msg));
  ASSERT_GT(w, 0);
  ASSERT_EQ(static_cast<size_t>(w), strlen(msg));

  pipe.close_write();
  char buf[64] = {};
  auto r = reader.read(buf, sizeof(buf));
  ASSERT_EQ(r, w);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

TEST(PipeReaderWriterTest, PipeWriterMoveAssign) {
  auto pipe1 = Pipe::create();
  auto pipe2 = Pipe::create();
  PipeWriter writer1(pipe1);
  PipeWriter writer2(pipe2);

  writer2 = std::move(writer1);

  PipeReader reader(pipe1);
  const char msg[] = "writer_move_assign";
  auto w = writer2.write(msg, strlen(msg));
  ASSERT_GT(w, 0);
  ASSERT_EQ(static_cast<size_t>(w), strlen(msg));

  pipe1.close_write();
  char buf[64] = {};
  auto r = reader.read(buf, sizeof(buf));
  ASSERT_EQ(r, w);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

// ===========================================================================
// Polymorphic usage via Readable* / Writable*
// ===========================================================================

TEST(PipeReaderWriterTest, ReadablePolymorphic) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);

  const char msg[] = "polymorphic_read";
  writer.write(msg, strlen(msg));
  pipe.close_write();

  PipeReader reader(pipe);
  Readable* r = &reader;

  char buf[64] = {};
  auto n = r->read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)), std::string(msg));
}

TEST(PipeReaderWriterTest, WritablePolymorphic) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  Writable* w = &writer;

  const char msg[] = "polymorphic_write";
  auto n = w->write(msg, strlen(msg));
  ASSERT_GT(n, 0);

  pipe.close_write();
  PipeReader reader(pipe);
  char buf[64] = {};
  auto r = reader.read(buf, sizeof(buf));
  ASSERT_EQ(r, n);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

TEST(PipeReaderWriterTest, DeleteViaReadablePointer) {
  auto pipe = Pipe::create();
  auto* reader = static_cast<Readable*>(new PipeReader(pipe));
  delete reader;  // Must not leak or crash (tests virtual destructor).
  SUCCEED();
}

TEST(PipeReaderWriterTest, DeleteViaWritablePointer) {
  auto pipe = Pipe::create();
  auto* writer = static_cast<Writable*>(new PipeWriter(pipe));
  delete writer;
  SUCCEED();
}

// ===========================================================================
// Large data transfer
// ===========================================================================

TEST(PipeReaderWriterTest, LargeDataTransfer) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  PipeReader reader(pipe);

  // 64 KiB of printable ASCII.
  std::string large(64 * 1024, '\0');
  for (size_t i = 0; i < large.size(); ++i) {
    large[i] = static_cast<char>((i % 95) + 32);
  }

  // Drain the pipe from a dedicated thread so the write never blocks.
  std::string result;
  std::thread reader_thread([&]() {
    char buf[1024];
    ssize_t n;
    while ((n = reader.read(buf, sizeof(buf))) > 0) {
      result.append(buf, static_cast<size_t>(n));
    }
  });

  // Write in a loop — write_some may not accept everything at once.
  size_t written = 0;
  while (written < large.size()) {
    auto w = writer.write(large.data() + written, large.size() - written);
    ASSERT_GE(w, 0);
    if (w == 0) {
      break;
    }
    written += static_cast<size_t>(w);
  }
  ASSERT_EQ(written, large.size());

  // Signal EOF so the reader thread exits its loop.
  pipe.close_write();
  reader_thread.join();

  ASSERT_EQ(result, large);
}

// ===========================================================================
// Multiple readers from the same pipe
// ===========================================================================

TEST(PipeReaderWriterTest, TwoReadersSharePipe) {
  auto pipe = Pipe::create();
  PipeReader reader1(pipe);
  PipeReader reader2(pipe);

  PipeWriter writer(pipe);
  const char msg[] = "two_readers";
  writer.write(msg, strlen(msg));
  pipe.close_write();

  // Either reader can read the data (data is consumed once from the pipe).
  char buf[64] = {};
  auto n1 = reader1.read(buf, sizeof(buf));
  auto n2 = reader2.read(buf, sizeof(buf));

  // Exactly one of the readers should have gotten the data.
  bool read1 = n1 > 0;
  bool read2 = n2 > 0;
  ASSERT_TRUE(read1 || read2);
  if (read1) {
    ASSERT_EQ(std::string(buf, static_cast<size_t>(n1)), std::string(msg));
  } else {
    ASSERT_EQ(std::string(buf, static_cast<size_t>(n2)), std::string(msg));
  }
}

// ===========================================================================
// Zero-size operations
// ===========================================================================

TEST(PipeReaderWriterTest, ReadZeroBytesReturnsZero) {
  auto pipe = Pipe::create();
  PipeReader reader(pipe);

  char buf[1];
  auto n = reader.read(buf, 0);
  ASSERT_EQ(n, 0);
}

TEST(PipeReaderWriterTest, WriteZeroBytesReturnsZero) {
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);

  const char data[] = "x";
  auto n = writer.write(data, 0);
  ASSERT_EQ(n, 0);
}

// ===========================================================================
// Default construction + move-assignment (the primary use case for
// allowing default constructibility)
// ===========================================================================

TEST(PipeReaderWriterTest, PipeReaderDefaultConstructThenMoveAssign) {
  PipeReader reader;  // default-constructed, null pair_

  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  const char msg[] = "move_assign_to_default";
  writer.write(msg, strlen(msg));
  pipe.close_write();

  reader = PipeReader(pipe);  // move-assign a valid reader

  char buf[64] = {};
  auto n = reader.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)), std::string(msg));
}

TEST(PipeReaderWriterTest, PipeWriterDefaultConstructThenMoveAssign) {
  PipeWriter writer;  // default-constructed, null pair_

  auto pipe = Pipe::create();
  writer = PipeWriter(pipe);  // move-assign a valid writer

  const char msg[] = "writer_default_then_assign";
  auto w = writer.write(msg, strlen(msg));
  ASSERT_GT(w, 0);
  ASSERT_EQ(static_cast<size_t>(w), strlen(msg));

  pipe.close_write();
  PipeReader reader(pipe);
  char buf[64] = {};
  auto r = reader.read(buf, sizeof(buf));
  ASSERT_EQ(r, w);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(r)), std::string(msg));
}

// ===========================================================================
// Moved-from state: reading/writing on a moved-from PipeReader/PipeWriter
// must throw because the internal shared_ptr is null.
// ===========================================================================

TEST(PipeReaderWriterTest, PipeReaderMovedFromThrowsOnRead) {
  auto pipe = Pipe::create();
  PipeReader reader1(pipe);
  PipeReader reader2(std::move(reader1));  // reader1 is now moved-from

  char buf[16];
  EXPECT_THROW(reader1.read(buf, sizeof(buf)), std::runtime_error);

  // reader2 should still work fine.
  SUCCEED();
}

TEST(PipeReaderWriterTest, PipeWriterMovedFromThrowsOnWrite) {
  auto pipe = Pipe::create();
  PipeWriter writer1(pipe);
  PipeWriter writer2(std::move(writer1));  // writer1 is now moved-from

  const char msg[] = "x";
  EXPECT_THROW(writer1.write(msg, 1), std::runtime_error);

  // writer2 should still work fine.
  SUCCEED();
}

TEST(PipeReaderWriterTest, PipeReaderMoveAssignFromMovedFromState) {
  // Default-construct, then move away, then move-assign a valid one back.
  PipeReader reader;
  {
    auto pipe = Pipe::create();
    reader = PipeReader(pipe);
  }  // pipe destroyed but reader keeps pair_ alive
  PipeReader reader2(std::move(reader));  // reader is now moved-from

  char buf[16];
  EXPECT_THROW(reader.read(buf, sizeof(buf)), std::runtime_error);

  // Move a fresh valid reader back in.
  auto pipe2 = Pipe::create();
  PipeWriter w(pipe2);
  const char msg[] = "revalidate";
  w.write(msg, strlen(msg));
  pipe2.close_write();

  reader = PipeReader(pipe2);  // re-validate via move-assign
  auto n = reader.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)), std::string(msg));
}

TEST(PipeReaderWriterTest, PipeWriterMoveAssignFromMovedFromState) {
  PipeWriter writer;
  {
    auto pipe = Pipe::create();
    writer = PipeWriter(pipe);
  }
  PipeWriter writer2(std::move(writer));  // writer is now moved-from

  const char msg[] = "x";
  EXPECT_THROW(writer.write(msg, 1), std::runtime_error);

  // Move a fresh valid writer back in.
  auto pipe2 = Pipe::create();
  PipeReader r(pipe2);
  writer = PipeWriter(pipe2);  // re-validate via move-assign
  const char msg2[] = "revalidated_writer";
  auto n = writer.write(msg2, strlen(msg2));
  ASSERT_GT(n, 0);
  ASSERT_EQ(static_cast<size_t>(n), strlen(msg2));
}

TEST(PipeReaderWriterTest, ReadFromStdout) {
  using namespace subprocess::named_arguments;
  auto pipe = Pipe::create();
  PipeReader reader(pipe);
#if defined(_WIN32)
  subprocess::detail::builder proc($shell, "<nul set /p=hello&exit /b 0",
                                   $stdout > pipe);
#else
  subprocess::detail::builder proc($shell, "printf '%s' hello", $stdout > pipe);
#endif
  auto p = proc.spawn();

  char buf[16];
  auto n = reader.read(buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_EQ(std::string(buf, static_cast<size_t>(n)), "hello");
  p.wait();
}

TEST(PipeReaderWriterTest, WriteToStdin) {
  using namespace subprocess::named_arguments;
  auto pipe = Pipe::create();
  PipeWriter writer(pipe);
  subprocess::buffer outbuf;
#if defined(_WIN32)
  subprocess::detail::builder proc("more.com", {},
                                   $stdin<pipe, $stdout> outbuf);
#else
  subprocess::detail::builder proc("cat", {}, $stdin<pipe, $stdout> outbuf);
#endif

  std::string_view data = "hello";
  std::thread writer_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writer.write(data.data(), data.size());
    writer.close();
  });
  proc.run();
  writer_thread.join();

  auto output = outbuf.to_string();
  ASSERT_TRUE(output.starts_with(data));
}
