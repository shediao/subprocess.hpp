#ifndef __TESTS_UTILS_H__
#define __TESTS_UTILS_H__

#if defined(_WIN32)
#include <Windows.h>
#endif  // !_WIN32
#if !defined(_WIN32)
#include <unistd.h>
#endif  // !_WIN32

#include <filesystem>
#include <fstream>
#include <string>

inline std::string getTempFilePath(std::string const& prefix,
                                   std::string const& postfix) {
  std::string temp_file_path;

#ifdef _WIN32
  char temp_dir[MAX_PATH];
  if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
    throw std::runtime_error("Failed to get temporary directory.");
  }

  char temp_file[MAX_PATH];
  if (GetTempFileNameA(temp_dir, prefix.c_str(), 0, temp_file) == 0) {
    throw std::runtime_error("Failed to create temporary file.");
  }
  temp_file_path = temp_file;
  if (!postfix.empty()) {
    temp_file_path += postfix;
  }

#else
  std::string temp_dir = []() -> std::string {
    char* tmpdir = getenv("TMPDIR");
    if (tmpdir != nullptr) {
      return std::string(tmpdir);
    } else {
      return "/tmp";
    }
  }();
  std::string template_str = temp_dir;
  if (!template_str.ends_with('/')) {
    template_str.push_back('/');
  }
  if (!prefix.empty()) {
    template_str += prefix;
  }
  template_str += "XXXXXX";
  [[maybe_unused]] int suffix_len = 0;
  if (!postfix.empty()) {
    template_str += postfix;
    suffix_len = postfix.length();
  }
  // int fd = mkstemp(template_str.data());
  int fd = mkstemps(template_str.data(), suffix_len);
  if (fd == -1) {
    throw std::runtime_error("Failed to create temporary file.");
  }
  close(fd);
  temp_file_path = template_str;
#endif
  return temp_file_path;
}

class TempFile {
 public:
  TempFile(std::string const& prefix, std::string const& postfix)
      : path_{getTempFilePath(prefix, postfix)} {}
  TempFile() : path_{getTempFilePath("", "")} {}
  ~TempFile() {
    if (std::filesystem::exists(path_)) {
      std::filesystem::remove(path_);
    }
  }
  std::string const& path() { return path_; }
  std::vector<char> content() {
    std::ifstream input(path_);
    if (input.is_open()) {
      return std::vector<char>{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    }
    return {};
  }
  std::string content_str() {
    std::ifstream input(path_);
    if (input.is_open()) {
      return std::string{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
    }
    return {};
  }

  template <typename T>
  auto write(T const& content) {
    std::ofstream output{path_};
    output.write(content.data(), content.size());
    return content.size();
  }

 private:
  std::string path_;
};

#endif  // __TESTS_UTILS_H__
