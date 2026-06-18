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

static auto constexpr size_t_max = (std::numeric_limits<std::size_t>::max)();

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
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,c"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, EmptyString) {
  std::vector<std::string> v;
  split_to_if(
      std::string(""), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "");
}

TEST(SplitToIfTest, NoDelimiter) {
  std::vector<std::string> v;
  split_to_if(
      std::string("hello"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "hello");
}

TEST(SplitToIfTest, OnlyDelimiters) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",,"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, LeadingDelimiter) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",a,b"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, TrailingDelimiter) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, ConsecutiveDelimiters) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,,b"), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, ComplexPattern) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",a,,b,"), [](char c) { return c == ','; }, back_inserter(v));
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
  std::vector<std::string> v;
  split_to_if(
      std::string("a,,b"), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, CompressLeading) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",,a,b"), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
}

TEST(SplitToIfTest, CompressTrailing) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,,"), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "");
}

TEST(SplitToIfTest, CompressBothEnds) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",a,,b,"), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b");
  EXPECT_EQ(v[3], "");
}

TEST(SplitToIfTest, CompressOnlyDelimiters) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",,"), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, CompressWhitespace) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a  b\tc"), [](char c) { return c == ' ' || c == '\t'; },
      back_inserter(v), size_t_max, true);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, CompressAllWhitespace) {
  std::vector<std::string> v;
  split_to_if(
      std::string("   "), [](char c) { return c == ' '; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

// ============================================================================
// split_to_if() — split_count
// ============================================================================

TEST(SplitToIfTest, SplitCountZero) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,c"), [](char c) { return c == ','; }, back_inserter(v),
      0);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], "a,b,c");
}

TEST(SplitToIfTest, SplitCountOne) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,c"), [](char c) { return c == ','; }, back_inserter(v),
      1);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,c");
}

TEST(SplitToIfTest, SplitCountTwo) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,c,d"), [](char c) { return c == ','; }, back_inserter(v),
      2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c,d");
}

TEST(SplitToIfTest, SplitCountExceedsTokens) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b"), [](char c) { return c == ','; }, back_inserter(v),
      10);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, SplitCountWithLeadingDelimiter) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",a,b,c"), [](char c) { return c == ','; }, back_inserter(v),
      2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
  EXPECT_EQ(v[2], "b,c");
}

TEST(SplitToIfTest, SplitCountWithCompress) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,,,b,,c"), [](char c) { return c == ','; },
      back_inserter(v), 1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,,c");
}

TEST(SplitToIfTest, SplitCountWithCompressTrailing) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b,,"), [](char c) { return c == ','; }, back_inserter(v),
      1, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b,,");
}

// ============================================================================
// split_to_if() — predicate varieties
// ============================================================================

static bool IsComma(char c) { return c == ','; }

TEST(SplitToIfTest, FunctionPointerPredicate) {
  std::vector<std::string> v;
  split_to_if(std::string("a,b,c"), IsComma, back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
  EXPECT_EQ(v[2], "c");
}

TEST(SplitToIfTest, LambdaPredicate) {
  auto delim = ',';
  std::vector<std::string> v;
  split_to_if(
      std::string("x,y,z"), [delim](char c) { return c == delim; },
      back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], "x");
  EXPECT_EQ(v[1], "y");
  EXPECT_EQ(v[2], "z");
}

TEST(SplitToIfTest, PredicateReturnsInt) {
  // std::convertible_to<bool> should accept int-returning predicates
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b"), [](char c) -> int { return c == ',' ? 1 : 0; },
      back_inserter(v));
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

TEST(SplitToIfTest, PredicateReturnsPointer) {
  // pointer is convertible to bool
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b"),
      [](char c) -> const char* { return c == ',' ? "yes" : nullptr; },
      back_inserter(v));
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "a");
  EXPECT_EQ(v[1], "b");
}

// ============================================================================
// split_to_if() — wide character support
// ============================================================================

TEST(SplitToIfTest, WideString) {
  std::vector<std::wstring> v;
  split_to_if(
      std::wstring(L"w1,w2,w3"), [](wchar_t c) { return c == L','; },
      back_inserter(v));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], L"w1");
  EXPECT_EQ(v[1], L"w2");
  EXPECT_EQ(v[2], L"w3");
}

TEST(SplitToIfTest, WideStringCompress) {
  std::vector<std::wstring> v;
  split_to_if(
      std::wstring(L"w1,,w2"), [](wchar_t c) { return c == L','; },
      back_inserter(v), size_t_max, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], L"w1");
  EXPECT_EQ(v[1], L"w2");
}

TEST(SplitToIfTest, WideStringSplitCount) {
  std::vector<std::wstring> v;
  split_to_if(
      std::wstring(L"a,b,c,d"), [](wchar_t c) { return c == L','; },
      back_inserter(v), 2);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], L"a");
  EXPECT_EQ(v[1], L"b");
  EXPECT_EQ(v[2], L"c,d");
}

// ============================================================================
// split_to_if() — alternative container types
// ============================================================================

TEST(SplitToIfTest, SetContainer) {
  std::set<std::string> s;
  split_to_if(
      std::string("c,b,a"), [](char c) { return c == ','; },
      std::inserter(s, s.end()));
  ASSERT_EQ(s.size(), 3u);
  EXPECT_TRUE(s.count("a"));
  EXPECT_TRUE(s.count("b"));
  EXPECT_TRUE(s.count("c"));
}

TEST(SplitToIfTest, ListContainer) {
  std::list<std::string> l;
  split_to_if(
      std::string("a,b,c"), [](char c) { return c == ','; },
      std::inserter(l, l.end()));
  ASSERT_EQ(l.size(), 3u);
  auto it = l.begin();
  EXPECT_EQ(*it++, "a");
  EXPECT_EQ(*it++, "b");
  EXPECT_EQ(*it++, "c");
}

TEST(SplitToIfTest, DequeContainer) {
  std::deque<std::string> d;
  split_to_if(
      std::string("a,b,c"), [](char c) { return c == ','; },
      std::inserter(d, d.end()));
  ASSERT_EQ(d.size(), 3u);
  EXPECT_EQ(d[0], "a");
  EXPECT_EQ(d[1], "b");
  EXPECT_EQ(d[2], "c");
}

// ============================================================================
// split_to_if() — edge cases
// ============================================================================

TEST(SplitToIfTest, SingleDelimiterChar) {
  std::vector<std::string> v;
  split_to_if(
      std::string(","), [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, SingleDelimiterCharCompress) {
  std::vector<std::string> v;
  split_to_if(
      std::string(","), [](char c) { return c == ','; }, back_inserter(v),
      size_t_max, true);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "");
}

TEST(SplitToIfTest, MultiCharDelimiterPredicate) {
  // Use a predicate that matches any of several delimiter characters
  std::vector<std::string> v;
  split_to_if(
      std::string("a:b;c|d"),
      [](char c) { return c == ':' || c == ';' || c == '|'; },
      back_inserter(v));
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
  std::vector<std::string> v;
  split_to_if(long_str, [](char c) { return c == ','; }, back_inserter(v));
  ASSERT_EQ(v.size(), 1000u);
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(v[i], "token" + std::to_string(i));
  }
}

TEST(SplitToIfTest, LeadingEmptyWithSplitCount) {
  std::vector<std::string> v;
  split_to_if(
      std::string(",a"), [](char c) { return c == ','; }, back_inserter(v), 1);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], "");
  EXPECT_EQ(v[1], "a");
}

TEST(SplitToIfTest, TrailingEmptyWithSplitCount) {
  std::vector<std::string> v;
  split_to_if(
      std::string("a,"), [](char c) { return c == ','; }, back_inserter(v), 1);
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
  std::vector<std::string> v;
  split_to_if(
      std::string("a,b"), [](char c) { return c == ','; }, back_inserter(v));
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
