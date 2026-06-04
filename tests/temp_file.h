#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

class TempFile {
 public:
  TempFile();
  TempFile(std::string const& prefix, std::string const& postfix);
  ~TempFile();
  std::string path() const;
  std::optional<std::string> content() const;
  std::string read_trimmed();
  template <typename T>
  auto write(T const& content) {
    std::ofstream output{path_};
    output.write(content.data(), content.size());
    return content.size();
  }
  bool wait_for_file(
      std::chrono::milliseconds max_wait = std::chrono::seconds(5));

 private:
  std::filesystem::path path_;
};
std::string getTempFilePath(std::string const& prefix,
                            std::string const& postfix);
