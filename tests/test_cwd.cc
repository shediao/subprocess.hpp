#include <gtest/gtest.h>

#include <cstdlib>  // For getenv
#include <string>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;

TEST(SubprocessTest, CWD) {
  subprocess::buffer out;

  auto home_dir = subprocess::home();

#if !defined(_WIN32)
  run({"/bin/pwd"}, $stdout > out, $cwd = home_dir.value());
#else
  run({TEXT("cmd.exe"), TEXT("/c"), TEXT("echo %CD%")}, $stdout > out,
      $cwd = home_dir.value());
#endif

  ASSERT_FALSE(out.empty());

  auto out_view = out.to_string_view();
  auto it = std::find_if(rbegin(out_view), rend(out_view),
                         [](char c) { return c != '\n' && c != '\r'; });
  out_view = out_view.substr(0, it.base() - out_view.begin());

  ASSERT_EQ(out_view, home_dir.value());
}

TEST(SubprocessTest, CWD2) {
  subprocess::buffer out;

  auto home_dir = subprocess::home();

#if !defined(_WIN32)
  run("/bin/pwd", $stdout > out, $cwd = home_dir.value());
#else
  run(TEXT("cmd.exe"), TEXT("/c"), TEXT("echo %CD%"), $stdout > out,
      $cwd = home_dir.value());
#endif

  ASSERT_FALSE(out.empty());

  auto out_view = out.to_string_view();
  auto it = std::find_if(rbegin(out_view), rend(out_view),
                         [](char c) { return c != '\n' && c != '\r'; });
  out_view = out_view.substr(0, it.base() - out_view.begin());

  ASSERT_EQ(out_view, home_dir.value());
}
