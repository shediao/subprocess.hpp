#include <gtest/gtest.h>

#include <cstdlib>  // For getenv
#include <string>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, CWD) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;

  auto home_dir = process::home();

#if !defined(_WIN32)
  run({"/bin/pwd"}, $stdout > out, $cwd = home_dir);
#else
  run({"cmd.exe", "/c", "echo %CD%"}, $stdout > out, $cwd = home_dir);
#endif

  ASSERT_FALSE(out.empty());

  auto it = std::find_if(rbegin(out), rend(out),
                         [](char c) { return c != '\n' && c != '\r'; });
  out.erase(it.base(), out.end());

  ASSERT_EQ(std::string_view(out.data(), out.size()), home_dir);
}
