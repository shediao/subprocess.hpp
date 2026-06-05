#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "subprocess/subprocess.hpp"

using subprocess::detail::const_buffer;

// ============================================================================
// Default construction
// ============================================================================

TEST(ConstBufferTest, DefaultConstruct) {
  // Value-initialization zero-initializes the members since the default
  // constructor is = default and no in-class initializers are provided.
  const_buffer buf{};
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  // data() may be nullptr when size is 0 — both are valid.
}

// ============================================================================
// Construction from std::string_view
// ============================================================================

TEST(ConstBufferTest, FromStringView) {
  std::string_view sv = "hello world";
  const_buffer buf(sv);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), sv.size());
  EXPECT_EQ(memcmp(buf.data(), sv.data(), sv.size()), 0);
}

TEST(ConstBufferTest, FromEmptyStringView) {
  std::string_view sv;
  const_buffer buf(sv);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(ConstBufferTest, FromStringViewWithNulls) {
  std::string_view sv("a\0b\0c", 5);
  const_buffer buf(sv);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(memcmp(buf.data(), sv.data(), 5), 0);
}

// ============================================================================
// Construction from std::wstring_view
// ============================================================================

TEST(ConstBufferTest, FromWstringView) {
  std::wstring_view wsv = L"hello";
  const_buffer buf(wsv);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), wsv.size() * sizeof(wchar_t));
  EXPECT_EQ(memcmp(buf.data(), wsv.data(), buf.size()), 0);
}

TEST(ConstBufferTest, FromEmptyWstringView) {
  std::wstring_view wsv;
  const_buffer buf(wsv);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

// ============================================================================
// Construction from raw pointer + size
// ============================================================================

TEST(ConstBufferTest, FromPointerAndSize) {
  const char data[] = "raw data";
  const_buffer buf(data, sizeof(data));
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.size(), sizeof(data));
  EXPECT_EQ(memcmp(buf.data(), data, sizeof(data)), 0);
}

TEST(ConstBufferTest, FromNullPointerAndZeroSize) {
  const_buffer buf(nullptr, 0);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(ConstBufferTest, FromPointerNonCharData) {
  int numbers[] = {1, 2, 3, 4};
  const_buffer buf(numbers, sizeof(numbers));
  EXPECT_EQ(buf.size(), sizeof(numbers));
  EXPECT_EQ(memcmp(buf.data(), numbers, sizeof(numbers)), 0);
}

// ============================================================================
// Construction from contiguous container (template constructor)
// ============================================================================

TEST(ConstBufferTest, FromVector) {
  std::vector<int> v = {10, 20, 30};
  const_buffer buf(v);
  EXPECT_EQ(buf.size(), v.size() * sizeof(int));
  EXPECT_EQ(memcmp(buf.data(), v.data(), buf.size()), 0);
}

TEST(ConstBufferTest, FromEmptyVector) {
  std::vector<char> v;
  const_buffer buf(v);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(ConstBufferTest, FromArray) {
  std::array<short, 4> arr = {1, 2, 3, 4};
  const_buffer buf(arr);
  EXPECT_EQ(buf.size(), arr.size() * sizeof(short));
  EXPECT_EQ(memcmp(buf.data(), arr.data(), buf.size()), 0);
}

TEST(ConstBufferTest, FromStdString) {
  std::string s = "container string";
  const_buffer buf(s);
  EXPECT_EQ(buf.size(), s.size());
  EXPECT_EQ(memcmp(buf.data(), s.data(), s.size()), 0);
}

TEST(ConstBufferTest, FromStdWstring) {
  std::wstring ws = L"wide container";
  const_buffer buf(ws);
  EXPECT_EQ(buf.size(), ws.size() * sizeof(wchar_t));
  EXPECT_EQ(memcmp(buf.data(), ws.data(), buf.size()), 0);
}

TEST(ConstBufferTest, FromSpan) {
  int arr[] = {5, 6, 7, 8};
  std::span<int> sp(arr);
  const_buffer buf(sp);
  EXPECT_EQ(buf.size(), sp.size() * sizeof(int));
  EXPECT_EQ(memcmp(buf.data(), sp.data(), buf.size()), 0);
}

// ============================================================================
// data() / size() / empty() consistency
// ============================================================================

TEST(ConstBufferTest, DataReturnsCorrectPointer) {
  const char* str = "pointer check";
  const_buffer buf{std::string_view(str)};
  EXPECT_EQ(buf.data(), reinterpret_cast<const unsigned char*>(str));
}

TEST(ConstBufferTest, SizeMatchesRawBytes) {
  int values[] = {100, 200, 300};
  const_buffer buf(values, sizeof(values));
  EXPECT_EQ(buf.size(), 3 * sizeof(int));
}

TEST(ConstBufferTest, EmptyConsistency) {
  const_buffer empty_buf{};
  EXPECT_TRUE(empty_buf.empty());
  EXPECT_EQ(empty_buf.size(), 0u);

  const_buffer non_empty(std::string_view("x"));
  EXPECT_FALSE(non_empty.empty());
  EXPECT_GT(non_empty.size(), 0u);
}

// ============================================================================
// constexpr usage
// ============================================================================

TEST(ConstBufferTest, ConstexprDefaultConstruct) {
  constexpr const_buffer buf{};
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(ConstBufferTest, ConstexprFromStringView) {
  constexpr const_buffer buf(std::string_view("abc"));
  EXPECT_EQ(buf.size(), 3u);
  EXPECT_FALSE(buf.empty());
}

TEST(ConstBufferTest, ConstexprFromPointerSize) {
  constexpr const_buffer buf(static_cast<const void*>(nullptr), 0);
  EXPECT_TRUE(buf.empty());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ConstBufferTest, FromSpanOfConstInt) {
  const int arr[] = {10, 20, 30};
  std::span<const int> sp(arr);
  const_buffer buf(sp);
  EXPECT_EQ(buf.size(), 3 * sizeof(int));
  EXPECT_EQ(memcmp(buf.data(), arr, sizeof(arr)), 0);
}

TEST(ConstBufferTest, FromVectorOfDouble) {
  std::vector<double> v = {1.5, 2.5, 3.5};
  const_buffer buf(v);
  EXPECT_EQ(buf.size(), v.size() * sizeof(double));
  EXPECT_EQ(memcmp(buf.data(), v.data(), buf.size()), 0);
}

TEST(ConstBufferTest, PointerArithmeticOnData) {
  const char data[] = "abcdef";
  const_buffer buf(data, 6);
  // data() returns const unsigned char*, verify we can do pointer arithmetic
  EXPECT_EQ(buf.data()[0], 'a');
  EXPECT_EQ(buf.data()[5], 'f');
  EXPECT_EQ(static_cast<const void*>(buf.data() + 3),
            static_cast<const void*>(data + 3));
}

TEST(ConstBufferTest, SizeIsInBytesNotElements) {
  std::vector<int> v = {1, 2, 3};
  const_buffer buf(v);
  EXPECT_EQ(buf.size(), v.size() * sizeof(int));
  // NOT v.size() (3) but 3 * sizeof(int)
  EXPECT_NE(buf.size(), v.size());  // unless sizeof(int) == 1
}

TEST(ConstBufferTest, DataConsistencyAfterDefaultConstruction) {
  // Default-constructed const_buffer has size 0 and nullptr data.
  // Both are valid; empty() is the canonical emptiness check.
  constexpr const_buffer buf{};
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  // Whether buf.data() is nullptr or not is unspecified for default
  // construction; we only verify that size is 0 and empty is true.
}
