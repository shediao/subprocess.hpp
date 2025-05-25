
#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, Pipe) {
#if !defined(_WIN32)
  std::vector<char> out;
  auto pipe1 = process::detail::Pipe::create();
  auto pipe2 = process::detail::Pipe::create();

  process::detail::subprocess p1({"echo", "123\n456"}, $stdout > pipe1);
  process::detail::subprocess p2({"sed", "-e", "s/3/4/g"},
                                 $stdin<pipe1, $stdout> pipe2);
  process::detail::subprocess p3({"grep", "4"}, $stdin<pipe2, $stdout> out);

  p1.run_no_wait();
  p2.run_no_wait();
  p3.run_no_wait();

  auto r1 = p1.wait_for_exit();
  auto r2 = p2.wait_for_exit();
  auto r3 = p3.wait_for_exit();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ((std::vector<char>{'1', '2', '4', '\n', '4', '5', '6', '\n'}), out);
#endif
}
