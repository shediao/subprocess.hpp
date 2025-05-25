#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;

TEST(SubprocessTest, Devnull) {
  auto ret =
#if defined(_WIN32)
      run("cmd.exe", "/c", "echo 123", $stdout > $devnull);
#else
      run("/bin/echo", "123", $stdout > $devnull);
#endif
  ASSERT_EQ(0, ret);
}
