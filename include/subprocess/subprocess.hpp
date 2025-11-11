#ifndef __SUBPROCESS_SUBPROCESS_HPP__
#define __SUBPROCESS_SUBPROCESS_HPP__

/*******************************************************************************
 * namespace subprocess {
 *   int run(...);
 *   int $(...);
 *   namespace named_arguments {
 *     cwd;     $cwd;
 *     devnull; $devnull;
 *     env;     $env;
 *     stderr;  $stderr;
 *     stdin;   $stdin;
 *     stdout;  $stdout;
 *   }
 * }
 * using subprocess::$;
 * using subprocess::named_arguments::$cwd;
 * using subprocess::named_arguments::$devnull;
 * using subprocess::named_arguments::$env;
 * using subprocess::named_arguments::$stderr;
 * using subprocess::named_arguments::$stdin;
 * using subprocess::named_arguments::$stdout;
 *
 * EXAMPLES:
 *
 *   #include <subprocess/subprocess.hpp>
 *   using namespace subprocess::named_arguments;
 *   using subprocess::run
 *
 *   sbuprocess::buffer inbuf{"xxxxxxxxx"};
 *   sbuprocess::buffer outbuf;
 *   sbuprocess::buffer errbuf;
 *
 *   auto exit_code = run("command", "arg1", "arg2",..., "argN");
 *
 *   run("command", "arg1", "arg2",..., "argN",
 *            $stdin < $devnull,
 *            $stdout > $devnull,
 *            $stderr > $devnull);
 *
 *   run("pwd", $cwd = "/tmp/", $stdout > outbuf);
 *
 *   run("printenv", env+={{"env1","val1"}, {"env2", "val2"}});
 *
 *   run("printenv", "PATH", env["PATH"]+="/tmp/");
 *
 *   run(L"cmd", L"echo %PATH% & exit /b 0", $stdout > outbuf);
 *
 *
 *   auto [exit_code, out, err] = capture_run("command", "arg1", "arg2",...,
 *"argN");
 *
 ******************************************************************************/

#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#if (defined(_MSVC_LANG) && _MSVC_LANG < 202002L) || \
    (!defined(_MSVC_LANG) && __cplusplus < 202002L)
#error "This code requires C++20 or later."
#endif

#if defined(_MSVC_LANG)
#define CPLUSPLUS_VERSION _MSVC_LANG
#else
#define CPLUSPLUS_VERSION __cplusplus
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>

#include <locale>
#else
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>

#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
#include <spawn.h>
#endif
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#if !defined(_WIN32)
extern char** environ;
#endif  // !_WIN32

namespace subprocess {
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

namespace detail {
class subprocess;
class Stdio;
class Stdin;
class Stdout;
class Stderr;
}  // namespace detail

#if defined(_WIN32)
// Helper function to convert a UTF-8 std::string to a UTF-16 std::wstring
inline std::wstring to_wstring(const std::string_view str,
                               const UINT from_codepage = CP_UTF8) {
  if (str.empty()) {
    return {};
  }
  int size_needed = MultiByteToWideChar(from_codepage, 0, str.data(),
                                        (int)str.size(), NULL, 0);
  if (size_needed <= 0) {
    throw std::runtime_error("MultiByteToWideChar error: " +
                             std::to_string(GetLastError()));
  }
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(from_codepage, 0, str.data(), (int)str.size(), &wstr[0],
                      size_needed);
  return wstr;
}

// Helper function to convert a UTF-16 std::wstring to a UTF-8 std::string
inline std::string to_string(const std::wstring_view wstr,
                             const UINT to_codepage = CP_UTF8) {
  if (wstr.empty()) {
    return {};
  }
  int size_needed = WideCharToMultiByte(to_codepage, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  if (size_needed <= 0) {
    throw std::runtime_error("WideCharToMultiByte error: " +
                             std::to_string(GetLastError()));
  }
  std::string str(size_needed, 0);
  WideCharToMultiByte(to_codepage, 0, wstr.data(), (int)wstr.size(), &str[0],
                      size_needed, NULL, NULL);
  return str;
}
#endif  // _WIN32

class buffer {
  friend class detail::subprocess;
  friend class detail::Stdio;
  friend class detail::Stdin;
  friend class detail::Stdout;
  friend class detail::Stderr;

 public:
  buffer() = default;
  buffer(std::string_view const& str) : buf_(str.begin(), str.end()) {}
  auto* data() { return buf_.data(); }
  auto size() { return buf_.size(); }
  auto to_string() {
#if defined(_WIN32)
    if (encode_codepage_ == decode_codepage_) {
      return std::string(buf_.data(), buf_.size());
    } else {
      return subprocess::to_string(
          subprocess::to_wstring({buf_.data(), buf_.size()}, encode_codepage_),
          decode_codepage_);
    }
#else
    return std::string(buf_.data(), buf_.size());
#endif
  }
  auto empty() { return buf_.empty(); }
  auto clear() { return buf_.clear(); }
#if defined(_WIN32)
  UINT encode_codepage() { return encode_codepage_; };
  UINT decode_codepage() { return decode_codepage_; };
  void encode_codepage(UINT codepage) { encode_codepage_ = codepage; };
  void decode_codepage(UINT codepage) { decode_codepage_ = codepage; };
#endif

 private:
#if defined(_WIN32)
  UINT encode_codepage_{CP_UTF8};
  UINT decode_codepage_{CP_UTF8};
#endif
  std::vector<char> buf_;
};

namespace detail {
#if defined(_WIN32)
using NativeHandle = HANDLE;
const static inline NativeHandle INVALID_NATIVE_HANDLE_VALUE =
    INVALID_HANDLE_VALUE;
#else   // _WIN32
using NativeHandle = int;
constexpr NativeHandle INVALID_NATIVE_HANDLE_VALUE = -1;
#endif  // !_WIN32

inline void close_native_handle(NativeHandle& handle) {
  if (handle != INVALID_NATIVE_HANDLE_VALUE) {
#if defined(_WIN32)
    CloseHandle(handle);
#else
    close(handle);
#endif
    handle = INVALID_NATIVE_HANDLE_VALUE;
  }
}
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
  HandleGuard(const HandleGuard&) = delete;
  HandleGuard& operator=(const HandleGuard&) = delete;
  HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_) {
    other.handle_ =
#if defined(_WIN32)
        INVALID_HANDLE_VALUE
#else
        -1
#endif
        ;
  }
  HandleGuard& operator=(HandleGuard&& other) noexcept {
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
  NativeHandle* p_get() { return &handle_; }

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

template <typename T, typename = std::void_t<>>
struct has_push_back : public std::false_type {};
template <typename C>
struct has_push_back<C, std::void_t<decltype(std::declval<C>().push_back(
                            std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_back_v = has_push_back<T>::value;

template <typename T, typename = std::void_t<>>
struct has_emplace_back : public std::false_type {};
template <typename C>
struct has_emplace_back<C, std::void_t<decltype(std::declval<C>().emplace_back(
                               std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_emplace_back_v = has_emplace_back<T>::value;

template <typename T, typename = std::void_t<>>
struct has_insert : public std::false_type {};
template <typename C>
struct has_insert<C, std::void_t<decltype(std::declval<C>().insert(
                         std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_insert_v = has_insert<T>::value;

template <typename T, typename = std::void_t<>>
struct has_emplace : public std::false_type {};
template <typename C>
struct has_emplace<C, std::void_t<decltype(std::declval<C>().emplace(
                          std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_emplace_v = has_emplace<T>::value;

template <typename T, typename = std::void_t<>>
struct has_push : public std::false_type {};
template <typename C>
struct has_push<C, std::void_t<decltype(std::declval<C>().push(
                       std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_v = has_push<T>::value;

template <typename T, typename = std::void_t<>>
struct has_push_front : public std::false_type {};
template <typename C>
struct has_push_front<C, std::void_t<decltype(std::declval<C>().push_front(
                             std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_front_v = has_push_front<T>::value;

template <typename CharT, typename F, typename C>
  requires std::is_same_v<bool,
                          decltype(std::declval<F>()(std::declval<CharT>()))>
C& split_to_if(C& to, const std::basic_string<CharT>& str, F f,
               int max_count = -1, bool is_compress_token = false) {
  auto begin = str.begin();
  auto delimiter = begin;
  int count = 0;

  while ((max_count < 0 || count++ < max_count) &&
         (delimiter = std::find_if(begin, str.end(), f)) != str.end()) {
    to.insert(to.end(), {begin, delimiter});
    if (is_compress_token) {
      begin = std::find_if_not(delimiter, str.end(), f);
    } else {
      begin = std::next(delimiter);
    }
  }

  if constexpr (has_emplace_back_v<C>) {
    to.emplace_back(begin, str.end());
  } else if constexpr (has_emplace_v<C>) {
    to.emplace(begin, str.end());
  } else if constexpr (has_push_back_v<C>) {
    to.push_back({begin, str.end()});
  } else if constexpr (has_insert_v<C>) {
    to.insert({begin, str.end()});
  } else if constexpr (has_push_v<C>) {
    to.push({begin, str.end()});
  } else if constexpr (has_push_front_v<C>) {
    to.push_front({begin, str.end()});
  } else {
    static_assert(
        !std::is_same_v<C, C>,
        "The container does not support adding elements via a known method.");
  }

  return to;
}

template <typename CharT>
inline std::vector<std::basic_string<CharT>> split(
    const std::basic_string<CharT>& s, CharT delim) {
  std::vector<std::basic_string<CharT>> ret;
  split_to_if(ret, s, [delim](CharT c) { return c == delim; });
  return ret;
}

#if defined(_WIN32)
inline std::vector<wchar_t> argv_to_command_line_string(
    std::vector<std::basic_string<wchar_t>> const& cmds) {
  std::vector<wchar_t> command;
  for (auto const& cmd : cmds) {
    if (!command.empty()) {
      command.push_back(L' ');
    }
    auto need_quota =
        cmd.empty() || std::any_of(cmd.begin(), cmd.end(), [](wchar_t c) {
          return c <= L' ' || c == L'"';
        });
    if (need_quota) {
      command.push_back(L'"');
      for (auto c : cmd) {
        if (c == L'"') {
          command.push_back(L'\\');
        }
        command.push_back(c);
      }
      command.push_back(L'"');
    } else {
      command.insert(command.end(), cmd.begin(), cmd.end());
    }
  }
  command.push_back(L'\0');
  return command;
}

inline std::vector<wchar_t> create_environment_string_data(
    std::map<std::basic_string<wchar_t>, std::basic_string<wchar_t>> const&
        envs) {
  std::vector<wchar_t> env_block;
  for (auto const& [key, value] : envs) {
    env_block.insert(env_block.end(), key.begin(), key.end());
    env_block.push_back(L'=');
    env_block.insert(env_block.end(), value.begin(), value.end());
    env_block.push_back(L'\0');
  }
  if (!env_block.empty()) {
    env_block.push_back(L'\0');
  }
  return env_block;
}

#endif

inline std::string get_last_error_msg() {
#if defined(_WIN32)
  DWORD error = GetLastError();
  LPVOID errorMsg{NULL};

  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPWSTR)&errorMsg, 0, NULL);
  if (errorMsg) {
    auto ret = to_string((wchar_t*)errorMsg);
    LocalFree(errorMsg);
    return ret;
  } else {
    return "Unknown error or FormatMessageW failed, error code: " +
           std::to_string(error);
  }
#else   // _WIN32
  int error = errno;
  auto* err_msg = strerror(error);
  if (err_msg) {
    return std::string(err_msg);
  } else {
    return "Unknown error or strerror failed, error code: " +
           std::to_string(errno);
  }
#endif  // !_WIN32
}

inline void write_to_native_handle(NativeHandle& fd,
                                   std::vector<char>& write_data) {
  HandleGuard auto_closed(fd);
  std::string_view write_view{write_data.data(), write_data.size()};
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

inline void read_from_native_handle(NativeHandle& fd,
                                    std::vector<char>& out_buf) {
  HandleGuard auto_closed(fd);
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
    NativeHandle& in, std::vector<char>& in_buf, NativeHandle& out,
    std::vector<char>& out_buf, NativeHandle& err, std::vector<char>& err_buf) {
  struct pollfd fds[3]{{.fd = in, .events = POLLOUT, .revents = 0},
                       {.fd = out, .events = POLLIN, .revents = 0},
                       {.fd = err, .events = POLLIN, .revents = 0}};

  std::string_view stdin_str{in_buf.data(), in_buf.size()};

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
    for (auto& pfd : fds) {
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
    NativeHandle& in, std::vector<char>& in_buf, NativeHandle& out,
    std::vector<char>& out_buf, NativeHandle& err, std::vector<char>& err_buf) {
  std::string_view stdin_str{in_buf.data(), in_buf.size()};
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
    NativeHandle&, std::vector<char>&, NativeHandle&, std::vector<char>&,
    NativeHandle&, std::vector<char>&) {
  // TODO
}
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
[[maybe_unused]] inline void multiplexing_use_kqueue(
    NativeHandle&, std::vector<char>&, NativeHandle&, std::vector<char>&,
    NativeHandle&, std::vector<char>&) {
  // TODO
}
#endif

[[maybe_unused]]
inline void read_write_per_thread(NativeHandle& in, std::vector<char>& in_buf,
                                  NativeHandle& out, std::vector<char>& out_buf,
                                  NativeHandle& err,
                                  std::vector<char>& err_buf) {
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

  for (auto& thread : threads) {
    thread.join();
  }
}

inline void read_write_pipes(NativeHandle& in, std::vector<char>& in_buf,
                             NativeHandle& out, std::vector<char>& out_buf,
                             NativeHandle& err, std::vector<char>& err_buf) {
#if defined(_WIN32)
  return read_write_per_thread(in, in_buf, out, out_buf, err, err_buf);
#else
#if defined(SUBPROCESS_MULTIPLEXING_USE_SELECT) && \
    SUBPROCESS_MULTIPLEXING_USE_SELECT
  return multiplexing_use_select(in, in_buf, out, out_buf, err, err_buf);
#else
  return multiplexing_use_poll(in, in_buf, out, out_buf, err, err_buf);
#endif
#endif
}

#if defined(_WIN32)
inline std::optional<std::string> get_file_extension(std::string const& f) {
  auto const dotPos = f.rfind('.');
  if (dotPos == std::string::npos) {
    return std::nullopt;
  }

  if (dotPos == f.length() - 1) {
    return std::nullopt;
  }

  auto const separatorPos = f.find_last_of("/\\");
  if (separatorPos != std::string::npos) {
    if (separatorPos > dotPos) {
      return std::nullopt;
    }
    if (dotPos == separatorPos + 1) {
      return std::nullopt;
    }
  } else {
    if (dotPos == 0) {
      return std::nullopt;
    }
  }
  return f.substr(dotPos + 1);
}
#endif  // !_WIN32

inline bool is_executable(std::string const& f) {
#if defined(_WIN32)
  auto attr = GetFileAttributesW(to_wstring(f).c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat sb;
  return (stat(f.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) &&
          access(f.c_str(), X_OK) == 0);
#endif
}

inline std::optional<std::string> get_env(std::string const& key) {
#if defined(_WIN32)
  auto wkey = to_wstring(key);
  auto const size =
      GetEnvironmentVariableW(wkey.c_str(), nullptr, static_cast<DWORD>(0));
  if (size == 0 || GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    return std::nullopt;
  }
  std::vector<wchar_t> buf;
  buf.resize(static_cast<size_t>(size));
  GetEnvironmentVariableW(wkey.c_str(), buf.data(),
                          static_cast<DWORD>(buf.size()));
  return to_string(std::wstring{static_cast<const wchar_t*>(buf.data())});
#else
  auto* env = ::getenv(key.c_str());
  if (env) {
    return std::string(env);
  }
  return std::nullopt;
#endif
}

#if defined(_WIN32)
inline std::optional<std::wstring> get_env(std::wstring const& key) {
  auto const size =
      GetEnvironmentVariableW(key.c_str(), nullptr, static_cast<DWORD>(0));
  if (size == 0 || GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    return std::nullopt;
  }
  std::vector<wchar_t> buf;
  buf.resize(static_cast<size_t>(size));
  GetEnvironmentVariableW(key.c_str(), buf.data(),
                          static_cast<DWORD>(buf.size()));
  return std::wstring(static_cast<const wchar_t*>(buf.data()));
}
#endif

inline std::optional<std::string> find_command_in_path(
    std::string const& exe_file) {
#ifdef _WIN32
  char separator = '\\';
  char path_env_sep = ';';
#else
  char separator = '/';
  char path_env_sep = ':';
#endif

  if (exe_file.find_last_of("/\\") != std::string::npos) {
    return std::nullopt;
  }
  auto paths = split(get_env("PATH").value_or(""), path_env_sep);
#ifdef _WIN32
  auto path_exts = split(
      get_env("PATHEXT").value_or(
          ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC;.PY;.PYW"),
      path_env_sep);
  for (auto& ext : path_exts) {
    std::transform(begin(ext), end(ext), ext.begin(), ::tolower);
  }
  path_exts.insert(path_exts.begin(), "");
#endif
  for (auto& p : paths) {
#ifdef _WIN32
    std::string f = p + separator + exe_file;
    if (get_file_extension(f).has_value()) {
      if (is_executable(f)) {
        return f;
      }
    } else {
      for (auto ext : path_exts) {
        std::string f_with_ext{f};
        if (!ext.empty() && ext[0] != '.') {
          f_with_ext += ".";
        }
        f_with_ext += ext;
        if (is_executable(f_with_ext)) {
          return f_with_ext;
        }
      }
    }
#else
    std::string f = p + separator + exe_file;
    if (is_executable(f)) {
      return f;
    }
#endif
  }
  return std::nullopt;
}

#if defined(_WIN32)
inline std::map<std::wstring, std::wstring> get_all_envs() {
  std::map<std::wstring, std::wstring> envs;

  auto* envBlock = GetEnvironmentStringsW();
  if (envBlock == nullptr) {
    return envs;
  }

  const auto* currentEnv = envBlock;
  while (*currentEnv != L'\0') {
    std::wstring_view envString(currentEnv);
    auto pos = envString.find(L'=');
    // Weird variables in Windows that start with '='.
    // The key is the name of a drive, like "=C:", and the value is the
    // current working directory on that drive.
    if (pos == 0) {
      pos = envString.find(L'=', 1);
    }
    if (pos != std::wstring_view::npos) {
      auto key = std::wstring(envString.substr(0, pos));
      auto value = std::wstring(envString.substr(pos + 1));
      std::transform(key.begin(), key.end(), key.begin(),
                     [](wchar_t c) { return std::toupper(c, std::locale()); });
      envs[key] = value;
    }
    currentEnv +=
        envString.length() + 1;  // Move to the next environment variable
  }

  FreeEnvironmentStringsW(envBlock);
  return envs;
}
#else
inline std::map<std::string, std::string> get_all_envs() {
  std::map<std::string, std::string> envs;
  if (environ == nullptr) {
    return envs;
  }

  for (char** env = environ; *env != nullptr; ++env) {
    std::string_view envString(*env);
    auto pos = envString.find('=');
    if (pos != std::string::npos) {
      std::string_view key = envString.substr(0, pos);
      std::string_view value = envString.substr(pos + 1);
      envs[std::string(key)] = std::string(value);
    }
  }
  return envs;
}
#endif
class Pipe {
 public:
  static Pipe create() { return Pipe{}; }
  void close_read() { close_native_handle(fds_[0]); }
  void close_write() { close_native_handle(fds_[1]); }
  void close_all() {
    close_read();
    close_write();
  }
  NativeHandle& read() { return fds_[0]; }
  NativeHandle& write() { return fds_[1]; }

 private:
  static inline void create_native_pipe(NativeHandle* fds) {
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
  explicit Pipe() : fds_{std::make_shared<NativeHandle[]>(2)} {
    create_native_pipe(fds_.get());
  }
  std::shared_ptr<NativeHandle[]> fds_;
};

struct File {
  enum class OpenType {
    ReadOnly,
    WriteTruncate,
    WriteAppend,
  };

  explicit File(std::string const &p, bool append = false)
      : path_{
#if defined(_WIN32)
        to_wstring(p)
#else
          p
#endif
      }, append_{append} {
  }
#if defined(_WIN32)
  explicit File(std::wstring const& p, bool append = false)
      : path_{p}, append_{append} {}
#endif

  File(File&& o) noexcept
      : path_{std::move(o.path_)}, append_{o.append_}, fd_{o.fd_} {
    o.path_.clear();
    o.append_ = false;
    o.fd_ = INVALID_NATIVE_HANDLE_VALUE;
  }
  File& operator=(File&& o) noexcept {
    close();
    path_ = std::move(o.path_);
    append_ = o.append_;
    fd_ = o.fd_;
    o.path_.clear();
    o.append_ = false;
    o.fd_ = INVALID_NATIVE_HANDLE_VALUE;
    return *this;
  }
  ~File() { close_native_handle(fd_); }

  void open_for_read() { open_impl(OpenType::ReadOnly); }
  void open_for_write() {
    if (append_) {
      open_impl(OpenType::WriteAppend);
    } else {
      open_impl(OpenType::WriteTruncate);
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

    fd_ = CreateFileW(
        path_.c_str(),
        type == OpenType::ReadOnly
            ? GENERIC_READ
            : (type == OpenType::WriteAppend ? FILE_APPEND_DATA
                                             : GENERIC_WRITE),
        FILE_SHARE_READ,
        &sa,  // Security attributes
        type == OpenType::ReadOnly
            ? OPEN_EXISTING
            : (type == OpenType::WriteAppend ? OPEN_ALWAYS : CREATE_ALWAYS),
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fd_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error{"open failed: " + to_string(path_) +
                               ", error: " + std::to_string(GetLastError())};
    }
#else
    fd_ = (type == OpenType::ReadOnly)
              ? open(path_.c_str(), O_RDONLY)
              : open(path_.c_str(),
                     (type == OpenType::WriteAppend)
                         ? (O_WRONLY | O_CREAT | O_APPEND)
                         : (O_WRONLY | O_CREAT | O_TRUNC),
                     0644);
    if (fd_ == -1) {
      throw std::runtime_error{"open failed: " + path_};
    }
#endif
  }
#if defined(_WIN32)
  std::wstring path_;
#else
  std::string path_;
#endif
  bool append_{false};
  NativeHandle fd_{INVALID_NATIVE_HANDLE_VALUE};
};

class Buffer {
  using buffer_container_type = buffer;

 public:
  Buffer(buffer_container_type& buf)
      : buf_{std::ref(buf)}, pipe_{Pipe::create()} {}
  Buffer() : buf_{buffer_container_type{}}, pipe_{Pipe::create()} {}

  buffer& buf() {
    return std::visit(
        []<typename T>(T& value) -> buffer_container_type& {
          if constexpr (std::is_same_v<T, buffer_container_type>) {
            return value;
          } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                     buffer_container_type>>) {
            return value.get();
          }
        },
        buf_);
  }
  Pipe& pipe() { return pipe_; }

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
  explicit Stdio(Pipe const& p) : redirect_(std::make_unique<value_type>(p)) {}
  explicit Stdio(File f)
      : redirect_(std::make_unique<value_type>(std::move(f))) {}
  explicit Stdio(buffer& buf)
      : redirect_(std::make_unique<value_type>(Buffer(buf))) {}
  Stdio(Stdio&&) noexcept = default;
  Stdio& operator=(Stdio&&) noexcept = default;
  Stdio(Stdio const&) = delete;
  Stdio& operator=(Stdio const&) = delete;
  virtual ~Stdio() {
    if (!redirect_) {
      return;
    }
    std::visit(
        []<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            if (value.read() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe.read() not closed!" << '\n';
            }
            if (value.write() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe.write() not closed!" << '\n';
            }

          } else if constexpr (std::is_same_v<T, Buffer>) {
            if (value.pipe().read() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer.pipe.read() not closed!" << '\n';
            }
            if (value.pipe().write() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer.pipe.write() not closed!" << '\n';
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
        [this]<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
#if defined(_WIN32)
            NativeHandle non_inherit_handle =
                fileno() == 0 ? value.write() : value.read();
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
                fileno() == 0 ? value.pipe().write() : value.pipe().read();
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
        [this]<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            close_native_handle(fileno() == 0 ? value.read() : value.write());
          } else if constexpr (std::is_same_v<T, File>) {
            value.close();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            close_native_handle(fileno() == 0 ? value.pipe().read()
                                              : value.pipe().write());
          }
        },
        *redirect_);
  }
  void close_all() {
    if (!redirect_) {
      return;
    }
    std::visit(
        []<typename T>([[maybe_unused]] T& value) {
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
            [[maybe_unused]] T& value) -> std::optional<NativeHandle> {
          if constexpr (std::is_same_v<T, Pipe>) {
            return fileno() == 0 ? value.read() : value.write();
          } else if constexpr (std::is_same_v<T, File>) {
            return value.fd();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            return fileno() == 0 ? value.pipe().read() : value.pipe().write();
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
        [this]<typename T>([[maybe_unused]] T& value)
            -> std::optional<std::reference_wrapper<NativeHandle>> {
          if constexpr (std::is_same_v<T, Buffer>) {
            return std::ref(fileno() == 0 ? value.pipe().write()
                                          : value.pipe().read());
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
        [this]<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            dup2(fileno() == 0 ? value.read() : value.write(), fileno());
            close_native_handle(value.read());
            close_native_handle(value.write());
          } else if constexpr (std::is_same_v<T, File>) {
            dup2(value.fd(), fileno());
            value.close();
          } else if constexpr (std::is_same_v<T, Buffer>) {
            dup2(fileno() == 0 ? value.pipe().read() : value.pipe().write(),
                 fileno());
            value.pipe().close_all();
          }
        },
        *redirect_);
  }

#if defined(SUBPROCESS_USE_POSIX_SPAWN) && SUBPROCESS_USE_POSIX_SPAWN
  void setup_stdio_for_posix_spawn(posix_spawn_file_actions_t& action) {
    if (!redirect_) {
      return;
    }
    std::visit(
        [this, &action]<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            posix_spawn_file_actions_adddup2(
                &action, fileno() == 0 ? value.read() : value.write(),
                fileno());
            posix_spawn_file_actions_addclose(&action, value.read());
            posix_spawn_file_actions_addclose(&action, value.write());
          } else if constexpr (std::is_same_v<T, File>) {
            posix_spawn_file_actions_adddup2(&action, value.fd(), fileno());
            posix_spawn_file_actions_addclose(&action, value.fd());
          } else if constexpr (std::is_same_v<T, Buffer>) {
            posix_spawn_file_actions_adddup2(
                &action,
                fileno() == 0 ? value.pipe().read() : value.pipe().write(),
                fileno());
            posix_spawn_file_actions_addclose(&action, value.pipe().read());
            posix_spawn_file_actions_addclose(&action, value.pipe().write());
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
        [this]<typename T>([[maybe_unused]] T& value)
            -> std::optional<std::reference_wrapper<NativeHandle>> {
          if constexpr (std::is_same_v<T, Buffer>) {
            return std::ref(fileno() == 0 ? value.pipe().write()
                                          : value.pipe().read());
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
  Stdin(Stdin&&) noexcept = default;
  Stdin& operator=(Stdin&&) noexcept = default;
  Stdin(Stdin const&) = delete;
  Stdin& operator=(Stdin const&) = delete;
  int fileno() const override { return 0; }
};
class Stdout : public Stdio {
 public:
  using Stdio::Stdio;
  Stdout(Stdout&&) noexcept = default;
  Stdout& operator=(Stdout&&) noexcept = default;
  Stdout(Stdout const&) = delete;
  Stdout& operator=(Stdout const&) = delete;
  int fileno() const override { return 1; }
};
class Stderr : public Stdio {
 public:
  using Stdio::Stdio;
  Stderr(Stderr&&) noexcept = default;
  Stderr& operator=(Stderr&&) noexcept = default;
  Stderr(Stderr const&) = delete;
  Stderr& operator=(Stderr const&) = delete;
  int fileno() const override { return 2; }
};

struct stdin_redirector {
  Stdin operator<(Pipe p) const { return Stdin{std::move(p)}; }
  Stdin operator<(std::string const& file) const { return Stdin{File{file}}; }
#if defined(_WIN32)
  Stdin operator<(std::wstring const& file) const { return Stdin{File{file}}; }
#endif
  Stdin operator<(buffer& buf) const { return Stdin{buf}; }
};

struct stdout_redirector {
  Stdout operator>(Pipe const& p) const { return Stdout{p}; }
  Stdout operator>(std::string const& file) const { return Stdout{File{file}}; }
#if defined(_WIN32)
  Stdout operator>(std::wstring const& file) const {
    return Stdout{File{file}};
  }
#endif
  Stdout operator>(buffer& buf) const {
    buf.clear();
    return Stdout{buf};
  }
  Stdout operator>>(buffer& buf) const { return Stdout{buf}; }

  Stdout operator>>(std::string const& file) const {
    return Stdout{File{file, true}};
  }
#if defined(_WIN32)
  Stdout operator>>(std::wstring const& file) const {
    return Stdout{File{file, true}};
  }
#endif
};

struct stderr_redirector {
  Stderr operator>(Pipe p) const { return Stderr{std::move(p)}; }
  Stderr operator>(std::string const& file) const { return Stderr{File{file}}; }
#if defined(_WIN32)
  Stderr operator>(std::wstring const& file) const {
    return Stderr{File{file}};
  }
#endif
  Stderr operator>(buffer& buf) const {
    buf.clear();
    return Stderr{buf};
  }
  Stderr operator>>(buffer& buf) const { return Stderr{buf}; }
  Stderr operator>>(std::string const& file) const {
    return Stderr{File{file, true}};
  }
#if defined(_WIN32)
  Stderr operator>>(std::wstring const& file) const {
    return Stderr{File{file, true}};
  }
#endif
};

struct Cwd {
#if defined(_WIN32)
  std::wstring cwd;
#else
  std::string cwd;
#endif
};

// set envs to process environment(override)
struct Env {
#if defined(_WIN32)
  std::map<std::wstring, std::wstring> env;
#else
  std::map<std::string, std::string> env;
#endif
};

// append envs to process environment
struct EnvAppend {
#if defined(_WIN32)
  std::map<std::wstring, std::wstring> env;
#else
  std::map<std::string, std::string> env;
#endif
};

// append value for special environment, for example: PATH
struct EnvItemAppend {
  EnvItemAppend& operator+=(std::string val) {
#if defined(_WIN32)
    std::get<1>(kv) = to_wstring(val);
#else
    std::get<1>(kv) = val;
#endif
    std::get<2>(kv) = true;
    return *this;
  }
  EnvItemAppend& operator<<=(std::string val) {
#if defined(_WIN32)
    std::get<1>(kv) = to_wstring(val);
#else
    std::get<1>(kv) = val;
#endif
    std::get<2>(kv) = false;
    return *this;
  }

#if defined(_WIN32)
  EnvItemAppend& operator+=(std::wstring val) {
    std::get<1>(kv) = val;
    std::get<2>(kv) = true;
    return *this;
  }
  EnvItemAppend& operator<<=(std::wstring val) {
    std::get<1>(kv) = val;
    std::get<2>(kv) = false;
    return *this;
  }
#endif

#if defined(_WIN32)
  // name, value, is_append
  std::tuple<std::wstring, std::wstring, bool> kv;
#else
  std::tuple<std::string, std::string, bool> kv;
#endif
};

struct cwd_operator {
  Cwd operator=(std::string const& p) const {
#if defined(_WIN32)
    return Cwd{to_wstring(p)};
#else
    return Cwd{p};
#endif
  }
#if defined(_WIN32)
  Cwd operator=(std::wstring const& p) const { return Cwd{p}; }
#endif
};
struct env_operator {
  Env operator=(std::map<std::string, std::string> env) const {
#if defined(_WIN32)
    std::map<std::wstring, std::wstring> wenv;
    for (auto const& entry : env) {
      wenv[to_wstring(entry.first)] = to_wstring(entry.second);
    }
    return Env{std::move(wenv)};
#else
    return Env{std::move(env)};
#endif
  }
#if defined(_WIN32)
  Env operator=(std::map<std::wstring, std::wstring> env) const {
    return Env{std::move(env)};
  }
#endif
  EnvAppend operator+=(std::map<std::string, std::string> env) const {
#if defined(_WIN32)
    std::map<std::wstring, std::wstring> wenv;
    for (auto const& entry : env) {
      wenv[to_wstring(entry.first)] = to_wstring(entry.second);
    }
    return EnvAppend{std::move(wenv)};
#else
    return EnvAppend{std::move(env)};
#endif
  }
#if defined(_WIN32)
  EnvAppend operator+=(std::map<std::wstring, std::wstring> env) const {
    return EnvAppend{std::move(env)};
  }
#endif
  EnvItemAppend operator[](std::string key) const {
#if defined(_WIN32)
    return EnvItemAppend{{to_wstring(key), L"", true}};
#else
    return EnvItemAppend{{key, "", true}};
#endif
  }
};

namespace named_args {
#if defined(_WIN32)
[[maybe_unused]] inline const static auto devnull = std::string{"NUL"};
#else
[[maybe_unused]] inline const static auto devnull = std::string{"/dev/null"};
#endif
[[maybe_unused]] inline constexpr static stdin_redirector std_in;
[[maybe_unused]] inline constexpr static stdout_redirector std_out;
[[maybe_unused]] inline constexpr static stderr_redirector std_err;
[[maybe_unused]] inline constexpr static cwd_operator cwd;
[[maybe_unused]] inline constexpr static env_operator env;

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
#if defined(_WIN32)
[[maybe_unused]] inline const static auto $devnull = std::string{"NUL"};
#else
[[maybe_unused]] inline const static auto $devnull = std::string{"/dev/null"};
#endif
[[maybe_unused]] inline constexpr static stdin_redirector $stdin;
[[maybe_unused]] inline constexpr static stdout_redirector $stdout;
[[maybe_unused]] inline constexpr static stderr_redirector $stderr;
[[maybe_unused]] inline constexpr static cwd_operator $cwd;
[[maybe_unused]] inline constexpr static env_operator $env;
#endif
}  // namespace named_args
template <typename T>
concept is_named_argument = std::is_same_v<Env, std::decay_t<T>> ||
                            std::is_same_v<Stdin, std::decay_t<T>> ||
                            std::is_same_v<Stdout, std::decay_t<T>> ||
                            std::is_same_v<Stderr, std::decay_t<T>> ||
                            std::is_same_v<Cwd, std::decay_t<T>> ||
                            std::is_same_v<EnvAppend, std::decay_t<T>> ||
                            std::is_same_v<EnvItemAppend, std::decay_t<T>>;
template <typename T>
concept is_string_type = std::is_same_v<char*, std::decay_t<T>> ||
                         std::is_same_v<wchar_t*, std::decay_t<T>> ||
                         std::is_same_v<const char*, std::decay_t<T>> ||
                         std::is_same_v<const wchar_t*, std::decay_t<T>> ||
                         std::is_same_v<std::string, std::decay_t<T>> ||
                         std::is_same_v<std::wstring, std::decay_t<T>>;
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
    requires(is_named_argument<T> && ...)
#if defined(_WIN32)
  explicit subprocess(std::vector<std::wstring> cmd, T&&... args)
#else
  explicit subprocess(std::vector<std::string> cmd, T&&... args)
#endif
      : cmd_(std::move(cmd)) {
#if defined(_WIN32)
    std::map<std::wstring, std::wstring> environments;
    std::map<std::wstring, std::wstring> env_appends;
    std::vector<std::tuple<std::wstring, std::wstring, bool>> env_item_appends;
#else
    std::map<std::string, std::string> environments;
    std::map<std::string, std::string> env_appends;
    std::vector<std::tuple<std::string, std::string, bool>> env_item_appends;
#endif
    (void)(..., ([&]<typename Arg>(Arg&& arg) {
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
             if constexpr (std::is_same_v<ArgType, EnvAppend>) {
               env_appends.insert(arg.env.begin(), arg.env.end());
             }
             if constexpr (std::is_same_v<ArgType, EnvItemAppend>) {
               env_item_appends.push_back(std::forward<Arg>(arg).kv);
             }
             if constexpr (std::is_same_v<ArgType, Cwd>) {
               cwd_ = arg.cwd;
             }
             static_assert(std::is_same_v<Env, std::decay_t<T>> ||
                               std::is_same_v<Stdin, std::decay_t<T>> ||
                               std::is_same_v<Stdout, std::decay_t<T>> ||
                               std::is_same_v<Stderr, std::decay_t<T>> ||
                               std::is_same_v<Cwd, std::decay_t<T>> ||
                               std::is_same_v<EnvAppend, std::decay_t<T>> ||
                               std::is_same_v<EnvItemAppend, std::decay_t<T>>,
                           "Invalid argument type passed to run function.");
           }(std::forward<T>(args))));
    if ((!env_item_appends.empty() || !env_appends.empty()) &&
        environments.empty()) {
      environments = get_all_envs();
    }
    environments.insert(env_appends.begin(), env_appends.end());
#ifdef _WIN32
    char path_env_sep = ';';
#else
    char path_env_sep = ':';
#endif
    for (auto const& [name, value, is_append] : env_item_appends) {
      auto it = environments.find(name);
#ifdef _WIN32
      if (it == environments.end()) {
        auto upper_key = name;
        std::transform(upper_key.begin(), upper_key.end(), upper_key.begin(),
                       [](auto c) { return std::toupper(c, std::locale()); });
        it = environments.find(upper_key);
      }
#endif
      if (it == environments.end()) {
        environments.insert({name, value});
      } else {
        if (is_append) {
          it->second.push_back(path_env_sep);
          it->second.append(value);
        } else {
          it->second.insert(it->second.begin(), value.begin(), value.end());
          it->second.insert(it->second.begin(), path_env_sep);
        }
      }
    }
    env_ = environments;
#if defined(_WIN32)
    ZeroMemory(&process_information_, sizeof(process_information_));
    ZeroMemory(&startupinfo_, sizeof(startupinfo_));
    startupinfo_.cb = sizeof(startupinfo_);
#endif
  }

#if defined(_WIN32)
  template <typename... T>
    requires(is_named_argument<T> && ...)
  explicit subprocess(std::vector<std::string> cmd, T&&... args)
      : subprocess(
            [](auto const& cmd) {
              std::vector<std::wstring> ret;
              std::transform(cmd.begin(), cmd.end(), std::back_inserter(ret),
                             [](auto const& s) { return to_wstring(s); });
              return ret;
            }(cmd),
            std::forward<T>(args)...) {}
#endif

  subprocess(subprocess&&) noexcept = default;
  subprocess& operator=(subprocess&&) noexcept = default;
  subprocess(const subprocess&) = delete;
  subprocess& operator=(const subprocess&) = delete;

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

    auto command = argv_to_command_line_string(cmd_);

    auto env_block = create_environment_string_data(env_);

    auto success = CreateProcessW(
        nullptr, command.data(), NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT,
        env_block.empty() ? nullptr : env_block.data(),
        cwd_.empty() ? nullptr : cwd_.data(), &startupinfo_,
        &process_information_);

    if (success) {
      manage_pipe_io();
    } else {
      std::wcerr << to_wstring(get_last_error_msg()) << L'\n';
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
        in.has_value() ? std::get<Buffer>(*stdin_.redirect_).buf().buf_
                       : tmp_buf,
        out.has_value() ? out.value().get() : tmp_handle,
        out.has_value() ? std::get<Buffer>(*stdout_.redirect_).buf().buf_
                        : tmp_buf,
        err.has_value() ? err.value().get() : tmp_handle,
        err.has_value() ? std::get<Buffer>(*stderr_.redirect_).buf().buf_
                        : tmp_buf);
#else
    auto in = stdin_.get_parent_pipe_fd_for_polling();
    auto out = stdout_.get_parent_pipe_fd_for_polling();
    auto err = stderr_.get_parent_pipe_fd_for_polling();

    NativeHandle tmp_handle = INVALID_NATIVE_HANDLE_VALUE;
    std::vector<char> tmp_buf;
    read_write_pipes(
        in.has_value() ? in.value().get() : tmp_handle,
        in.has_value() ? std::get<Buffer>(*stdin_.redirect_).buf().buf_
                       : tmp_buf,
        out.has_value() ? out.value().get() : tmp_handle,
        out.has_value() ? std::get<Buffer>(*stdout_.redirect_).buf().buf_
                        : tmp_buf,
        err.has_value() ? err.value().get() : tmp_handle,
        err.has_value() ? std::get<Buffer>(*stderr_.redirect_).buf().buf_
                        : tmp_buf);
#endif
  }
#if !defined(_WIN32)
  void execute_command_in_child() {
    stdin_.setup_stdio_in_child_process();
    stdout_.setup_stdio_in_child_process();
    stderr_.setup_stdio_in_child_process();

    std::vector<char*> cmd{};
    std::transform(cmd_.begin(), cmd_.end(), std::back_inserter(cmd),
                   [](std::string& s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!cwd_.empty() && (-1 == chdir(cwd_.data()))) {
      throw std::runtime_error(get_last_error_msg());
    }

    std::string exe_to_exec = cmd_[0];
    if (exe_to_exec.find('/') == std::string::npos) {
      auto resolved_path = find_command_in_path(exe_to_exec);
      if (resolved_path.has_value()) {
        exe_to_exec = resolved_path.value();
      }
    }

    if (!env_.empty()) {
      std::vector<std::string> env_tmp{};

      std::transform(
          env_.begin(), env_.end(), std::back_inserter(env_tmp),
          [](auto& entry) { return entry.first + "=" + entry.second; });

      std::vector<char*> envs{};
      std::transform(env_tmp.begin(), env_tmp.end(), std::back_inserter(envs),
                     [](auto& s) { return s.data(); });
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
  void add_posix_spawn_file_actions(posix_spawn_file_actions_t& action) {
    stdin_.setup_stdio_for_posix_spawn(action);
    stdout_.setup_stdio_for_posix_spawn(action);
    stderr_.setup_stdio_for_posix_spawn(action);

    std::vector<char*> cmd{};
    std::transform(cmd_.begin(), cmd_.end(), std::back_inserter(cmd),
                   [](std::string& s) { return s.data(); });
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
      auto resolved_path = find_command_in_path(exe_to_exec);
      if (resolved_path.has_value()) {
        exe_to_exec = resolved_path.value();
      }
    }

    std::vector<char*> envs{};
    if (!env_.empty()) {
      std::vector<std::string> env_tmp{};

      std::transform(
          env_.begin(), env_.end(), std::back_inserter(env_tmp),
          [](auto& entry) { return entry.first + "=" + entry.second; });

      std::transform(env_tmp.begin(), env_tmp.end(), std::back_inserter(envs),
                     [](auto& s) { return s.data(); });
      envs.push_back(nullptr);
      auto ret = posix_spawn(&pid_, exe_to_exec.c_str(), &action, nullptr,
                             cmd.data(), envs.data());
      if (ret != 0) {
        pid_ = INVALID_NATIVE_HANDLE_VALUE;
      }
    } else {
      auto ret = posix_spawn(&pid_, exe_to_exec.c_str(), &action, nullptr,
                             cmd.data(), nullptr);
      if (ret != 0) {
        pid_ = INVALID_NATIVE_HANDLE_VALUE;
      }
    }
  }
#endif  // SUBPROCESS_USE_POSIX_SPAWN
#endif  // !_WIN32

 private:
#if defined(_WIN32)
  std::vector<std::wstring> cmd_;
  std::wstring cwd_{};
  std::map<std::wstring, std::wstring> env_;
#else
  std::vector<std::string> cmd_;
  std::string cwd_{};
  std::map<std::string, std::string> env_;
#endif
  Stdin stdin_;
  Stdout stdout_;
  Stderr stderr_;
#if defined(_WIN32)
  PROCESS_INFORMATION process_information_;
  STARTUPINFOW startupinfo_;
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
  requires(detail::is_named_argument<T> && ...)
inline int run(std::vector<std::string> cmd, T&&... args) {
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}

#if defined(_WIN32)
template <typename... T>
  requires(detail::is_named_argument<T> && ...)
inline int run(std::vector<std::wstring> cmd, T&&... args) {
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}
#endif

template <typename... Args>
  requires((detail::is_named_argument<Args> || detail::is_string_type<Args>) &&
           ...)
inline int run(Args... args) {
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
  requires(detail::is_named_argument<T> && ...)
inline int $(std::vector<std::string> cmd, T&&... args) {
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}

#if defined(_WIN32)
#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
template <typename... T>
  requires(detail::is_named_argument<T> && ...)
inline int $(std::vector<std::wstring> cmd, T&&... args) {
  return detail::subprocess(std::move(cmd), std::forward<T>(args)...).run();
}
#endif  // USE_DOLLAR_NAMED_VARIABLES
#endif  // _WIN32

template <typename... Args>
  requires((detail::is_named_argument<Args> || detail::is_string_type<Args>) &&
           ...)
inline int $(Args... args) {
  return run(std::forward<Args>(args)...);
}
#endif

template <typename... T>
  requires((detail::is_named_argument<T> && ...) && true)
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    std::vector<std::string> cmd, T&&... args) {
  using namespace named_arguments;
  std::tuple<int, subprocess::buffer, subprocess::buffer> result;
  auto& [exit_code_, std_out_, std_err_] = result;
  exit_code_ = run(std::move(cmd), std::forward<T>(args)..., std_out > std_out_,
                   std_err > std_err_);
  return result;
}
#if defined(_WIN32)
template <typename... T>
  requires((detail::is_named_argument<T> && ...) && true)
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    std::vector<std::wstring> cmd, T&&... args) {
  using namespace named_arguments;
  std::tuple<int, subprocess::buffer, subprocess::buffer> result;
  auto& [exit_code_, std_out_, std_err_] = result;
  exit_code_ = run(std::move(cmd), std::forward<T>(args)..., std_out > std_out_,
                   std_err > std_err_);
  return result;
}
#endif  // _WIN32

template <typename... Args>
  requires((detail::is_named_argument<Args> || detail::is_string_type<Args>) &&
           ...)
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    Args... args) {
  using namespace named_arguments;
  std::tuple<int, subprocess::buffer, subprocess::buffer> result;
  auto& [exit_code_, std_out_, std_err_] = result;
  exit_code_ =
      run(std::forward<Args>(args)..., std_out > std_out_, std_err > std_err_);
  return result;
}

inline std::optional<std::string> getenv(const std::string& name) {
  return detail::get_env(name);
}
#if defined(_WIN32)
inline std::optional<std::wstring> getenv(const std::wstring& name) {
  return detail::get_env(name);
}
#endif

inline std::optional<std::string> home() {
#if defined(_WIN32)
  auto user_profile = detail::get_env("USERPROFILE");
  if (user_profile.has_value() && !user_profile->empty()) {
    return user_profile;
  }
  auto home_drive = detail::get_env("HOMEDRIVE");
  auto homepath = detail::get_env("HOMEPATH");
  if (home_drive.has_value() && homepath.has_value() &&
      !home_drive.value().empty() && !homepath.value().empty()) {
    return home_drive.value() + homepath.value();
  }
#else
  auto home_dir = detail::get_env("HOME");
  if (home_dir.has_value() && !home_dir->empty()) {
    return home_dir;
  }

  // If HOME is not set, fallback to getpwuid
  // This is a more reliable method for finding the home directory
  uid_t uid = getuid();
  struct passwd* pw = getpwuid(uid);
  if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0') {
    return std::string(pw->pw_dir);
  }
#endif
  return std::nullopt;
}

inline
#if defined(_WIN32)
    std::map<std::wstring, std::wstring>
#else
    std::map<std::string, std::string>
#endif
    environs() {
  return detail::get_all_envs();
}

#if defined(_WIN32)
using pid_type = unsigned long;
#else
using pid_type = int;
#endif
inline pid_type pid() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

inline std::string getcwd() {
#if defined(_WIN32)
  auto size = GetCurrentDirectoryW(0, NULL);
  if (size == 0) {
    return "";
  }
  std::vector<wchar_t> buffer(size);
  if (GetCurrentDirectoryW(size, buffer.data()) == 0) {
    return "";
  }
  return to_string(buffer.data());
#else
  std::unique_ptr<char, decltype(&::free)> ret(::getcwd(nullptr, 0), &::free);
  if (ret) {
    return std::string(ret.get());
  } else {
    return "";
  }
#endif
}

inline bool chdir(std::string const& dir) {
#if defined(_WIN32)
  return SetCurrentDirectoryW(to_wstring(dir).c_str());
#else
  return -1 != ::chdir(dir.c_str());
#endif
}
#if defined(_WIN32)
inline bool chdir(std::wstring const& dir) {
  return SetCurrentDirectoryW(dir.c_str());
}
#endif

}  // namespace subprocess

namespace process {
using subprocess::chdir;
using subprocess::environs;
using subprocess::getcwd;
using subprocess::getenv;
using subprocess::home;
using subprocess::pid;
}  // namespace process

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
using subprocess::$;
using subprocess::named_arguments::$cwd;
using subprocess::named_arguments::$devnull;
using subprocess::named_arguments::$env;
using subprocess::named_arguments::$stderr;
using subprocess::named_arguments::$stdin;
using subprocess::named_arguments::$stdout;
#endif

#endif  // __SUBPROCESS_SUBPROCESS_HPP__
