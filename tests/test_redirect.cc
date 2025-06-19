#include <gtest/gtest.h>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::run;
TEST(SubprocessTest, RedirectOut) {
  TempFile tmp_file;
  subprocess::buffer content{"123"};
#if !defined(_WIN32)
  run({"echo", "-n", content.to_string()}, std_out > tmp_file.path());
#else
  run({"cmd.exe", "/c", "<nul set /p=" + content.to_string()},
      std_out > tmp_file.path());
#endif
  ASSERT_EQ(content.to_string(), tmp_file.content_str());
}
TEST(SubprocessTest, RedirectOutAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string{"000"});
  subprocess::buffer content{"123"};
#if !defined(_WIN32)
  run({"echo", "-n", content.to_string()}, std_out >> tmp_file.path());
#else
  run({"cmd.exe", "/c", "<nul set /p=" + content.to_string()},
      std_out >> tmp_file.path());
#endif
  ASSERT_EQ("000123", tmp_file.content_str());
}

TEST(SubprocessTest, RedirectErr) {
  TempFile tmp_file;
  subprocess::buffer content{"123"};
#if !defined(_WIN32)
  run({"bash", "-c", "echo -n " + content.to_string() + " >&2"},
      std_err > tmp_file.path());
#else
  run({"cmd.exe", "/c", "<nul set /p=" + content.to_string() + ">&2"},
      std_err > tmp_file.path());
#endif
  ASSERT_EQ(content.to_string(), tmp_file.content_str());
}

TEST(SubprocessTest, RedirectErrAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string("999"));
  subprocess::buffer content{"123"};
#if !defined(_WIN32)
  run({"bash", "-c", "echo -n " + content.to_string() + " >&2"},
      std_err >> tmp_file.path());
#else
  run({"cmd.exe", "/c", "<nul set /p=" + content.to_string() + ">&2"},
      std_err >> tmp_file.path());
#endif
  ASSERT_EQ("999123", tmp_file.content_str());
}
