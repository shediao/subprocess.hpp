#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;
TEST(SubprocessTest, Stdin) {
  subprocess::buffer in{"123"};
  subprocess::buffer out;
#if !defined(_WIN32)
  run("/bin/cat", "-", std_in<in, std_out> out);
  ASSERT_EQ(in, out);
#else
  run(TEXT("more.com"), std_in<in, std_out> out);
  auto out_span = out.span();
  auto it = std::find_if(out_span.rbegin(), out_span.rend(),
                         [](char c) { return c != '\r' && c != '\n'; });
  if (it != out_span.rend()) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }
  ASSERT_TRUE(
      std::equal(in.begin(), in.end(), out_span.begin(), out_span.end()));

#endif
}
