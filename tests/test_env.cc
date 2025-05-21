#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, Environment) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;
#if !defined(_WIN32)
  auto ret = run({"/usr/bin/printenv", "env1"}, env = {{"env1", "value1"}},
                 std_out > out);
#else
  auto ret = run({"cmd.exe", "/c", "<nul set /p=%env1%"},
                 env = {{"env1", "value1"}}, std_out > out);
#endif

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1\n");
}

TEST(SubprocessTest, Environment2) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;
#if !defined(_WIN32)
  auto ret = run({"/bin/bash", "-c", "echo -n $env1"},
                 $env = {{"env1", "value1"}}, $stdout > out);
#else
  auto ret = run({"cmd.exe", "/c", "<nul set /p=%env1%"},
                 $env = {{"env1", "value1"}}, $stdout > out);
#endif

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
}
