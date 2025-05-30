#include <gtest/gtest.h>

#include "./utils.h"
#include "subprocess/subprocess.hpp"

using namespace process::named_arguments;
using process::run;
TEST(SubprocessTest, RedirectOut) {
  TempFile tmp_file;
  std::vector<char> content{'1', '2', '3'};
#if !defined(_WIN32)
  run({"echo", "-n", std::string(content.data(), content.size())},
      std_out > tmp_file.path());
#else
  run({"cmd.exe", "/c",
       "<nul set /p=" + std::string(content.data(), content.size())},
      std_out > tmp_file.path());
#endif
  ASSERT_EQ(content, tmp_file.content());
}
TEST(SubprocessTest, RedirectOutAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string{"000"});
  std::vector<char> content{'1', '2', '3'};
#if !defined(_WIN32)
  run({"echo", "-n", std::string(content.data(), content.size())},
      std_out >> tmp_file.path());
#else
  run({"cmd.exe", "/c",
       "<nul set /p=" + std::string(content.data(), content.size())},
      std_out >> tmp_file.path());
#endif
  ASSERT_EQ("000123", tmp_file.content_str());
}

TEST(SubprocessTest, RedirectErr) {
  TempFile tmp_file;
  std::vector<char> content{'1', '2', '3'};
#if !defined(_WIN32)
  run({"bash", "-c",
       "echo -n " + std::string(content.data(), content.size()) + " >&2"},
      std_err > tmp_file.path());
#else
  run({"cmd.exe", "/c",
       "<nul set /p=" + std::string(content.data(), content.size()) + ">&2"},
      std_err > tmp_file.path());
#endif
  ASSERT_EQ(content, tmp_file.content());
}

TEST(SubprocessTest, RedirectErrAppend) {
  TempFile tmp_file;
  tmp_file.write(std::string("999"));
  std::vector<char> content{'1', '2', '3'};
#if !defined(_WIN32)
  run({"bash", "-c",
       "echo -n " + std::string(content.data(), content.size()) + " >&2"},
      std_err >> tmp_file.path());
#else
  run({"cmd.exe", "/c",
       "<nul set /p=" + std::string(content.data(), content.size()) + ">&2"},
      std_err >> tmp_file.path());
#endif
  ASSERT_EQ("999123", tmp_file.content_str());
}
