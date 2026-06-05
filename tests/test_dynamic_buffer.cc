#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "subprocess/subprocess.hpp"

using subprocess::detail::dynamic_buffer;

// ============================================================================
// Default construction
// ============================================================================

TEST(DynamicBufferTest, DefaultConstruct) {
  dynamic_buffer buf;
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.span().size(), 0u);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST(DynamicBufferTest, MoveConstruct) {
  dynamic_buffer buf1(std::string_view("hello"));
  dynamic_buffer buf2(std::move(buf1));
  // buf2 should have the data
  EXPECT_FALSE(buf2.empty());
  EXPECT_EQ(buf2.size(), 5u);
  EXPECT_EQ(buf2, std::string_view("hello"));
}

TEST(DynamicBufferTest, MoveAssign) {
  dynamic_buffer buf1(std::string_view("world"));
  dynamic_buffer buf2;
  buf2 = std::move(buf1);
  EXPECT_FALSE(buf2.empty());
  EXPECT_EQ(buf2.size(), 5u);
  EXPECT_EQ(buf2, std::string_view("world"));
}

TEST(DynamicBufferTest, SelfMoveAssign) {
  dynamic_buffer buf(std::string_view("self"));
  // Self-move assignment is a no-op due to the `if (this != &other)` guard.
  // The buffer retains its contents unchanged.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
  buf = std::move(buf);
#pragma clang diagnostic pop
  // After self-move, the buffer still holds its original data.
  EXPECT_FALSE(buf.empty());
  buf.append("recover", 7);
  EXPECT_EQ(buf, std::string_view("selfrecover"));
}

// ============================================================================
// Construction from string_view
// ============================================================================

TEST(DynamicBufferTest, FromStringView) {
  dynamic_buffer buf(std::string_view("hello world"));
  EXPECT_EQ(buf.size(), 11u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf, std::string_view("hello world"));
}

TEST(DynamicBufferTest, FromEmptyStringView) {
  dynamic_buffer buf(std::string_view(""));
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(DynamicBufferTest, FromStringViewWithNulls) {
  std::string_view sv("a\0b\0c", 5);
  dynamic_buffer buf(sv);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf, sv);
}

// ============================================================================
// Construction from string_view + callback
// ============================================================================

TEST(DynamicBufferTest, FromStringViewWithCallback) {
  size_t call_count = 0;
  auto cb = [&call_count](const unsigned char*, size_t) { ++call_count; };
  dynamic_buffer buf(std::string_view("hello"), std::move(cb));
  EXPECT_EQ(buf.size(), 5u);
  // Callback is NOT called during construction; only on append/commit
  EXPECT_EQ(call_count, 0u);
}

// ============================================================================
// Construction from iterator pair
// ============================================================================

TEST(DynamicBufferTest, FromIteratorPair) {
  std::string s = "iterator test";
  dynamic_buffer buf(s.begin(), s.end());
  EXPECT_EQ(buf.size(), s.size());
  EXPECT_EQ(buf, std::string_view("iterator test"));
}

TEST(DynamicBufferTest, FromIteratorPairEmpty) {
  std::string s;
  dynamic_buffer buf(s.begin(), s.end());
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(DynamicBufferTest, FromIteratorPairWithCallback) {
  size_t call_count = 0;
  auto cb = [&call_count](const unsigned char*, size_t) { ++call_count; };
  std::string s = "iter+cb";
  dynamic_buffer buf(s.begin(), s.end(), std::move(cb));
  EXPECT_EQ(buf.size(), 7u);
  EXPECT_EQ(buf, std::string_view("iter+cb"));
  EXPECT_EQ(call_count, 0u);
}

// ============================================================================
// Construction from contiguous container (template)
// ============================================================================

TEST(DynamicBufferTest, FromVector) {
  std::vector<int> v = {1, 2, 3, 4};
  dynamic_buffer buf(v);
  EXPECT_EQ(buf.size(), v.size() * sizeof(int));
  EXPECT_EQ(buf, std::span(v));
}

TEST(DynamicBufferTest, FromArray) {
  std::array<short, 3> arr = {10, 20, 30};
  dynamic_buffer buf(arr);
  EXPECT_EQ(buf.size(), arr.size() * sizeof(short));
  // Compare byte-for-byte via to<vector>
  auto result = buf.to<std::vector<short>>();
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], 10);
  EXPECT_EQ(result[1], 20);
  EXPECT_EQ(result[2], 30);
}

TEST(DynamicBufferTest, FromStdString) {
  std::string s = "container";
  dynamic_buffer buf(s);
  EXPECT_EQ(buf.size(), s.size());
  EXPECT_EQ(buf, std::string_view(s));
}

TEST(DynamicBufferTest, FromStdWstring) {
  std::wstring ws = L"wide";
  dynamic_buffer buf(ws);
  EXPECT_EQ(buf.size(), ws.size() * sizeof(wchar_t));
  EXPECT_EQ(buf, std::wstring_view(ws));
}

TEST(DynamicBufferTest, FromSpan) {
  std::vector<double> v = {1.1, 2.2, 3.3};
  std::span<double> sp(v);
  dynamic_buffer buf(sp);
  EXPECT_EQ(buf.size(), sp.size() * sizeof(double));
  EXPECT_EQ(buf, sp);
}

// ============================================================================
// size() / empty() / clear()
// ============================================================================

TEST(DynamicBufferTest, SizeAndEmpty) {
  dynamic_buffer buf;
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_TRUE(buf.empty());

  buf = dynamic_buffer(std::string_view("data"));
  EXPECT_EQ(buf.size(), 4u);
  EXPECT_FALSE(buf.empty());
}

TEST(DynamicBufferTest, Clear) {
  dynamic_buffer buf(std::string_view("clear me"));
  EXPECT_FALSE(buf.empty());

  buf.clear();
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

// ============================================================================
// span()
// ============================================================================

TEST(DynamicBufferTest, Span) {
  dynamic_buffer buf(std::string_view("span test"));
  auto sp = buf.span();
  EXPECT_EQ(sp.size(), buf.size());
  EXPECT_EQ(memcmp(sp.data(), "span test", 9), 0);
}

TEST(DynamicBufferTest, SpanEmpty) {
  dynamic_buffer buf;
  auto sp = buf.span();
  EXPECT_EQ(sp.size(), 0u);
}

// ============================================================================
// to<T>() / to_string()
// ============================================================================

TEST(DynamicBufferTest, ToStdString) {
  dynamic_buffer buf(std::string_view("convert to string"));
  auto result = buf.to<std::string>();
  EXPECT_EQ(result, "convert to string");
}

TEST(DynamicBufferTest, ToStringConvenience) {
  dynamic_buffer buf(std::string_view("convenience"));
  auto result = buf.to_string();
  EXPECT_EQ(result, "convenience");
}

TEST(DynamicBufferTest, ToStdWstring) {
  std::wstring ws = L"wide string";
  dynamic_buffer buf(ws);
  auto result = buf.to<std::wstring>();
  EXPECT_EQ(result, ws);
}

TEST(DynamicBufferTest, ToVectorOfInt) {
  int values[] = {10, 20, 30};
  std::vector<int> v(values, values + 3);
  dynamic_buffer buf(v);
  auto result = buf.to<std::vector<int>>();
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], 10);
  EXPECT_EQ(result[1], 20);
  EXPECT_EQ(result[2], 30);
}

TEST(DynamicBufferTest, ToInvalidSizeThrows) {
  // A buffer of 5 bytes cannot be interpreted as a vector of int (4 bytes)
  dynamic_buffer buf(std::string_view("12345"));
  EXPECT_THROW(buf.to<std::vector<int>>(), std::runtime_error);
}

TEST(DynamicBufferTest, ToEmptyBuffer) {
  dynamic_buffer buf;
  auto result = buf.to<std::string>();
  EXPECT_TRUE(result.empty());
}

// ============================================================================
// append()
// ============================================================================

TEST(DynamicBufferTest, Append) {
  dynamic_buffer buf;
  buf.append("hello", 5);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf, std::string_view("hello"));

  buf.append(" world", 6);
  EXPECT_EQ(buf.size(), 11u);
  EXPECT_EQ(buf, std::string_view("hello world"));
}

TEST(DynamicBufferTest, AppendEmpty) {
  dynamic_buffer buf(std::string_view("data"));
  buf.append(nullptr, 0);
  EXPECT_EQ(buf.size(), 4u);
  EXPECT_EQ(buf, std::string_view("data"));
}

TEST(DynamicBufferTest, AppendCallbackCalled) {
  size_t call_count = 0;
  std::string last_data;
  size_t last_size = 0;

  auto cb = [&](const unsigned char* data, size_t size) {
    ++call_count;
    last_data.assign(reinterpret_cast<const char*>(data), size);
    last_size = size;
  };

  dynamic_buffer buf(std::move(cb));
  buf.append("abc", 3);
  EXPECT_EQ(call_count, 1u);
  EXPECT_EQ(last_data, "abc");
  EXPECT_EQ(last_size, 3u);

  buf.append("def", 3);
  EXPECT_EQ(call_count, 2u);
  EXPECT_EQ(last_data, "def");
  EXPECT_EQ(last_size, 3u);
}

// ============================================================================
// prepare() / commit()
// ============================================================================

TEST(DynamicBufferTest, PrepareAndCommit) {
  dynamic_buffer buf;

  auto write_span = buf.prepare(10);
  EXPECT_GE(write_span.size(), 10u);

  // Write some data into the prepared buffer
  memcpy(write_span.data(), "0123456789", 10);

  buf.commit(5);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf, std::string_view("01234"));

  buf.commit(3);
  EXPECT_EQ(buf.size(), 8u);
  EXPECT_EQ(buf, std::string_view("01234012"));
}

TEST(DynamicBufferTest, PrepareReuse) {
  dynamic_buffer buf;

  // First prepare; then request larger size; internal buffer should grow
  buf.prepare(5);
  auto sp2 = buf.prepare(100);
  EXPECT_GE(sp2.size(), 100u);
}

TEST(DynamicBufferTest, CommitExact) {
  dynamic_buffer buf;
  auto sp = buf.prepare(8);
  memcpy(sp.data(), "exactfit", 8);
  buf.commit(8);
  EXPECT_EQ(buf.size(), 8u);
  EXPECT_EQ(buf, std::string_view("exactfit"));
}

TEST(DynamicBufferTest, CommitZero) {
  dynamic_buffer buf;
  buf.prepare(10);
  // Committing zero bytes should be a no-op (no data appended).
  buf.commit(0);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(DynamicBufferTest, CommitZeroWithCallback) {
  size_t call_count = 0;
  size_t last_size = 1;  // non-zero sentinel
  auto cb = [&](const unsigned char*, size_t s) {
    ++call_count;
    last_size = s;
  };
  dynamic_buffer buf(std::move(cb));
  buf.prepare(8);
  buf.commit(0);
  // callback fires even for zero-byte commit.
  EXPECT_EQ(call_count, 1u);
  EXPECT_EQ(last_size, 0u);
  EXPECT_TRUE(buf.empty());
}

TEST(DynamicBufferTest, PrepareZero) {
  dynamic_buffer buf;
  auto sp = buf.prepare(0);
  EXPECT_EQ(sp.size(), 0u);
  // prepare(0) returns an empty span; data() may be nullptr if the
  // internal buffer has not been allocated yet. Both are valid.
}

TEST(DynamicBufferTest, PrepareAndCommitCallbackCalled) {
  size_t call_count = 0;
  auto cb = [&call_count](const unsigned char*, size_t) { ++call_count; };

  dynamic_buffer buf(std::move(cb));
  auto sp = buf.prepare(5);
  memcpy(sp.data(), "hello", 5);
  buf.commit(3);

  EXPECT_EQ(call_count, 1u);
  EXPECT_EQ(buf, std::string_view("hel"));
}

// ============================================================================
// consume()
// ============================================================================

TEST(DynamicBufferTest, ConsumePartial) {
  dynamic_buffer buf(std::string_view("hello world"));
  buf.consume(6);
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf, std::string_view("world"));
}

TEST(DynamicBufferTest, ConsumeAll) {
  dynamic_buffer buf(std::string_view("remove all"));
  buf.consume(buf.size());
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

TEST(DynamicBufferTest, ConsumeZero) {
  dynamic_buffer buf(std::string_view("no change"));
  buf.consume(0);
  EXPECT_EQ(buf.size(), 9u);
  EXPECT_EQ(buf, std::string_view("no change"));
}

TEST(DynamicBufferTest, ConsumeZeroEmptyBuffer) {
  dynamic_buffer buf;
  // Consuming zero from an empty buffer is safe (erase with same begin/end).
  buf.consume(0);
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
}

// ============================================================================
// operator== with dynamic_buffer
// ============================================================================

TEST(DynamicBufferTest, EqualsAnotherDynamicBuffer) {
  dynamic_buffer a(std::string_view("same"));
  dynamic_buffer b(std::string_view("same"));
  EXPECT_TRUE(a == b);

  dynamic_buffer c(std::string_view("different"));
  EXPECT_FALSE(a == c);
}

// ============================================================================
// operator== with std::span
// ============================================================================

TEST(DynamicBufferTest, EqualsSpan) {
  dynamic_buffer buf(std::string_view("span compare"));
  std::string_view sv("span compare");
  std::span<const char> sp(sv.data(), sv.size());
  EXPECT_TRUE(buf == sp);

  std::string_view diff("different");
  std::span<const char> sp2(diff.data(), diff.size());
  EXPECT_FALSE(buf == sp2);
}

TEST(DynamicBufferTest, EqualsSpanInt) {
  std::vector<int> v1 = {1, 2, 3};
  std::vector<int> v2 = {1, 2, 3};
  std::vector<int> v3 = {4, 5, 6};

  dynamic_buffer buf(v1);
  EXPECT_TRUE(buf == std::span(v2));
  EXPECT_FALSE(buf == std::span(v3));
}

// ============================================================================
// operator== with basic_string_view
// ============================================================================

TEST(DynamicBufferTest, EqualsStringView) {
  dynamic_buffer buf(std::string_view("string view eq"));
  EXPECT_TRUE(buf == std::string_view("string view eq"));
  EXPECT_FALSE(buf == std::string_view("nope"));
}

TEST(DynamicBufferTest, EqualsWstringView) {
  dynamic_buffer buf(std::wstring_view(L"wide eq"));
  EXPECT_TRUE(buf == std::wstring_view(L"wide eq"));
  EXPECT_FALSE(buf == std::wstring_view(L"wide no"));
}

TEST(DynamicBufferTest, EqualsU8stringView) {
  dynamic_buffer buf(std::string_view("u8"));
  EXPECT_TRUE(buf == std::u8string_view(u8"u8"));
  EXPECT_FALSE(buf == std::u8string_view(u8"no"));
}

TEST(DynamicBufferTest, EqualsU16stringView) {
  std::u16string s16 = u"16bit";
  dynamic_buffer buf(s16);
  EXPECT_TRUE(buf == std::u16string_view(s16));
}

TEST(DynamicBufferTest, EqualsU32stringView) {
  std::u32string s32 = U"32bit";
  dynamic_buffer buf(s32);
  EXPECT_TRUE(buf == std::u32string_view(s32));
}

// ============================================================================
// operator== with basic_string
// ============================================================================

TEST(DynamicBufferTest, EqualsStdString) {
  dynamic_buffer buf(std::string_view("string compare"));
  std::string s("string compare");
  EXPECT_TRUE(buf == s);

  std::string diff("other");
  EXPECT_FALSE(buf == diff);
}

TEST(DynamicBufferTest, EqualsStdWstring) {
  dynamic_buffer buf(std::wstring_view(L"wstring comp"));
  std::wstring ws(L"wstring comp");
  EXPECT_TRUE(buf == ws);

  std::wstring diff(L"other");
  EXPECT_FALSE(buf == diff);
}

// ============================================================================
// operator== with raw pointer
// ============================================================================

TEST(DynamicBufferTest, EqualsCharPointer) {
  dynamic_buffer buf(std::string_view("ptr test"));
  EXPECT_TRUE(buf == "ptr test");
  EXPECT_FALSE(buf == "different");
}

TEST(DynamicBufferTest, EqualsWcharPointer) {
  dynamic_buffer buf(std::wstring_view(L"wptr"));
  EXPECT_TRUE(buf == L"wptr");
  EXPECT_FALSE(buf == L"other");
}

TEST(DynamicBufferTest, EqualsChar8Pointer) {
  dynamic_buffer buf(std::string_view("u8ptr"));
  EXPECT_TRUE(buf == u8"u8ptr");
  EXPECT_FALSE(buf == u8"other");
}

TEST(DynamicBufferTest, EqualsChar16Pointer) {
  std::u16string s16 = u"16ptr";
  dynamic_buffer buf(s16);
  EXPECT_TRUE(buf == u"16ptr");
}

TEST(DynamicBufferTest, EqualsChar32Pointer) {
  std::u32string s32 = U"32ptr";
  dynamic_buffer buf(s32);
  EXPECT_TRUE(buf == U"32ptr");
}

// ============================================================================
// operator!= (via negation of ==)
// ============================================================================

TEST(DynamicBufferTest, NotEquals) {
  dynamic_buffer a(std::string_view("aaa"));
  dynamic_buffer b(std::string_view("bbb"));
  EXPECT_FALSE(a == b);

  EXPECT_FALSE(a == std::string_view("bbb"));
  EXPECT_FALSE(a == std::string("bbb"));
  EXPECT_FALSE(a == std::span<const char>("bbb", 3));
}

// ============================================================================
// Non-copyable verification
// ============================================================================

TEST(DynamicBufferTest, NonCopyable) {
  static_assert(!std::is_copy_constructible_v<dynamic_buffer>);
  static_assert(!std::is_copy_assignable_v<dynamic_buffer>);
  static_assert(std::is_move_constructible_v<dynamic_buffer>);
  static_assert(std::is_move_assignable_v<dynamic_buffer>);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(DynamicBufferTest, LargeAppend) {
  dynamic_buffer buf;
  std::vector<char> large_data(10000, 'X');
  buf.append(large_data.data(), large_data.size());
  EXPECT_EQ(buf.size(), 10000u);
  EXPECT_EQ(buf, std::string_view(large_data.data(), large_data.size()));
}

TEST(DynamicBufferTest, AppendAfterConsume) {
  dynamic_buffer buf(std::string_view("abcdef"));
  buf.consume(3);
  EXPECT_EQ(buf, std::string_view("def"));

  buf.append("xyz", 3);
  EXPECT_EQ(buf.size(), 6u);
  EXPECT_EQ(buf, std::string_view("defxyz"));
}

TEST(DynamicBufferTest, PrepareCommitThenAppend) {
  dynamic_buffer buf;
  auto sp = buf.prepare(4);
  memcpy(sp.data(), "prep", 4);
  buf.commit(4);

  buf.append("append", 6);
  EXPECT_EQ(buf.size(), 10u);
  EXPECT_EQ(buf, std::string_view("prepappend"));
}

TEST(DynamicBufferTest, StringWithEmbeddedNulls) {
  const char data[] = {'h', 'e', '\0', 'l', 'o'};
  dynamic_buffer buf;
  buf.append(data, 5);
  EXPECT_EQ(buf.size(), 5u);
  // Compare raw bytes, not as C-string
  EXPECT_EQ(buf, std::string_view(data, 5));
}

TEST(DynamicBufferTest, CallbackWithNullptrCallbackNoCrash) {
  // Construct with no callback; append/commit should not crash
  dynamic_buffer buf;
  buf.append("test", 4);
  EXPECT_EQ(buf.size(), 4u);

  buf.prepare(4);
  buf.commit(2);
  EXPECT_EQ(buf.size(), 6u);
}
