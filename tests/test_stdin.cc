#include <gtest/gtest.h>

#include "subprocess.hpp"

TEST(SubprocessTest, Stdin) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> in{'1', '2', '3'};
  std::vector<char> out;
  run({"/bin/cat", "-"}, std_in<in, std_out> out);

  ASSERT_EQ(in, out);
}
