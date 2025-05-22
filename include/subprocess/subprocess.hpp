#ifndef __SUBPROCESS_HPP__
#define __SUBPROCESS_HPP__

#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || \
    (!defined(_MSVC_LANG) && __cplusplus < 201703L)
#error "This code requires C++17 or later."
#endif

#pragma once
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

// POSIX (Linux, macOS, etc.) implementation
#if !defined(_WIN32)
extern char **environ;
#endif  // !_WIN32

namespace process {
#if defined(__APPLE__) && defined(__MACH__)
constexpr bool is_macos = true;
#else
constexpr bool is_macos = false;
#endif
#if defined(_WIN32) || defined(_WIN64)
constexpr bool is_win = true;
#else
constexpr bool is_win = false;
#endif

#if defined(__linux__)
constexpr bool is_linux = true;
#else
constexpr bool is_linux = false;
#endif
#if defined(__ANDROID__)
constexpr bool is_android = true;
#else
constexpr bool is_android = false;
#endif

#if defined(__CYGWIN__)
constexpr bool is_cygwin = true;
#else
constexpr bool is_cygwin = false;
#endif

#if defined(__FreeBSD__)
constexpr bool is_freebsd = true;
#else
constexpr bool is_freebsd = false;
#endif
#if defined(__NetBSD__)
constexpr bool is_netbsd = true;
#else
constexpr bool is_netbsd = false;
#endif
#if defined(__OpenBSD__)
constexpr bool is_openbsd = true;
#else
constexpr bool is_openbsd = false;
#endif

constexpr bool is_bsd = is_freebsd || is_openbsd || is_netbsd;
constexpr bool is_posix =
    is_macos || is_linux || is_android || is_cygwin || is_bsd;

#if defined(_WIN32)
using NativeHandle = HANDLE;
#else   // _WIN32
using NativeHandle = int;
#endif  // !_WIN32
namespace {
#if !defined(_WIN32)
inline bool fd_is_open(
    int fd) {  // Made static as it's a utility in anonymous namespace
  if (fd < 0) {
    return false;
  }
  if (fcntl(fd, F_GETFL) == -1 && errno == EBADF) {
    return false;  // fd 无效
  }
  return true;
}
#endif  // !_WIN32

#if defined(_WIN32)
std::string get_last_error_msg() {
  DWORD error = GetLastError();
  LPVOID errorMsg;
  std::stringstream out;
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL, error,
                0,  // Default language
                (LPTSTR)&errorMsg, 0, NULL);
  out << "Error " << error << ": " << (char *)errorMsg;
  LocalFree(errorMsg);
  return out.str();
}
#endif  // _WIN32

inline std::filesystem::path find_executable_in_path(
    std::string const &exe_file) {
#ifdef _WIN32
  char separator = '\\';
  char path_env_sep = ';';
#else
  char separator = '/';
  char path_env_sep = ':';
#endif
  auto split_env = [](const char *env, char sep) {
    std::istringstream stream;
#if defined(_WIN32)
    char *buf{nullptr};
    size_t len = 0;
    _dupenv_s(&buf, &len, env);
    if (buf != nullptr) {
      stream.str(std::string(buf));
      free(buf);
    }
#else
    stream.str(getenv(env));
#endif

    std::vector<std::string> ret;
    std::string line;
    while (std::getline(stream, line, sep)) {
      if (!line.empty()) {
        ret.push_back(line);
      }
    }
    return ret;
  };
  if (exe_file.find(separator) == std::string::npos) {
    std::vector<std::string> paths{split_env("PATH", path_env_sep)};
#ifdef _WIN32
    std::vector<std::string> path_exts{split_env("PATHEXT", path_env_sep)};
#endif
    for (auto &p : paths) {
#ifdef _WIN32
      // Windows: Prefer PATHEXT, then the name itself if it's a file.
      // If exe_file already has an extension, check it directly.
      std::filesystem::path current_exe_path =
          std::filesystem::path(p) / exe_file;
      if (std::filesystem::path(exe_file).has_extension()) {
        if (exists(current_exe_path) &&
            std::filesystem::is_regular_file(current_exe_path)) {
          return current_exe_path;
        }
      } else {
        // No extension in exe_file, try with PATHEXT first
        for (auto &ext : path_exts) {
          // Ensure ext starts with a dot or handle cases where it might not.
          // PATHEXT entries are usually like ".EXE", ".COM".
          std::string exe_with_ext = exe_file;
          if (!ext.empty() && ext[0] != '.') {
            // This case should ideally not happen if PATHEXT is parsed
            // correctly. but as a safeguard if ext is "EXE" instead of ".EXE"
            exe_with_ext += ".";
          }
          exe_with_ext += ext;
          auto f = std::filesystem::path(p) / exe_with_ext;
          if (exists(f) && std::filesystem::is_regular_file(f)) {
            return f;
          }
        }
        // If not found with any PATHEXT, check the original name (e.g.,
        // "myprog" itself might be executable)
        if (exists(current_exe_path) &&
            std::filesystem::is_regular_file(current_exe_path)) {
          return current_exe_path;
        }
      }
#else  // POSIX
      auto f = std::filesystem::path(p) / exe_file;
      // On POSIX, check for executable permission and if it's a regular file.
      if (std::filesystem::is_regular_file(f) && 0 == access(f.c_str(), X_OK)) {
        return f;
      }
#endif
    }
  }
  return {};  // Not found
}

inline std::map<std::string, std::string> get_current_environment_variables() {
  std::map<std::string, std::string> envMap;

#ifdef _WIN32
  // Windows implementation
#if defined(UNICODE)
  char *envBlock = GetEnvironmentStringsA();
#else
  char *envBlock = GetEnvironmentStrings();
#endif
  if (envBlock == nullptr) {
    std::cerr << "Error getting environment strings." << std::endl;
    return envMap;  // Return an empty map in case of error
  }

  char *currentEnv = envBlock;
  while (*currentEnv != '\0') {
    std::string envString(currentEnv);
    size_t pos = envString.find('=');
    if (pos != std::string::npos) {
      std::string key = envString.substr(0, pos);
      std::string value = envString.substr(pos + 1);
      envMap[key] = value;
    }
    currentEnv +=
        envString.length() + 1;  // Move to the next environment variable
  }

#if defined(UNICODE)
  FreeEnvironmentStringsA(envBlock);
#else
  FreeEnvironmentStrings(envBlock);
#endif

#else

  if (environ == nullptr) {
    return envMap;  // Return an empty map in case of error
  }

  for (char **env = environ; *env != nullptr; ++env) {
    std::string envString(*env);
    size_t pos = envString.find('=');
    if (pos != std::string::npos) {
      std::string key = envString.substr(0, pos);
      std::string value = envString.substr(pos + 1);
      envMap[key] = value;
    }
  }
#endif
  return envMap;
}
}  // namespace

class Stdio {
  friend class subprocess;

 public:
  Stdio() : redirect_(std::nullopt) {}
  Stdio(NativeHandle fd) : redirect_(fd) {}
  Stdio(std::string const &file) : redirect_(file) {}
  Stdio(std::vector<char> &buf) : redirect_(std::ref(buf)) {}
  void prepare_redirection() {
    if (!redirect_.has_value()) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, NativeHandle>) {
#if !defined(_WIN32)
            if (!fd_is_open(value)) {  // 'value' is the NativeHandle (int fd)
              throw std::runtime_error{
                  "Provided file descriptor is not valid: " +
                  std::to_string(value)};
            }
#else
            if (value == INVALID_HANDLE_VALUE || value == nullptr) {
              throw std::runtime_error{"Provided native handle is invalid."};
            }
        // Further checks like GetHandleInformation could be added for more
        // robustness
#endif
          } else if constexpr (std::is_same_v<T, std::string>) {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.lpSecurityDescriptor = nullptr;
            sa.bInheritHandle = TRUE;  // Make the handle inheritable

            const HANDLE hFile = CreateFileA(
                value.c_str(),
                this->fileno() == 0
                    ? GENERIC_READ  // Desired access
                    : (GENERIC_WRITE |
                       (this->is_append() ? FILE_APPEND_DATA : 0)),
                this->fileno() == 0 ? FILE_SHARE_READ : 0,  // Share mode
                &sa,  // Security attributes
                this->fileno() == 0
                    ? OPEN_EXISTING
                    : (this->is_append()
                           ? OPEN_ALWAYS
                           : CREATE_ALWAYS),  // Creation disposition
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
              throw std::runtime_error{"open failed: " + value + ", error: " +
                                       std::to_string(GetLastError())};
            }
            this->redirect_.value() = hFile;
#else
            auto fd =
                this->fileno() == 0
                    ? open(value.c_str(), O_RDONLY)
                    : open(value.c_str(),
                           this->is_append() ? (O_WRONLY | O_CREAT | O_APPEND)
                                             : (O_WRONLY | O_CREAT | O_TRUNC),
                           0644);
            if (fd == -1) {
              throw std::runtime_error{"open failed: " + value};
            }
            this->redirect_.value() = fd;
#endif
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     std::vector<char>>>) {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES at;
            at.bInheritHandle = true;
            at.nLength = sizeof(SECURITY_ATTRIBUTES);
            at.lpSecurityDescriptor = nullptr;

            if (!CreatePipe(&(this->pipe_fds_[0]), &(this->pipe_fds_[1]), &at,
                            0)) {
              throw std::runtime_error{"pipe failed: " +
                                       std::to_string(GetLastError())};
            }

        // child process close
        // if (!SetHandleInformation(
        //         this->pipe_fds_[this->fileno() == 0 ? 1 : 0],
        //         HANDLE_FLAG_INHERIT, 0)) {
        //   throw std::runtime_error("SetHandleInformation Failed: " +
        //                            std::to_string(GetLastError()));
        // }

#else
            if (-1 == pipe(this->pipe_fds_)) {
              throw std::runtime_error{"pipe failed"};
            }
#endif
          } else {
          }
        },
        redirect_.value());
  }
  void close_unused_pipe_ends_in_parent() {
    if (!redirect_.has_value()) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, NativeHandle>) {
          } else if constexpr (std::is_same_v<T, std::string>) {
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     std::vector<char>>>) {
#if defined(_WIN32)
            CloseHandle(pipe_fds_[this->fileno() == 0 ? 0 : 1]);
#else
            close(pipe_fds_[this->fileno() == 0 ? 0 : 1]);
#endif
          }
        },
        redirect_.value());
  }
#if defined(_WIN32)
  std::optional<NativeHandle> get_child_process_stdio_handle() {
    if (!redirect_.has_value()) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>(
            [[maybe_unused]] T &value) -> std::optional<NativeHandle> {
          if constexpr (std::is_same_v<T, NativeHandle>) {
            return value;
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     std::vector<char>>>) {
            return pipe_fds_[this->fileno() == 0 ? 0 : 1];
          } else {
            return std::nullopt;
          }
        },
        redirect_.value());
  }
  std::optional<NativeHandle> get_parent_communication_pipe_handle() {
    if (!redirect_.has_value()) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>(
            [[maybe_unused]] T &value) -> std::optional<NativeHandle> {
          if constexpr (std::is_same_v<
                            T, std::reference_wrapper<std::vector<char>>>) {
            return pipe_fds_[this->fileno() == 0 ? 1 : 0];
          } else {
            return std::nullopt;
          }
        },
        redirect_.value());
  }
#endif
#if !defined(_WIN32)
  void setup_stdio_in_child_process() {
    if (!redirect_.has_value()) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, NativeHandle>) {
            if (value >= 0 && value != this->fileno()) {
              dup2(value, this->fileno());
            }
          } else if constexpr (std::is_same_v<T, std::string>) {
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     std::vector<char>>>) {
            dup2(pipe_fds_[this->fileno() == 0 ? 0 : 1], this->fileno());
            close(pipe_fds_[this->fileno() == 0 ? 1 : 0]);
          }
        },
        redirect_.value());
  }
  std::optional<NativeHandle> get_parent_pipe_fd_for_polling() {
    if (!redirect_.has_value()) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>(
            [[maybe_unused]] T &value) -> std::optional<NativeHandle> {
          if constexpr (std::is_same_v<
                            T, std::reference_wrapper<std::vector<char>>>) {
            return pipe_fds_[this->fileno() == 0 ? 1 : 0];
          } else {
            return std::nullopt;
          }
        },
        redirect_.value());
  }
#endif  // !_WIN32
  virtual int fileno() const = 0;
  virtual bool is_append() const = 0;

 protected:
  std::optional<std::variant<
      NativeHandle, std::reference_wrapper<std::vector<char>>, std::string>>
      redirect_{std::nullopt};
  NativeHandle pipe_fds_[2]{
#if defined(_WIN32)
      nullptr, nullptr
#else
      -1, -1
#endif
  };
};

class Stdin : public Stdio {
 public:
  using Stdio::Stdio;
  int fileno() const override { return 0; }
  bool is_append() const override { return false; }
};
class Stdout : public Stdio {
 public:
  using Stdio::Stdio;
  Stdout(std::string const &file, bool append) : Stdio(file), append_(append) {}
  int fileno() const override { return 1; }
  bool is_append() const override { return append_; }

 private:
  bool append_{false};
};
class Stderr : public Stdio {
 public:
  using Stdio::Stdio;
  Stderr(std::string const &file, bool append) : Stdio(file), append_(append) {}
  int fileno() const override { return 2; }
  bool is_append() const override { return append_; }

 private:
  bool append_{false};
};

struct stdin_redirector {
  Stdin operator<(NativeHandle fd) const { return Stdin{fd}; }
  Stdin operator<(std::string const &file) const { return Stdin{file}; }
  Stdin operator<(std::vector<char> &buf) const { return Stdin{buf}; }
};

struct stdout_redirector {
#if defined(_WIN32)
  Stdout operator>(int fd) const {
    if (fd == 0) {
      return Stdout{GetStdHandle(STD_INPUT_HANDLE)};
    } else if (fd == 1) {
      return Stdout{GetStdHandle(STD_OUTPUT_HANDLE)};
    } else if (fd == 2) {
      return Stdout{GetStdHandle(STD_ERROR_HANDLE)};
    }
    throw std::runtime_error("should redirect to 0,1,2");
  }
#endif  // _WIN32
  Stdout operator>(NativeHandle fd) const { return Stdout{fd}; }
  Stdout operator>(std::string const &file) const { return Stdout{file}; }
  Stdout operator>(std::vector<char> &buf) const { return Stdout{buf}; }

  Stdout operator>>(std::string const &file) const {
    return Stdout{file, true};
  }
};

struct stderr_redirector {
#if defined(_WIN32)
  Stderr operator>(int fd) const {
    if (fd == 0) {
      return Stderr{GetStdHandle(STD_INPUT_HANDLE)};
    } else if (fd == 1) {
      return Stderr{GetStdHandle(STD_OUTPUT_HANDLE)};
    } else if (fd == 2) {
      return Stderr{GetStdHandle(STD_ERROR_HANDLE)};
    }
    throw std::runtime_error("should redirect to 0,1,2");
  }
#endif
  Stderr operator>(NativeHandle fd) const { return Stderr{fd}; }
  Stderr operator>(std::string const &file) const { return Stderr{file}; }
  Stderr operator>(std::vector<char> &buf) const { return Stderr{buf}; }
  Stderr operator>>(std::string const &file) const {
    return Stderr{file, true};
  }
};

struct Cwd {
  std::filesystem::path cwd;
};

struct Env {
  std::map<std::string, std::string> env;
};

struct EnvItemAppend {
  EnvItemAppend &operator=(EnvItemAppend const &) = delete;
  EnvItemAppend &operator+=(std::string val) {
    kv.second = val;
    return *this;
  }
  std::pair<std::string, std::string> kv;
};

struct cwd_operator {
  Cwd operator=(std::string const &p) { return Cwd{p}; }
};
struct env_operator {
  Env operator=(std::map<std::string, std::string> env) {
    return Env{std::move(env)};
  }
  Env operator+=(std::map<std::string, std::string> env) {
    std::map<std::string, std::string> env_tmp{
        get_current_environment_variables()};
    env_tmp.insert(env.begin(), env.end());
    return Env{std::move(env_tmp)};
  }
  EnvItemAppend operator[](std::string key) { return EnvItemAppend{{key, ""}}; }
};

namespace named_arguments {
[[maybe_unused]] inline static auto devnull =
    std::filesystem::path("/dev/null");
[[maybe_unused]] inline static stdin_redirector std_in;
[[maybe_unused]] inline static stdout_redirector std_out;
[[maybe_unused]] inline static stderr_redirector std_err;
[[maybe_unused]] inline static cwd_operator cwd;
[[maybe_unused]] inline static env_operator env;

[[maybe_unused]] inline static auto $devnull =
    std::filesystem::path("/dev/null");
[[maybe_unused]] inline static stdin_redirector $stdin;
[[maybe_unused]] inline static stdout_redirector $stdout;
[[maybe_unused]] inline static stderr_redirector $stderr;
[[maybe_unused]] inline static cwd_operator $cwd;
[[maybe_unused]] inline static env_operator $env;
}  // namespace named_arguments

using namespace named_arguments;

class subprocess {
  template <typename... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };
  template <typename... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;

 public:
  subprocess(std::vector<std::string> cmd, Stdin in, Stdout out, Stderr err,
             std::string working_directory = {},
             std::map<std::string, std::string> environments = {})
      : _cmd{std::move(cmd)},
        _cwd{std::move(working_directory)},
        _env{std::move(environments)},
        _stdin{std::move(in)},
        _stdout{std::move(out)},
        _stderr{std::move(err)} {}

  int run() {
    prepare_all_stdio_redirections();
#if defined(_WIN32)
    PROCESS_INFORMATION pInfo;
    STARTUPINFOA sInfo;
    ZeroMemory(&pInfo, sizeof(pInfo));
    ZeroMemory(&sInfo, sizeof(sInfo));
    sInfo.cb = sizeof(sInfo);
    auto in = _stdin.get_child_process_stdio_handle();
    sInfo.hStdInput =
        in.has_value() ? in.value() : GetStdHandle(STD_INPUT_HANDLE);
    auto out = _stdout.get_child_process_stdio_handle();
    sInfo.hStdOutput =
        out.has_value() ? out.value() : GetStdHandle(STD_OUTPUT_HANDLE);
    auto err = _stderr.get_child_process_stdio_handle();
    sInfo.hStdError =
        err.has_value() ? err.value() : GetStdHandle(STD_ERROR_HANDLE);

    sInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::vector<char> command;
    for (auto const &cmd : _cmd) {
      if (!command.empty()) {
        command.push_back(' ');
      }
      auto need_quota = std::any_of(
          cmd.begin(), cmd.end(), [](char c) { return c <= ' ' || c == '"'; });
      if (need_quota) {
        command.push_back('"');
      }
      if (need_quota) {
        for (auto c : cmd) {
          if (c == '"') {
            command.push_back('\\');
          }
          command.push_back(c);
        }
      } else {
        command.insert(command.end(), cmd.begin(), cmd.end());
      }
      if (need_quota) {
        command.push_back('"');
      }
    }
    command.push_back('\0');

    std::vector<char> env_block;
    for (auto const &[key, value] : _env) {
      env_block.insert(env_block.end(), key.begin(), key.end());
      env_block.push_back('=');
      env_block.insert(env_block.end(), value.begin(), value.end());
      env_block.push_back('\0');
    }
    if (!env_block.empty()) {
      env_block.push_back('\0');
    }

    auto success = CreateProcessA(
        nullptr, command.data(),
        NULL,  // Process handle not inheritable
        NULL,  // Thread handle not inheritable
        TRUE,  // !!! Set handle inheritance to TRUE
        0,
        env_block.empty() ? nullptr : env_block.data(),  // environment block
        _cwd.empty() ? nullptr : _cwd.data(),            // working directory
        &sInfo,  // Pointer to STARTUPINFO structure
        &pInfo   // Pointer to PROCESS_INFORMATION structure
    );

    if (!success) {
      std::cerr << get_last_error_msg() << '\n';
      return 127;
    }

    auto ret = manage_pipe_io_and_wait_for_exit(pInfo.hProcess);

    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);
    return ret;
#else
    auto pid = fork();
    if (pid < 0) {
      throw std::runtime_error("fork failed");
    } else if (pid == 0) {
      execute_command_in_child();
      return 127;
    } else {
      return manage_pipe_io_and_wait_for_exit(pid);
    }
#endif
  }

 private:
  void prepare_all_stdio_redirections() {
    _stdin.prepare_redirection();
    _stdout.prepare_redirection();
    _stderr.prepare_redirection();
  }
  int manage_pipe_io_and_wait_for_exit(NativeHandle pid) {
    _stdin.close_unused_pipe_ends_in_parent();
    _stdout.close_unused_pipe_ends_in_parent();
    _stderr.close_unused_pipe_ends_in_parent();

#if defined(_WIN32)
    auto in = _stdin.get_parent_communication_pipe_handle();
    auto out = _stdout.get_parent_communication_pipe_handle();
    auto err = _stderr.get_parent_communication_pipe_handle();
    NativeHandle fds[3]{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE,
                        INVALID_HANDLE_VALUE};
    if (in.has_value()) {
      fds[0] = in.value();
    }
    if (out.has_value()) {
      fds[1] = out.value();
    }
    if (err.has_value()) {
      fds[2] = err.value();
    }

    std::string_view stdin_str{};
    if (in.has_value()) {
      auto &tmp = std::get<std::reference_wrapper<std::vector<char>>>(
                      _stdin.redirect_.value())
                      .get();
      stdin_str = std::string_view{tmp.data(), tmp.size()};
      DWORD written{0};
      do {
        DWORD len{0};
        if (!WriteFile(in.value(), stdin_str.data(), stdin_str.size(), &len,
                       0)) {
          break;
        }
        written += len;
      } while (written < stdin_str.size());
    }

    auto readData = [](NativeHandle fd, std::vector<char> &value) {
      char buf[1024];
      DWORD readed{0};
      while (ReadFile(fd, buf, sizeof(buf), &readed, 0) && readed > 0) {
        value.insert(value.end(), buf, buf + readed);
      }
    };

    if (out.has_value()) {
      readData(out.value(), std::get<std::reference_wrapper<std::vector<char>>>(
                                _stdout.redirect_.value())
                                .get());
    }
    if (err.has_value()) {
      readData(err.value(), std::get<std::reference_wrapper<std::vector<char>>>(
                                _stderr.redirect_.value())
                                .get());
    }
    for (auto const h : fds) {
      if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
      }
    }

    WaitForSingleObject(pid, INFINITE);
    DWORD ret;
    GetExitCodeProcess(pid, &ret);
    return ret;
#else
    struct pollfd fds[3]{{.fd = -1, .events = POLLOUT, .revents = 0},
                         {.fd = -1, .events = POLLIN, .revents = 0},
                         {.fd = -1, .events = POLLIN, .revents = 0}};

    for (auto *stdio :
         {(Stdio *)&_stdin, (Stdio *)&_stdout, (Stdio *)&_stderr}) {
      if (auto fd = stdio->get_parent_pipe_fd_for_polling(); fd.has_value()) {
        fds[stdio->fileno()].fd = fd.value();
      }
    }
    std::string_view stdin_str{};
    if (_stdin.get_parent_pipe_fd_for_polling().has_value()) {
      auto &tmp = std::get<std::reference_wrapper<std::vector<char>>>(
                      _stdin.redirect_.value())
                      .get();
      stdin_str = std::string_view{tmp.data(), tmp.size()};
    }

    while (fds[0].fd != -1 || fds[1].fd != -1 || fds[2].fd != -1) {
      int ret = poll(fds, 3, -1);
      if (ret == -1) {
        throw std::runtime_error("poll failed!");
      }
      if (ret == 0) {
        break;
      }
      if (fds[0].fd != -1 && (fds[0].revents & POLLOUT)) {
        if (stdin_str.empty()) {
          close(fds[0].fd);
          fds[0].fd = -1;
        } else {
          auto len = write(fds[0].fd, stdin_str.data(), stdin_str.size());
          if (len > 0) {
            stdin_str.remove_prefix(static_cast<size_t>(len));
          }
          if (len < 0) {
            close(fds[0].fd);
            fds[0].fd = -1;
          }
        }
      }
      if (fds[1].fd != -1 && (fds[1].revents & POLLIN)) {
        char buf[1024];
        auto len = read(fds[1].fd, buf, 1024);
        if (len > 0) {
          auto &tmp = std::get<std::reference_wrapper<std::vector<char>>>(
                          _stdout.redirect_.value())
                          .get();
          tmp.insert(tmp.end(), buf, buf + len);
        }
        if (len == 0) {
          close(fds[1].fd);
          fds[1].fd = -1;
        }
        if (len < 0) {
          std::string err{"read failed: "};
          err += strerror(errno);
          throw std::runtime_error(err);
        }
      }
      if (fds[2].fd != -1 && (fds[2].revents & POLLIN)) {
        char buf[1024];
        auto len = read(fds[2].fd, buf, 1024);
        if (len > 0) {
          auto &tmp = std::get<std::reference_wrapper<std::vector<char>>>(
                          _stderr.redirect_.value())
                          .get();
          tmp.insert(tmp.end(), buf, buf + len);
        }
        if (len == 0) {
          close(fds[2].fd);
          fds[2].fd = -1;
        }
        if (len < 0) {
          std::string err{"read failed: "};
          err += strerror(errno);
          throw std::runtime_error(err);
        }
      }
      if (fds[0].fd != -1 &&
          (fds[0].revents & POLLNVAL || fds[0].revents & POLLHUP ||
           fds[0].revents & POLLERR)) {
        close(fds[0].fd);
        fds[0].fd = -1;
      }
      if (fds[1].fd != -1 &&
          (fds[1].revents & POLLNVAL || fds[1].revents & POLLHUP ||
           fds[1].revents & POLLERR)) {
        close(fds[1].fd);
        fds[1].fd = -1;
      }
      if (fds[2].fd != -1 &&
          (fds[2].revents & POLLNVAL || fds[2].revents & POLLHUP ||
           fds[2].revents & POLLERR)) {
        close(fds[2].fd);
        fds[2].fd = -1;
      }
      if (fds[0].fd == -1 && fds[1].fd == -1 && fds[2].fd == -1) {
        break;
      }
    }

    int status;
    waitpid(pid, &status, 0);
    auto return_code = -1;
    if (WIFEXITED(status)) {
      return_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      return_code = 128 + (WTERMSIG(status));
    }
    return return_code;
#endif
  }
#if !defined(_WIN32)
  void execute_command_in_child() {
    _stdin.setup_stdio_in_child_process();
    _stdout.setup_stdio_in_child_process();
    _stderr.setup_stdio_in_child_process();

    std::vector<char *> cmd{};
    std::transform(_cmd.begin(), _cmd.end(), std::back_inserter(cmd),
                   [](std::string &s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!_cwd.empty() && (-1 == chdir(_cwd.data()))) {
      // strerror_r or equivalent for thread-safe errno string is better for
      // libraries
      throw std::runtime_error("chdir failed for path: " + _cwd +
                               " error: " + strerror(errno));
    }

    std::string exe_to_exec = _cmd[0];
    // If exe_to_exec does not contain a path separator, search for it in PATH
    if (exe_to_exec.find('/') == std::string::npos) {
      std::filesystem::path resolved_path =
          find_executable_in_path(exe_to_exec);
      if (!resolved_path.empty()) {
        exe_to_exec = resolved_path.string();
      }
      // If not found in PATH, execv/execve will search CWD or fail, which is
      // standard.
    }

    if (!_env.empty()) {
      std::vector<std::string> env_tmp{};

      std::transform(
          _env.begin(), _env.end(), std::back_inserter(env_tmp),
          [](auto &entry) { return entry.first + "=" + entry.second; });

      std::vector<char *> envs{};
      std::transform(env_tmp.begin(), env_tmp.end(), std::back_inserter(envs),
                     [](auto &s) { return s.data(); });
      envs.push_back(nullptr);
      execve(exe_to_exec.c_str(), cmd.data(), envs.data());
    } else {
      execv(exe_to_exec.c_str(), cmd.data());
    }
    // This output to std::cerr in a library might be undesirable.
    // Consider throwing an exception or having a configurable error handler.
    // For now, keeping original behavior.
    std::cerr << "exec failed for: " << exe_to_exec
              << ", error: " << strerror(errno) << '\n';
    _Exit(127);  // _Exit is correct here to not call atexit handlers etc.
  }
#endif  // !_WIN32

 private:
  std::vector<std::string> _cmd;
  std::string _cwd{};
  std::map<std::string, std::string> _env;
  Stdin _stdin;
  Stdout _stdout;
  Stderr _stderr;
};

#if __cplusplus >= 202002L
template <typename T>
concept is_run_args_type = std::is_same_v<Env, std::decay_t<T>> ||
                           std::is_same_v<Stdin, std::decay_t<T>> ||
                           std::is_same_v<Stdout, std::decay_t<T>> ||
                           std::is_same_v<Stderr, std::decay_t<T>> ||
                           std::is_same_v<Cwd, std::decay_t<T>> ||
                           std::is_same_v<EnvItemAppend, std::decay_t<T>>;
#endif

template <typename... T>
#if __cplusplus >= 202002L
  requires(is_run_args_type<T> && ...)
#endif
inline int run(std::vector<std::string> cmd, T &&...args) {
  Stdin stdin_;
  Stdout stdout_;
  Stderr stderr_;
  std::string working_directory;
  std::map<std::string, std::string> environments;
  std::vector<std::pair<std::string, std::string>> env_appends;
  (void)(..., ([&]<typename Arg>(Arg &&arg) {
           using ArgType = std::decay_t<Arg>;
           if constexpr (std::is_same_v<ArgType, Stdin>) {
             stdin_ = std::forward<Arg>(arg);
           }
           if constexpr (std::is_same_v<ArgType, Stdout>) {
             stdout_ = std::forward<Arg>(arg);
           }
           if constexpr (std::is_same_v<ArgType, Stderr>) {
             stderr_ = std::forward<Arg>(arg);
           }
           if constexpr (std::is_same_v<ArgType, Env>) {
             environments.insert(arg.env.begin(), arg.env.end());
           }
           if constexpr (std::is_same_v<ArgType, EnvItemAppend>) {
             env_appends.push_back(arg.kv);
           }
           if constexpr (std::is_same_v<ArgType, Cwd>) {
             working_directory = arg.cwd.string();
           }
           static_assert(std::is_same_v<Env, std::decay_t<T>> ||
                             std::is_same_v<Stdin, std::decay_t<T>> ||
                             std::is_same_v<Stdout, std::decay_t<T>> ||
                             std::is_same_v<Stderr, std::decay_t<T>> ||
                             std::is_same_v<Cwd, std::decay_t<T>> ||
                             std::is_same_v<EnvItemAppend, std::decay_t<T>>,
                         "Invalid argument type passed to run function.");
         }(std::forward<T>(args))));
  return subprocess(std::move(cmd), stdin_, stdout_, stderr_,
                    std::move(working_directory), std::move(environments))
      .run();
}

template <typename... T>
#if __cplusplus >= 202002L
  requires(is_run_args_type<T> && ...)
#endif
inline int $(std::vector<std::string> cmd, T &&...args) {
  return run(std::move(cmd), std::forward<T>(args)...);
}

inline std::string const &home() {
  static std::string home_dir = []() {
    auto envs = get_current_environment_variables();
#if defined(_WIN32)
    return envs["HOMEDRIVE"] + envs["HOMEPATH"];
#else
    return envs["HOME"];
#endif
  }();
  return home_dir;
}

inline std::map<std::string, std::string> environments() {
  return get_current_environment_variables();
}

#if defined(_WIN32)
using pid_type = unsigned long;
#else
using pid_type = int;
#endif
#if (defined(_WIN32) || defined(__FreeBSD__) || defined(__DragonFly__) || \
     defined(__NetBSD__) || defined(__sun))
constexpr static NativeHandle root_pid = 0;
#elif (defined(__APPLE__) && defined(__MACH__) || defined(__linux__) || \
       defined(__ANDROID__) || defined(__OpenBSD__))
constexpr static NativeHandle root_pid = 1;
#endif
inline pid_type pid() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

}  // namespace process

using process::$;
using process::named_arguments::$cwd;
using process::named_arguments::$devnull;
using process::named_arguments::$env;
using process::named_arguments::$stderr;
using process::named_arguments::$stdin;
using process::named_arguments::$stdout;
#endif  // __SUBPROCESS_HPP__
