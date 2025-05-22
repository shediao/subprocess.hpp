#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, Stdin) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> in{'1', '2', '3'};
  std::vector<char> out;
#if !defined(_WIN32)
  run({"/bin/cat", "-"}, std_in<in, std_out> out);
#else
  run({"powershell", "-command", "$input"}, std_in<in, std_out> out);

#endif

  ASSERT_EQ(in, out);
}
