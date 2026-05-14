/**
 * test_filename_with_space.cc — Commands with spaces in filenames
 *
 * Covers:
 *   - Shell scripts / batch files with spaces in the file path
 *   - Execution, stdout/stderr capture, and exit code verification
 *   - Cross-platform: .bat on Windows, .sh on Unix
 */

#include <string>

#include "./utils.h"
#include "gtest/gtest.h"
#include "subprocess/subprocess.hpp"

using namespace std::string_literals;
using namespace subprocess;
using namespace subprocess::named_arguments;

// ===========================================================================
// Script with spaces in filename — stdout, stderr, exit code
// ===========================================================================
TEST(FilenameWithSpaceTest, ScriptWithSpacesInPath) {
#if defined(_WIN32)
  TempFile temp_file("", "file with space script.bat");
  ASSERT_TRUE(temp_file.write(
      "@echo off\necho script_out\necho script_err 1>&2\nexit /b 5"s));

  buffer stdout_buf;
  buffer stderr_buf;
  int exit_code =
      run({temp_file.path()}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out\r\n");
  ASSERT_EQ(stderr_buf.to_string(), "script_err \r\n");
#else
  TempFile temp_file("", "file with space script.sh");
  ASSERT_TRUE(
      temp_file.write("#!/usr/bin/env bash\n"
                      "echo -n 'script_out'\n"
                      "echo -n 'script_err' >&2\n"
                      "exit 5"s));

  int chmod_exit_code = run({"/bin/chmod", "+x", temp_file.path()});
  ASSERT_EQ(chmod_exit_code, 0);

  buffer stdout_buf;
  buffer stderr_buf;
  int script_exit_code =
      run({temp_file.path()}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(script_exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out");
  ASSERT_EQ(stderr_buf.to_string(), "script_err");
#endif
}

// ===========================================================================
// Run shell script / batch full: same pattern, explicit path
// ===========================================================================
TEST(FilenameWithSpaceTest, RunShellScriptFull) {
#if defined(_WIN32)
  TempFile temp_file("", "-test_run_script.bat");
  ASSERT_TRUE(temp_file.write(
      "@echo off\necho script_out\necho script_err 1>&2\nexit /b 5"s));

  buffer stdout_buf;
  buffer stderr_buf;
  int exit_code = run({"cmd.exe", "/c", temp_file.path()}, std_out > stdout_buf,
                      std_err > stderr_buf);

  ASSERT_EQ(exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out\r\n");
  ASSERT_EQ(stderr_buf.to_string(), "script_err \r\n");
#else
  const std::string script_path = "/tmp/test_run_script.sh";
  std::remove(script_path.c_str());

  {
    std::ofstream outfile(script_path);
    outfile << "#!/usr/bin/env bash\n"
            << "echo -n 'script_out'\n"
            << "echo -n 'script_err' >&2\n"
            << "exit 5";
    outfile.close();
  }

  int chmod_exit_code = run({"/bin/chmod", "+x", script_path});
  ASSERT_EQ(chmod_exit_code, 0);

  buffer stdout_buf;
  buffer stderr_buf;
  int script_exit_code =
      run({script_path}, std_out > stdout_buf, std_err > stderr_buf);

  ASSERT_EQ(script_exit_code, 5);
  ASSERT_EQ(stdout_buf.to_string(), "script_out");
  ASSERT_EQ(stderr_buf.to_string(), "script_err");
  std::remove(script_path.c_str());
#endif
}
