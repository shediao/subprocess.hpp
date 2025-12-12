#include <gtest/gtest.h>

#include "subprocess/subprocess.hpp"

using namespace subprocess::named_arguments;
using subprocess::capture_run;
using subprocess::run;

TEST(SubprocessTest, CaptureOutputs) {
  subprocess::buffer out;
  subprocess::buffer err;

#if !defined(_WIN32)
  run("/bin/bash", "-c", "echo -n 123; echo -n '345' >&2", std_out > out,
      std_err > err);
#else
  run(TEXT("cmd.exe"), TEXT("/c"),
      TEXT("<nul set /p=123& <nul set /p=345>&2\r\n"), std_out > out,
      std_err > err);
#endif
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_EQ("345", std::string_view(err.data(), err.size()));

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run("/bin/bash", "-c", "echo -n 123", std_out > out, std_err > err);
#else
  run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=123"), std_out > out,
      std_err > err);
#endif
  ASSERT_EQ("123", std::string_view(out.data(), out.size()));
  ASSERT_TRUE(err.empty());

  out.clear();
  err.clear();
#if !defined(_WIN32)
  run("/bin/bash", "-c", "echo -n '123' >&2", std_out > out, std_err > err);
#else
  run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=123>&2"), std_out > out,
      std_err > err);
#endif
  ASSERT_TRUE(out.empty());
  ASSERT_FALSE(err.empty());
  ASSERT_EQ("123", std::string_view(err.data(), err.size()));
}

TEST(SubprocessTest, CaptureOutputs2) {
  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("/bin/bash", "-c", "echo -n 123; echo -n '345' >&2");
#else
        capture_run(TEXT("cmd.exe"), TEXT("/c"),
                    TEXT("<nul set /p=123& <nul set /p=345>&2\r\n"));
#endif
    ASSERT_EQ("123", std::string_view(out.data(), out.size()));
    ASSERT_EQ("345", std::string_view(err.data(), err.size()));
  }

  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("/bin/bash", "-c", "echo -n 123");
#else
        capture_run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=123"));
#endif
    ASSERT_EQ("123", std::string_view(out.data(), out.size()));
    ASSERT_TRUE(err.empty());
  }

  {
    auto [exit_code, out, err] =
#if !defined(_WIN32)
        capture_run("/bin/bash", "-c", "echo -n '123' >&2");
#else
        capture_run(TEXT("cmd.exe"), TEXT("/c"), TEXT("<nul set /p=123>&2"));
#endif
    ASSERT_TRUE(out.empty());
    ASSERT_FALSE(err.empty());
    ASSERT_EQ("123", std::string_view(err.data(), err.size()));
  }
#if !defined(_WIN32)
  {
    auto [exit_code, out, err] =
        capture_run("dd", "if=/dev/zero", "bs=4M", "count=100");
    ASSERT_EQ(out.size(), (100 * 4 * 1024 * 1024));
  }
#else
  {
    auto [exit_code, out, err] = capture_run("powershell", "-c", "'A'*400M");
    ASSERT_EQ(exit_code, 0) << err.data();
    ASSERT_EQ(out.size(), (400 * 1024 * 1024 + 2));  // \r\n
  }
#endif
}
