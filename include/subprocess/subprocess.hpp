#ifndef __SUBPROCESS_SUBPROCESS_HPP__
#define __SUBPROCESS_SUBPROCESS_HPP__

#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || \
    (!defined(_MSVC_LANG) && __cplusplus < 201703L)
#error "This code requires C++17 or later."
#endif

#if defined(_MSVC_LANG)
#define CPLUSPLUS_VERSION _MSVC_LANG
#else
#define CPLUSPLUS_VERSION __cplusplus
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
#include <spawn.h>
#endif
#include <sys/select.h>
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
#include <thread>
#include <variant>
#include <vector>

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

#ifndef USE_DOLLAR_NAMED_VARIABLES
#define USE_DOLLAR_NAMED_VARIABLES 1
#endif

#if defined(_WIN32)
using NativeHandle = HANDLE;
const static inline NativeHandle INVALID_NATIVE_HANDLE_VALUE =
    INVALID_HANDLE_VALUE;
#else   // _WIN32
using NativeHandle = int;
constexpr NativeHandle INVALID_NATIVE_HANDLE_VALUE = -1;
#endif  // !_WIN32

namespace {
void close_native_handle(NativeHandle &handle) {
  if (handle != INVALID_NATIVE_HANDLE_VALUE) {
#if defined(_WIN32)
    CloseHandle(handle);
#else
    close(handle);
#endif
    handle = INVALID_NATIVE_HANDLE_VALUE;
  }
}
}  // namespace

namespace detail {
class HandleGuard {
 public:
  explicit HandleGuard(NativeHandle h =
#if defined(_WIN32)
                           INVALID_HANDLE_VALUE
#else
                           -1
#endif
                       )
      : handle_(h) {
  }
  ~HandleGuard() { Close(); }
  HandleGuard(const HandleGuard &) = delete;
  HandleGuard &operator=(const HandleGuard &) = delete;
  HandleGuard(HandleGuard &&other) noexcept : handle_(other.handle_) {
    other.handle_ =
#if defined(_WIN32)
        INVALID_HANDLE_VALUE
#else
        -1
#endif
        ;
  }
  HandleGuard &operator=(HandleGuard &&other) noexcept {
    if (this != &other) {
      Close();
      handle_ = other.handle_;
      other.handle_ =
#if defined(_WIN32)
          INVALID_HANDLE_VALUE
#else
          -1
#endif
          ;
    }
    return *this;
  }

  NativeHandle get() const { return handle_; }
  NativeHandle *p_get() { return &handle_; }

  void Close() { close_native_handle(handle_); }

  bool IsValid() const {
#if defined(_WIN32)
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return handle_ != -1;
#endif
  }

 private:
  NativeHandle handle_;
};
}  // namespace detail

namespace {

#if defined(_WIN32)
inline std::vector<char> create_command_string_data(
    std::vector<std::string> const &cmds) {
  std::vector<char> command;
  for (auto const &cmd : cmds) {
    if (!command.empty()) {
      command.push_back(' ');
    }
    if (cmd.empty()) {
      command.push_back('"');
      command.push_back('"');
      continue;
    }
    auto need_quota = std::any_of(cmd.begin(), cmd.end(),
                                  [](char c) { return c <= ' ' || c == '"'; });
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
  return command;
}

inline std::vector<char> create_environment_string_data(
    std::map<std::string, std::string> const &envs) {
  std::vector<char> env_block;
  for (auto const &[key, value] : envs) {
    env_block.insert(env_block.end(), key.begin(), key.end());
    env_block.push_back('=');
    env_block.insert(env_block.end(), value.begin(), value.end());
    env_block.push_back('\0');
  }
  if (!env_block.empty()) {
    env_block.push_back('\0');
  }
  return env_block;
}

#endif

inline std::string get_last_error_msg() {
#if defined(_WIN32)
  DWORD error = GetLastError();
  LPVOID errorMsg{NULL};
  std::stringstream out;
  out << "Error: " << error;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPSTR)&errorMsg, 0, NULL);
  if (errorMsg) {
    out << ": " << (char *)errorMsg;
    LocalFree(errorMsg);
  }
  return out.str();
#else   // _WIN32
  int error = errno;
  auto *err_msg = strerror(error);
  if (err_msg) {
    return std::string(err_msg);
  } else {
    return "Unknown error or strerror failed, error code: " +
           std::to_string(errno);
  }
#endif  // !_WIN32
}

inline void write_to_native_handle(NativeHandle &fd,
                                   std::vector<char> &write_data) {
  detail::HandleGuard auto_closed(fd);
  std::string_view write_view{write_data.begin(), write_data.end()};
  while (!write_view.empty()) {
#if defined(_WIN32)
    DWORD write_count{0};
    if (!WriteFile(fd, write_view.data(), static_cast<DWORD>(write_view.size()),
                   &write_count, 0)) {
      throw std::runtime_error("WriteFile error: " + get_last_error_msg());
    }
    if (write_count > 0) {
      write_view.remove_prefix(static_cast<size_t>(write_count));
    }
#else
    auto write_count = write(fd, write_view.data(), write_view.size());
    if (write_count > 0) {
      write_view.remove_prefix(static_cast<size_t>(write_count));
    }
    if (write_count == -1) {
      throw std::runtime_error("write error: " + std::to_string(errno));
    }
#endif
  }
  fd = INVALID_NATIVE_HANDLE_VALUE;
}

inline void read_from_native_handle(NativeHandle &fd,
                                    std::vector<char> &out_buf) {
  detail::HandleGuard auto_closed(fd);
  char buf[1024];
#if defined(_WIN32)
  DWORD read_count{0};
  while (ReadFile(fd, buf, sizeof(buf), &read_count, 0) && read_count > 0) {
    out_buf.insert(out_buf.end(), buf, buf + read_count);
  }
#else
  ssize_t read_count = 0;
  do {
    read_count = read(fd, buf, std::size(buf));
    if (read_count > 0) {
      out_buf.insert(out_buf.end(), buf, buf + read_count);
    }
    if (read_count == -1) {
      throw std::runtime_error(get_last_error_msg());
    }
  } while (read_count > 0);
#endif
  fd = INVALID_NATIVE_HANDLE_VALUE;
}

#if !defined(_WIN32)
[[maybe_unused]] inline void multiplexing_use_poll(
    NativeHandle &in, std::vector<char> &in_buf, NativeHandle &out,
    std::vector<char> &out_buf, NativeHandle &err, std::vector<char> &err_buf) {
  struct pollfd fds[3]{{.fd = in, .events = POLLOUT, .revents = 0},
                       {.fd = out, .events = POLLIN, .revents = 0},
                       {.fd = err, .events = POLLIN, .revents = 0}};

  std::string_view stdin_str{in_buf.begin(), in_buf.end()};

  char buf[1024];
  while (fds[0].fd != INVALID_NATIVE_HANDLE_VALUE ||
         fds[1].fd != INVALID_NATIVE_HANDLE_VALUE ||
         fds[2].fd != INVALID_NATIVE_HANDLE_VALUE) {
    int poll_count = poll(fds, 3, -1);
    if (poll_count == -1) {
      throw std::runtime_error("poll failed!");
    }
    if (poll_count == 0) {
      break;
    }
    if (fds[0].fd != INVALID_NATIVE_HANDLE_VALUE &&
        (fds[0].revents & POLLOUT)) {
      auto write_count = write(fds[0].fd, stdin_str.data(), stdin_str.size());
      if (write_count > 0) {
        stdin_str.remove_prefix(static_cast<size_t>(write_count));
      }
      if (write_count == -1) {
        throw std::runtime_error("write error: " + std::to_string(errno));
      }
      if (stdin_str.empty()) {
        close_native_handle(fds[0].fd);
      }
    }
    if (fds[1].fd != INVALID_NATIVE_HANDLE_VALUE && (fds[1].revents & POLLIN)) {
      auto read_count = read(fds[1].fd, buf, std::size(buf));
      if (read_count > 0) {
        out_buf.insert(out_buf.end(), buf, buf + read_count);
      }
      if (read_count == 0) {
        close_native_handle(fds[1].fd);
      }
      if (read_count == -1) {
        throw std::runtime_error(get_last_error_msg());
      }
    }
    if (fds[2].fd != INVALID_NATIVE_HANDLE_VALUE && (fds[2].revents & POLLIN)) {
      auto read_count = read(fds[2].fd, buf, std::size(buf));
      if (read_count > 0) {
        err_buf.insert(err_buf.end(), buf, buf + read_count);
      }
      if (read_count == 0) {
        close_native_handle(fds[2].fd);
      }
      if (read_count == -1) {
        throw std::runtime_error(get_last_error_msg());
      }
    }
    for (auto &pfd : fds) {
      if (pfd.fd != INVALID_NATIVE_HANDLE_VALUE &&
          (pfd.revents & (POLLNVAL | POLLHUP | POLLERR))) {
        close_native_handle(pfd.fd);
      }
    }
    if (fds[0].fd == INVALID_NATIVE_HANDLE_VALUE &&
        fds[1].fd == INVALID_NATIVE_HANDLE_VALUE &&
        fds[2].fd == INVALID_NATIVE_HANDLE_VALUE) {
      break;
    }
  }
  in = INVALID_NATIVE_HANDLE_VALUE;
  out = INVALID_NATIVE_HANDLE_VALUE;
  err = INVALID_NATIVE_HANDLE_VALUE;
}
[[maybe_unused]] inline void multiplexing_use_select(
    NativeHandle &in, std::vector<char> &in_buf, NativeHandle &out,
    std::vector<char> &out_buf, NativeHandle &err, std::vector<char> &err_buf) {
  std::string_view stdin_str{in_buf.begin(), in_buf.end()};
  char buf[1024];

  fd_set read_fds;
  fd_set write_fds;
  while (in != INVALID_NATIVE_HANDLE_VALUE ||
         out != INVALID_NATIVE_HANDLE_VALUE ||
         err != INVALID_NATIVE_HANDLE_VALUE) {
    FD_ZERO(&write_fds);
    FD_ZERO(&read_fds);
    if (in != INVALID_NATIVE_HANDLE_VALUE) {
      FD_SET(in, &write_fds);
    }
    if (out != INVALID_NATIVE_HANDLE_VALUE) {
      FD_SET(out, &read_fds);
    }
    if (err != INVALID_NATIVE_HANDLE_VALUE) {
      FD_SET(err, &read_fds);
    }
    auto max_fd = std::max(std::max(in, out), err);
    auto const ready =
        select(max_fd + 1, &read_fds, &write_fds, nullptr, nullptr);
    if (ready == 0) {
      close_native_handle(in);
      close_native_handle(out);
      close_native_handle(err);
      break;
    }
    if (ready == -1) {
      throw std::runtime_error(get_last_error_msg());
    }
    if (in != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(in, &write_fds)) {
      auto write_count = write(in, stdin_str.data(), stdin_str.size());
      if (write_count > 0) {
        stdin_str.remove_prefix(static_cast<size_t>(write_count));
      }
      if (write_count == -1) {
        throw std::runtime_error("write error: " + std::to_string(errno));
      }
      if (stdin_str.empty()) {
        close_native_handle(in);
      }
    }
    if (out != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(out, &read_fds)) {
      auto read_count = read(out, buf, std::size(buf));
      if (read_count > 0) {
        out_buf.insert(out_buf.end(), buf, buf + read_count);
      }
      if (read_count == 0) {
        close_native_handle(out);
      }
      if (read_count == -1) {
        throw std::runtime_error(get_last_error_msg());
      }
    }
    if (err != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(err, &read_fds)) {
      auto read_count = read(err, buf, std::size(buf));
      if (read_count > 0) {
        err_buf.insert(err_buf.end(), buf, buf + read_count);
      }
      if (read_count == 0) {
        close_native_handle(err);
      }
      if (read_count == -1) {
        throw std::runtime_error(get_last_error_msg());
      }
    }
  }
}
#endif
#if defined(__linux__)
[[maybe_unused]] inline void multiplexing_use_epoll(
    NativeHandle &, std::vector<char> &, NativeHandle &, std::vector<char> &,
    NativeHandle &, std::vector<char> &) {
  // TODO
}
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
[[maybe_unused]] inline void multiplexing_use_kqueue(
    NativeHandle &, std::vector<char> &, NativeHandle &, std::vector<char> &,
    NativeHandle &, std::vector<char> &) {
  // TODO
}
#endif

[[maybe_unused]]
inline void read_write_per_thread(NativeHandle &in, std::vector<char> &in_buf,
                                  NativeHandle &out, std::vector<char> &out_buf,
                                  NativeHandle &err,
                                  std::vector<char> &err_buf) {
  std::vector<std::thread> threads;

  if (in != INVALID_NATIVE_HANDLE_VALUE) {
    threads.emplace_back(write_to_native_handle, std::ref(in),
                         std::ref(in_buf));
  }
  if (out != INVALID_NATIVE_HANDLE_VALUE) {
    threads.emplace_back(read_from_native_handle, std::ref(out),
                         std::ref(out_buf));
  }
  if (err != INVALID_NATIVE_HANDLE_VALUE) {
    threads.emplace_back(read_from_native_handle, std::ref(err),
                         std::ref(err_buf));
  }

  for (auto &thread : threads) {
    thread.join();
  }
}

inline void create_pipe(NativeHandle *fds) {
#if defined(_WIN32)
  SECURITY_ATTRIBUTES at;
  at.bInheritHandle = true;
  at.nLength = sizeof(SECURITY_ATTRIBUTES);
  at.lpSecurityDescriptor = nullptr;

  if (!CreatePipe(&(fds[0]), &(fds[1]), &at, 0)) {
    throw std::runtime_error{get_last_error_msg()};
  }
#else
  if (-1 == pipe(fds)) {
    throw std::runtime_error{"pipe failed"};
  }
#endif
}

inline void read_write_pipes(NativeHandle &in, std::vector<char> &in_buf,
                             NativeHandle &out, std::vector<char> &out_buf,
                             NativeHandle &err, std::vector<char> &err_buf) {
#if defined(_WIN32)
  return read_write_per_thread(in, in_buf, out, out_buf, err, err_buf);
#else
  return multiplexing_use_poll(in, in_buf, out, out_buf, err, err_buf);
#endif
}

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
      std::filesystem::path current_exe_path =
          std::filesystem::path(p) / exe_file;
      if (std::filesystem::path(exe_file).has_extension()) {
        if (exists(current_exe_path) &&
            std::filesystem::is_regular_file(current_exe_path)) {
          return current_exe_path;
        }
      } else {
        for (auto &ext : path_exts) {
          std::string exe_with_ext = exe_file;
          if (!ext.empty() && ext[0] != '.') {
            exe_with_ext += ".";
          }
          exe_with_ext += ext;
          auto f = std::filesystem::path(p) / exe_with_ext;
          if (exists(f) && std::filesystem::is_regular_file(f)) {
            return f;
          }
        }
        if (exists(current_exe_path) &&
            std::filesystem::is_regular_file(current_exe_path)) {
          return current_exe_path;
        }
      }
#else
      auto f = std::filesystem::path(p) / exe_file;
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
#if defined(UNICODE)
#if defined(GetEnvironmentStrings)
#define GetEnvironmentStrings_IS_A_MACRO 1
#define TEMP_GetEnvironmentStrings_VALUE GetEnvironmentStrings
#undef GetEnvironmentStrings
  char *envBlock = GetEnvironmentStrings();
#else
#define GetEnvironmentStrings_IS_A_MACRO 0
  char *envBlock = GetEnvironmentStrings();
#endif

#if GetEnvironmentStrings_IS_A_MACRO
#define GetEnvironmentStrings TEMP_GetEnvironmentStrings_VALUE
#undef TEMP_GetEnvironmentStrings_VALUE
#undef GetEnvironmentStrings_IS_A_MACRO
#endif

#else
  char *envBlock = GetEnvironmentStrings();
#endif
  if (envBlock == nullptr) {
    std::cerr << "Error getting environment strings." << std::endl;
    return envMap;
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
    return envMap;
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
namespace detail {
class Pipe {
 public:
  static Pipe create() { return Pipe{}; }
  void close_read() { close_native_handle(fds_[0]); }
  void close_write() { close_native_handle(fds_[1]); }
  void close_all() {
    close_read();
    close_write();
  }
  NativeHandle &operator[](int i) { return fds_[i]; }

 private:
  explicit Pipe() : fds_{std::make_shared<NativeHandle[]>(2)} {
    create_pipe(fds_.get());
  }
  std::shared_ptr<NativeHandle[]> fds_;
};

struct File {
  enum class OpenType {
    ReadOnly,
    WriteTruncate,
    WriteAppend,
  };
  using OpenType::ReadOnly;
  using OpenType::WriteAppend;
  using OpenType::WriteTruncate;

  explicit File(std::string const &p, bool append = false)
      : path_{p}, append_{append} {}

  File(File &&o) : path_{std::move(o.path_)}, append_{o.append_}, fd_{o.fd_} {
    o.path_ = "";
    o.append_ = false;
    o.fd_ = INVALID_NATIVE_HANDLE_VALUE;
  }
  File &operator=(File &&o) {
    close();
    path_ = std::move(o.path_);
    append_ = o.append_;
    fd_ = o.fd_;
    o.path_ = "";
    o.append_ = false;
    o.fd_ = INVALID_NATIVE_HANDLE_VALUE;
    return *this;
  }
  ~File() { close_native_handle(fd_); }

  void open_for_read() { open_impl(ReadOnly); }
  void open_for_write() {
    if (append_) {
      open_impl(WriteAppend);
    } else {
      open_impl(WriteTruncate);
    }
  }
  void close() { close_native_handle(fd_); }
  NativeHandle fd() { return fd_; }

 private:
  void open_impl(OpenType type) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;  // Make the handle inheritable

    fd_ = CreateFileA(
        path_.c_str(),
        type == ReadOnly
            ? GENERIC_READ
            : (type == WriteAppend ? FILE_APPEND_DATA : GENERIC_WRITE),
        FILE_SHARE_READ,
        &sa,  // Security attributes
        type == ReadOnly ? OPEN_EXISTING
                         : (type == WriteAppend ? OPEN_ALWAYS : CREATE_ALWAYS),
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fd_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error{"open failed: " + path_ +
                               ", error: " + std::to_string(GetLastError())};
    }
#else
    fd_ = (type == ReadOnly)
              ? open(path_.c_str(), O_RDONLY)
              : open(path_.c_str(),
                     (type == WriteAppend) ? (O_WRONLY | O_CREAT | O_APPEND)
                                           : (O_WRONLY | O_CREAT | O_TRUNC),
                     0644);
    if (fd_ == -1) {
      throw std::runtime_error{"open failed: " + path_};
    }
#endif
  }
  std::string path_;
  bool append_{false};
  NativeHandle fd_{INVALID_NATIVE_HANDLE_VALUE};
};

class Buffer {
  using buffer_container_type = std::vector<char>;

 public:
  Buffer(buffer_container_type &buf)
      : buf_{std::ref(buf)}, pipe_{Pipe::create()} {}
  Buffer() : buf_{buffer_container_type{}}, pipe_{Pipe::create()} {}

  std::vector<char> &buf() {
    return std::visit(
        []<typename T>(T &value) -> buffer_container_type & {
          if constexpr (std::is_same_v<T, buffer_container_type>) {
            return value;
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     buffer_container_type>>) {
            return value.get();
          }
        },
        buf_);
  }
  Pipe &pipe() { return pipe_; }

 private:
  std::variant<std::reference_wrapper<buffer_container_type>,
               buffer_container_type>
      buf_;
  Pipe pipe_;
};

class Stdio {
  friend class subprocess;
  using value_type = std::variant<Pipe, File, Buffer>;

 public:
  explicit Stdio() : redirect_(nullptr) {}
  explicit Stdio(Pipe const &p) : redirect_(std::make_unique<value_type>(p)) {}
  explicit Stdio(File f)
      : redirect_(std::make_unique<value_type>(std::move(f))) {}
  explicit Stdio(std::vector<char> &buf)
      : redirect_(std::make_unique<value_type>(Buffer(buf))) {}
  Stdio(Stdio &&) = default;
  Stdio &operator=(Stdio &&) = default;
  Stdio(Stdio const &) = delete;
  Stdio &operator=(Stdio const &) = delete;
  virtual ~Stdio() {
    if (!redirect_) {
      return;
    }
    std::visit(
        []<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            if (value[0] != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe[0] not closed!" << '\n';
            }
            if (value[1] != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe[1] not closed!" << '\n';
            }

          } else if constexpr (std::is_same_v<T, Buffer>) {
            if (value.pipe()[0] != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer.pipe[0] not closed!" << '\n';
            }
            if (value.pipe()[1] != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer.pipe[1] not closed!" << '\n';
            }
          }
        },
        *redirect_);
  }
  void prepare_redirection() {
    if (!redirect_) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
#if defined(_WIN32)
            NativeHandle non_inherit_handle = value[fileno() == 0 ? 1 : 0];
            if (non_inherit_handle != INVALID_NATIVE_HANDLE_VALUE &&
                !SetHandleInformation(non_inherit_handle, HANDLE_FLAG_INHERIT,
                                      0)) {
              throw std::runtime_error("SetHandleInformation Failed: " +
                                       std::to_string(GetLastError()));
            }
#endif
          } else if constexpr (std::is_same_v<T, File>) {
            if (fileno() == 0) {
              value.open_for_read();
            } else {
              value.open_for_write();
            }
          } else if constexpr (std::is_same_v<T, Buffer>) {
#if defined(_WIN32)
            NativeHandle non_inherit_handle =
                value.pipe()[fileno() == 0 ? 1 : 0];
            if (non_inherit_handle != INVALID_NATIVE_HANDLE_VALUE &&
                !SetHandleInformation(non_inherit_handle, HANDLE_FLAG_INHERIT,
                                      0)) {
              throw std::runtime_error("SetHandleInformation Failed: " +
                                       std::to_string(GetLastError()));
            }
#endif
          }
        },
        *redirect_);
  }
  void close_unused_pipe_ends_in_parent() {
    if (!redirect_) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            close_native_handle(value[fileno() == 0 ? 0 : 1]);
          } else if constexpr (std::is_same_v<T, File>) {
            value.close();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            close_native_handle(value.pipe()[fileno() == 0 ? 0 : 1]);
          }
        },
        *redirect_);
  }
  void close_all() {
    if (!redirect_) {
      return;
    }
    std::visit(
        []<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            value.close_all();
          } else if constexpr (std::is_same_v<T, File>) {
            value.close();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            value.pipe().close_all();
          }
        },
        *redirect_);
  }
#if defined(_WIN32)
  std::optional<NativeHandle> get_child_process_stdio_handle() {
    if (!redirect_) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>(
            [[maybe_unused]] T &value) -> std::optional<NativeHandle> {
          if constexpr (std::is_same_v<T, Pipe>) {
            return value[fileno() == 0 ? 0 : 1];
          } else if constexpr (std::is_same_v<T, File>) {
            return value.fd();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            return value.pipe()[fileno() == 0 ? 0 : 1];
          }
        },
        *redirect_);
  }
  std::optional<std::reference_wrapper<NativeHandle>>
  get_parent_communication_pipe_handle() {
    if (!redirect_) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>([[maybe_unused]] T &value)
            -> std::optional<std::reference_wrapper<NativeHandle>> {
          if constexpr (std::is_same_v<T, Buffer>) {
            return std::ref(value.pipe()[fileno() == 0 ? 1 : 0]);
          } else {
            return std::nullopt;
          }
        },
        *redirect_);
  }
#endif
#if !defined(_WIN32)
  void setup_stdio_in_child_process() {
    if (!redirect_) {
      return;
    }
    std::visit(
        [this]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            dup2(value[fileno() == 0 ? 0 : 1], fileno());
            close_native_handle(value[0]);
            close_native_handle(value[1]);
          } else if constexpr (std::is_same_v<T, File>) {
            dup2(value.fd(), fileno());
            value.close();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            dup2(value.pipe()[fileno() == 0 ? 0 : 1], fileno());
            value.pipe().close_all();
          }
        },
        *redirect_);
  }

#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
  void setup_stdio_for_posix_spawn(posix_spawn_file_actions_t &action) {
    if (!redirect_) {
      return;
    }
    std::visit(
        [this, &action]<typename T>([[maybe_unused]] T &value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            posix_spawn_file_actions_adddup2(
                &action, value[fileno() == 0 ? 0 : 1], fileno());
            posix_spawn_file_actions_addclose(&action, value[0]);
            posix_spawn_file_actions_addclose(&action, value[1]);
          } else if constexpr (std::is_same_v<T, File>) {
            posix_spawn_file_actions_adddup2(&action, value.fd(), fileno());
            posix_spawn_file_actions_addclose(&action, value.fd());
          } else if constexpr (std::is_same_v<T, Buffer>) {
            posix_spawn_file_actions_adddup2(
                &action, value.pipe()[fileno() == 0 ? 0 : 1], fileno());
            posix_spawn_file_actions_addclose(&action, value.pipe()[0]);
            posix_spawn_file_actions_addclose(&action, value.pipe()[1]);
          }
        },
        *redirect_);
  }
#endif  // SUBPROCESS_USE_POSIX_SPAWN
  std::optional<std::reference_wrapper<NativeHandle>>
  get_parent_pipe_fd_for_polling() {
    if (!redirect_) {
      return std::nullopt;
    }
    return std::visit(
        [this]<typename T>([[maybe_unused]] T &value)
            -> std::optional<std::reference_wrapper<NativeHandle>> {
          if constexpr (std::is_same_v<T, Buffer>) {
            return std::ref(value.pipe()[fileno() == 0 ? 1 : 0]);
          } else {
            return std::nullopt;
          }
        },
        *redirect_);
  }
#endif  // !_WIN32
  virtual int fileno() const = 0;

 protected:
  std::unique_ptr<value_type> redirect_{nullptr};
};

class Stdin : public Stdio {
 public:
  using Stdio::Stdio;
  Stdin(Stdin &&) = default;
  Stdin &operator=(Stdin &&) = default;
  Stdin(Stdin const &) = delete;
  Stdin &operator=(Stdin const &) = delete;
  int fileno() const override { return 0; }
};
class Stdout : public Stdio {
 public:
  using Stdio::Stdio;
  Stdout(Stdout &&) = default;
  Stdout &operator=(Stdout &&) = default;
  Stdout(Stdout const &) = delete;
  Stdout &operator=(Stdout const &) = delete;
  int fileno() const override { return 1; }
};
class Stderr : public Stdio {
 public:
  using Stdio::Stdio;
  Stderr(Stderr &&) = default;
  Stderr &operator=(Stderr &&) = default;
  Stderr(Stderr const &) = delete;
  Stderr &operator=(Stderr const &) = delete;
  int fileno() const override { return 2; }
};

struct stdin_redirector {
  Stdin operator<(Pipe p) const { return Stdin{std::move(p)}; }
  Stdin operator<(std::string const &file) const { return Stdin{File{file}}; }
  Stdin operator<(std::vector<char> &buf) const { return Stdin{buf}; }
};

struct stdout_redirector {
  Stdout operator>(Pipe const &p) const { return Stdout{p}; }
  Stdout operator>(std::string const &file) const { return Stdout{File{file}}; }
  Stdout operator>(std::vector<char> &buf) const { return Stdout{buf}; }

  Stdout operator>>(std::string const &file) const {
    return Stdout{File{file, true}};
  }
};

struct stderr_redirector {
  Stderr operator>(Pipe p) const { return Stderr{std::move(p)}; }
  Stderr operator>(std::string const &file) const { return Stderr{File{file}}; }
  Stderr operator>(std::vector<char> &buf) const { return Stderr{buf}; }
  Stderr operator>>(std::string const &file) const {
    return Stderr{File{file, true}};
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

namespace named_args {
#if defined(_WIN32)
[[maybe_unused]] inline static auto devnull = std::string{"NUL"};
#else
[[maybe_unused]] inline static auto devnull = std::string{"/dev/null"};
#endif
[[maybe_unused]] inline static stdin_redirector std_in;
[[maybe_unused]] inline static stdout_redirector std_out;
[[maybe_unused]] inline static stderr_redirector std_err;
[[maybe_unused]] inline static cwd_operator cwd;
[[maybe_unused]] inline static env_operator env;

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
#if defined(_WIN32)
[[maybe_unused]] inline static auto $devnull = std::string{"NUL"};
#else
[[maybe_unused]] inline static auto $devnull = std::string{"/dev/null"};
#endif
[[maybe_unused]] inline static stdin_redirector $stdin;
[[maybe_unused]] inline static stdout_redirector $stdout;
[[maybe_unused]] inline static stderr_redirector $stderr;
[[maybe_unused]] inline static cwd_operator $cwd;
[[maybe_unused]] inline static env_operator $env;
#endif
}  // namespace named_args
#if CPLUSPLUS_VERSION >= 202002L
template <typename T>
concept is_named_argument = std::is_same_v<Env, std::decay_t<T>> ||
                            std::is_same_v<Stdin, std::decay_t<T>> ||
                            std::is_same_v<Stdout, std::decay_t<T>> ||
                            std::is_same_v<Stderr, std::decay_t<T>> ||
                            std::is_same_v<Cwd, std::decay_t<T>> ||
                            std::is_same_v<EnvItemAppend, std::decay_t<T>>;
template <typename T>
concept is_string_type = std::is_same_v<char *, std::decay_t<T>> ||
                         std::is_same_v<const char *, std::decay_t<T>> ||
                         std::is_same_v<std::string, std::decay_t<T>>;
#else
template <typename T>
constexpr bool is_named_argument = std::integral_constant<
    bool, std::is_same_v<Env, std::decay_t<T>> ||
              std::is_same_v<Stdin, std::decay_t<T>> ||
              std::is_same_v<Stdout, std::decay_t<T>> ||
              std::is_same_v<Stderr, std::decay_t<T>> ||
              std::is_same_v<Cwd, std::decay_t<T>> ||
              std::is_same_v<EnvItemAppend, std::decay_t<T>>>::value;
template <typename T>
constexpr bool is_string_type = std::integral_constant<
    bool, std::is_same_v<char *, std::decay_t<T>> ||
              std::is_same_v<const char *, std::decay_t<T>> ||
              std::is_same_v<std::string, std::decay_t<T>>>::value;
#endif

template <typename...>
struct named_arg_typelist;

template <>
struct named_arg_typelist<> {
  using type = std::tuple<>;
};
template <typename Head, typename... Tails>
struct named_arg_typelist<Head, Tails...> {
  using type =
      std::conditional_t<is_named_argument<Head>, std::tuple<Head, Tails...>,
                         typename named_arg_typelist<Tails...>::type>;
};
template <typename... T>
using named_arg_typelist_t = typename named_arg_typelist<T...>::type;

class subprocess {
 public:
  template <typename... T>
#if CPLUSPLUS_VERSION >= 202002L
    requires(is_named_argument<T> && ...)
#endif
  explicit subprocess(std::vector<std::string> cmd, T &&...args)
      : cmd_(std::move(cmd)) {
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
               cwd_ = arg.cwd.string();
             }
             static_assert(std::is_same_v<Env, std::decay_t<T>> ||
                               std::is_same_v<Stdin, std::decay_t<T>> ||
                               std::is_same_v<Stdout, std::decay_t<T>> ||
                               std::is_same_v<Stderr, std::decay_t<T>> ||
                               std::is_same_v<Cwd, std::decay_t<T>> ||
                               std::is_same_v<EnvItemAppend, std::decay_t<T>>,
                           "Invalid argument type passed to run function.");
           }(std::forward<T>(args))));
    env_ = environments;
#if defined(_WIN32)
    ZeroMemory(&process_information_, sizeof(process_information_));
    ZeroMemory(&startupinfo_, sizeof(startupinfo_));
    startupinfo_.cb = sizeof(startupinfo_);
#endif
  }
  subprocess(subprocess &&) = default;
  subprocess &operator=(subprocess &&) = default;
  subprocess(const subprocess &) = delete;
  subprocess &operator=(const subprocess &) = delete;

  void run_no_wait() {
    prepare_all_stdio_redirections();
#if defined(_WIN32)
    auto in = stdin_.get_child_process_stdio_handle();
    startupinfo_.hStdInput =
        in.has_value() ? in.value() : GetStdHandle(STD_INPUT_HANDLE);
    auto out = stdout_.get_child_process_stdio_handle();
    startupinfo_.hStdOutput =
        out.has_value() ? out.value() : GetStdHandle(STD_OUTPUT_HANDLE);
    auto err = stderr_.get_child_process_stdio_handle();
    startupinfo_.hStdError =
        err.has_value() ? err.value() : GetStdHandle(STD_ERROR_HANDLE);

    startupinfo_.dwFlags |= STARTF_USESTDHANDLES;

    auto command = create_command_string_data(cmd_);

    auto env_block = create_environment_string_data(env_);

    auto success =
        CreateProcessA(nullptr, command.data(), NULL, NULL, TRUE, 0,
                       env_block.empty() ? nullptr : env_block.data(),
                       cwd_.empty() ? nullptr : cwd_.data(), &startupinfo_,
                       &process_information_);

    if (success) {
      manage_pipe_io();
    } else {
      std::cerr << get_last_error_msg() << '\n';
      process_information_.hProcess = INVALID_NATIVE_HANDLE_VALUE;
      stdin_.close_all();
      stdout_.close_all();
      stderr_.close_all();
    }
#else
#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
    posix_spawn_file_actions_t action;
    if (0 != posix_spawn_file_actions_init(&action)) {
      std::runtime_error("posix_spawn_file_actions_init failed: " +
                         get_last_error_msg());
    }
    add_posix_spawn_file_actions(action);
    posix_spawn_file_actions_destroy(&action);
    manage_pipe_io();
#else   // SUBPROCESS_USE_POSIX_SPAWN
    auto pid = fork();
    if (pid < 0) {
      throw std::runtime_error("fork failed");
    } else if (pid == 0) {
      execute_command_in_child();
    } else {
      pid_ = pid;
      manage_pipe_io();
    }
#endif  // !SUBPROCESS_USE_POSIX_SPAWN
#endif  // !_WIN32
  }

  int run() {
    run_no_wait();
    return wait_for_exit();
  }

#if defined(_WIN32)
  int wait_for_exit() {
    if (process_information_.hProcess == INVALID_NATIVE_HANDLE_VALUE) {
      return 127;
    }
    DWORD ret{127};
    HandleGuard process_guard(process_information_.hProcess);
    HandleGuard thread_guard(process_information_.hThread);

    WaitForSingleObject(process_information_.hProcess, INFINITE);
    GetExitCodeProcess(process_information_.hProcess, &ret);
    return static_cast<int>(ret);
  }
#else
  int wait_for_exit() {
    if (pid_ == INVALID_NATIVE_HANDLE_VALUE) {
      return 127;
    }
    int status;
    waitpid(pid_, &status, 0);
    auto return_code = -1;
    if (WIFEXITED(status)) {
      return_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      return_code = 128 + (WTERMSIG(status));
    }
    return return_code;
  }
#endif

 private:
  void prepare_all_stdio_redirections() {
    stdin_.prepare_redirection();
    stdout_.prepare_redirection();
    stderr_.prepare_redirection();
  }
  void manage_pipe_io() {
    stdin_.close_unused_pipe_ends_in_parent();
    stdout_.close_unused_pipe_ends_in_parent();
    stderr_.close_unused_pipe_ends_in_parent();

#if defined(_WIN32)
    auto in = stdin_.get_parent_communication_pipe_handle();
    auto out = stdout_.get_parent_communication_pipe_handle();
    auto err = stderr_.get_parent_communication_pipe_handle();

    NativeHandle tmp_handle = INVALID_NATIVE_HANDLE_VALUE;
    std::vector<char> tmp_buf;
    read_write_pipes(
        in.has_value() ? in.value().get() : tmp_handle,
        in.has_value() ? std::get<Buffer>(*stdin_.redirect_).buf() : tmp_buf,
        out.has_value() ? out.value().get() : tmp_handle,
        out.has_value() ? std::get<Buffer>(*stdout_.redirect_).buf() : tmp_buf,
        err.has_value() ? err.value().get() : tmp_handle,
        err.has_value() ? std::get<Buffer>(*stderr_.redirect_).buf() : tmp_buf);
#else
    auto in = stdin_.get_parent_pipe_fd_for_polling();
    auto out = stdout_.get_parent_pipe_fd_for_polling();
    auto err = stderr_.get_parent_pipe_fd_for_polling();

    NativeHandle tmp_handle = INVALID_NATIVE_HANDLE_VALUE;
    std::vector<char> tmp_buf;
    read_write_pipes(
        in.has_value() ? in.value().get() : tmp_handle,
        in.has_value() ? std::get<Buffer>(*stdin_.redirect_).buf() : tmp_buf,
        out.has_value() ? out.value().get() : tmp_handle,
        out.has_value() ? std::get<Buffer>(*stdout_.redirect_).buf() : tmp_buf,
        err.has_value() ? err.value().get() : tmp_handle,
        err.has_value() ? std::get<Buffer>(*stderr_.redirect_).buf() : tmp_buf);
#endif
  }
#if !defined(_WIN32)
  void execute_command_in_child() {
    stdin_.setup_stdio_in_child_process();
    stdout_.setup_stdio_in_child_process();
    stderr_.setup_stdio_in_child_process();

    std::vector<char *> cmd{};
    std::transform(cmd_.begin(), cmd_.end(), std::back_inserter(cmd),
                   [](std::string &s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!cwd_.empty() && (-1 == chdir(cwd_.data()))) {
      throw std::runtime_error(get_last_error_msg());
    }

    std::string exe_to_exec = cmd_[0];
    if (exe_to_exec.find('/') == std::string::npos) {
      std::filesystem::path resolved_path =
          find_executable_in_path(exe_to_exec);
      if (!resolved_path.empty()) {
        exe_to_exec = resolved_path.string();
      }
    }

    if (!env_.empty()) {
      std::vector<std::string> env_tmp{};

      std::transform(
          env_.begin(), env_.end(), std::back_inserter(env_tmp),
          [](auto &entry) { return entry.first + "=" + entry.second; });

      std::vector<char *> envs{};
      std::transform(env_tmp.begin(), env_tmp.end(), std::back_inserter(envs),
                     [](auto &s) { return s.data(); });
      envs.push_back(nullptr);
      execve(exe_to_exec.c_str(), cmd.data(), envs.data());
      std::cerr << "execve(" << exe_to_exec
                << ") failed: " << get_last_error_msg() << '\n';
    } else {
      execv(exe_to_exec.c_str(), cmd.data());
      std::cerr << "execv(" << exe_to_exec
                << ") failed: " << get_last_error_msg() << '\n';
    }
    _Exit(127);
  }
#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
  void add_posix_spawn_file_actions(posix_spawn_file_actions_t &action) {
    stdin_.setup_stdio_for_posix_spawn(action);
    stdout_.setup_stdio_for_posix_spawn(action);
    stderr_.setup_stdio_for_posix_spawn(action);

    std::vector<char *> cmd{};
    std::transform(cmd_.begin(), cmd_.end(), std::back_inserter(cmd),
                   [](std::string &s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!cwd_.empty() &&
#if defined(__APPLE__) && defined(__MACH__)
        (-1 == posix_spawn_file_actions_addchdir_np(&action, cwd_.data()))) {
      throw std::runtime_error(get_last_error_msg());
#else  // other POSIX systems, try the standard version
        (-1 == posix_spawn_file_actions_addchdir(&action, cwd_.data()))) {
      throw std::runtime_error(get_last_error_msg());
#endif
    }

    std::string exe_to_exec = cmd_[0];
    if (exe_to_exec.find('/') == std::string::npos) {
      std::filesystem::path resolved_path =
          find_executable_in_path(exe_to_exec);
      if (!resolved_path.empty()) {
        exe_to_exec = resolved_path.string();
      }
    }

    std::vector<char *> envs{};
    if (!env_.empty()) {
      std::vector<std::string> env_tmp{};

      std::transform(
          env_.begin(), env_.end(), std::back_inserter(env_tmp),
          [](auto &entry) { return entry.first + "=" + entry.second; });

      std::transform(env_tmp.begin(), env_tmp.end(), std::back_inserter(envs),
                     [](auto &s) { return s.data(); });
      envs.push_back(nullptr);
      auto ret = posix_spawn(&pid_, exe_to_exec.c_str(), &action, nullptr,
                             cmd.data(), envs.data());
      if (ret != 0) {
        pid_ = INVALID_NATIVE_HANDLE_VALUE;
      }
    }
    auto ret = posix_spawn(&pid_, exe_to_exec.c_str(), &action, nullptr,
                           cmd.data(), envs.empty() ? nullptr : envs.data());
    if (ret != 0) {
      pid_ = INVALID_NATIVE_HANDLE_VALUE;
    }
  }
#endif  // SUBPROCESS_USE_POSIX_SPAWN
#endif  // !_WIN32

 private:
  std::vector<std::string> cmd_;
  std::string cwd_{};
  std::map<std::string, std::string> env_;
  Stdin stdin_;
  Stdout stdout_;
  Stderr stderr_;
#if defined(_WIN32)
  PROCESS_INFORMATION process_information_;
  STARTUPINFOA startupinfo_;
#else
  NativeHandle pid_{INVALID_NATIVE_HANDLE_VALUE};
#endif
};
}  // namespace detail

namespace named_arguments {
#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
using detail::named_args::$cwd;
using detail::named_args::$devnull;
using detail::named_args::$env;
using detail::named_args::$stderr;
using detail::named_args::$stdin;
using detail::named_args::$stdout;
#endif
using detail::named_args::cwd;
using detail::named_args::devnull;
using detail::named_args::env;
using detail::named_args::std_err;
using detail::named_args::std_in;
using detail::named_args::std_out;
}  // namespace named_arguments

template <typename... T>
#if CPLUSPLUS_VERSION >= 202002L
  requires(detail::is_named_argument<T> && ...)
#endif
inline int run(std::vector<std::string> cmd, T &&...args) {
#if CPLUSPLUS_VERSION < 202002L
  static_assert((detail::is_named_argument<T>) && ...);
#endif
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}

template <typename... Args>
#if CPLUSPLUS_VERSION >= 202002L
  requires((detail::is_named_argument<Args> || detail::is_string_type<Args>) &&
           ...)
#endif
inline int run(Args... args) {
#if CPLUSPLUS_VERSION < 202002L
  static_assert(
      (detail::is_named_argument<Args> || detail::is_string_type<Args>) && ...);
#endif
  std::tuple<Args...> args_tuple{std::move(args)...};
  using ArgsTuple = std::tuple<Args...>;
  using NamedArgTypelist =
      typename detail::named_arg_typelist_t<std::decay_t<Args>...>;
  return [&args_tuple]<size_t... I, size_t... N>(std::index_sequence<I...>,
                                                 std::index_sequence<N...>) {
    static_assert(
        ((detail::is_string_type<std::tuple_element_t<I, ArgsTuple>>) && ...));
    static_assert(((detail::is_named_argument<
                       std::tuple_element_t<N, NamedArgTypelist>>) &&
                   ...));
    return run({std::move(std::get<I>(args_tuple))...},
               std::move(std::get<std::tuple_size_v<std::tuple<Args...>> -
                                  std::tuple_size_v<NamedArgTypelist> + N>(
                   args_tuple))...);
  }(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>> -
                             std::tuple_size_v<NamedArgTypelist>>{},
         std::make_index_sequence<std::tuple_size_v<NamedArgTypelist>>{});
}

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
template <typename... T>
#if CPLUSPLUS_VERSION >= 202002L
  requires(detail::is_named_argument<T> && ...)
#endif
inline int $(std::vector<std::string> cmd, T &&...args) {
#if CPLUSPLUS_VERSION < 202002L
  static_assert((detail::is_named_argument<T>) && ...);
#endif
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}
template <typename... Args>
#if CPLUSPLUS_VERSION >= 202002L
  requires((detail::is_named_argument<Args> || detail::is_string_type<Args>) &&
           ...)
#endif
inline int $(Args... args) {
#if CPLUSPLUS_VERSION < 202002L
  static_assert(
      (detail::is_named_argument<Args> || detail::is_string_type<Args>) && ...);
#endif
  return run(std::forward<Args>(args)...);
}
#endif

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

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
using process::$;
using process::named_arguments::$cwd;
using process::named_arguments::$devnull;
using process::named_arguments::$env;
using process::named_arguments::$stderr;
using process::named_arguments::$stdin;
using process::named_arguments::$stdout;
#endif
#endif  // __SUBPROCESS_SUBPROCESS_HPP__
