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

  auto out_span = out.span();
  auto it = std::find_if(rbegin(out_span), rend(out_span),
                         [](char c) { return c != '\n' && c != '\r'; });
  if (it != rend(out_span)) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }

  ASSERT_TRUE(std::equal(out_span.begin(), out_span.end(), home_dir->begin(),
                         home_dir->end()));
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

  auto out_span = out.span();
  auto it = std::find_if(rbegin(out_span), rend(out_span),
                         [](char c) { return c != '\n' && c != '\r'; });
  if (it != rend(out_span)) {
    out_span = out_span.subspan(0, it.base() - out_span.begin());
  }

  ASSERT_TRUE(std::equal(out_span.begin(), out_span.end(), home_dir->begin(),
                         home_dir->end()));
}
