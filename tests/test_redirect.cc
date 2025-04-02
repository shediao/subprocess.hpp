#include "subprocess.hpp"
#include <fstream>

#include <gtest/gtest.h>

std::string createTemporaryFile() {
#ifdef _WIN32
  wchar_t tempPathBuffer[MAX_PATH];
  DWORD tempPathLength = GetTempPathW(MAX_PATH, tempPathBuffer);
  if (tempPathLength == 0 || tempPathLength > MAX_PATH) {
    throw std::runtime_error("Failed to get temporary path.");
  }

  wchar_t tempFileNameBuffer[MAX_PATH];
  UINT uniqueId = GetTempFileNameW(tempPathBuffer, L"tmp", 0, tempFileNameBuffer);
  if (uniqueId == 0) {
    throw std::runtime_error("Failed to create temporary file.");
  }

  // Convert wchar_t to std::string
  std::wstring wFilename(tempFileNameBuffer);
  std::string filename(wFilename.begin(), wFilename.end());
  return filename;

#else
  // POSIX (Linux, macOS, etc.)
  std::string tempDir;
  const char* tmpDirEnv = std::getenv("TMPDIR");
  if (tmpDirEnv != nullptr) {
    tempDir = tmpDirEnv;
  } else {
    tempDir = "/tmp";
  }

  std::string filenameTemplate = tempDir + "/temp.XXXXXX"; // XXXXXX will be replaced

  int fd = mkstemp(filenameTemplate.data());
  if (fd == -1) {
    throw std::runtime_error("Failed to create temporary file.");
  }
  close(fd); // Close the file descriptor; we only need the filename.

  return filenameTemplate;
#endif
}

std::vector<char> readFile(std::string f) {
  std::ifstream input(f);
  if (input.is_open()) {
    return std::vector<char>{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  return {};
}

TEST(SubprocessTest, RedirectOut) {
  using namespace process;
  auto tmp_file = createTemporaryFile();
  std::vector<char> content{'1', '2', '3'};
  run({"/bin/echo", "-n", std::string(content.data(), content.size())}, std_out > std::filesystem::path(tmp_file));
  ASSERT_EQ(content, readFile(tmp_file));
  unlink(tmp_file.c_str());
}

TEST(SubprocessTest, RedirectErr) {
  using namespace process;
  auto tmp_file = createTemporaryFile();
  std::vector<char> content{'1', '2', '3'};
  run({"bash", "-c", "echo -n " + std::string(content.data(), content.size()) + " >&2"}, std_err > std::filesystem::path(tmp_file));
  ASSERT_EQ(content, readFile(tmp_file));
  unlink(tmp_file.c_str());
}
