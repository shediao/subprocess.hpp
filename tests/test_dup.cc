/**
 * test_dup.cc — Tests for the dup() functionality across
 *               Pipe, File, FileHandler, Buffer, and Redirector.
 *
 * Covers:
 *   - Pipe::dup() – basic dup, independent read/write, error path
 *   - File::dup() – basic dup, independent open/close
 *   - FileHandler::dup() – dup from raw native handle
 *   - Buffer::dup() – shared buffer, independent pipe
 *   - Redirector::dup() – each variant type (Pipe/File/FileHandler/Buffer)
 *   - Integration: dup'ed pipes/buffers through real subprocesses
 *
 * Important note on pipe I/O semantics:
 *   On POSIX, dup() / fcntl(F_DUPFD_CLOEXEC) creates a new fd that refers
 *   to the SAME open file description.  This means that a duped write end
 *   and the original write end share the same underlying pipe write-end.
 *   A read end will only see EOF once ALL write ends (original + all dups)
 *   have been closed.  The same applies on Windows with DuplicateHandle.
 *
 *   Therefore, when testing duped pipes, all write ends must be closed
 *   before attempting to read to EOF.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::buffer;
using subprocess::run;
#if defined(_WIN32)
using subprocess::detail::ssize_t;
#endif

using std::string_literals::operator""s;

namespace S = subprocess::detail;

static void write_all_and_close(S::unique_fd& fd, buffer const& write_data) {
  auto write_span = write_data.span();
  S::write_all(fd, write_span.data(), write_span.size());
  fd.close();
}

static void read_from_native_handle(S::unique_fd const& fd, buffer& read_data) {
  char buf[1024];
  ssize_t read;
  do {
    read = S::read_some(fd, buf, sizeof(buf));
    if (read > 0) {
      read_data.append(buf, static_cast<size_t>(read));
    }
  } while (read > 0);
}

// Pipe::dup() — basic functionality
// ===========================================================================

TEST(DupTest, PipeDupBasic) {
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  // Both pipes should have valid handles
  ASSERT_NE(p1.rfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(p1.wfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(p2.rfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(p2.wfd(), S::INVALID_NATIVE_HANDLE_VALUE);

  // The duped handles should be different from the original
  ASSERT_NE(p1.rfd(), p2.rfd());
  ASSERT_NE(p1.wfd(), p2.wfd());
}

// ===========================================================================
// Pipe::dup() — duped read can consume data written through original write
// ===========================================================================

TEST(DupTest, PipeDupReadFromDuppedEnd) {
  // Write through original write end, close ALL write ends, read through duped
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  const char* msg = "hello_dup";
  write_all_and_close(p1.wfd(), buffer{msg});

  // p2.wfd() still shares the same write-end; close it to signal EOF
  p2.close_write();

  buffer out;
  read_from_native_handle(p2.rfd(), out);
  ASSERT_EQ(out, msg);
}

TEST(DupTest, PipeDupReadFromOriginalEnd) {
  // Write through duped write end, close ALL write ends, read through original
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  const char* msg = "hello_reverse";
  write_all_and_close(p2.wfd(), buffer{msg});

  // p1.wfd() still shares the same write-end; close it to signal EOF
  p1.close_write();

  buffer out;
  read_from_native_handle(p1.rfd(), out);
  ASSERT_EQ(out, msg);
}

// ===========================================================================
// Pipe::dup() — close original read; duped read end still works
// ===========================================================================

TEST(DupTest, PipeDupCloseOriginalReadStillWorks) {
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  const char* msg = "before_close";
  write_all_and_close(p1.wfd(), buffer{msg});

  // Close BOTH write ends; close original read end
  p2.close_write();
  p1.close_read();

  // Duped read end should still read the data
  buffer out;
  read_from_native_handle(p2.rfd(), out);
  ASSERT_EQ(out, msg);
}

// ===========================================================================
// Pipe::dup() — close duped write; original write end still works
// ===========================================================================

TEST(DupTest, PipeDupCloseDuppedWriteStillWorks) {
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  // Close duped write immediately; original write still open
  p2.close_write();

  const char* msg = "close_dup_write";
  write_all_and_close(p1.wfd(), buffer{msg});

  buffer out;
  read_from_native_handle(p2.rfd(), out);
  ASSERT_EQ(out, msg);
}

// ===========================================================================
// Pipe::dup() — dup after one end is already closed (error path)
// ===========================================================================

TEST(DupTest, PipeDupAfterCloseReadEnd) {
  auto p1 = S::Pipe::create();
  p1.close_read();

  auto p2 = p1.dup();

  // The dup should have both ends invalid since fds_[0] dup fails
  ASSERT_EQ(p2.rfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  // fds_[1] dup is never attempted because fds_[0] failed
  ASSERT_EQ(p2.wfd(), S::INVALID_NATIVE_HANDLE_VALUE);
}

// ===========================================================================
// Pipe::dup() — dup of dup (chained)
// ===========================================================================

TEST(DupTest, PipeDupChained) {
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();
  auto p3 = p2.dup();

  ASSERT_NE(p3.rfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(p3.wfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(p3.rfd(), p1.rfd());
  ASSERT_NE(p3.rfd(), p2.rfd());
  ASSERT_NE(p3.wfd(), p1.wfd());
  ASSERT_NE(p3.wfd(), p2.wfd());

  // Write through p3's write, close p1 and p2 writes too, read through p1
  const char* msg = "chained_dup";
  write_all_and_close(p3.wfd(), buffer{msg});
  p1.close_write();
  p2.close_write();

  buffer out;
  read_from_native_handle(p1.rfd(), out);
  ASSERT_EQ(out, msg);
}

// ===========================================================================
// Pipe::dup() — integration: duped pipe used with real subprocess
// ===========================================================================

TEST(DupTest, PipeDupWithSubprocess) {
  auto p1 = S::Pipe::create();
  auto p2 = p1.dup();

  // Run a process writing to p1.  Use spawn + wait so that
  // the child completes before we attempt to read from p2.
#if defined(_WIN32)
  S::builder proc("cmd.exe"s, {"/c"s, "echo dup_subprocess_test&exit /b 0"},
                  $stdout > p1);
#else
  S::builder proc("echo", {"-n", "dup_subprocess_test"}, $stdout > p1);
#endif
  proc.spawn();
  proc.wait();

  // The child has exited, closing its stdout (a dup of the pipe write end).
  // close_child_end() already closed p1.wfd().  p2.wfd() still refers
  // to the same file description; close it to signal EOF.
  p2.close_write();

  buffer out;
  read_from_native_handle(p2.rfd(), out);

#if defined(_WIN32)
  ASSERT_TRUE(out.to_string().find("dup_subprocess_test") != std::string::npos);
#else
  ASSERT_EQ(out, "dup_subprocess_test");
#endif
}

// ===========================================================================
// File::dup() — basic functionality
// ===========================================================================

TEST(DupTest, FileDupBasic) {
  TempFile tmp;
  tmp.write(std::string{"file_dup_content"});

  S::File f1(tmp.path(), S::File::OpenType::ReadOnly);
  ASSERT_NE(f1.fd(), S::INVALID_NATIVE_HANDLE_VALUE);

  auto f2 = f1.dup();
  ASSERT_NE(f2.fd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(f2.fd(), f1.fd());

  // Both handles should be valid
  f2.close();
  f1.close();
}

// ===========================================================================
// File::dup() — integration: duped file read via read_from_native_handle
// ===========================================================================

TEST(DupTest, FileDupReadContent) {
  TempFile tmp;

  // Write content to temp file via subprocess
  run(
#if defined(_WIN32)
      "cmd.exe", "/c", "<nul set /p=file_dup_subprocess"
#else
      "/bin/echo", "-n", "file_dup_subprocess"
#endif
      ,
      std_out > tmp.path());

  // Open and read through original
  S::File f1(tmp.path(), S::File::OpenType::ReadOnly);
  auto f2 = f1.dup();

  // Read from duped fd
  buffer out2;
  auto& fd2 = f2.fd();
  read_from_native_handle(fd2, out2);
  ASSERT_EQ(out2, "file_dup_subprocess");

  // f1.fd() still shares the same file description; close it
  f1.close();
  // fd2 was consumed by read_from_native_handle (set to invalid) — no need to
  // close again.
}

// ===========================================================================
// File::dup() — dup after close returns invalid fd
// ===========================================================================

TEST(DupTest, FileDupAfterClose) {
  TempFile tmp;
  tmp.write(std::string{"file_dup_after_close"});

  S::File f1(tmp.path(), S::File::OpenType::ReadOnly);
  ASSERT_TRUE(f1.is_valid());
  f1.close();
  ASSERT_FALSE(f1.is_valid());

  // Dup after close — the duped File should be invalid
  auto f2 = f1.dup();
  ASSERT_FALSE(f2.is_valid());
  ASSERT_EQ(f2.fd(), S::INVALID_NATIVE_HANDLE_VALUE);
}

// ===========================================================================
// FileHandler::dup() — basic functionality
// ===========================================================================

TEST(DupTest, FileHandlerDupBasic) {
  // Create a pipe, get a native handle, wrap in FileHandler, dup it
  auto pipe = S::Pipe::create();
  S::FileHandler fh(pipe.rfd().dup());
  ASSERT_NE(fh.fd(), S::INVALID_NATIVE_HANDLE_VALUE);

  auto fh2 = fh.dup();
  ASSERT_NE(fh2.fd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(fh2.fd(), fh.fd());

  fh.close();
  fh2.close();
}

// ===========================================================================
// FileHandler::dup() — duped handle still functional
// ===========================================================================

TEST(DupTest, FileHandlerDupFunctional) {
  auto pipe = S::Pipe::create();

  const char* msg = "filehandler_dup_test";
  write_all_and_close(pipe.wfd(), buffer{msg});

  S::FileHandler fh(pipe.rfd().release());
  auto fh2 = fh.dup();

  // Close original, read through duped
  fh.close();

  buffer out;
  read_from_native_handle(fh2.fd(), out);
  ASSERT_EQ(out, msg);

  fh2.close();
}

// ===========================================================================
// Buffer::dup() — shared underlying buffer
// ===========================================================================

TEST(DupTest, BufferDupSharedBuffer) {
  buffer out_buf;
  S::Buffer buf1(out_buf);
  auto buf2 = buf1.dup();

  // Both should share the same underlying buffer
  ASSERT_EQ(&buf1.buf(), &buf2.buf());

  // But have different pipes
  ASSERT_NE(buf1.pipe().rfd(), buf2.pipe().rfd());
  ASSERT_NE(buf1.pipe().wfd(), buf2.pipe().wfd());
}

TEST(DupTest, BufferDupOwnedBuffer) {
  S::Buffer buf1;
  auto buf2 = buf1.dup();

  // Both should share the same underlying buffer
  ASSERT_EQ(&buf1.buf(), &buf2.buf());

  // But have different pipes
  ASSERT_NE(buf1.pipe().rfd(), buf2.pipe().rfd());
  ASSERT_NE(buf1.pipe().wfd(), buf2.pipe().wfd());
}

// ===========================================================================
// Buffer::dup() — integration: both share the same underlying buffer
// ===========================================================================

TEST(DupTest, BufferDupIntegration) {
  buffer out_buf;
  S::Buffer buf(out_buf);
  auto buf2 = buf.dup();

  // Both wrap the same underlying buffer
  ASSERT_EQ(&buf.buf(), &buf2.buf());

  // buf and buf2 have independent pipes
  ASSERT_NE(buf.pipe().rfd(), buf2.pipe().rfd());

#if defined(_WIN32)
  run("cmd.exe", "/c", "echo buffer_dup_integration&exit /b 0",
      std_out > out_buf);
#else
  run("echo", "-n", "buffer_dup_integration", std_out > out_buf);
#endif

#if defined(_WIN32)
  ASSERT_TRUE(out_buf.to_string().find("buffer_dup_integration") !=
              std::string::npos);
#else
  ASSERT_EQ(out_buf, "buffer_dup_integration");
#endif
}

// ===========================================================================
// Buffer::dup() — independent pipe objects, shared underlying file description
// ===========================================================================

TEST(DupTest, BufferDupIndependentPipes) {
  buffer shared;
  S::Buffer buf1(shared);
  auto buf2 = buf1.dup();

  // The two Buffer objects have distinct Pipe objects
  ASSERT_NE(buf1.pipe().rfd(), buf2.pipe().rfd());
  ASSERT_NE(buf1.pipe().wfd(), buf2.pipe().wfd());

  // Write through buf1's pipe, close both write ends, read via buf2's pipe
  write_all_and_close(buf1.pipe().wfd(), buffer{"from_buf1"});
  buf2.pipe().close_write();

  read_from_native_handle(buf2.pipe().rfd(), shared);
  ASSERT_EQ(shared, "from_buf1");
}

// ===========================================================================
// Redirector::dup() — each variant type
// ===========================================================================

TEST(DupTest, RedirectorDupPipe) {
  auto p1 = S::Pipe::create();
  S::StdinRedirector redir(std::move(p1));

  auto duped = redir.dup();
  ASSERT_NE(duped, nullptr);

  auto* duped_pipe = std::get_if<S::Pipe>(duped.get());
  ASSERT_NE(duped_pipe, nullptr);
  ASSERT_NE(duped_pipe->rfd(), S::INVALID_NATIVE_HANDLE_VALUE);
  ASSERT_NE(duped_pipe->wfd(), S::INVALID_NATIVE_HANDLE_VALUE);
}

TEST(DupTest, RedirectorDupFile) {
  TempFile tmp;
  tmp.write(std::string{"redirector_dup_file"});

  S::File f(tmp.path(), S::File::OpenType::ReadOnly);

  S::StdinRedirector redir(std::move(f));

  auto duped = redir.dup();
  ASSERT_NE(duped, nullptr);

  auto* duped_file = std::get_if<S::File>(duped.get());
  ASSERT_NE(duped_file, nullptr);
  ASSERT_NE(duped_file->fd(), S::INVALID_NATIVE_HANDLE_VALUE);
}

TEST(DupTest, RedirectorDupFileHandler) {
  auto pipe = S::Pipe::create();
  S::FileHandler fh(pipe.rfd().release());

  S::StdinRedirector redir(std::move(fh));

  auto duped = redir.dup();
  ASSERT_NE(duped, nullptr);

  auto* duped_fh = std::get_if<S::FileHandler>(duped.get());
  ASSERT_NE(duped_fh, nullptr);
  ASSERT_NE(duped_fh->fd(), S::INVALID_NATIVE_HANDLE_VALUE);

  duped_fh->close();
}

TEST(DupTest, RedirectorDupBuffer) {
  buffer buf;
  S::StderrRedirector redir(buf);

  auto duped = redir.dup();
  ASSERT_NE(duped, nullptr);

  auto* duped_buffer = std::get_if<S::Buffer>(duped.get());
  ASSERT_NE(duped_buffer, nullptr);
  ASSERT_EQ(&duped_buffer->buf(), &buf);
}

TEST(DupTest, RedirectorDupNullRedirector) {
  S::StdinRedirector redir;  // default, redirect_ is nullptr

  auto duped = redir.dup();
  ASSERT_EQ(duped, nullptr);
}

// ===========================================================================
// Redirector::dup() — demonstrate the dup-before-subprocess pattern
// ===========================================================================

TEST(DupTest, PipeDupBeforeSubprocessPattern) {
  auto p = S::Pipe::create();

  // Dup before passing to subprocess — the dup survives the child's cleanup
  auto p_reader = p.dup();

#if defined(_WIN32)
  S::builder proc("cmd.exe", {"/c", "echo dup_before_sp&exit /b 0"},
                  $stdout > p);
#else
  S::builder proc("/bin/echo", {"-n", "dup_before_sp"}, $stdout > p);
#endif
  proc.spawn();
  proc.wait();

  // p's fds were closed by the child; p_reader is independent.
  p_reader.close_write();

  buffer out;
  read_from_native_handle(p_reader.rfd(), out);

#if defined(_WIN32)
  ASSERT_TRUE(out.to_string().find("dup_before_sp") != std::string::npos);
#else
  ASSERT_EQ(out, "dup_before_sp");
#endif
}
