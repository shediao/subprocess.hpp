#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;
TEST(SubprocessTest, Stdin) {
  subprocess::buffer in{"123"};
  subprocess::buffer out;
#if !defined(_WIN32)
  run("/bin/cat", "-", std_in<in, std_out> out);
  ASSERT_EQ(std::string_view(in.data(), in.size()),
            std::string_view(out.data(), out.size()));
#else
  run(TEXT("more.com"), std_in<in, std_out> out);
  auto out_view = std::string_view(out.data(), out.size());
  out_view = out_view.substr(0, std::find_if(
                                    out_view.rbegin(), out_view.rend(),
                                    [](char c) {
                                      return c != '\r' && c != '\n';
                                    }).base() -
                                    out_view.begin());
  ASSERT_EQ(std::string_view(in.data(), in.size()), out_view);
#endif
}
