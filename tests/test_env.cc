#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;

TEST(SubprocessTest, Environment) {
  std::vector<char> out;
#if !defined(_WIN32)
  auto ret = run("/usr/bin/printenv", "env1", env = {{"env1", "value1"}},
                 std_out > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 env = {{"env1", "value1"}}, std_out > out);
#endif

  ASSERT_EQ(ret, 0);
#if !defined(_WIN32)
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1\n");
#else
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
#endif
}

TEST(SubprocessTest, Environment2) {
  std::vector<char> out;
#if !defined(_WIN32)
  auto ret = run("/bin/bash", "-c", "echo -n $env1",
                 $env = {{"env1", "value1"}}, $stdout > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 $env = {{"env1", "value1"}}, $stdout > out);
#endif

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
}
