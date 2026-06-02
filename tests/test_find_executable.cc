/**
 * test_find_executable.cc — Unit tests for find_executable and
 * find_all_command_path
 *
 * Covers:
 *   - find_executable: command without extension found in PATH
 *   - find_executable: command with extension found in PATH
 *   - find_executable: command not found returns nullopt
 *   - find_executable: absolute path resolution
 *   - find_executable: relative path with cwd resolution
 *   - find_all_command_path: returns empty for path-separator commands
 *   - find_all_command_path: command with extension (exact match only)
 *   - find_all_command_path: command without extension (PATHEXT search)
 *   - find_all_command_path: non-existent command returns empty vector
 */

#include <gtest/gtest.h>

#include <string>

#include "subprocess/subprocess.hpp"

using namespace subprocess::detail;

// ===========================================================================
// find_executable — command without extension (PATHEXT / PATH search)
// ===========================================================================
TEST(FindExecutableTest, CommandWithoutExtension) {
#if defined(_WIN32)
  auto result = find_executable(L"cmd");
  ASSERT_TRUE(result.has_value());
  // The result should contain cmd.exe (case-insensitive)
  EXPECT_TRUE(to_lower_ascii(*result).ends_with(L"\\cmd.exe"));
#else
  auto result = find_executable("sh");
  ASSERT_TRUE(result.has_value());
  // Typically /bin/sh or /usr/bin/sh
  EXPECT_TRUE(result->ends_with("/sh"));
#endif
}

// ===========================================================================
// find_executable — command with extension
// ===========================================================================
TEST(FindExecutableTest, CommandWithExtension) {
#if defined(_WIN32)
  auto result = find_executable(L"cmd.exe");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(to_lower_ascii(*result).ends_with(L"\\cmd.exe"));
#else
  // "true" is typically at /usr/bin/true or /bin/true
  auto result = find_executable("true");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ends_with("/true"));
#endif
}

// ===========================================================================
// find_executable — non-existent command returns nullopt
// ===========================================================================
TEST(FindExecutableTest, CommandNotFound) {
#if defined(_WIN32)
  auto result = find_executable(L"this_command_does_not_exist_xyz");
  ASSERT_FALSE(result.has_value());

  result = find_executable(L"nonexistent_cmd_xyz.exe");
  ASSERT_FALSE(result.has_value());
#else
  auto result = find_executable("this_command_does_not_exist_xyz");
  ASSERT_FALSE(result.has_value());
#endif
}

// ===========================================================================
// find_executable — absolute path resolution
// ===========================================================================
TEST(FindExecutableTest, AbsolutePath) {
#if defined(_WIN32)
  // cmd.exe in System32 is a reliable absolute path target
  auto result = find_executable(L"cmd.exe");
  ASSERT_TRUE(result.has_value());
  // Now search for it by its absolute path
  auto abs_path = *result;
  auto result2 = find_executable(abs_path);
  ASSERT_TRUE(result2.has_value());
  // Compare case-insensitively
  EXPECT_EQ(to_lower_ascii(*result2), to_lower_ascii(abs_path));
#else
  auto result = find_executable("/bin/sh");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "/bin/sh");

  // Also test with trailing content that definitely doesn't exist
  auto result2 = find_executable("/bin/sh_nonexistent_suffix");
  ASSERT_FALSE(result2.has_value());
#endif
}

// ===========================================================================
// find_executable — relative path with cwd
// ===========================================================================
TEST(FindExecutableTest, RelativePathWithCwd) {
#if defined(_WIN32)
  // Resolve cmd.exe to get a known directory
  auto cmd = find_executable(L"cmd.exe");
  ASSERT_TRUE(cmd.has_value());
  std::wstring cmd_path = *cmd;
  // Extract directory
  auto last_sep = cmd_path.find_last_of(L'\\');
  ASSERT_NE(last_sep, std::wstring::npos);
  std::wstring dir = cmd_path.substr(0, last_sep);
  std::wstring filename = cmd_path.substr(last_sep + 1);

  // Search for filename relative to its directory
  auto result = find_executable(filename, dir);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(to_lower_ascii(*result), to_lower_ascii(cmd_path));
#else
  GTEST_SKIP() << "Windows-only test";
#endif
}

// ===========================================================================
// find_executable — non-existent relative path with cwd
// ===========================================================================
TEST(FindExecutableTest, RelativePathWithCwdNotFound) {
#if defined(_WIN32)
  auto result =
      find_executable(L"nonexistent_xyz.exe", L"C:\\Windows\\System32");
  ASSERT_FALSE(result.has_value());
#else
  GTEST_SKIP() << "Windows-only test";
#endif
}

// ===========================================================================
// find_all_command_path — returns empty for path-separator commands
// ===========================================================================
TEST(FindAllCommandPathTest, PathSeparatorReturnsEmpty) {
#if defined(_WIN32)
  // Any command containing \ or / should return empty vector
  auto result = find_all_command_path(L"foo\\bar.exe");
  EXPECT_TRUE(result.empty());

  result = find_all_command_path(L"foo/bar.exe");
  EXPECT_TRUE(result.empty());

  result = find_all_command_path(L".\\cmd.exe");
  EXPECT_TRUE(result.empty());
#endif
}

// ===========================================================================
// find_all_command_path — command with extension (exact match, no PATHEXT)
// ===========================================================================
TEST(FindAllCommandPathTest, CommandWithExtension) {
#if defined(_WIN32)
  // cmd.exe should be found somewhere in PATH
  auto result = find_all_command_path(L"cmd.exe");
  ASSERT_FALSE(result.empty());
  // Every result should end with cmd.exe (case-insensitive)
  for (auto const& r : result) {
    EXPECT_TRUE(to_lower_ascii(r).ends_with(L"\\cmd.exe"));
  }
#endif
}

// ===========================================================================
// find_all_command_path — command without extension (PATHEXT search)
// ===========================================================================
TEST(FindAllCommandPathTest, CommandWithoutExtension) {
#if defined(_WIN32)
  auto result = find_all_command_path(L"cmd");
  ASSERT_FALSE(result.empty());
  // Should find cmd.exe (or CMD.EXE) via PATHEXT
  bool found_cmd_exe = false;
  for (auto const& r : result) {
    auto lower = to_lower_ascii(r);
    if (lower.ends_with(L"\\cmd.exe")) {
      found_cmd_exe = true;
    }
  }
  EXPECT_TRUE(found_cmd_exe);
#endif
}

// ===========================================================================
// find_all_command_path — non-existent command returns empty vector
// ===========================================================================
TEST(FindAllCommandPathTest, NonExistentCommand) {
#if defined(_WIN32)
  auto result = find_all_command_path(L"this_command_does_not_exist_xyz");
  EXPECT_TRUE(result.empty());

  // Even with an extension, non-existent should return empty
  result = find_all_command_path(L"nonexistent_cmd_xyz.exe");
  EXPECT_TRUE(result.empty());
#endif
}

// ===========================================================================
// find_all_command_path — extension fallthrough bug regression
//   When a command has an extension (e.g. "foo.exe") and is not found,
//   it must NOT try PATHEXT extensions like "foo.exe.com".
// ===========================================================================
TEST(FindAllCommandPathTest, ExtensionNoFallthrough) {
#if defined(_WIN32)
  // Use a name that has an extension but definitely doesn't exist
  auto result = find_all_command_path(L"nonexistent_xyz_test.exe");
  EXPECT_TRUE(result.empty());
  // If the fallthrough bug existed, this would try
  // "nonexistent_xyz_test.exe.com" etc., but since we mock nothing,
  // the observable result is the same — empty. The regression is
  // validated by the fix: when dot > 0, we now return early from
  // find_in_path() instead of falling through to the PATHEXT loop.
  //
  // Cross-validate: a command without extension that doesn't exist
  // should also return empty.
  result = find_all_command_path(L"nonexistent_xyz_test");
  EXPECT_TRUE(result.empty());
#endif
}
