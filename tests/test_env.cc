#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

TEST(SubprocessTest, Environment) {
  subprocess::buffer out;
#if !defined(_WIN32)
  auto ret = run("/usr/bin/printenv", "env1", env = {{"env1", "value1"}},
                 std_out > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 env = {{"env1", "value1"}}, std_out > out);
#endif

  ASSERT_EQ(ret, 0);
#if !defined(_WIN32)
  ASSERT_EQ(out.to_string_view(), "value1\n");
#else
  ASSERT_EQ(out.to_string_view(), "value1");
#endif
}

TEST(SubprocessTest, Environment2) {
  subprocess::buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $env1", $env += {{"env1", "value1"}},
                 $stdout > out);
#else
  auto ret = run("cmd.exe", "/c", "<nul set /p=%env1%&exit /b 0",
                 $env += {{"env1", "value1"}}, $stdout > out);
#endif

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(out.to_string_view(), "value1");
}
