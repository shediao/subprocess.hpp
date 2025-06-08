#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

TEST(SubprocessTest, Devnull) {
  auto ret =
#if defined(_WIN32)
      run("cmd.exe", "/c", "echo 123", $stdout > $devnull);
#else
      run("/bin/echo", "123", $stdout > $devnull);
#endif
  ASSERT_EQ(0, ret);
}
