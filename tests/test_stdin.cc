#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;
TEST(SubprocessTest, Stdin) {
  std::vector<char> in{'1', '2', '3'};
  std::vector<char> out;
#if !defined(_WIN32)
  run({"/bin/cat", "-"}, std_in<in, std_out> out);
#else
  run({"more.com"}, std_in<in, std_out> out);
  out.erase(std::find_if(out.rbegin(), out.rend(),
                         [](char c) { return c != '\r' && c != '\n'; })
                .base(),
            out.end());
#endif

  ASSERT_EQ(in, out);
}
