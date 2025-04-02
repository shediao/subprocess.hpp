#include "subprocess.hpp"

#include <gtest/gtest.h>

TEST(SubprocessTest, CaptureOutputs) {
  using namespace process;
  std::vector<char> out;
  std::vector<char> err;


  run({"/bin/bash", "-c", "echo -n 123; echo -n '345' >&2"}, std_out > out, std_err > err);
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_EQ("345", std::string_view(err.data(), err.size()));

  out.clear(); err.clear();
  run({"/bin/bash", "-c", "echo -n 123"}, std_out > out, std_err > err);
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_TRUE(err.empty());

  out.clear(); err.clear();
  run({"/bin/bash", "-c", "echo -n '123' >&2"}, std_out > out, std_err > err);
  ASSERT_TRUE(out.empty());
  ASSERT_FALSE(err.empty());
  ASSERT_EQ("123", std::string_view(err.data(), err.size()));
}
