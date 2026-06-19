#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::dynamic_buffer;

// ===========================================================================
// 1. Empty buffer → empty lines
// ===========================================================================
TEST(BufferToLinesTest, EmptyBufferReturnsEmpty) {
  dynamic_buffer buf;
  auto lines = buf.to_lines();
  EXPECT_TRUE(lines.empty());
}

// ===========================================================================
// 2. Single line without trailing newline → one line
// ===========================================================================
TEST(BufferToLinesTest, SingleLineNoNewline) {
  dynamic_buffer buf(std::string_view("hello"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "hello");
}

// ===========================================================================
// 3. Single line with trailing \n → one line (newline is a terminator, not
//    separator)
// ===========================================================================
TEST(BufferToLinesTest, SingleLineWithTrailingNewline) {
  dynamic_buffer buf(std::string_view("hello\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "hello");
}

// ===========================================================================
// 4. Multiple lines with \n separators
// ===========================================================================
TEST(BufferToLinesTest, MultipleLinesWithNewline) {
  dynamic_buffer buf(std::string_view("line1\nline2\nline3"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}

// ===========================================================================
// 5. Multiple lines with trailing \n (terminator semantics)
// ===========================================================================
TEST(BufferToLinesTest, MultipleLinesWithTrailingNewline) {
  dynamic_buffer buf(std::string_view("line1\nline2\nline3\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}

// ===========================================================================
// 6. Windows-style \r\n line endings
// ===========================================================================
TEST(BufferToLinesTest, WindowsStyleLineEndings) {
  dynamic_buffer buf(std::string_view("line1\r\nline2\r\nline3"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}

// ===========================================================================
// 7. Windows-style \r\n with trailing newline
// ===========================================================================
TEST(BufferToLinesTest, WindowsStyleWithTrailingNewline) {
  dynamic_buffer buf(std::string_view("line1\r\nline2\r\nline3\r\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}

// ===========================================================================
// 8. Mixed line endings (some \n, some \r\n)
// ===========================================================================
TEST(BufferToLinesTest, MixedLineEndings) {
  dynamic_buffer buf(std::string_view("unix\nwindows\r\nunix\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "unix");
  EXPECT_EQ(lines[1], "windows");
  EXPECT_EQ(lines[2], "unix");
}

// ===========================================================================
// 9. Only a single newline → one empty line
// ===========================================================================
TEST(BufferToLinesTest, OnlyNewline) {
  dynamic_buffer buf(std::string_view("\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "");
}

// ===========================================================================
// 10. Only \r\n → one empty line
// ===========================================================================
TEST(BufferToLinesTest, OnlyCarriageReturnNewline) {
  dynamic_buffer buf(std::string_view("\r\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "");
}

// ===========================================================================
// 11. Consecutive newlines (empty lines preserved)
// ===========================================================================
TEST(BufferToLinesTest, ConsecutiveNewlines) {
  dynamic_buffer buf(std::string_view("a\n\nb\n\n\nc"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 6u);
  EXPECT_EQ(lines[0], "a");
  EXPECT_EQ(lines[1], "");
  EXPECT_EQ(lines[2], "b");
  EXPECT_EQ(lines[3], "");
  EXPECT_EQ(lines[4], "");
  EXPECT_EQ(lines[5], "c");
}

// ===========================================================================
// 12. Consecutive newlines with trailing newline
// ===========================================================================
TEST(BufferToLinesTest, ConsecutiveNewlinesWithTrailing) {
  dynamic_buffer buf(std::string_view("a\n\nb\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "a");
  EXPECT_EQ(lines[1], "");
  EXPECT_EQ(lines[2], "b");
}

// ===========================================================================
// 13. Buffer with only \r (not a line ending, should be preserved)
// ===========================================================================
TEST(BufferToLinesTest, CarriageReturnOnly) {
  dynamic_buffer buf(std::string_view("hello\rworld"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "hello\rworld");
}

// ===========================================================================
// 14. Empty lines at beginning
// ===========================================================================
TEST(BufferToLinesTest, EmptyLinesAtBeginning) {
  dynamic_buffer buf(std::string_view("\n\nhello"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "");
  EXPECT_EQ(lines[1], "");
  EXPECT_EQ(lines[2], "hello");
}

// ===========================================================================
// 15. Buffer with append (not just constructor) → to_lines
// ===========================================================================
TEST(BufferToLinesTest, AppendedDataToLines) {
  dynamic_buffer buf;
  buf.append("chunk1\n", 7);
  buf.append("chunk2\r\n", 8);
  buf.append("chunk3", 6);

  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "chunk1");
  EXPECT_EQ(lines[1], "chunk2");
  EXPECT_EQ(lines[2], "chunk3");
}

// ===========================================================================
// 16. to_lines on const buffer
// ===========================================================================
TEST(BufferToLinesTest, ConstBufferToLines) {
  const dynamic_buffer buf(std::string_view("a\nb\nc\n"));
  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "a");
  EXPECT_EQ(lines[1], "b");
  EXPECT_EQ(lines[2], "c");
}

// ===========================================================================
// 17. Buffer that was cleared and re-filled
// ===========================================================================
TEST(BufferToLinesTest, ClearedAndRefilled) {
  dynamic_buffer buf(std::string_view("old\ndata\n"));
  buf.clear();
  buf.append("new\r\nline\n", 10);

  auto lines = buf.to_lines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "new");
  EXPECT_EQ(lines[1], "line");
}

// ===========================================================================
// 18. Integration: to_lines with real subprocess stdout
// ===========================================================================
TEST(BufferToLinesTest, IntegrationWithSubprocessStdout) {
  dynamic_buffer out;

#if !defined(_WIN32)
  int ret =
      subprocess::run("printf", "line1\\nline2\\nline3\\n", std_out > out);
#else
  int ret = subprocess::run(TEXT("cmd.exe"), TEXT("/c"),
                            TEXT("(echo line1&echo line2&echo line3)"),
                            std_out > out);
#endif

  EXPECT_EQ(ret, 0);

  auto lines = out.to_lines();
  ASSERT_GE(lines.size(), 3u);

  // Check that the first three lines match (subprocess output may have
  // trailing whitespace differences across platforms)
  EXPECT_EQ(lines[0], "line1");
  EXPECT_EQ(lines[1], "line2");
  EXPECT_EQ(lines[2], "line3");
}
