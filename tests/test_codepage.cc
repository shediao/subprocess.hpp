#include <gtest/gtest.h>

#include <subprocess/subprocess.hpp>

#if defined(_WIN32)
struct ScopeSetConsoleCP {
  ScopeSetConsoleCP(UINT cp) : origin_cp_(GetConsoleOutputCP()) {
    SetConsoleOutputCP(cp);
  }
  ~ScopeSetConsoleCP() {
    if (origin_cp_ != 0) {
      SetConsoleOutputCP(origin_cp_);
    }
  }
  UINT origin_cp_;
};
#endif

TEST(TestCodePage, Test1) {
#if defined(_WIN32)
  subprocess::buffer out;
  out.encode_codepage(GetConsoleOutputCP());
  auto ret = subprocess::run("cmd.exe", "/c", "<nul set /p=你好&exit /b 0",
                             $stdout > out);

  ASSERT_EQ(ret, 0);

  ASSERT_EQ(out.to_string(), "你好");
#endif
}

TEST(TestCodePage, Test2) {
#if defined(_WIN32)
  subprocess::buffer out;
  out.encode_codepage(936);
  {
    ScopeSetConsoleCP x(936);
    auto ret = subprocess::run("cmd.exe", "/c", "<nul set /p=你好&exit /b 0",
                               $stdout > out);
    ASSERT_EQ(ret, 0);
  }

  ASSERT_EQ(out.to_string(), "你好");
#endif
}

TEST(TestCodePage, Test3) {
#if defined(_WIN32)
  subprocess::buffer out;
  out.encode_codepage(65001);
  {
    ScopeSetConsoleCP x(65001);
    auto ret = subprocess::run("cmd.exe", "/c", "<nul set /p=你好&exit /b 0",
                               $stdout > out);
    ASSERT_EQ(ret, 0);
  }

  ASSERT_EQ(out.to_string(), "你好");
#endif
}
