#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::dynamic_buffer;
using subprocess::run;

// ===========================================================================
// 1. Basic callback — no initial data, verify callback is invoked
// ===========================================================================
TEST(BufferCallbackTest, CallbackInvokedOnAppend) {
  std::vector<std::pair<const unsigned char*, size_t>> chunks;
  dynamic_buffer buf([&chunks](const unsigned char* data, size_t size) {
    chunks.emplace_back(data, size);
  });

  std::string data = "hello";
  buf.append(data.data(), data.size());

  ASSERT_EQ(chunks.size(), 1u);
  ASSERT_EQ(chunks[0].second, 5u);
  ASSERT_EQ(std::string(reinterpret_cast<const char*>(chunks[0].first),
                        chunks[0].second),
            "hello");
}

// ===========================================================================
// 2. Callback with multiple append calls — receives each chunk separately
// ===========================================================================
TEST(BufferCallbackTest, CallbackReceivesEachChunkSeparately) {
  std::vector<std::string> received;
  dynamic_buffer buf([&received](const unsigned char* data, size_t size) {
    received.emplace_back(reinterpret_cast<const char*>(data), size);
  });

  buf.append("chunk1", 6);
  buf.append("chunk2", 6);
  buf.append("chunk3", 6);

  ASSERT_EQ(received.size(), 3u);
  EXPECT_EQ(received[0], "chunk1");
  EXPECT_EQ(received[1], "chunk2");
  EXPECT_EQ(received[2], "chunk3");
}

// ===========================================================================
// 3. Constructor with string_view + callback — initial data stored, no
//    callback triggered for initial data
// ===========================================================================
TEST(BufferCallbackTest, StringViewConstructorWithCallback) {
  std::vector<size_t> sizes;
  std::string initial = "initial_data";
  dynamic_buffer buf(
      std::string_view(initial),
      [&sizes](const unsigned char*, size_t size) { sizes.push_back(size); });

  // Initial data should be stored but NOT trigger the callback
  EXPECT_EQ(buf.to_string(), "initial_data");
  EXPECT_TRUE(sizes.empty());

  // Appending should trigger the callback
  buf.append("_extra", 6);
  ASSERT_EQ(sizes.size(), 1u);
  EXPECT_EQ(sizes[0], 6u);
  EXPECT_EQ(buf.to_string(), "initial_data_extra");
}

// ===========================================================================
// 4. Constructor with iterator pair + callback
// ===========================================================================
TEST(BufferCallbackTest, IteratorConstructorWithCallback) {
  std::vector<std::string> chunks;
  std::string data = "iterator_data";
  dynamic_buffer buf(data.begin(), data.end(),
                     [&chunks](const unsigned char* d, size_t s) {
                       chunks.emplace_back(reinterpret_cast<const char*>(d), s);
                     });

  EXPECT_EQ(buf.to_string(), "iterator_data");
  EXPECT_TRUE(chunks.empty());

  buf.append("_more", 5);
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0], "_more");
  EXPECT_EQ(buf.to_string(), "iterator_data_more");
}

// ===========================================================================
// 5. Callback-only constructor — dynamic_buffer starts empty
// ===========================================================================
TEST(BufferCallbackTest, CallbackOnlyConstructor) {
  size_t total_bytes = 0;
  dynamic_buffer buf([&total_bytes](const unsigned char*, size_t size) {
    total_bytes += size;
  });

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(total_bytes, 0u);

  buf.append("abcdef", 4);
  EXPECT_EQ(total_bytes, 4u);
  EXPECT_FALSE(buf.empty());
  EXPECT_EQ(buf.to_string(), "abcd");
}

// ===========================================================================
// 6. No callback — backward compatibility, append should work normally
// ===========================================================================
TEST(BufferCallbackTest, NoCallbackBackwardCompatibility) {
  dynamic_buffer buf;
  buf.append("test", 4);
  EXPECT_EQ(buf.to_string(), "test");
  EXPECT_EQ(buf.size(), 4u);

  dynamic_buffer buf2(std::string_view("hello"));
  buf2.append(" world", 6);
  EXPECT_EQ(buf2.to_string(), "hello world");

  std::string s = "iterator";
  dynamic_buffer buf3(s.begin(), s.end());
  buf3.append("_test", 5);
  EXPECT_EQ(buf3.to_string(), "iterator_test");
}

// ===========================================================================
// 7. Callback receives correct binary data (including null bytes)
// ===========================================================================
TEST(BufferCallbackTest, CallbackWithBinaryData) {
  std::vector<unsigned char> received;
  dynamic_buffer buf([&received](const unsigned char* data, size_t size) {
    received.insert(received.end(), data, data + size);
  });

  const unsigned char binary[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00};
  buf.append(reinterpret_cast<const char*>(binary), sizeof(binary));

  ASSERT_EQ(received.size(), sizeof(binary));
  for (size_t i = 0; i < sizeof(binary); ++i) {
    EXPECT_EQ(received[i], binary[i]) << "Mismatch at index " << i;
  }
}

// ===========================================================================
// 8. Multiple buffers with independent callbacks
// ===========================================================================
TEST(BufferCallbackTest, MultipleBuffersIndependentCallbacks) {
  std::vector<std::string> log_a, log_b;

  dynamic_buffer buf_a([&log_a](const unsigned char* data, size_t size) {
    log_a.emplace_back(reinterpret_cast<const char*>(data), size);
  });
  dynamic_buffer buf_b([&log_b](const unsigned char* data, size_t size) {
    log_b.emplace_back(reinterpret_cast<const char*>(data), size);
  });

  buf_a.append("AAA", 3);
  buf_b.append("BBB", 3);
  buf_a.append("CCC", 3);
  buf_b.append("DDD", 3);

  ASSERT_EQ(log_a.size(), 2u);
  EXPECT_EQ(log_a[0], "AAA");
  EXPECT_EQ(log_a[1], "CCC");

  ASSERT_EQ(log_b.size(), 2u);
  EXPECT_EQ(log_b[0], "BBB");
  EXPECT_EQ(log_b[1], "DDD");

  // Buffers' stored data should also be independent
  EXPECT_EQ(buf_a.to_string(), "AAACCC");
  EXPECT_EQ(buf_b.to_string(), "BBBDDD");
}

// ===========================================================================
// 9. clear() does not reset the callback — callback still works after clear
// ===========================================================================
TEST(BufferCallbackTest, CallbackPersistsAfterClear) {
  int call_count = 0;
  dynamic_buffer buf(
      [&call_count](const unsigned char*, size_t) { ++call_count; });

  buf.append("first", 5);
  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(buf.to_string(), "first");

  buf.clear();
  EXPECT_EQ(call_count, 1);
  EXPECT_TRUE(buf.empty());

  buf.append("second", 6);
  EXPECT_EQ(call_count, 2);
  EXPECT_EQ(buf.to_string(), "second");
}

// ===========================================================================
// 10. Callback that accumulates only line counts (practical use-case)
// ===========================================================================
TEST(BufferCallbackTest, CallbackLineCounter) {
  int line_count = 0;
  dynamic_buffer buf([&line_count](const unsigned char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (data[i] == '\n') {
        ++line_count;
      }
    }
  });

  buf.append("line1\nline2\n", 12);
  EXPECT_EQ(line_count, 2);

  buf.append("line3\npartial", 13);
  EXPECT_EQ(line_count, 3);

  buf.append("_end\n", 5);
  EXPECT_EQ(line_count, 4);
}

// ===========================================================================
// 11. Callback receives data in the same order as append calls
// ===========================================================================
TEST(BufferCallbackTest, CallbackPreservesAppendOrder) {
  std::string concatenated;
  dynamic_buffer buf([&concatenated](const unsigned char* data, size_t size) {
    concatenated.append(reinterpret_cast<const char*>(data), size);
  });

  for (int i = 0; i < 10; ++i) {
    std::string chunk = "chunk_" + std::to_string(i) + ";";
    buf.append(chunk.data(), chunk.size());
  }

  std::string expected;
  for (int i = 0; i < 10; ++i) {
    expected += "chunk_" + std::to_string(i) + ";";
  }

  EXPECT_EQ(concatenated, expected);
  EXPECT_EQ(buf.to_string(), expected);
}

// ===========================================================================
// 12. Integration: dynamic_buffer callback with real subprocess to capture
// streaming
//     stdout, verifying the callback sees each chunk as it arrives
// ===========================================================================
TEST(BufferCallbackTest, IntegrationWithSubprocessStdout) {
  std::vector<std::string> chunks;
  dynamic_buffer out([&chunks](const unsigned char* data, size_t size) {
    chunks.emplace_back(reinterpret_cast<const char*>(data), size);
  });

#if !defined(_WIN32)
  int ret = run("bash", "-c", "echo -n hello_world", std_out > out);
#else
  int ret = run(TEXT("cmd.exe"), TEXT("/c"),
                TEXT("<nul set /p=hello_world&exit /b 0"), std_out > out);
#endif

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(out.to_string(), "hello_world");
  EXPECT_FALSE(chunks.empty());

  // Reconstruct the full output from chunks
  std::string full;
  for (const auto& c : chunks) {
    full += c;
  }
  EXPECT_EQ(full, "hello_world");
}

// ===========================================================================
// 13. Integration: dynamic_buffer callback with real subprocess, capturing
// stderr
// ===========================================================================
TEST(BufferCallbackTest, IntegrationWithSubprocessStderr) {
  size_t total_bytes = 0;
  dynamic_buffer err([&total_bytes](const unsigned char*, size_t size) {
    total_bytes += size;
  });

#if !defined(_WIN32)
  int ret = run("bash", "-c", "echo -n error_msg >&2", std_err > err);
#else
  int ret = run(TEXT("cmd.exe"), TEXT("/c"),
                TEXT("<nul set /p=error_msg>&2&exit /b 0"), std_err > err);
#endif

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(err.to_string(), "error_msg");
  EXPECT_EQ(total_bytes, err.size());
}

// ===========================================================================
// 14. Integration: dynamic_buffer callback with stdin — verify callback is NOT
//     triggered for data passed into a subprocess via stdin
// ===========================================================================
TEST(BufferCallbackTest, IntegrationWithSubprocessStdin) {
  int callback_count = 0;
  std::string in_data_view{"hello_stdin"};
  dynamic_buffer in(
      in_data_view,
      [&callback_count](const unsigned char*, size_t) { ++callback_count; });
  dynamic_buffer out;

  // The stdin buffer's callback should NOT be invoked when the subprocess
  // reads from it — callbacks only fire on append(). The initial data
  // provided via constructor also does not trigger the callback.
  EXPECT_EQ(callback_count, 0);

#if !defined(_WIN32)
  int ret = run("/bin/cat", "-", std_in<in, std_out> out);
#else
  int ret = run(TEXT("more.com"), std_in<in, std_out> out);
#endif

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(callback_count, 0);  // No new data was appended to 'in'
  EXPECT_EQ(in_data_view, "hello_stdin");
  EXPECT_TRUE(in.empty());
}

// ===========================================================================
// 15. Integration: dynamic_buffer callback with a large output to verify
// callback
//     handles multiple chunks
// ===========================================================================
TEST(BufferCallbackTest, IntegrationWithLargeOutput) {
  size_t call_count = 0;
  size_t total_callback_bytes = 0;
  dynamic_buffer out(
      [&call_count, &total_callback_bytes](const unsigned char*, size_t size) {
        ++call_count;
        total_callback_bytes += size;
      });

#if !defined(_WIN32)
  // Generate ~128KB of output via dd
  int ret = run("dd", "if=/dev/zero", "bs=1024", "count=128", std_out > out);
  if (ret != 0) {
    out.clear();
    // If dd failed, try head
    ret = run("head", "-c", "131072", "/dev/zero", std_out > out);
  }
  EXPECT_EQ(ret, 0);
#else
  // On Windows, use powershell to generate output
  int ret = run(TEXT("powershell"), TEXT("-NoProfile"), TEXT("-c"),
                TEXT("'A'*131072"), std_out > out);
  EXPECT_EQ(ret, 0);
#endif

  EXPECT_EQ(out.size(), total_callback_bytes);
  EXPECT_GT(call_count, 0u) << "Callback should have been called at least once";
  EXPECT_EQ(out.size(), total_callback_bytes);
}

// ===========================================================================
// 16. Callback that copies data to another dynamic_buffer (tee-like behavior)
// ===========================================================================
TEST(BufferCallbackTest, CallbackTeeToSecondBuffer) {
  dynamic_buffer primary;
  dynamic_buffer secondary;

  // primary's callback feeds into secondary
  dynamic_buffer buf([&secondary](const unsigned char* data, size_t size) {
    secondary.append(reinterpret_cast<const char*>(data), size);
  });

  buf.append("part_a", 6);
  buf.append("part_b", 6);
  buf.append("part_c", 6);

  EXPECT_EQ(buf.to_string(), "part_apart_bpart_c");
  EXPECT_EQ(secondary.to_string(), "part_apart_bpart_c");
  // primary was not used for accumulation, just for tee verification
  EXPECT_EQ(buf.size(), secondary.size());
}

// ===========================================================================
// 17. Empty string_view + callback — empty initial buffer, callback not fired
// ===========================================================================
TEST(BufferCallbackTest, EmptyStringViewWithCallback) {
  int fired = 0;
  dynamic_buffer buf(std::string_view(""),
                     [&fired](const unsigned char*, size_t) { ++fired; });

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(fired, 0);

  buf.append("data", 4);
  EXPECT_EQ(fired, 1);
  EXPECT_EQ(buf.to_string(), "data");
}

// ===========================================================================
// 18. Stress test: many small appends to ensure callback stability
// ===========================================================================
TEST(BufferCallbackTest, ManySmallAppends) {
  std::string accumulated;
  dynamic_buffer buf([&accumulated](const unsigned char* data, size_t size) {
    accumulated.append(reinterpret_cast<const char*>(data), size);
  });

  constexpr int kNumChunks = 10000;
  for (int i = 0; i < kNumChunks; ++i) {
    char c = static_cast<char>('A' + (i % 26));
    buf.append(&c, 1);
  }

  EXPECT_EQ(buf.size(), static_cast<size_t>(kNumChunks));
  EXPECT_EQ(accumulated.size(), static_cast<size_t>(kNumChunks));
  EXPECT_EQ(buf.to_string(), accumulated);
}
