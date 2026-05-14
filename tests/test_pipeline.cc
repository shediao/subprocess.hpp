#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using std::literals::string_literals::operator""s;

TEST(PipelineTest, Test1) {
#if defined(_WIN32)
  subprocess::buffer out;
  auto subs =
      subprocess::detail::subprocess(
          {"cmd.exe"s, "/c", "echo 123&echo 124&echo 456&exit /b 0"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "2"}) |
      subprocess::detail::subprocess({"findstr.exe"s, "4"}, $stdout > out);

  subs.run();
  auto exit_codes = subs.exit_codes();
  EXPECT_EQ(exit_codes.size(), 3);
  EXPECT_EQ(exit_codes[0], 0);
  EXPECT_EQ(exit_codes[1], 0);
  EXPECT_EQ(exit_codes[2], 0);
  ASSERT_EQ(out, "124\r\n");
#else
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess({"echo"s, "123\n456"}) |
              subprocess::detail::subprocess({"sed"s, "-e", "s/3/4/g"}) |
              subprocess::detail::subprocess({"grep"s, "4"}, $stdout > out);

  subs.run();
  auto exit_codes = subs.exit_codes();
  EXPECT_EQ(exit_codes.size(), 3);
  EXPECT_EQ(exit_codes[0], 0);
  EXPECT_EQ(exit_codes[1], 0);
  EXPECT_EQ(exit_codes[2], 0);
  ASSERT_EQ(out, "124\n456\n");
#endif
}
