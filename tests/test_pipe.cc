
#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

TEST(SubprocessTest, Pipe) {
#if !defined(_WIN32)
  subprocess::buffer out;
  auto pipe1 = subprocess::detail::Pipe::create();
  auto pipe2 = subprocess::detail::Pipe::create();

  subprocess::detail::subprocess p1({"echo", "123\n456"}, $stdout > pipe1);
  subprocess::detail::subprocess p2({"sed", "-e", "s/3/4/g"},
                                    $stdin<pipe1, $stdout> pipe2);
  subprocess::detail::subprocess p3({"grep", "4"}, $stdin<pipe2, $stdout> out);

  p1.run_no_wait();
  p2.run_no_wait();
  p3.run_no_wait();

  auto r1 = p1.wait_for_exit();
  auto r2 = p2.wait_for_exit();
  auto r3 = p3.wait_for_exit();
  ASSERT_EQ(r1, 0);
  ASSERT_EQ(r2, 0);
  ASSERT_EQ(r3, 0);
  ASSERT_EQ((std::string_view{"124\n456\n"}),
            (std::string_view{out.data(), out.size()}));
#endif
}
