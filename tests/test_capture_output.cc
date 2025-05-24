#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;

TEST(SubprocessTest, CaptureOutputs) {
  std::vector<char> out;
  std::vector<char> err;

#if !defined(_WIN32)
  run({"/bin/bash", "-c", "echo -n 123; echo -n '345' >&2"}, std_out > out,
      std_err > err);
#else
  run({"cmd.exe", "/c", "<nul set /p=123& <nul set /p=345>&2\r\n"},
      std_out > out, std_err > err);
#endif
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_EQ("345", std::string_view(err.data(), err.size()));

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run({"/bin/bash", "-c", "echo -n 123"}, std_out > out, std_err > err);
#else
  run({"cmd.exe", "/c", "<nul set /p=123"}, std_out > out, std_err > err);
#endif
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_TRUE(err.empty());

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run({"/bin/bash", "-c", "echo -n '123' >&2"}, std_out > out, std_err > err);
#else
  run({"cmd.exe", "/c", "<nul set /p=123>&2"}, std_out > out, std_err > err);
#endif
  ASSERT_TRUE(out.empty());
  ASSERT_FALSE(err.empty());
  ASSERT_EQ("123", std::string_view(err.data(), err.size()));
}
