
#include <string>
#include <vector>

#include "./utils.h"
#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

using namespace std::string_literals;
using namespace subprocess;
using namespace subprocess::named_arguments;

// 1. Test simple command success
TEST(SubprocessTest, WinFilePathWithSpace) {
#if defined(_WIN32)
  TempFile temp_file("", "file with space script.bat");
  ASSERT_TRUE(temp_file.write(
      "@echo off\necho script_out\necho script_err 1>&2\nexit /b 5"s));

  subprocess::buffer stdout_buf;
  subprocess::buffer stderr_buf;
  // cmd /c script.bat to run
  int exit_code =
      run({temp_file.path()}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(exit_code, 5);
  // Output from echo in batch includes \r\n
  ASSERT_EQ(stdout_buf.to_string(), "script_out\r\n");
  ASSERT_EQ(
      stderr_buf.to_string(),
      "script_err \r\n");  // Note: stderr from echo might have trailing space
                           //
#else
  TempFile temp_file("", "file with space script.sh");
  ASSERT_TRUE(
      temp_file.write("#!/bin/bash\n"
                      "echo -n 'script_out'\n"
                      "echo -n 'script_err' >&2\n"
                      "exit 5"s));

  // Make the script executable
  int chmod_exit_code = run({"/bin/chmod", "+x", temp_file.path()});
  ASSERT_EQ(chmod_exit_code, 0);

  subprocess::buffer stdout_buf;
  subprocess::buffer stderr_buf;
  int script_exit_code =
      run({temp_file.path()}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(script_exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out");
  ASSERT_EQ(stderr_buf.to_string(), "script_err");
#endif
}
