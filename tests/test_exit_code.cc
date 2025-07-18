#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

TEST(SubprocessTest, ExitCode) {
#if !defined(_WIN32)
  ASSERT_EQ(0, run("true"));
  ASSERT_EQ(1, run("false"));

  for (int i = 0; i < 127; i++) {
    ASSERT_EQ(i, run("bash", "-c", "exit " + std::to_string(i)));
  }

  for (int i : {4, 9, 15}) {
    ASSERT_EQ(128 + i, run("bash", "-c", "kill -" + std::to_string(i) + " $$"));
  }
#else
  for (int i = 0; i < 127; i++) {
    ASSERT_EQ(i, run("cmd.exe", "/c", "exit /b " + std::to_string(i)));
  }
#endif
}
