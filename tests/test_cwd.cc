#include <gtest/gtest.h>

#include <cstdlib>  // For getenv
#include <string>

#include "subprocess/subprocess.hpp"

#ifdef _WIN32
#include <windows.h>  // For GetUserProfileDirectory
#else
#include <pwd.h>     // For struct passwd
#include <unistd.h>  // For getpwuid, getuid
#endif

std::string getHomeDirectory() {
#ifdef _WIN32
  // Windows implementation
  char *homeDrive = getenv("HOMEDRIVE");
  char *homePath = getenv("HOMEPATH");

  if (homeDrive != nullptr && homePath != nullptr) {
    return std::string(homeDrive) + std::string(homePath);
  } else {
    // Fallback to GetUserProfileDirectory if HOMEDRIVE and HOMEPATH are not set
    char path[MAX_PATH];
    DWORD pathLen = MAX_PATH;
    if (GetUserProfileDirectory(nullptr, path, &pathLen)) {
      return std::string(path);
    } else {
      // Last resort: return an empty string or a default path
      return "";  // Or "C:\\Users\\Default" or something similar
    }
  }
#else
  // Unix-like implementation (Linux, macOS, etc.)
  const char *homeDir = getenv("HOME");
  if (homeDir != nullptr) {
    return std::string(homeDir);
  } else {
    // If HOME is not set, try using getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != nullptr) {
      return std::string(pw->pw_dir);
    } else {
      // Last resort: return an empty string or a default path
      return "";  // Or "/home/default" or something similar
    }
  }
#endif
}

TEST(SubprocessTest, CWD) {
  using namespace process::named_arguments;
  using process::run;
  std::vector<char> out;

  auto home_dir = getHomeDirectory();

  run({"/bin/pwd"}, std_out > out, cwd = home_dir);

  ASSERT_FALSE(out.empty());

  auto it =
      std::find_if(rbegin(out), rend(out), [](char c) { return c != '\n'; });
  out.erase(it.base(), out.end());

  ASSERT_EQ(std::string_view(out.data(), out.size()), home_dir);
}
