#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <deque>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"

using subprocess::detail::split;
using subprocess::detail::split_to_if;

// ============================================================================
// split() convenience wrapper
// ============================================================================

TEST(SplitTest, BasicComma) {
  auto v = split(std::string("a,b,c"), ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitTest, EmptyString) {
  auto v = split(std::string(""), ',');
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "");
}

TEST(SplitTest, NoDelimiter) {
  auto v = split(std::string("hello"), ',');
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "hello");
}

TEST(SplitTest, OnlyDelimiters) {
  auto v = split(std::string(",,"), ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "");
}

TEST(SplitTest, LeadingDelimiter) {
  auto v = split(std::string(",a,b"), ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitTest, TrailingDelimiter) {
  auto v = split(std::string("a,b,"), ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "");
}

TEST(SplitTest, ConsecutiveDelimiters) {
  auto v = split(std::string("a,,b"), ',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitTest, ComplexPattern) {
  auto v = split(std::string(",a,,b,"), ',');
  ASSERT_EQ(v.size(), 5u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "");
  EXPECT_EQ(v[3], "b");
  EXPECT_EQ(v[4], "");
}

TEST(SplitTest, WideString) {
  auto v = split(std::wstring(L"x,y,z"), L',');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], L"x");
  EXPECT_EQ(v[1], L"y");
  EXPECT_EQ(v[2], L"z");
}

TEST(SplitTest, ColonDelimiter) {
  auto v = split(std::string("/usr/bin:/usr/local/bin:/opt/bin"), ':');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "/usr/bin");
  EXPECT_EQ(v[1], "/usr/local/bin");
  EXPECT_EQ(v[2], "/opt/bin");
}

// ============================================================================
// split_to_if() — basic scenarios
// ============================================================================

TEST(SplitToIfTest, Basic) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,c"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, EmptyString) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(""), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "");
}

TEST(SplitToIfTest, NoDelimiter) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("hello"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "hello");
}

TEST(SplitToIfTest, OnlyDelimiters) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",,"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, LeadingDelimiter) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",a,b"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, TrailingDelimiter) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, ConsecutiveDelimiters) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,,b"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, ComplexPattern) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",a,,b,"), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 5u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "");
  EXPECT_EQ(v[3], "b");
  EXPECT_EQ(v[4], "");
}

// ============================================================================
// split_to_if() — compress_tokens
// ============================================================================

TEST(SplitToIfTest, CompressConsecutive) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,,b"), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, CompressLeading) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",,a,b"), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, CompressTrailing) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,,"), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, CompressBothEnds) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",a,,b,"), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
  EXPECT_EQ(v[3], "");
}

TEST(SplitToIfTest, CompressOnlyDelimiters) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",,"), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, CompressWhitespace) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a  b\tc"), [](char c) { return c == ' ' || c == '\t'; }, -1,
      true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, CompressAllWhitespace) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("   "), [](char c) { return c == ' '; }, -1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

// ============================================================================
// split_to_if() — split_count
// ============================================================================

TEST(SplitToIfTest, SplitCountZero) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,c"), [](char c) { return c == ','; }, 0);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "a,b,c");
}

TEST(SplitToIfTest, SplitCountOne) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,c"), [](char c) { return c == ','; }, 1);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,c");
}

TEST(SplitToIfTest, SplitCountTwo) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,c,d"), [](char c) { return c == ','; }, 2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c,d");
}

TEST(SplitToIfTest, SplitCountExceedsTokens) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b"), [](char c) { return c == ','; }, 10);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, SplitCountWithLeadingDelimiter) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",a,b,c"), [](char c) { return c == ','; }, 2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b,c");
}

TEST(SplitToIfTest, SplitCountWithCompress) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,,,b,,c"), [](char c) { return c == ','; }, 1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,,c");
}

TEST(SplitToIfTest, SplitCountWithCompressTrailing) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b,,"), [](char c) { return c == ','; }, 1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,,");
}

// ============================================================================
// split_to_if() — predicate varieties
// ============================================================================

static bool IsComma(char c) { return c == ','; }

TEST(SplitToIfTest, FunctionPointerPredicate) {
  auto v = split_to_if<std::vector<std::string>>(std::string("a,b,c"), IsComma);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, LambdaPredicate) {
  auto delim = ',';
  auto v = split_to_if<std::vector<std::string>>(
      std::string("x,y,z"), [delim](char c) { return c == delim; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "x");
  EXPECT_EQ(v[1], "y");
  EXPECT_EQ(v[2], "z");
}

TEST(SplitToIfTest, PredicateReturnsInt) {
  // std::convertible_to<bool> should accept int-returning predicates
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b"), [](char c) -> int { return c == ',' ? 1 : 0; });
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, PredicateReturnsPointer) {
  // pointer is convertible to bool
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b"),
      [](char c) -> const char* { return c == ',' ? "yes" : nullptr; });
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

// ============================================================================
// split_to_if() — wide character support
// ============================================================================

TEST(SplitToIfTest, WideString) {
  auto v = split_to_if<std::vector<std::wstring>>(
      std::wstring(L"w1,w2,w3"), [](wchar_t c) { return c == L','; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], L"w1");
  EXPECT_EQ(v[1], L"w2");
  EXPECT_EQ(v[2], L"w3");
}

TEST(SplitToIfTest, WideStringCompress) {
  auto v = split_to_if<std::vector<std::wstring>>(
      std::wstring(L"w1,,w2"), [](wchar_t c) { return c == L','; }, -1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], L"w1");
  EXPECT_EQ(v[1], L"w2");
}

TEST(SplitToIfTest, WideStringSplitCount) {
  auto v = split_to_if<std::vector<std::wstring>>(
      std::wstring(L"a,b,c,d"), [](wchar_t c) { return c == L','; }, 2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], L"a");
  EXPECT_EQ(v[1], L"b");
  EXPECT_EQ(v[2], L"c,d");
}

// ============================================================================
// split_to_if() — alternative container types
// ============================================================================

TEST(SplitToIfTest, SetContainer) {
  auto s = split_to_if<std::set<std::string>>(std::string("c,b,a"),
                                              [](char c) { return c == ','; });
  ASSERT_EQ(s.size(), 3u);
  EXPECT_TRUE(s.count("a"));
  EXPECT_TRUE(s.count("b"));
  EXPECT_TRUE(s.count("c"));
}

TEST(SplitToIfTest, ListContainer) {
  auto l = split_to_if<std::list<std::string>>(std::string("a,b,c"),
                                               [](char c) { return c == ','; });
  ASSERT_EQ(l.size(), 3u);
  auto it = l.begin();
  EXPECT_EQ(*it++, "a");
  EXPECT_EQ(*it++, "b");
  EXPECT_EQ(*it++, "c");
}

TEST(SplitToIfTest, DequeContainer) {
  auto d = split_to_if<std::deque<std::string>>(
      std::string("a,b,c"), [](char c) { return c == ','; });
  ASSERT_EQ(d.size(), 3u);
  EXPECT_EQ(d[0], "a");
  EXPECT_EQ(d[1], "b");
  EXPECT_EQ(d[2], "c");
}

// ============================================================================
// split_to_if() — edge cases
// ============================================================================

TEST(SplitToIfTest, SingleDelimiterChar) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(","), [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, SingleDelimiterCharCompress) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(","), [](char c) { return c == ','; }, -1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, MultiCharDelimiterPredicate) {
  // Use a predicate that matches any of several delimiter characters
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a:b;c|d"),
      [](char c) { return c == ':' || c == ';' || c == '|'; });
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
  EXPECT_EQ(v[3], "d");
}

TEST(SplitToIfTest, LongString) {
  // Build a string with many delimiters
  std::string long_str;
  for (int i = 0; i < 1000; ++i) {
    if (i > 0) {
      long_str += ',';
    }
    long_str += "token" + std::to_string(i);
  }
  auto v = split_to_if<std::vector<std::string>>(
      long_str, [](char c) { return c == ','; });
  ASSERT_EQ(v.size(), 1000u);
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(v[i], "token" + std::to_string(i));
  }
}

TEST(SplitToIfTest, LeadingEmptyWithSplitCount) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string(",a"), [](char c) { return c == ','; }, 1);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
}

TEST(SplitToIfTest, TrailingEmptyWithSplitCount) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,"), [](char c) { return c == ','; }, 1);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "");
}

// ============================================================================
// split() — PATH-like scenarios (actual library usage)
// ============================================================================

TEST(SplitTest, PathLikeColon) {
  auto v = split(std::string("/usr/bin:/bin:/usr/local/bin"), ':');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "/usr/bin");
  EXPECT_EQ(v[1], "/bin");
  EXPECT_EQ(v[2], "/usr/local/bin");
}

TEST(SplitTest, PathLikeSemicolon) {
  auto v = split(std::string("C:\\Windows;C:\\Windows\\System32"), ';');
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "C:\\Windows");
  EXPECT_EQ(v[1], "C:\\Windows\\System32");
}

TEST(SplitTest, PathLikeEmptyEntries) {
  // This happens in PATH like "/bin::/usr/bin" (empty entry = current dir)
  auto v = split(std::string("/bin::/usr/bin"), ':');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "/bin");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "/usr/bin");
}

TEST(SplitTest, PathLikeTrailingColon) {
  auto v = split(std::string("/bin:/usr/bin:"), ':');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "/bin");
  EXPECT_EQ(v[1], "/usr/bin");
  EXPECT_EQ(v[2], "");
}

TEST(SplitTest, PathLikeLeadingColon) {
  auto v = split(std::string(":/bin:/usr/bin"), ':');
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "/bin");
  EXPECT_EQ(v[2], "/usr/bin");
}

// ============================================================================
// split_to_if() — return type consistency
// ============================================================================

TEST(SplitToIfTest, ReturnTypeIsVector) {
  auto v = split_to_if<std::vector<std::string>>(
      std::string("a,b"), [](char c) { return c == ','; });
  // Verify we can use vector operations
  v.push_back("extra");
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v.back(), "extra");
}

TEST(SplitTest, ReturnTypeIsVector) {
  auto v = split(std::string("a,b"), ',');
  v.reserve(10);
  ASSERT_EQ(v.capacity(), 10u);
  EXPECT_EQ(v.size(), 2u);
}

TEST(SplitTest, WstringReturnTypeIsVector) {
  auto v = split(std::wstring(L"a,b"), L',');
  ASSERT_EQ(v.size(), 2u);
  v.push_back(L"c");
  EXPECT_EQ(v.back(), L"c");
}
