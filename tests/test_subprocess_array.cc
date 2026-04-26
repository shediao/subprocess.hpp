#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessArrayTest, Test1) {
#if !defined(_WIN32)
  subprocess::buffer out;
  auto subs = subprocess::detail::subprocess(
                  std::vector<std::string>{"echo", "123\n456"}) |
              subprocess::detail::subprocess(
                  std::vector<std::string>{"sed", "-e", "s/3/4/g"}) |
              subprocess::detail::subprocess(
                  std::vector<std::string>{"grep", "4"}, $stdout > out);

  subs.run();
  auto exit_codes = subs.exit_codes();
  EXPECT_EQ(exit_codes.size(), 3);
  EXPECT_EQ(exit_codes[0], 0);
  EXPECT_EQ(exit_codes[1], 0);
  EXPECT_EQ(exit_codes[2], 0);
  ASSERT_EQ((std::string_view{"124\n456\n"}),
            (std::string_view{out.data(), out.size()}));
#endif
}
