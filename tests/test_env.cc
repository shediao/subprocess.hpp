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
  auto ret =
      run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=%env1%&exit /b 0"),
          env = {{TEXT("env1"), TEXT("value1")}}, std_out > out);
#endif

  ASSERT_EQ(ret, 0);
#if !defined(_WIN32)
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1\n");
#else
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
#endif
}

TEST(SubprocessTest, Environment2) {
  subprocess::buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $env1", $env += {{"env1", "value1"}},
                 $stdout > out);
#else
  auto ret =
      run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=%env1%&exit /b 0"),
          $env += {{TEXT("env1"), TEXT("value1")}}, $stdout > out);
#endif

  ASSERT_EQ(ret, 0);
  ASSERT_EQ(std::string_view(out.data(), out.size()), "value1");
}

TEST(SubprocessTest, Environment3) {
  subprocess::buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $PATH", $env["PATH"] += "XXXXXXXXX",
                 $stdout > out);
#else
  auto ret =
      run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=%PATH%&exit /b 0"),
          $env["PATH"] += "XXXXXXXXX", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_GT(out.size(), 10);
  ASSERT_EQ(std::string_view(out.data() + out.size() - 9, 9), "XXXXXXXXX");
}

TEST(SubprocessTest, Environment4) {
  subprocess::buffer out;
#if !defined(_WIN32)
  auto ret = run("bash", "-c", "echo -n $PATH", $env["PATH"] <<= "XXXXXXXXX",
                 $stdout > out);
#else
  auto ret =
      run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=%PATH%&exit /b 0"),
          $env["PATH"] <<= "XXXXXXXXX", $stdout > out);
#endif
  ASSERT_EQ(ret, 0);
  ASSERT_GT(out.size(), 10);
  ASSERT_NE(std::string_view(out.data(), 9), "XXXXXXXXX");
}
