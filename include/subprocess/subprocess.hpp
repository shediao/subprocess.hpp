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
 *   subprocess::buffer inbuf{"xxxxxxxxx"};
 *   subprocess::buffer outbuf;
 *   subprocess::buffer errbuf;
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
#if defined(__linux__)
#include <sys/epoll.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
#include <sys/event.h>
#endif
#endif

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#if !defined(_WIN32)
extern char** environ;
#endif  // !_WIN32

namespace subprocess {

#ifndef SUBPROCESS_HAS_EXCEPTIONS
#if defined(_MSC_VER) && defined(_CPPUNWIND)
// MSVC defines _CPPUNWIND to 1 if and only if exceptions are enabled.
#define SUBPROCESS_HAS_EXCEPTIONS 1
#elif defined(__BORLANDC__)
// C++Builder's implementation of the STL uses the _HAS_EXCEPTIONS
// macro to enable exceptions, so we'll do the same.
// Assumes that exceptions are enabled by default.
#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif  // _HAS_EXCEPTIONS
#define SUBPROCESS_HAS_EXCEPTIONS _HAS_EXCEPTIONS
#elif defined(__clang__)
// clang defines __EXCEPTIONS if and only if exceptions are enabled before clang
// 220714, but if and only if cleanups are enabled after that. In Obj-C++ files,
// there can be cleanups for ObjC exceptions which also need cleanups, even if
// C++ exceptions are disabled. clang has __has_feature(cxx_exceptions) which
// checks for C++ exceptions starting at clang r206352, but which checked for
// cleanups prior to that. To reliably check for C++ exception availability with
// clang, check for
// __EXCEPTIONS && __has_feature(cxx_exceptions).
#if defined(__EXCEPTIONS) && __EXCEPTIONS && __has_feature(cxx_exceptions)
#define SUBPROCESS_HAS_EXCEPTIONS 1
#else
#define SUBPROCESS_HAS_EXCEPTIONS 0
#endif
#elif defined(__GNUC__) && defined(__EXCEPTIONS) && __EXCEPTIONS
// gcc defines __EXCEPTIONS to 1 if and only if exceptions are enabled.
#define SUBPROCESS_HAS_EXCEPTIONS 1
#elif defined(__SUNPRO_CC)
// Sun Pro CC supports exceptions.  However, there is no compile-time way of
// detecting whether they are enabled or not.  Therefore, we assume that
// they are enabled unless the user tells us otherwise.
#define SUBPROCESS_HAS_EXCEPTIONS 1
#elif defined(__IBMCPP__) && defined(__EXCEPTIONS) && __EXCEPTIONS
// xlC defines __EXCEPTIONS to 1 if and only if exceptions are enabled.
#define SUBPROCESS_HAS_EXCEPTIONS 1
#elif defined(__HP_aCC)
// Exception handling is in effect by default in HP aCC compiler. It has to
// be turned of by +noeh compiler option if desired.
#define SUBPROCESS_HAS_EXCEPTIONS 1
#else
// For other compilers, we assume exceptions are disabled to be
// conservative.
#define SUBPROCESS_HAS_EXCEPTIONS 0
#endif  // defined(_MSC_VER) || defined(__BORLANDC__)
#endif  // SUBPROCESS_HAS_EXCEPTIONS

#if SUBPROCESS_HAS_EXCEPTIONS
#include <stdexcept>
#endif

#ifndef USE_DOLLAR_NAMED_VARIABLES
#define USE_DOLLAR_NAMED_VARIABLES 1
#endif

namespace detail {
class subprocess;
class Redirector;
class StdinRedirector;
class StdoutRedirector;
class StderrRedirector;

inline void die(std::string const& msg) {
#if SUBPROCESS_HAS_EXCEPTIONS
  throw std::runtime_error(msg);
#else
  if (!msg.empty()) {
#if defined(_WIN32)
    (void)WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg.c_str(),
                    static_cast<DWORD>(msg.size()), NULL, NULL);
#else
    [[maybe_unused]] auto ret = write(STDERR_FILENO, msg.c_str(), msg.size());
#endif
  }
  abort();
#endif
}

#if defined(_WIN32)
// Helper function to convert a UTF-8 std::string to a UTF-16 std::wstring
inline std::wstring utf8_to_utf16(const std::string_view str) {
  if (str.empty()) {
    return {};
  }
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
  if (size_needed <= 0) {
    die("MultiByteToWideChar error: " + std::to_string(GetLastError()));
  }
  std::wstring wstr(static_cast<size_t>(size_needed), 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0],
                      size_needed);
  return wstr;
}

// Helper function to convert a UTF-16 std::wstring to a UTF-8 std::string
inline std::string utf16_to_utf8(const std::wstring_view wstr) {
  if (wstr.empty()) {
    return {};
  }
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  if (size_needed <= 0) {
    die("WideCharToMultiByte error: " + std::to_string(GetLastError()));
  }
  std::string str(static_cast<size_t>(size_needed), 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0],
                      size_needed, NULL, NULL);
  return str;
}
#endif  // _WIN32

#if defined(_WIN32)
using NativeHandle = HANDLE;
static inline const NativeHandle INVALID_NATIVE_HANDLE_VALUE =
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

class buffer {
 public:
  buffer() = default;
  buffer(std::string_view const& str) : buf_(str.begin(), str.end()) {}
  buffer(std::string::iterator first, std::string::iterator last)
      : buf_(first, last) {}
  auto* data() const { return buf_.data(); }
  auto size() const { return buf_.size(); }
  auto span() const { return std::span(buf_.data(), buf_.size()); }
  auto to_string() const {
    return std::string(buf_.data(), buf_.data() + buf_.size());
  }
  auto empty() const { return buf_.empty(); }
  auto clear() { return buf_.clear(); }

  auto begin() const { return buf_.begin(); }
  auto end() const { return buf_.end(); }

  void append(const char* data, size_t size) {
    buf_.insert(buf_.end(), data, data + size);
  }

  // operator== for test
  bool operator==(const buffer& other) const {
    return std::equal(buf_.begin(), buf_.end(), other.buf_.begin(),
                      other.buf_.end());
  }
  template <typename T>
  bool operator==(std::span<T> other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<unsigned char const*>(other.data()),
        other.size() * sizeof(T)};
    return std::equal(buf_.begin(), buf_.end(), other_span.begin(),
                      other_span.end());
  }
  template <typename CharT>
  bool operator==(std::basic_string_view<CharT> other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<unsigned char const*>(other.data()),
        other.size() * sizeof(CharT)};
    return std::equal(buf_.begin(), buf_.end(), other_span.begin(),
                      other_span.end());
  }
  template <typename CharT>
  bool operator==(std::basic_string<CharT> other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<unsigned char const*>(other.data()),
        other.size() * sizeof(CharT)};
    return std::equal(buf_.begin(), buf_.end(), other_span.begin(),
                      other_span.end());
  }
  bool operator==(const char* other) const {
    return this->operator==(std::string_view(other));
  }
  bool operator==(const wchar_t* other) const {
    return this->operator==(std::wstring_view(other));
  }

 private:
  std::vector<unsigned char> buf_;
};

class HandleGuard {
 public:
  explicit HandleGuard(NativeHandle h = INVALID_NATIVE_HANDLE_VALUE)
      : handle_(h) {}
  ~HandleGuard() { Close(); }
  HandleGuard(const HandleGuard&) = delete;
  HandleGuard& operator=(const HandleGuard&) = delete;
  HandleGuard(HandleGuard&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_NATIVE_HANDLE_VALUE;
  }
  HandleGuard& operator=(HandleGuard&& other) noexcept {
    if (this != &other) {
      Close();
      handle_ = other.handle_;
      other.handle_ = INVALID_NATIVE_HANDLE_VALUE;
    }
    return *this;
  }

  NativeHandle get() const { return handle_; }
  NativeHandle* p_get() { return &handle_; }

  void Close() { close_native_handle(handle_); }

  bool IsValid() const { return handle_ != INVALID_NATIVE_HANDLE_VALUE; }

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
               int max_count = -1, bool compress_tokens = false) {
  auto begin = str.begin();
  auto delimiter = begin;
  int count = 0;

  while ((max_count < 0 || count++ < max_count) &&
         (delimiter = std::find_if(begin, str.end(), f)) != str.end()) {
    to.insert(to.end(), {begin, delimiter});
    if (compress_tokens) {
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

// Escape and quote a single argument according to Windows command-line rules.
// Rules (from MSDN):
//  - Arguments with whitespace or quotes are wrapped in double quotes.
//  - Backslashes preceding a double quote are doubled.
//  - Backslashes at the end of the argument (before the closing quote) are
//    doubled to prevent them from escaping the closing quote.
inline void append_windows_arg(std::vector<wchar_t>& cmd,
                               std::wstring const& arg) {
  bool needs_quoting =
      arg.empty() || arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
  if (!needs_quoting) {
    cmd.insert(cmd.end(), arg.begin(), arg.end());
    return;
  }
  cmd.push_back(L'"');
  // Count backslashes and handle escaped quotes
  for (auto it = arg.begin(); it != arg.end(); ++it) {
    if (*it == L'\\') {
      // Count consecutive backslashes
      auto start = it;
      while (it != arg.end() && *it == L'\\') {
        ++it;
      }
      size_t backslash_count = static_cast<size_t>(it - start);
      if (it == arg.end() || *it == L'"') {
        // Backslashes before a quote or at end: double them
        backslash_count *= 2;
      }
      cmd.insert(cmd.end(), backslash_count, L'\\');
      if (it == arg.end()) {
        break;
      }
      // Now handle the non-backslash character
      if (*it == L'"') {
        cmd.push_back(L'\\');
      }
      cmd.push_back(*it);
    } else if (*it == L'"') {
      cmd.push_back(L'\\');
      cmd.push_back(L'"');
    } else {
      cmd.push_back(*it);
    }
  }
  cmd.push_back(L'"');
}

inline std::vector<wchar_t> argv_to_command_line_string(
    std::vector<std::basic_string<wchar_t>> const& cmds) {
  std::vector<wchar_t> command;
  if (cmds.empty()) {
    return command;
  }
  append_windows_arg(command, cmds[0]);
  for (auto it = std::next(cmds.begin()); it != cmds.end(); ++it) {
    command.push_back(L' ');
    append_windows_arg(command, *it);
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

inline std::string get_last_error_message() {
#if defined(_WIN32)
  DWORD error = GetLastError();
  LPVOID error_msg{NULL};

  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPWSTR)&error_msg, 0, NULL);
  if (error_msg) {
    auto ret = utf16_to_utf8((wchar_t*)error_msg);
    LocalFree(error_msg);
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

inline void write_to_native_handle(NativeHandle& fd, buffer const& write_data) {
  HandleGuard guard(fd);
  auto write_span = write_data.span();
  while (!write_span.empty()) {
#if defined(_WIN32)
    DWORD write_count{0};
    if (!WriteFile(fd, write_span.data(), static_cast<DWORD>(write_span.size()),
                   &write_count, 0)) {
      die("WriteFile error: " + get_last_error_message());
    }
    if (write_count > 0) {
      write_span = write_span.subspan(static_cast<size_t>(write_count));
    }
#else
    auto write_count = write(fd, write_data.data(), write_data.size());
    if (write_count > 0) {
      write_span = write_span.subspan(static_cast<size_t>(write_count));
    }
    if (write_count == -1) {
      die("write() error: " + std::to_string(errno));
    }
#endif
  }
  fd = INVALID_NATIVE_HANDLE_VALUE;
}

inline void read_from_native_handle(NativeHandle& fd, buffer& reate_data) {
  HandleGuard guard(fd);
  char buf[1024];
#if defined(_WIN32)
  DWORD read_count{0};
  while (ReadFile(fd, buf, sizeof(buf), &read_count, 0) && read_count > 0) {
    reate_data.append(buf, static_cast<size_t>(read_count));
  }
#else
  ssize_t read_count = 0;
  do {
    read_count = read(fd, buf, std::size(buf));
    if (read_count > 0) {
      reate_data.append(buf, static_cast<size_t>(read_count));
    }
    if (read_count == -1) {
      die(get_last_error_message());
    }
  } while (read_count > 0);
#endif
  fd = INVALID_NATIVE_HANDLE_VALUE;
}

#if !defined(_WIN32)
[[maybe_unused]] inline void set_nonblocking(int fd) {
  if (fd == INVALID_NATIVE_HANDLE_VALUE) {
    return;
  }
  auto const flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    die("fcntl(F_GETFL) failed");
  }
  if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
    die("fcntl(F_SETFL) failed");
  }
}

[[maybe_unused]] inline void multiplex_using_poll(
    NativeHandle& in, buffer const& in_buf, NativeHandle& out, buffer& out_buf,
    NativeHandle& err, buffer& err_buf) {
  set_nonblocking(in);
  set_nonblocking(out);
  set_nonblocking(err);
  struct pollfd fds[3]{{.fd = in, .events = POLLOUT, .revents = 0},
                       {.fd = out, .events = POLLIN, .revents = 0},
                       {.fd = err, .events = POLLIN, .revents = 0}};

  auto in_data_span = in_buf.span();

  char buf[1024];
  while (fds[0].fd != INVALID_NATIVE_HANDLE_VALUE ||
         fds[1].fd != INVALID_NATIVE_HANDLE_VALUE ||
         fds[2].fd != INVALID_NATIVE_HANDLE_VALUE) {
    int poll_count = poll(fds, 3, -1);
    if (poll_count == -1) {
      die("poll() failed");
    }
    if (poll_count == 0) {
      break;
    }
    if (fds[0].fd != INVALID_NATIVE_HANDLE_VALUE &&
        (fds[0].revents & POLLOUT)) {
      auto write_count =
          write(fds[0].fd, in_data_span.data(), in_data_span.size());
      while (write_count > 0) {
        in_data_span = in_data_span.subspan(static_cast<size_t>(write_count));
        write_count =
            write(fds[0].fd, in_data_span.data(), in_data_span.size());
      }
      if (write_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die("write() error: " + std::to_string(errno));
        }
      }
      if (in_data_span.empty()) {
        close_native_handle(fds[0].fd);
      }
    }
    if (fds[1].fd != INVALID_NATIVE_HANDLE_VALUE && (fds[1].revents & POLLIN)) {
      auto read_count = read(fds[1].fd, buf, std::size(buf));
      while (read_count > 0) {
        out_buf.append(buf, static_cast<size_t>(read_count));
        read_count = read(fds[1].fd, buf, std::size(buf));
      }
      if (read_count == 0) {
        close_native_handle(fds[1].fd);
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die(get_last_error_message());
        }
      }
    } else if (fds[1].fd != INVALID_NATIVE_HANDLE_VALUE &&
               (fds[1].revents & (POLLHUP | POLLERR))) {
      close_native_handle(fds[1].fd);
    }
    if (fds[2].fd != INVALID_NATIVE_HANDLE_VALUE && (fds[2].revents & POLLIN)) {
      auto read_count = read(fds[2].fd, buf, std::size(buf));
      while (read_count > 0) {
        err_buf.append(buf, static_cast<size_t>(read_count));
        read_count = read(fds[2].fd, buf, std::size(buf));
      }
      if (read_count == 0) {
        close_native_handle(fds[2].fd);
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die(get_last_error_message());
        }
      }
    } else if (fds[2].fd != INVALID_NATIVE_HANDLE_VALUE &&
               (fds[2].revents & (POLLHUP | POLLERR))) {
      close_native_handle(fds[2].fd);
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

[[maybe_unused]] inline void multiplex_using_select(
    NativeHandle& in, buffer const& in_buf, NativeHandle& out, buffer& out_buf,
    NativeHandle& err, buffer& err_buf) {
  auto in_data_span = in_buf.span();
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
      die(get_last_error_message());
    }
    if (in != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(in, &write_fds)) {
      auto write_count = write(in, in_data_span.data(), in_data_span.size());
      while (write_count > 0) {
        in_data_span = in_data_span.subspan(static_cast<size_t>(write_count));
        write_count = write(in, in_data_span.data(), in_data_span.size());
      }
      if (write_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die("write() error: " + std::to_string(errno));
        }
      }
      if (in_data_span.empty()) {
        close_native_handle(in);
      }
    }
    if (out != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(out, &read_fds)) {
      auto read_count = read(out, buf, std::size(buf));
      while (read_count > 0) {
        out_buf.append(buf, static_cast<size_t>(read_count));
        read_count = read(out, buf, std::size(buf));
      }
      if (read_count == 0) {
        close_native_handle(out);
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die(get_last_error_message());
        }
      }
    }
    if (err != INVALID_NATIVE_HANDLE_VALUE && FD_ISSET(err, &read_fds)) {
      auto read_count = read(err, buf, std::size(buf));
      while (read_count > 0) {
        err_buf.append(buf, static_cast<size_t>(read_count));
        read_count = read(err, buf, std::size(buf));
      }
      if (read_count == 0) {
        close_native_handle(err);
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          die(get_last_error_message());
        }
      }
    }
  }
}
#endif
#if defined(__linux__)
[[maybe_unused]] inline void multiplex_using_epoll(
    NativeHandle& in, buffer const& in_buf, NativeHandle& out, buffer& out_buf,
    NativeHandle& err, buffer& err_buf) {
  set_nonblocking(in);
  set_nonblocking(out);
  set_nonblocking(err);

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd == -1) {
    die("epoll_create1() failed: " + get_last_error_message());
  }
  HandleGuard epoll_guard(epfd);

  struct epoll_event ev;
  struct epoll_event events[3];

  // Add valid file descriptors to the epoll interest list
  if (in != INVALID_NATIVE_HANDLE_VALUE) {
    ev.events = EPOLLOUT;
    ev.data.fd = in;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, in, &ev) == -1) {
      die("epoll_ctl(EPOLL_CTL_ADD, in) failed: " + get_last_error_message());
    }
  }
  if (out != INVALID_NATIVE_HANDLE_VALUE) {
    ev.events = EPOLLIN;
    ev.data.fd = out;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, out, &ev) == -1) {
      die("epoll_ctl(EPOLL_CTL_ADD, out) failed: " + get_last_error_message());
    }
  }
  if (err != INVALID_NATIVE_HANDLE_VALUE) {
    ev.events = EPOLLIN;
    ev.data.fd = err;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, err, &ev) == -1) {
      die("epoll_ctl(EPOLL_CTL_ADD, err) failed: " + get_last_error_message());
    }
  }

  auto in_data_span = in_buf.span();
  char buf[1024];

  while (in != INVALID_NATIVE_HANDLE_VALUE ||
         out != INVALID_NATIVE_HANDLE_VALUE ||
         err != INVALID_NATIVE_HANDLE_VALUE) {
    int nfds = epoll_wait(epfd, events, 3, -1);
    if (nfds == -1) {
      if (errno == EINTR) {
        continue;
      }
      die("epoll_wait() failed: " + get_last_error_message());
    }

    for (int i = 0; i < nfds; ++i) {
      int fd = events[i].data.fd;
      uint32_t revents = events[i].events;

      if (fd == in) {
        if (revents & EPOLLOUT) {
          auto write_count =
              write(fd, in_data_span.data(), in_data_span.size());
          while (write_count > 0) {
            in_data_span =
                in_data_span.subspan(static_cast<size_t>(write_count));
            write_count = write(fd, in_data_span.data(), in_data_span.size());
          }
          if (write_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die("write() error: " + std::to_string(errno));
            }
          }
          if (in_data_span.empty()) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, in, nullptr);
            close_native_handle(in);
          }
        }
        if (revents & (EPOLLHUP | EPOLLERR)) {
          epoll_ctl(epfd, EPOLL_CTL_DEL, in, nullptr);
          close_native_handle(in);
        }
      } else if (fd == out) {
        if (revents & EPOLLIN) {
          auto read_count = read(fd, buf, std::size(buf));
          while (read_count > 0) {
            out_buf.append(buf, static_cast<size_t>(read_count));
            read_count = read(fd, buf, std::size(buf));
          }
          if (read_count == 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, out, nullptr);
            close_native_handle(out);
          }
          if (read_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die(get_last_error_message());
            }
          }
        }
        if (revents & (EPOLLHUP | EPOLLERR)) {
          epoll_ctl(epfd, EPOLL_CTL_DEL, out, nullptr);
          close_native_handle(out);
        }
      } else if (fd == err) {
        if (revents & EPOLLIN) {
          auto read_count = read(fd, buf, std::size(buf));
          while (read_count > 0) {
            err_buf.append(buf, static_cast<size_t>(read_count));
            read_count = read(fd, buf, std::size(buf));
          }
          if (read_count == 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, err, nullptr);
            close_native_handle(err);
          }
          if (read_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die(get_last_error_message());
            }
          }
        }
        if (revents & (EPOLLHUP | EPOLLERR)) {
          epoll_ctl(epfd, EPOLL_CTL_DEL, err, nullptr);
          close_native_handle(err);
        }
      }
    }
  }

  in = INVALID_NATIVE_HANDLE_VALUE;
  out = INVALID_NATIVE_HANDLE_VALUE;
  err = INVALID_NATIVE_HANDLE_VALUE;
}
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)

[[maybe_unused]] inline void multiplex_using_kqueue(
    NativeHandle& in, buffer const& in_buf, NativeHandle& out, buffer& out_buf,
    NativeHandle& err, buffer& err_buf) {
  set_nonblocking(in);
  set_nonblocking(out);
  set_nonblocking(err);

  int kq = kqueue();
  if (kq == -1) {
    die("kqueue() failed: " + get_last_error_message());
  }
  HandleGuard kq_guard(kq);

  struct kevent changes[3];
  int nchanges = 0;

  if (in != INVALID_NATIVE_HANDLE_VALUE) {
    EV_SET(&changes[nchanges++], in, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0,
           nullptr);
  }
  if (out != INVALID_NATIVE_HANDLE_VALUE) {
    EV_SET(&changes[nchanges++], out, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
           nullptr);
  }
  if (err != INVALID_NATIVE_HANDLE_VALUE) {
    EV_SET(&changes[nchanges++], err, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
           nullptr);
  }

  if (nchanges > 0) {
    if (kevent(kq, changes, nchanges, nullptr, 0, nullptr) == -1) {
      die("kevent() register failed: " + get_last_error_message());
    }
  }

  auto in_data_span = in_buf.span();
  char buf[1024];

  struct kevent events[3];

  while (in != INVALID_NATIVE_HANDLE_VALUE ||
         out != INVALID_NATIVE_HANDLE_VALUE ||
         err != INVALID_NATIVE_HANDLE_VALUE) {
    int nev = kevent(kq, nullptr, 0, events, 3, nullptr);
    if (nev == -1) {
      if (errno == EINTR) {
        continue;
      }
      die("kevent() failed: " + get_last_error_message());
    }

    for (int i = 0; i < nev; ++i) {
      int fd = static_cast<int>(events[i].ident);
      int16_t filter = events[i].filter;
      uint16_t flags = events[i].flags;

      if (flags & EV_ERROR) {
        struct kevent ch;
        EV_SET(&ch, fd, filter, EV_DELETE, 0, 0, nullptr);
        kevent(kq, &ch, 1, nullptr, 0, nullptr);
        if (fd == in) {
          close_native_handle(in);
        } else if (fd == out) {
          close_native_handle(out);
        } else if (fd == err) {
          close_native_handle(err);
        }
        continue;
      }

      if (fd == in) {
        if (filter == EVFILT_WRITE) {
          auto write_count =
              write(fd, in_data_span.data(), in_data_span.size());
          while (write_count > 0) {
            in_data_span =
                in_data_span.subspan(static_cast<size_t>(write_count));
            write_count = write(fd, in_data_span.data(), in_data_span.size());
          }
          if (write_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die("write() error: " + std::to_string(errno));
            }
          }
          if (in_data_span.empty()) {
            struct kevent ch;
            EV_SET(&ch, in, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kq, &ch, 1, nullptr, 0, nullptr);
            close_native_handle(in);
          }
        }
        if (flags & EV_EOF) {
          struct kevent ch;
          EV_SET(&ch, in, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
          kevent(kq, &ch, 1, nullptr, 0, nullptr);
          close_native_handle(in);
        }
      } else if (fd == out) {
        if (filter == EVFILT_READ) {
          auto read_count = read(fd, buf, std::size(buf));
          while (read_count > 0) {
            out_buf.append(buf, static_cast<size_t>(read_count));
            read_count = read(fd, buf, std::size(buf));
          }
          if (read_count == 0) {
            struct kevent ch;
            EV_SET(&ch, out, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(kq, &ch, 1, nullptr, 0, nullptr);
            close_native_handle(out);
          } else if (read_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die(get_last_error_message());
            }
          }
        }
      } else if (fd == err) {
        if (filter == EVFILT_READ) {
          auto read_count = read(fd, buf, std::size(buf));
          while (read_count > 0) {
            err_buf.append(buf, static_cast<size_t>(read_count));
            read_count = read(fd, buf, std::size(buf));
          }
          if (read_count == 0) {
            struct kevent ch;
            EV_SET(&ch, err, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(kq, &ch, 1, nullptr, 0, nullptr);
            close_native_handle(err);
          } else if (read_count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              die(get_last_error_message());
            }
          }
        }
      }
    }
  }

  in = INVALID_NATIVE_HANDLE_VALUE;
  out = INVALID_NATIVE_HANDLE_VALUE;
  err = INVALID_NATIVE_HANDLE_VALUE;
}
#endif

[[maybe_unused]]
inline void read_write_with_threads(NativeHandle& in, buffer const& in_buf,
                                    NativeHandle& out, buffer& out_buf,
                                    NativeHandle& err, buffer& err_buf) {
  std::vector<std::thread> threads;
#if SUBPROCESS_HAS_EXCEPTIONS
  std::exception_ptr exception{nullptr};
  std::mutex exception_mutex;
#endif

  if (in != INVALID_NATIVE_HANDLE_VALUE) {
#if SUBPROCESS_HAS_EXCEPTIONS
    threads.emplace_back(
        [](NativeHandle& fd, buffer const& buf, std::exception_ptr& exc,
           std::mutex& mtx) {
          try {
            write_to_native_handle(fd, buf);
          } catch (...) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!exc) {
              exc = std::current_exception();
            }
          }
        },
        std::ref(in), std::ref(in_buf), std::ref(exception),
        std::ref(exception_mutex));
#else
    threads.emplace_back(write_to_native_handle, std::ref(in),
                         std::ref(in_buf));
#endif
  }
  if (out != INVALID_NATIVE_HANDLE_VALUE) {
#if SUBPROCESS_HAS_EXCEPTIONS
    threads.emplace_back(
        [](NativeHandle& fd, buffer& buf, std::exception_ptr& exc,
           std::mutex& mtx) {
          try {
            read_from_native_handle(fd, buf);
          } catch (...) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!exc) {
              exc = std::current_exception();
            }
          }
        },
        std::ref(out), std::ref(out_buf), std::ref(exception),
        std::ref(exception_mutex));
#else
    threads.emplace_back(read_from_native_handle, std::ref(out),
                         std::ref(out_buf));
#endif
  }
  if (err != INVALID_NATIVE_HANDLE_VALUE) {
#if SUBPROCESS_HAS_EXCEPTIONS
    threads.emplace_back(
        [](NativeHandle& fd, buffer& buf, std::exception_ptr& exc,
           std::mutex& mtx) {
          try {
            read_from_native_handle(fd, buf);
          } catch (...) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!exc) {
              exc = std::current_exception();
            }
          }
        },
        std::ref(err), std::ref(err_buf), std::ref(exception),
        std::ref(exception_mutex));
#else
    threads.emplace_back(read_from_native_handle, std::ref(err),
                         std::ref(err_buf));
#endif
  }

  for (auto& thread : threads) {
    thread.join();
  }

#if SUBPROCESS_HAS_EXCEPTIONS
  if (exception) {
    std::rethrow_exception(exception);
  }
#endif
}

inline void read_write_pipes(NativeHandle& in, buffer const& in_buf,
                             NativeHandle& out, buffer& out_buf,
                             NativeHandle& err, buffer& err_buf) {
#if defined(_WIN32)
  return read_write_with_threads(in, in_buf, out, out_buf, err, err_buf);
#else
#if defined(SUBPROCESS_MULTIPLEXING_USE_SELECT) && \
    SUBPROCESS_MULTIPLEXING_USE_SELECT
  return multiplex_using_select(in, in_buf, out, out_buf, err, err_buf);
#else
  return multiplex_using_poll(in, in_buf, out, out_buf, err, err_buf);
#endif
#endif
}

#if defined(_WIN32)
inline std::optional<std::string> get_file_extension(std::string const& f) {
  auto const dot_pos = f.rfind('.');
  if (dot_pos == std::string::npos) {
    return std::nullopt;
  }

  if (dot_pos == f.length() - 1) {
    return std::nullopt;
  }

  auto const separator_pos = f.find_last_of("/\\");
  if (separator_pos != std::string::npos) {
    if (separator_pos > dot_pos) {
      return std::nullopt;
    }
    if (dot_pos == separator_pos + 1) {
      return std::nullopt;
    }
  } else {
    if (dot_pos == 0) {
      return std::nullopt;
    }
  }
  return f.substr(dot_pos + 1);
}
#endif  // !_WIN32

inline bool is_executable(std::string const& f) {
#if defined(_WIN32)
  auto attr = GetFileAttributesW(utf8_to_utf16(f).c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat sb;
  return (stat(f.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) &&
          access(f.c_str(), X_OK) == 0);
#endif
}

inline std::optional<std::string> get_env(std::string const& key) {
#if defined(_WIN32)
  auto wkey = utf8_to_utf16(key);
  auto const size =
      GetEnvironmentVariableW(wkey.c_str(), nullptr, static_cast<DWORD>(0));
  if (size == 0 || GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    return std::nullopt;
  }
  std::vector<wchar_t> buf;
  buf.resize(static_cast<size_t>(size));
  GetEnvironmentVariableW(wkey.c_str(), buf.data(),
                          static_cast<DWORD>(buf.size()));
  return utf16_to_utf8(std::wstring{static_cast<const wchar_t*>(buf.data())});
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
inline std::map<std::wstring, std::wstring> get_all_env_vars() {
  std::map<std::wstring, std::wstring> envs;

  auto* env_block = GetEnvironmentStringsW();
  if (env_block == nullptr) {
    return envs;
  }

  const auto* current_env = env_block;
  while (*current_env != L'\0') {
    std::wstring_view env_string(current_env);
    auto pos = env_string.find(L'=');
    // Windows has hidden environment variables that start with '='.
    // The key is the name of a drive, like "=C:", and the value is the
    // current working directory on that drive.
    if (pos == 0) {
      pos = env_string.find(L'=', 1);
    }
    if (pos != std::wstring_view::npos) {
      auto key = std::wstring(env_string.substr(0, pos));
      auto value = std::wstring(env_string.substr(pos + 1));
      std::transform(key.begin(), key.end(), key.begin(),
                     [](wchar_t c) { return ::towupper(c); });
      envs[key] = value;
    }
    current_env +=
        env_string.length() + 1;  // Move to the next environment variable
  }

  FreeEnvironmentStringsW(env_block);
  return envs;
}
#else
inline std::map<std::string, std::string> get_all_env_vars() {
  std::map<std::string, std::string> envs;
  if (environ == nullptr) {
    return envs;
  }

  for (char** env = environ; *env != nullptr; ++env) {
    std::string_view env_string(*env);
    auto pos = env_string.find('=');
    if (pos != std::string::npos) {
      std::string_view key = env_string.substr(0, pos);
      std::string_view value = env_string.substr(pos + 1);
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
      die(get_last_error_message());
    }
#else
    if (-1 == pipe(fds)) {
      die("pipe() failed");
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
        utf8_to_utf16(p)
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
      die("open failed: " + utf16_to_utf8(path_) +
          ", error: " + std::to_string(GetLastError()));
    }
#else
    fd_ = (type == OpenType::ReadOnly)
              ? open(path_.c_str(), O_RDONLY | O_CLOEXEC)
              : open(path_.c_str(),
                     (type == OpenType::WriteAppend)
                         ? (O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC)
                         : (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC),
                     0644);
    if (fd_ == -1) {
      die("open failed: " + path_);
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

class Redirector {
  friend class subprocess;
  using value_type = std::variant<Pipe, File, Buffer>;

 public:
  explicit Redirector() : redirect_(nullptr) {}
  explicit Redirector(Pipe const& p)
      : redirect_(std::make_unique<value_type>(p)) {}
  explicit Redirector(File f)
      : redirect_(std::make_unique<value_type>(std::move(f))) {}
  explicit Redirector(buffer& buf)
      : redirect_(std::make_unique<value_type>(Buffer(buf))) {}
  Redirector(Redirector&&) noexcept = default;
  Redirector& operator=(Redirector&&) noexcept = default;
  Redirector(Redirector const&) = delete;
  Redirector& operator=(Redirector const&) = delete;
  virtual ~Redirector() {
    if (!redirect_) {
      return;
    }
    std::visit(
        []<typename T>([[maybe_unused]] T& value) {
          if constexpr (std::is_same_v<T, Pipe>) {
            if (value.read() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe read handle not closed!" << '\n';
            }
            if (value.write() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> pipe write handle not closed!" << '\n';
            }

          } else if constexpr (std::is_same_v<T, Buffer>) {
            if (value.pipe().read() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer pipe read handle not closed!" << '\n';
            }
            if (value.pipe().write() != INVALID_NATIVE_HANDLE_VALUE) {
              std::cerr << ">> buffer pipe write handle not closed!" << '\n';
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
            if (NativeHandle non_inheritable_handle =
                    fileno() == 0 ? value.write() : value.read();
                non_inheritable_handle != INVALID_NATIVE_HANDLE_VALUE) {
              if (!SetHandleInformation(non_inheritable_handle,
                                        HANDLE_FLAG_INHERIT, 0)) {
                die("SetHandleInformation failed: " +
                    std::to_string(GetLastError()));
              }
            }
            if (NativeHandle inheritable_handle =
                    fileno() == 0 ? value.read() : value.write();
                inheritable_handle != INVALID_NATIVE_HANDLE_VALUE) {
              if (!SetHandleInformation(inheritable_handle, HANDLE_FLAG_INHERIT,
                                        HANDLE_FLAG_INHERIT)) {
                die("SetHandleInformation failed: " +
                    std::to_string(GetLastError()));
              }
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
            if (NativeHandle non_inheritable_handle =
                    fileno() == 0 ? value.pipe().write() : value.pipe().read();
                non_inheritable_handle != INVALID_NATIVE_HANDLE_VALUE) {
              if (!SetHandleInformation(non_inheritable_handle,
                                        HANDLE_FLAG_INHERIT, 0)) {
                die("SetHandleInformation failed: " +
                    std::to_string(GetLastError()));
              }
            }
            if (NativeHandle inheritable_handle =
                    fileno() == 0 ? value.pipe().read() : value.pipe().write();
                inheritable_handle != INVALID_NATIVE_HANDLE_VALUE) {
              if (!SetHandleInformation(inheritable_handle, HANDLE_FLAG_INHERIT,
                                        HANDLE_FLAG_INHERIT)) {
                die("SetHandleInformation failed: " +
                    std::to_string(GetLastError()));
              }
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

class StdinRedirector : public Redirector {
 public:
  using Redirector::Redirector;
  StdinRedirector(StdinRedirector&&) noexcept = default;
  StdinRedirector& operator=(StdinRedirector&&) noexcept = default;
  StdinRedirector(StdinRedirector const&) = delete;
  StdinRedirector& operator=(StdinRedirector const&) = delete;
  int fileno() const override { return 0; }
};
class StdoutRedirector : public Redirector {
 public:
  using Redirector::Redirector;
  StdoutRedirector(StdoutRedirector&&) noexcept = default;
  StdoutRedirector& operator=(StdoutRedirector&&) noexcept = default;
  StdoutRedirector(StdoutRedirector const&) = delete;
  StdoutRedirector& operator=(StdoutRedirector const&) = delete;
  int fileno() const override { return 1; }
};
class StderrRedirector : public Redirector {
 public:
  using Redirector::Redirector;
  StderrRedirector(StderrRedirector&&) noexcept = default;
  StderrRedirector& operator=(StderrRedirector&&) noexcept = default;
  StderrRedirector(StderrRedirector const&) = delete;
  StderrRedirector& operator=(StderrRedirector const&) = delete;
  int fileno() const override { return 2; }
};

struct stdin_redirector {
  StdinRedirector operator<(Pipe p) const {
    return StdinRedirector{std::move(p)};
  }
  StdinRedirector operator<(std::string const& file) const {
    return StdinRedirector{File{file}};
  }
#if defined(_WIN32)
  StdinRedirector operator<(std::wstring const& file) const {
    return StdinRedirector{File{file}};
  }
#endif
  StdinRedirector operator<(buffer& buf) const { return StdinRedirector{buf}; }
};

struct stdout_redirector {
  StdoutRedirector operator>(Pipe const& p) const {
    return StdoutRedirector{p};
  }
  StdoutRedirector operator>(std::string const& file) const {
    return StdoutRedirector{File{file}};
  }
#if defined(_WIN32)
  StdoutRedirector operator>(std::wstring const& file) const {
    return StdoutRedirector{File{file}};
  }
#endif
  StdoutRedirector operator>(buffer& buf) const {
    buf.clear();
    return StdoutRedirector{buf};
  }
  StdoutRedirector operator>>(buffer& buf) const {
    return StdoutRedirector{buf};
  }

  StdoutRedirector operator>>(std::string const& file) const {
    return StdoutRedirector{File{file, true}};
  }
#if defined(_WIN32)
  StdoutRedirector operator>>(std::wstring const& file) const {
    return StdoutRedirector{File{file, true}};
  }
#endif
};

struct stderr_redirector {
  StderrRedirector operator>(Pipe p) const {
    return StderrRedirector{std::move(p)};
  }
  StderrRedirector operator>(std::string const& file) const {
    return StderrRedirector{File{file}};
  }
#if defined(_WIN32)
  StderrRedirector operator>(std::wstring const& file) const {
    return StderrRedirector{File{file}};
  }
#endif
  StderrRedirector operator>(buffer& buf) const {
    buf.clear();
    return StderrRedirector{buf};
  }
  StderrRedirector operator>>(buffer& buf) const {
    return StderrRedirector{buf};
  }
  StderrRedirector operator>>(std::string const& file) const {
    return StderrRedirector{File{file, true}};
  }
#if defined(_WIN32)
  StderrRedirector operator>>(std::wstring const& file) const {
    return StderrRedirector{File{file, true}};
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

// Set environment variables, replacing existing ones.
struct Env {
#if defined(_WIN32)
  std::map<std::wstring, std::wstring> env;
#else
  std::map<std::string, std::string> env;
#endif
};

// Append environment variables to the existing environment.
struct EnvAppend {
#if defined(_WIN32)
  std::map<std::wstring, std::wstring> env;
#else
  std::map<std::string, std::string> env;
#endif
};

// Append or prepend a value to a specific environment variable, e.g., PATH.
struct EnvItemAppend {
  EnvItemAppend& operator+=(std::string val) {
#if defined(_WIN32)
    std::get<1>(kv) = utf8_to_utf16(val);
#else
    std::get<1>(kv) = val;
#endif
    std::get<2>(kv) = true;
    return *this;
  }
  EnvItemAppend& operator<<=(std::string val) {
#if defined(_WIN32)
    std::get<1>(kv) = utf8_to_utf16(val);
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
    return Cwd{utf8_to_utf16(p)};
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
      wenv[utf8_to_utf16(entry.first)] = utf8_to_utf16(entry.second);
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
      wenv[utf8_to_utf16(entry.first)] = utf8_to_utf16(entry.second);
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
    return EnvItemAppend{{utf8_to_utf16(key), L"", true}};
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
                            std::is_same_v<StdinRedirector, std::decay_t<T>> ||
                            std::is_same_v<StdoutRedirector, std::decay_t<T>> ||
                            std::is_same_v<StderrRedirector, std::decay_t<T>> ||
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
struct named_arg_type_list;

template <>
struct named_arg_type_list<> {
  using type = std::tuple<>;
};
template <typename Head, typename... Tails>
struct named_arg_type_list<Head, Tails...> {
  using type =
      std::conditional_t<is_named_argument<Head>, std::tuple<Head, Tails...>,
                         typename named_arg_type_list<Tails...>::type>;
};
template <typename... T>
using named_arg_type_list_t = typename named_arg_type_list<T...>::type;

class subprocess {
  friend class subprocess_array;

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
             if constexpr (std::is_same_v<ArgType, StdinRedirector>) {
               stdin_ = std::forward<Arg>(arg);
             }
             if constexpr (std::is_same_v<ArgType, StdoutRedirector>) {
               stdout_ = std::forward<Arg>(arg);
             }
             if constexpr (std::is_same_v<ArgType, StderrRedirector>) {
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
             static_assert(
                 std::is_same_v<Env, std::decay_t<T>> ||
                     std::is_same_v<StdinRedirector, std::decay_t<T>> ||
                     std::is_same_v<StdoutRedirector, std::decay_t<T>> ||
                     std::is_same_v<StderrRedirector, std::decay_t<T>> ||
                     std::is_same_v<Cwd, std::decay_t<T>> ||
                     std::is_same_v<EnvAppend, std::decay_t<T>> ||
                     std::is_same_v<EnvItemAppend, std::decay_t<T>>,
                 "Unsupported argument type passed to run().");
           }(std::forward<T>(args))));
    if ((!env_item_appends.empty() || !env_appends.empty()) &&
        environments.empty()) {
      environments = get_all_env_vars();
    }
    environments.insert(env_appends.begin(), env_appends.end());
#ifdef _WIN32
    wchar_t path_env_sep = L';';
#else
    char path_env_sep = ':';
#endif
    for (auto const& [name, value, is_append] : env_item_appends) {
      auto it = environments.find(name);
#ifdef _WIN32
      if (it == environments.end()) {
        auto upper_key = name;
        std::transform(upper_key.begin(), upper_key.end(), upper_key.begin(),
                       [](wchar_t c) { return ::towupper(c); });
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
    ZeroMemory(&startup_info_, sizeof(startup_info_));
    startup_info_.cb = sizeof(startup_info_);
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
                             [](auto const& s) { return utf8_to_utf16(s); });
              return ret;
            }(cmd),
            std::forward<T>(args)...) {}
#endif

  subprocess(subprocess&&) noexcept = default;
  subprocess& operator=(subprocess&&) noexcept = default;
  subprocess(const subprocess&) = delete;
  subprocess& operator=(const subprocess&) = delete;

  void async_run() {
    prepare_all_stdio_redirections();
#if defined(_WIN32)
    auto in = stdin_.get_child_process_stdio_handle();
    startup_info_.hStdInput =
        in.has_value() ? in.value() : GetStdHandle(STD_INPUT_HANDLE);
    auto out = stdout_.get_child_process_stdio_handle();
    startup_info_.hStdOutput =
        out.has_value() ? out.value() : GetStdHandle(STD_OUTPUT_HANDLE);
    auto err = stderr_.get_child_process_stdio_handle();
    startup_info_.hStdError =
        err.has_value() ? err.value() : GetStdHandle(STD_ERROR_HANDLE);

    startup_info_.dwFlags |= STARTF_USESTDHANDLES;

    auto command = argv_to_command_line_string(cmd_);

    auto env_block = create_environment_string_data(env_);

    auto success = CreateProcessW(
        nullptr, command.data(), NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT,
        env_block.empty() ? nullptr : env_block.data(),
        cwd_.empty() ? nullptr : cwd_.data(), &startup_info_,
        &process_information_);

    if (success) {
      manage_pipe_io();
    } else {
      std::wcerr << utf8_to_utf16(get_last_error_message()) << L'\n';
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
                         get_last_error_message());
    }
    add_posix_spawn_file_actions(action);
    posix_spawn_file_actions_destroy(&action);
    manage_pipe_io();
#else   // SUBPROCESS_USE_POSIX_SPAWN
    auto pid = fork();
    if (pid < 0) {
      die("fork() failed");
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
    async_run();
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
    buffer tmp_buf;
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
    buffer tmp_buf;
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

    std::vector<char*> cmd{};
    std::transform(cmd_.begin(), cmd_.end(), std::back_inserter(cmd),
                   [](std::string& s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!cwd_.empty() && (-1 == chdir(cwd_.data()))) {
      die(get_last_error_message());
    }

    std::string executable_path = cmd_[0];
    if (executable_path.find('/') == std::string::npos) {
      auto resolved_path = find_command_in_path(executable_path);
      if (resolved_path.has_value()) {
        executable_path = resolved_path.value();
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
      execve(executable_path.c_str(), cmd.data(), envs.data());
      std::cerr << "execve(" << executable_path
                << ") failed: " << get_last_error_message() << '\n';
    } else {
      execv(executable_path.c_str(), cmd.data());
      std::cerr << "execv(" << executable_path
                << ") failed: " << get_last_error_message() << '\n';
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
        (-1 == posix_spawn_file_actions_addchdir_np(&action, cwd_.data()))
#else  // other POSIX systems, try the standard version
        (-1 == posix_spawn_file_actions_addchdir(&action, cwd_.data()))
#endif
    ) {
      die(get_last_error_message());
    }

    std::string executable_path = cmd_[0];
    if (executable_path.find('/') == std::string::npos) {
      auto resolved_path = find_command_in_path(executable_path);
      if (resolved_path.has_value()) {
        executable_path = resolved_path.value();
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
      auto ret = posix_spawn(&pid_, executable_path.c_str(), &action, nullptr,
                             cmd.data(), envs.data());
      if (ret != 0) {
        pid_ = INVALID_NATIVE_HANDLE_VALUE;
      }
    } else {
      auto ret = posix_spawn(&pid_, executable_path.c_str(), &action, nullptr,
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
  StdinRedirector stdin_;
  StdoutRedirector stdout_;
  StderrRedirector stderr_;
#if defined(_WIN32)
  PROCESS_INFORMATION process_information_;
  STARTUPINFOW startup_info_;
#else
  NativeHandle pid_{INVALID_NATIVE_HANDLE_VALUE};
#endif
};

class subprocess_array {
 public:
  subprocess_array(subprocess&& sub) { subs_.push_back(std::move(sub)); }
  subprocess_array(subprocess_array&&) = default;
  subprocess_array& operator=(subprocess_array&&) = default;
  subprocess_array(subprocess_array const&) = delete;
  subprocess_array& operator=(subprocess_array const&) = delete;
  subprocess_array& append(subprocess&& sub) {
    subs_.push_back(std::move(sub));
    return *this;
  }

  int run() {
    std::vector<Pipe> pipes;
    if (subs_.size() > 1) {
      for (auto it = subs_.begin(); it != subs_.end() - 1; ++it) {
        pipes.push_back(Pipe::create());
#if defined(_WIN32)
        SetHandleInformation(pipes.back().read(), HANDLE_FLAG_INHERIT,
                             HANDLE_FLAG_INHERIT);
        SetHandleInformation(pipes.back().write(), HANDLE_FLAG_INHERIT,
                             HANDLE_FLAG_INHERIT);
#endif
        it->stdout_ = (named_args::std_out > pipes.back());
        (it + 1)->stdin_ = (named_args::std_in < pipes.back());
      }
    }
    for (auto& sub : subs_) {
      sub.async_run();
    }
    for (auto& sub : subs_) {
      exit_codes_.push_back(sub.wait_for_exit());
    }
    return exit_codes_.back();
  }

  int exit_code() const { return exit_codes_.back(); }
  std::vector<int> exit_codes() const { return exit_codes_; }

 private:
  std::vector<subprocess> subs_;
  std::vector<int> exit_codes_;
};

inline subprocess_array operator|(subprocess&& lhs, subprocess&& rhs) {
  subprocess_array subs(std::move(lhs));
  subs.append(std::move(rhs));
  return subs;
}

inline subprocess_array operator|(subprocess_array lhs, subprocess&& rhs) {
  lhs.append(std::move(rhs));
  return lhs;
}
}  // namespace detail

using detail::buffer;

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
  using NamedArgTypeList =
      typename detail::named_arg_type_list_t<std::decay_t<Args>...>;
  return [&args_tuple]<size_t... I, size_t... N>(std::index_sequence<I...>,
                                                 std::index_sequence<N...>) {
    static_assert(
        ((detail::is_string_type<std::tuple_element_t<I, ArgsTuple>>) && ...));
    static_assert(((detail::is_named_argument<
                       std::tuple_element_t<N, NamedArgTypeList>>) &&
                   ...));
    return run({std::move(std::get<I>(args_tuple))...},
               std::move(std::get<std::tuple_size_v<std::tuple<Args...>> -
                                  std::tuple_size_v<NamedArgTypeList> + N>(
                   args_tuple))...);
  }(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>> -
                             std::tuple_size_v<NamedArgTypeList>>{},
         std::make_index_sequence<std::tuple_size_v<NamedArgTypeList>>{});
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
  requires(detail::is_named_argument<T> && ...)
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
  requires(detail::is_named_argument<T> && ...)
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

  // If HOME is not set, fall back to getpwuid.
  // This is a more reliable method for finding the home directory.
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
  return detail::get_all_env_vars();
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
  return detail::utf16_to_utf8(buffer.data());
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
  return SetCurrentDirectoryW(detail::utf8_to_utf16(dir).c_str());
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
