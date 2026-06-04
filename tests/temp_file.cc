#include "temp_file.h"

#ifdef _WIN32
#include <objbase.h>
#endif

#include <filesystem>
#include <fstream>
#include <string>
#include <subprocess/subprocess.hpp>
#include <thread>

namespace fs = std::filesystem;

namespace {
#if defined(_WIN32)
std::string create_uuid() {
  GUID guid;

  if (FAILED(CoCreateGuid(&guid))) {
    throw std::runtime_error("CoCreateGuid failed");
  }

  char guid_str[64];

  sprintf_s(guid_str, std::size(guid_str), "%08lX-%04X-%04X-%04X-%012llX",
            guid.Data1, guid.Data2, guid.Data3,
            static_cast<unsigned int>((guid.Data4[0] << 8) | guid.Data4[1]),
            ((unsigned long long)guid.Data4[2] << 40) |
                ((unsigned long long)guid.Data4[3] << 32) |
                ((unsigned long long)guid.Data4[4] << 24) |
                ((unsigned long long)guid.Data4[5] << 16) |
                ((unsigned long long)guid.Data4[6] << 8) |
                ((unsigned long long)guid.Data4[7]));
  return std::string(guid_str);
}
#endif  // _WIN32
}  // namespace

std::string getTempFilePath(std::string const& prefix,
                            std::string const& postfix) {
#ifdef _WIN32
  return subprocess::detail::utf16_to_utf8(std::wstring(
      fs::temp_directory_path() /
      (subprocess::detail::utf8_to_utf16(prefix + create_uuid() + postfix))));
#else
  std::string temp_file_path;

  std::string temp_dir = []() -> std::string {
    auto tmpdir = subprocess::detail::getenv("TMPDIR");
    if (tmpdir.has_value()) {
      return tmpdir.value();
    }
    return "/tmp";
  }();
  std::string template_str = temp_dir;
  if (!template_str.ends_with('/')) {
    template_str.push_back('/');
  }
  if (!prefix.empty()) {
    template_str += prefix;
  }
  template_str += "XXXXXX";
  // mkstemps is a BSD extension; use standard mkstemp and append suffix
  // after generating the unique path for portability (Linux, *BSD, macOS).
  int fd = mkstemp(template_str.data());
  if (fd == -1) {
    throw std::runtime_error("Failed to create temporary file.");
  }
  close(fd);
  ::remove(template_str.c_str());
  temp_file_path = template_str;
  if (!postfix.empty()) {
    temp_file_path += postfix;
  }
  return temp_file_path;
#endif
}
TempFile::TempFile(std::string const& prefix, std::string const& postfix)
    : path_{getTempFilePath(prefix, postfix)} {}
TempFile::TempFile() : TempFile("", ".tmp") {}
TempFile::~TempFile() {
  if (!path_.empty() && fs::exists(path_)) {
    fs::remove(path_);
  }
}
std::string TempFile::path() const { return path_.string(); }
std::optional<std::string> TempFile::content() const {
  std::ifstream f(path_);
  if (f.is_open()) {
    std::string ret{std::istreambuf_iterator<char>(f),
                    std::istreambuf_iterator<char>()};
    return ret;
  }
  return std::nullopt;
}

std::string TempFile::read_trimmed() {
  std::string content;
  std::ifstream f(path_);
  if (f.is_open()) {
    content = std::string{std::istreambuf_iterator<char>(f),
                          std::istreambuf_iterator<char>()};
  }

  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r')) {
    content.pop_back();
  }
  return content;
}

bool TempFile::wait_for_file(std::chrono::milliseconds max_wait) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path_, ec);
    if (!ec && sz > 0) {
      return true;
    }
    if (std::chrono::steady_clock::now() - start > max_wait) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
