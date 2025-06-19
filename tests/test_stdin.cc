#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;
TEST(SubprocessTest, Stdin) {
  subprocess::buffer in{{'1', '2', '3'}};
  subprocess::buffer out;
#if !defined(_WIN32)
  run("/bin/cat", "-", std_in<in, std_out> out);
  ASSERT_EQ(in.to_string_view(), out.to_string_view());
#else
  run("more.com", std_in<in, std_out> out);
  auto out_view = out.to_string_view();
  out_view = out_view.substr(0, std::find_if(
                                    out_view.rbegin(), out_view.rend(),
                                    [](char c) {
                                      return c != '\r' && c != '\n';
                                    }).base() -
                                    out_view.begin());
  ASSERT_EQ(in.to_string_view(), out_view);
#endif
}
