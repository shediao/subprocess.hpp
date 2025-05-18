#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, Environment) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;
  auto ret = run({"/usr/bin/printenv", "env1"}, env = {{"env1", "value1"}},
                 std_out > out);

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1\n");
}

TEST(SubprocessTest, Environment2) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;
  auto ret = run({"/bin/bash", "-c", "echo -n $env1"},
                 $env = {{"env1", "value1"}}, $stdout > out);

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
}
