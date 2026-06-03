/*
 *               __                                           __
 *   _______  __/ /_  ____  _________  ________  __________  / /_  ____  ____
 *  / ___/ / / / __ \/ __ \/ ___/ __ \/ ___/ _ \/ ___/ ___/ / __ \/ __ \/ __ \
 * (__  ) /_/ / /_/ / /_/ / /  / /_/ / /__/  __(__  |__  ) / / / / /_/ / /_/ /
 * /____/\__,_/_.___/ .___/_/   \____/\___/\___/____/____(_)_/ /_/ .___/ .___/
 *                 /_/                                          /_/   /_/
 *
 *  subprocess.hpp — A lightweight, header-only C++ subprocess library
 *                   providing an easy-to-use interface for spawning and
 *                   managing child processes.
 *
 *  Author  : shediao.xsd <xushediao1987@163.com>
 *  Repo    : https://github.com/shediao/subprocess.hpp
 *  License : MIT
 *
 *  Copyright (c) 2024-2026 shediao.xsd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  SPDX-License-Identifier: MIT
 */

#ifndef __SUBPROCESS_SUBPROCESS_HPP__
#define __SUBPROCESS_SUBPROCESS_HPP__

/*******************************************************************************
 * namespace subprocess {
 *   int run(...);
 *   int $(...);
 *   namespace named_arguments {
 *     cwd;              $cwd;
 *     devnull;          $devnull;
 *     env;              $env;
 *     stderr;           $stderr;
 *     stdin;            $stdin;
 *     stdout;           $stdout;
 *     timeout;          $timeout;
 *     timeout_infinite; $timeout_infinite;
 *   }
 * }
 * using subprocess::$;
 * using subprocess::named_arguments::$cwd;
 * using subprocess::named_arguments::$devnull;
 * using subprocess::named_arguments::$env;
 * using subprocess::named_arguments::$stderr;
 * using subprocess::named_arguments::$stdin;
 * using subprocess::named_arguments::$stdout;
 * using subprocess::named_arguments::$timeout;
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

#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#if (defined(_MSVC_LANG) && _MSVC_LANG < 202002L) || \
    (!defined(_MSVC_LANG) && __cplusplus < 202002L)
#error "This code requires C++20 or later."
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif  // !_WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstring>
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

#ifndef USE_DOLLAR_NAMED_VARIABLES
#define USE_DOLLAR_NAMED_VARIABLES 1
#endif

namespace subprocess {

namespace detail {
class builder;
class Redirector;
class StdinRedirector;
class StdoutRedirector;
class StderrRedirector;

template <typename... T>
struct visitor : T... {
  using T::operator()...;
};

#if defined(_WIN32)
using ssize_t = std::ptrdiff_t;
#endif

template <typename T>
concept string_like_type =
#if defined(_GLIBCXX_FILESYSTEM) || defined(_LIBCPP_FILESYSTEM) || \
    defined(_FILESYSTEM_)
    std::same_as<std::filesystem::path, std::decay_t<T>> ||
#endif
#if defined(_WIN32)
    std::same_as<wchar_t*, std::decay_t<T>> ||
    std::same_as<const wchar_t*, std::decay_t<T>> ||
    std::same_as<std::wstring, std::decay_t<T>> ||
    std::same_as<std::wstring_view, std::decay_t<T>> ||
#endif
    std::same_as<char*, std::decay_t<T>> ||
    std::same_as<const char*, std::decay_t<T>> ||
    std::same_as<std::string, std::decay_t<T>> ||
    std::same_as<std::string_view, std::decay_t<T>>;

template <typename T>
struct get_char_type;

template <typename CharT>
struct get_char_type<std::basic_string<CharT>> {
  using type = CharT;
};
template <typename CharT>
struct get_char_type<std::basic_string_view<CharT>> {
  using type = CharT;
};
template <typename CharT>
struct get_char_type<CharT*> {
  using type = std::remove_cv_t<CharT>;
};

#if defined(_GLIBCXX_FILESYSTEM) || defined(_LIBCPP_FILESYSTEM) || \
    defined(_FILESYSTEM_)
template <>
struct get_char_type<std::filesystem::path> {
  using type = typename std::filesystem::path::value_type;
};
#endif

template <typename T>
using get_char_type_t = get_char_type<std::decay_t<T>>::type;

template <typename T>
using to_string_view_t =
    std::basic_string_view<get_char_type_t<std::decay_t<T>>>;

template <typename T>
using to_string_t = std::basic_string<get_char_type_t<std::decay_t<T>>>;

template <typename CharT>
std::basic_string<CharT> to_lower_ascii(std::basic_string_view<CharT> str) {
  std::basic_string<CharT> ret{str};
  for (auto& c : ret) {
    if (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z')) {
      c += static_cast<CharT>('a' - 'A');
    }
  }
  return ret;
}

template <typename CharT>
std::basic_string<CharT> to_upper_ascii(std::basic_string_view<CharT> str) {
  std::basic_string<CharT> ret{str};
  for (auto& c : ret) {
    if (c >= static_cast<CharT>('a') && c <= static_cast<CharT>('z')) {
      c -= static_cast<CharT>('a' - 'A');
    }
  }
  return ret;
}

template <string_like_type String>
auto to_lower_ascii(String&& s) {
  using char_type = get_char_type_t<String>;
  return to_lower_ascii<char_type>(
      std::basic_string_view<char_type>(std::forward<String>(s)));
}
template <string_like_type String>
auto to_upper_ascii(String&& s) {
  using char_type = get_char_type_t<String>;
  return to_upper_ascii<char_type>(
      std::basic_string_view<char_type>(std::forward<String>(s)));
}

#if defined(_WIN32)
inline std::wstring win_MultiByteToWideChar(std::string_view str,
                                            UINT FromCodePage = CP_UTF8) {
  if (str.empty()) {
    return {};
  }
  DWORD size_needed = ::MultiByteToWideChar(
      FromCodePage, 0, str.data(), static_cast<DWORD>(str.size()), NULL, 0);
  if (size_needed <= 0) {
    return {};
  }
  std::wstring wstr(static_cast<size_t>(size_needed), L'\0');
  DWORD size = ::MultiByteToWideChar(FromCodePage, 0, str.data(),
                                     static_cast<DWORD>(str.size()),
                                     wstr.data(), size_needed);
  wstr.resize(size);
  return wstr;
}
inline std::string win_WideCharToMultiByte(std::wstring_view wstr,
                                           UINT ToCodePage = CP_UTF8) {
  if (wstr.empty()) {
    return {};
  }
  DWORD size_needed = ::WideCharToMultiByte(ToCodePage, 0, wstr.data(),
                                            static_cast<DWORD>(wstr.size()),
                                            NULL, 0, NULL, NULL);
  if (size_needed <= 0) {
    return {};
  }
  std::string str(static_cast<size_t>(size_needed), '\0');
  DWORD size = ::WideCharToMultiByte(ToCodePage, 0, wstr.data(),
                                     static_cast<DWORD>(wstr.size()),
                                     str.data(), size_needed, NULL, NULL);
  str.resize(size);
  return str;
}

inline std::wstring win_GetFullPathName(std::wstring const& path) {
  DWORD size = ::GetFullPathNameW(path.c_str(), 0, 0, 0);

  std::wstring fpath(size, L'\0');
  size = ::GetFullPathNameW(path.c_str(), static_cast<DWORD>(fpath.size()),
                            fpath.data(), 0);
  fpath.resize(size);
  return fpath;
}

inline std::wstring win_SearchPath(std::wstring const& name,
                                   std::wstring const& ext) {
  DWORD size = ::SearchPathW(0, name.c_str(),
                             ext.empty() ? nullptr : ext.c_str(), 0, 0, 0);

  std::wstring fpath(size, L'\0');
  size = ::SearchPathW(0, name.c_str(), ext.empty() ? nullptr : ext.c_str(),
                       static_cast<DWORD>(fpath.size()), fpath.data(), 0);
  fpath.resize(size);
  return fpath;
}

// Helper function to convert a UTF-8 std::string to a UTF-16 std::wstring
inline std::wstring utf8_to_utf16(std::string_view str_view) {
  return win_MultiByteToWideChar(str_view, CP_UTF8);
}

// Helper function to convert a UTF-16 std::wstring to a UTF-8 std::string
inline std::string utf16_to_utf8(std::wstring_view wstr_view) {
  return win_WideCharToMultiByte(wstr_view, CP_UTF8);
}
inline void print_error(std::wstring_view msg) {
  if (msg.empty()) {
    return;
  }

  auto stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  if (stderr_handle == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD mode;
  if (GetConsoleMode(stderr_handle, &mode)) {
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
      (void)WriteConsoleW(stderr_handle, msg.data(),
                          static_cast<DWORD>(msg.size()), NULL, NULL);
      return;
    }
  }
  // Not a VT-enabled console (or not a console at all) — fall back to
  // WriteFile with UTF-8 encoded data.
  auto utf8_msg = utf16_to_utf8(msg);
  (void)WriteFile(stderr_handle, utf8_msg.data(),
                  static_cast<DWORD>(utf8_msg.size()), NULL, NULL);
}
#else
inline void print_error(std::string_view const msg) {
  if (msg.empty()) {
    return;
  }
  ssize_t ret;
  do {
    ret = write(STDERR_FILENO, msg.data(), msg.size());
  } while (ret == -1 && errno == EINTR);
}
#endif  // _WIN32

#if defined(_WIN32)
using NativeHandle = HANDLE;
static inline const NativeHandle INVALID_NATIVE_HANDLE_VALUE =
    INVALID_HANDLE_VALUE;
using NativeString = std::wstring;
using NativeStringView = std::wstring_view;
#define TO_NATIVE_STRING(str) utf8_to_utf16(str)
#else  // _WIN32
using NativeHandle = int;
constexpr NativeHandle INVALID_NATIVE_HANDLE_VALUE = -1;
using NativeString = std::string;
using NativeStringView = std::string_view;
#define TO_NATIVE_STRING(str) str
#endif  // !_WIN32

inline bool invalid_handle(NativeHandle handle) {
  return INVALID_NATIVE_HANDLE_VALUE == handle;
}

inline void close_native_handle(NativeHandle& handle) {
  if (!invalid_handle(handle)) {
#if defined(_WIN32)
    CloseHandle(handle);
#else
    close(handle);
#endif
    handle = INVALID_NATIVE_HANDLE_VALUE;
  }
}

inline NativeHandle dup_native_handle(NativeHandle handle) {
  if (invalid_handle(handle)) {
    return INVALID_NATIVE_HANDLE_VALUE;
  }
#if defined(_WIN32)
  NativeHandle duped = INVALID_NATIVE_HANDLE_VALUE;
  if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &duped,
                       0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return INVALID_NATIVE_HANDLE_VALUE;
  }
  return duped;
#else
  return fcntl(handle, F_DUPFD_CLOEXEC, 0);
#endif
}

#if !defined(_WIN32)
[[maybe_unused]] inline bool set_nonblocking(int fd) {
  if (invalid_handle(fd)) {
    return false;
  }
  auto const flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }
  return -1 != fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

template <typename F>
class scope_exit {
 public:
  explicit scope_exit(F&& f) noexcept(std::is_nothrow_move_constructible_v<F>)
      : fn_(std::forward<F>(f)) {}

  scope_exit(scope_exit&& other) noexcept
      : fn_(std::move(other.fn_)), active_(other.active_) {
    other.active_ = false;
  }

  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  ~scope_exit() noexcept(std::is_nothrow_invocable_v<F>) {
    if (active_) {
      fn_();
    }
  }
  void release() noexcept { active_ = false; }

 private:
  F fn_;
  bool active_ = true;
};

template <typename F>
auto make_scope_exit(F&& f) {
  return scope_exit<std::decay_t<F>>{std::forward<F>(f)};
}

template <typename T>
struct fd_traits;

template <>
struct fd_traits<NativeHandle> {
  static NativeHandle invalid_value() {
#if defined(_WIN32)
    return INVALID_NATIVE_HANDLE_VALUE;
#else
    return -1;
#endif
  }
};

template <typename T, typename Derived, void (*deleter)(T&) = nullptr,
          typename Trait = fd_traits<T>>
class unique_fd_base {
 public:
  unique_fd_base() : handle_(Trait::invalid_value()) {}
  explicit unique_fd_base(NativeHandle handle) : handle_(handle) {}
  ~unique_fd_base() {
    if (deleter) {
      deleter(handle_);
    }
    handle_ = Trait::invalid_value();
  }
  unique_fd_base(unique_fd_base&& other) noexcept : handle_(other.handle_) {
    other.handle_ = Trait::invalid_value();
  }
  unique_fd_base& operator=(unique_fd_base&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (deleter) {
      deleter(handle_);
    }
    handle_ = other.handle_;
    other.handle_ = Trait::invalid_value();
    return *this;
  }
  unique_fd_base(const unique_fd_base&) = delete;
  unique_fd_base& operator=(const unique_fd_base&) = delete;

  NativeHandle get() const { return handle_; }
  explicit operator bool() const { return !invalid_handle(handle_); }
  NativeHandle release() {
    auto handle = handle_;
    handle_ = Trait::invalid_value();
    return handle;
  }
  void reset(NativeHandle handle) {
    if (deleter) {
      deleter(handle_);
    }
    handle_ = handle;
  }

  void swap(Derived& other) noexcept { std::swap(handle_, other.handle_); }

  friend bool operator==(Derived const& lhs, Derived const& rhs) {
    return lhs.handle_ == rhs.handle_;
  }
  friend bool operator!=(Derived const& lhs, Derived const& rhs) {
    return lhs.handle_ != rhs.handle_;
  }
  friend bool operator==(Derived const& lhs, NativeHandle rhs) {
    return lhs.handle_ == rhs;
  }
  friend bool operator!=(Derived const& lhs, NativeHandle rhs) {
    return lhs.handle_ != rhs;
  }
  friend bool operator==(NativeHandle lhs, Derived const& rhs) {
    return lhs == rhs.handle_;
  }
  friend bool operator!=(NativeHandle lhs, Derived const& rhs) {
    return lhs != rhs.handle_;
  }

  friend void swap(Derived& lhs, Derived& rhs) noexcept { lhs.swap(rhs); }

 private:
  NativeHandle handle_;
};

class unique_fd
    : public unique_fd_base<NativeHandle, unique_fd, close_native_handle> {
 public:
  using unique_fd_base::unique_fd_base;

  void close() { reset(fd_traits<NativeHandle>::invalid_value()); }

  unique_fd dup() const {
    auto duped = dup_native_handle(get());
    if (invalid_handle(duped)) {
      return unique_fd{};
    }
    return unique_fd{duped};
  }

  bool isatty() const {
#if defined(_WIN32)
    DWORD mode;
    return GetConsoleMode(get(), &mode);
#else
    return ::isatty(get());
#endif
  }

#if !defined(_WIN32)
  void set_nonblocking() const { detail::set_nonblocking(get()); }
#endif
};

#if !defined(_WIN32)
inline void noop_pid_deleter(pid_t&) {}

class unique_pid
    : public unique_fd_base<NativeHandle, unique_pid, noop_pid_deleter> {
 public:
  using unique_fd_base::unique_fd_base;
};
#endif

class buffer {
  using callback = std::function<void(const unsigned char*, size_t)>;

 public:
  buffer() = default;
  ~buffer() = default;
  buffer(const buffer&) = delete;
  buffer& operator=(const buffer&) = delete;
  buffer(buffer&& other) noexcept
      : buf_(std::move(other.buf_)), callback_(std::move(other.callback_)) {}
  buffer& operator=(buffer&& other) noexcept {
    buf_ = std::move(other.buf_);
    callback_ = std::move(other.callback_);
    return *this;
  }
  explicit buffer(callback&& cb) : callback_(std::move(cb)) {}
  explicit buffer(std::string_view str) : buf_(str.begin(), str.end()) {}
  buffer(std::string_view const str, callback&& cb)
      : buf_(str.begin(), str.end()), callback_(std::move(cb)) {}
  buffer(const std::string::iterator first, const std::string::iterator last)
      : buf_(first, last) {}
  buffer(const std::string::iterator first, const std::string::iterator last,
         callback&& cb)
      : buf_(first, last), callback_(std::move(cb)) {}

  [[nodiscard]] auto* data() const { return buf_.data(); }
  [[nodiscard]] auto size() const { return buf_.size(); }
  [[nodiscard]] auto span() const {
    return std::span(buf_.data(), buf_.size());
  }
  [[nodiscard]] auto to_string() const {
    return std::string(buf_.data(), buf_.data() + buf_.size());
  }
  [[nodiscard]] auto empty() const { return buf_.empty(); }
  auto clear() { return buf_.clear(); }

  [[nodiscard]] auto begin() const { return buf_.begin(); }
  [[nodiscard]] auto end() const { return buf_.end(); }

  void append(const char* data, size_t size) {
    buf_.insert(buf_.end(), data, data + size);
    if (callback_) {
      callback_(reinterpret_cast<const unsigned char*>(data), size);
    }
  }
  // operator== for test
  bool operator==(const buffer& other) const { return buf_ == other.buf_; }
  template <typename T>
    requires(!std::same_as<T, char> && !std::same_as<T, wchar_t> &&
             !std::same_as<T, char8_t> && !std::same_as<T, char16_t> &&
             !std::same_as<T, char32_t>)
  bool operator==(std::span<T> other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<const unsigned char*>(other.data()),
        other.size() * sizeof(T)};
    return std::ranges::equal(buf_, other_span);
  }
  template <typename CharT>
  bool operator==(std::basic_string_view<CharT> other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<const unsigned char*>(other.data()),
        other.size() * sizeof(CharT)};
    return std::ranges::equal(buf_, other_span);
  }
  template <typename CharT>
  bool operator==(const std::basic_string<CharT>& other) const {
    std::span<const unsigned char> other_span{
        reinterpret_cast<const unsigned char*>(other.data()),
        other.size() * sizeof(CharT)};
    return std::ranges::equal(buf_, other_span);
  }
  bool operator==(const char* other) const {
    return this->operator==(std::string_view(other));
  }
  bool operator==(const wchar_t* other) const {
    return this->operator==(std::wstring_view(other));
  }
  bool operator==(const char8_t* other) const {
    return this->operator==(std::u8string_view(other));
  }
  bool operator==(const char16_t* other) const {
    return this->operator==(std::u16string_view(other));
  }
  bool operator==(const char32_t* other) const {
    return this->operator==(std::u32string_view(other));
  }

 private:
  std::vector<unsigned char> buf_;
  callback callback_{nullptr};
};

template <typename C, typename CharT>
concept has_emplace_back_range = requires(C c, std::basic_string<CharT> s) {
  c.emplace_back(s.begin(), s.end());
};

template <typename C, typename CharT, typename F>
  requires requires(F f, CharT c) {
    { f(c) } -> std::convertible_to<bool>;
  } && (has_emplace_back_range<C, CharT> ||
        requires(C c) {
          c.insert(c.end(), std::declval<std::basic_string<CharT>>());
        })
void split_to_if(
    C& c, const std::basic_string<CharT>& str, F f,
    std::size_t split_count = (std::numeric_limits<std::size_t>::max)(),
    bool compress_tokens = false) {
  auto begin = str.begin();
  auto delimiter = begin;
  std::size_t count = 0;

  if constexpr (requires { c.reserve(std::declval<std::size_t>()); }) {
    if (split_count < (std::numeric_limits<std::size_t>::max)()) {
      c.reserve(c.size() + split_count + 1);
    }
  }

  while ((count++ < split_count) &&
         (delimiter = std::find_if(begin, str.end(), f)) != str.end()) {
    if constexpr (has_emplace_back_range<C, CharT>) {
      c.emplace_back(begin, delimiter);
    } else {
      c.insert(c.end(), std::basic_string<CharT>{begin, delimiter});
    }
    if (compress_tokens) {
      begin = std::find_if_not(delimiter, str.end(), f);
    } else {
      begin = std::next(delimiter);
    }
  }

  if constexpr (has_emplace_back_range<C, CharT>) {
    c.emplace_back(begin, str.end());
  } else {
    c.insert(c.end(), std::basic_string<CharT>{begin, str.end()});
  }
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
  if (!cmd.empty()) {
    cmd.push_back(L' ');
  }
  if (!needs_quoting) {
    cmd.insert(cmd.end(), arg.begin(), arg.end());
    return;
  }
  cmd.push_back(L'"');
  size_t backslashes = 0;
  // Count backslashes and handle escaped quotes
  for (auto it = arg.begin(); it != arg.end(); ++it) {
    if (*it == L'\\') {
      ++backslashes;
    } else if (*it == L'"') {
      for (size_t i = 0; i < backslashes * 2 + 1; i++) {
        cmd.push_back('\\');
      }
      backslashes = 0;
      cmd.push_back('"');
    } else {
      for (size_t i = 0; i < backslashes; i++) {
        cmd.push_back('\\');
      }
      backslashes = 0;
      cmd.push_back(*it);
    }
  }
  for (size_t i = 0; i < backslashes * 2; i++) {
    cmd.push_back('\\');
  }
  cmd.push_back(L'"');
}

inline std::vector<wchar_t> argv_to_command_line(
    std::wstring app, std::vector<std::basic_string<wchar_t>> const& args) {
  std::vector<wchar_t> command;

  auto dot = app.find_last_of(L'.');
  auto path_sep = app.find_last_of(L"\\/");

  auto app_stem_start = path_sep == std::wstring::npos ? 0 : path_sep + 1;
  auto app_stem_end = dot == std::wstring::npos ? app.size() : dot;
  auto app_stem =
      to_lower_ascii(app.substr(app_stem_start, app_stem_end - app_stem_start));

  auto is_cmd = app_stem == L"cmd";

  append_windows_arg(command, app);
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (is_cmd) {
      command.push_back(L' ');
      command.insert(command.end(), it->begin(), it->end());
    } else {
      append_windows_arg(command, *it);
    }
  }
  command.push_back(L'\0');
  return command;
}

inline std::vector<wchar_t> argv_to_command_line_for_cmd(
    std::wstring app, std::vector<std::basic_string<wchar_t>> const& args) {
  std::vector<wchar_t> command;

  append_windows_arg(command, app);

  auto it = args.begin();
  while (it != args.end()) {
    if (!it->empty() && (*it)[0] == L'/') {
      command.push_back(L' ');
      command.insert(command.end(), it->begin(), it->end());
      ++it;
      continue;
    }
    break;
  }
  if (it != args.end()) {
    command.push_back(L' ');
    command.push_back(L'"');
    command.insert(command.end(), it->begin(), it->end());
    while (++it != args.end()) {
      command.push_back(L' ');
      command.insert(command.end(), it->begin(), it->end());
    }
    command.push_back(L'"');
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

#if defined(_WIN32)
inline std::wstring get_last_error_message() {
  DWORD error = GetLastError();
  LPVOID error_msg{NULL};

  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPWSTR)&error_msg, 0, NULL);
  if (error_msg) {
    auto ret = std::wstring{static_cast<wchar_t*>(error_msg)};
    LocalFree(error_msg);
    return ret;
  }
  return L"Unknown error or FormatMessageW failed, error code: " +
         std::to_wstring(error);
}
#else   // _WIN32
inline std::string get_last_error_message() {
  const int error = errno;
  if (auto* err_msg = strerror(error)) {
    return {err_msg};
  }
  return "Unknown error or strerror failed, error code: " +
         std::to_string(errno);
}
#endif  // !_WIN32

// return value: -1 on error, >=0 on success
inline ssize_t write_some(unique_fd const& fd, void const* data,
                          std::size_t size) {
#if defined(_WIN32)
  DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(0x7ffff000u, size));
  DWORD written{0};
  BOOL ok = WriteFile(fd.get(), data, chunk, &written, 0);
  if (!ok) {
    return -1;
  }
  return static_cast<ssize_t>(written);
#else
  ssize_t written = -1;
  const std::size_t chunk =
      std::min<std::size_t>(std::numeric_limits<ssize_t>::max(), size);
  do {
    written = ::write(fd.get(), data, chunk);
  } while (written == -1 && errno == EINTR);
  return written;
#endif
}

inline bool write_all(unique_fd const& fd, void const* data, std::size_t size) {
  auto* p = static_cast<std::byte const*>(data);
  while (size > 0) {
    const ssize_t written = write_some(fd, p, size);
    if (written <= 0) {
      return false;
    }
    p += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

inline ssize_t read_some(unique_fd const& fd, void* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }
#if defined(_WIN32)
  DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(0x7ffff000u, size));
  DWORD read{0};
  BOOL ok = ReadFile(fd.get(), data, chunk, &read, 0);
  if (!ok) {
    auto err = GetLastError();
    if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
      return 0;
    }
    return -1;
  }
  return static_cast<ssize_t>(read);
#else
  ssize_t read = -1;
  const std::size_t chunk =
      std::min<std::size_t>(std::numeric_limits<ssize_t>::max(), size);
  do {
    read = ::read(fd.get(), data, chunk);
  } while (read == -1 && errno == EINTR);
  return read;
#endif
}

inline bool read_exact(unique_fd const& fd, void* data, std::size_t size) {
  auto* p = static_cast<std::byte*>(data);
  while (size > 0) {
    const ssize_t read = read_some(fd, p, size);
    if (read <= 0) {
      return false;
    }
    p += read;
    size -= static_cast<std::size_t>(read);
  }
  return true;
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
      envs[key] = value;
    }
    current_env +=
        env_string.length() + 1;  // Move to the next environment variable
  }

  FreeEnvironmentStringsW(env_block);
  return envs;
}

inline std::optional<std::wstring> getenv(std::wstring const& name) {
  DWORD const size = GetEnvironmentVariableW(name.data(), nullptr, 0);
  if (size == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    return std::nullopt;
  }
  std::wstring ret(size, L'\0');
  DWORD const copied = GetEnvironmentVariableW(name.data(), ret.data(), size);
  ret.resize(copied);
  return ret;
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
inline std::optional<std::string> getenv(std::string const& name) {
  auto const env = std::getenv(name.c_str());
  if (env == nullptr) {
    return std::nullopt;
  }
  return std::string(env);
}
#endif

#if defined(_WIN32)
inline std::vector<std::wstring> find_all_command_path(
    std::wstring const& command) {
  if (command.find_last_of(L"/\\") != std::wstring::npos) {
    return {};
  }

  auto exts =
      split(to_lower_ascii(
                detail::getenv(L"PATHEXT").value_or(L".COM;.EXE;.BAT;.CMD")),
            L';');
  std::erase_if(exts, [](auto const& v) { return v.empty(); });

  auto paths = split(detail::getenv(L"PATH").value_or(L""), L';');
  std::erase_if(paths, [](auto const& v) { return v.empty(); });

  auto is_file = [](std::wstring const& path) {
    DWORD const attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
  };

  auto find_in_path = [&is_file, &paths](std::wstring const& name) {
    std::vector<std::wstring> ret;
    for (auto const& p : paths) {
      if (auto f = p + L'\\' + name; is_file(f)) {
        ret.emplace_back(std::move(f));
      }
    }
    return ret;
  };

  if (auto dot = command.find_last_of(L'.');
      dot != std::wstring::npos && dot > 0) {
    return find_in_path(command);
  }
  std::vector<std::wstring> ret;
  for (auto ext : exts) {
    if (auto f = find_in_path(command + ext); !f.empty()) {
      ret.insert(ret.end(), f.begin(), f.end());
    }
  }
  return ret;
}
inline std::optional<std::wstring> find_executable(
    std::wstring command, std::wstring const& cwd = L"") {
  auto is_file = [](std::wstring const& path) {
    DWORD const attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
  };

  for (auto& c : command) {
    if (c == L'/') {
      c = L'\\';
    }
  }
  if (command.find_last_of(L'\\') != std::wstring::npos) {
    if (command.size() > 2 && command[1] == L':' && is_file(command)) {
      return command;
    }
    if (command.size() > 1 && command[0] == L'\\' && is_file(command)) {
      return command;
    }
    if (!cwd.empty()) {
      if (std::wstring fpath = win_GetFullPathName(cwd + L'\\' + command);
          is_file(fpath)) {
        return fpath;
      }
    } else if (std::wstring fpath = win_GetFullPathName(command);
               is_file(fpath)) {
      return fpath;
    }

    return std::nullopt;
  }

  if (auto dot = command.find_last_of(L'.');
      dot != std::wstring::npos && dot > 0) {
    if (auto ret = win_SearchPath(command, L""); !ret.empty()) {
      return ret;
    }
  } else {
    auto exts = split(
        detail::getenv(L"PATHEXT").value_or(L".COM;.EXE;.BAT;.CMD"), L';');
    std::erase_if(exts, [](auto const& v) { return v.empty(); });
    for (auto ext : exts) {
      if (auto ret = win_SearchPath(command, ext); !ret.empty()) {
        return ret;
      }
    }
  }
  return std::nullopt;
}
#else   // _WIN32
inline std::optional<std::string> find_executable(std::string const& exe_file) {
  constexpr char path_env_sep = ':';

  auto is_executable = [](std::string const& f) {
    struct stat sb{};
    return (stat(f.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) &&
            access(f.c_str(), X_OK) == 0);
  };

  if (exe_file.find_last_of('/') != std::string::npos) {
    if (is_executable(exe_file)) {
      return exe_file;
    }
    return std::nullopt;
  }

  for (const auto paths =
           split(std::string(detail::getenv("PATH").value_or(
                     "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin")),
                 path_env_sep);
       auto& p : paths) {
    constexpr char separator = '/';
    if (std::string f = p + separator + exe_file; is_executable(f)) {
      return f;
    }
  }
  return std::nullopt;
}
#endif  // !_WIN32

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
class Timer {
 public:
  Timer() = default;

  template <typename F, typename Duration>
  Timer(F&& f, Duration timeout) {
    start(std::forward<F>(f), timeout);
  }

  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  Timer(Timer&& other) noexcept
      : state_{std::atomic_exchange(&other.state_, std::shared_ptr<State>{})} {}
  Timer& operator=(Timer&& other) noexcept {
    if (this != &other) {
      stop();
      std::atomic_store(&state_, std::atomic_exchange(
                                     &other.state_, std::shared_ptr<State>{}));
    }
    return *this;
  }

  ~Timer() { stop(); }

  bool running() const {
    auto s = std::atomic_load(&state_);
    return s && s->running_.load();
  }

  void stop() {
    auto state = std::atomic_exchange(&state_, std::shared_ptr<State>{});
    if (!state) {
      return;
    }
    if (state->worker_.get_id() == std::this_thread::get_id()) {
      state->worker_.detach();
      return;
    }

    state->cancelled_.store(true);
    state->cv_.notify_all();

    if (state->worker_.joinable()) {
      state->worker_.join();
    }

    state->running_.store(false);
  }

  template <typename F, typename Duration>
  void start(F&& f, Duration timeout) {
    stop();
    auto state = std::make_shared<State>();
    std::atomic_store(&state_, state);

    state->cancelled_.store(false);
    state->running_.store(true);

    state->worker_ =
        std::thread([state = state, f = std::forward<F>(f), timeout]() mutable {
          std::unique_lock<std::mutex> lock(state->mutex_);
          bool cancelled = state->cv_.wait_for(
              lock, timeout, [&] { return state->cancelled_.load(); });
          if (!cancelled) {
            lock.unlock();
            try {
              f();
            } catch (std::exception const& e) {
#if defined(_WIN32)
              std::string what(e.what());
              print_error(std::wstring(what.begin(), what.end()));
#else
              print_error(e.what());
#endif
            } catch (...) {
#if defined(_WIN32)
              print_error(L"Timer: unknown exception");
#else
              print_error("Timer: unknown exception");
#endif
            }
          }
          state->running_.store(false);
        });
  }

  template <typename F, typename Duration>
  static Timer after(F&& f, Duration timeout) {
    return Timer(std::forward<F>(f), timeout);
  }

 private:
  struct State {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool cancelled_{true};
    std::atomic_bool running_{false};
    std::thread worker_;
  };
  std::shared_ptr<State> state_;
};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

class Readable {
 public:
  virtual ~Readable() = default;
  virtual ssize_t read(void* data, size_t size) = 0;
};

class Writable {
 public:
  virtual ~Writable() = default;
  virtual ssize_t write(void const* data, size_t size) = 0;
};

struct Device {
  NativeStringView name_;
};

class Pipe {
  friend class PipeReader;
  friend class PipeWriter;

 public:
  Pipe(Pipe&& other) = default;
  Pipe& operator=(Pipe&& other) = default;

  Pipe(Pipe const&) = default;
  Pipe& operator=(Pipe const&) = default;

  ~Pipe() = default;

  inline static Pipe create() {
    Pipe p;
    detail::Pipe::create_native_pipe(*p.pair_);
    return p;
  }
  void close_read() const { pair_->rfd_.close(); }
  void close_write() const { pair_->wfd_.close(); }
  void close_all() const {
    close_read();
    close_write();
  }
  unique_fd const& rfd() const { return pair_->rfd_; }
  unique_fd const& wfd() const { return pair_->wfd_; }

  // TODO: remove
  unique_fd& rfd() { return pair_->rfd_; }
  unique_fd& wfd() { return pair_->wfd_; }

  ssize_t read_some(void* data, std::size_t size) const {
    return detail::read_some(pair_->rfd_, data, size);
  }
  bool read_exact(void* data, std::size_t size) const {
    return detail::read_exact(pair_->rfd_, data, size);
  }
  ssize_t write_some(void const* data, std::size_t size) const {
    return detail::write_some(pair_->wfd_, data, size);
  }
  bool write_all(void const* data, std::size_t size) const {
    return detail::write_all(pair_->wfd_, data, size);
  }

  Pipe dup() const {
    Pipe p;
    p.pair_->rfd_ = pair_->rfd_.dup();
    if (!p.pair_->rfd_) {
      return p;
    }
    p.pair_->wfd_ = pair_->wfd_.dup();
    if (!p.pair_->wfd_) {
      p.pair_->rfd_.close();
    }
    return p;
  }

 private:
  Pipe() = default;
  struct pipe_pair {
    unique_fd rfd_, wfd_;
  };
  static inline void create_native_pipe(pipe_pair& pair) {
    NativeHandle fds[2] = {INVALID_NATIVE_HANDLE_VALUE,
                           INVALID_NATIVE_HANDLE_VALUE};
#if defined(_WIN32)
    SECURITY_ATTRIBUTES at;
    at.bInheritHandle = false;
    at.nLength = sizeof(SECURITY_ATTRIBUTES);
    at.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&fds[0], &fds[1], &at, 64 * 1024)) {
      print_error(get_last_error_message());
      return;
    }
#else
#if defined(__linux__)
    if (-1 == pipe2(fds, O_CLOEXEC)) {
      print_error("pipe2() failed");
      return;
    }
#else
    if (-1 == pipe(fds)) {
      print_error("pipe() failed");
      return;
    }
    // Set close-on-exec manually on platforms without pipe2().
    // This prevents pipe fds from leaking into grandchildren via exec().
    if (-1 == fcntl(fds[0], F_SETFD, FD_CLOEXEC) ||
        -1 == fcntl(fds[1], F_SETFD, FD_CLOEXEC)) {
      print_error("fcntl(FD_CLOEXEC) failed");
      close(fds[0]);
      close(fds[1]);
      fds[0] = INVALID_NATIVE_HANDLE_VALUE;
      fds[1] = INVALID_NATIVE_HANDLE_VALUE;
      return;
    }
#endif
#endif
    pair.rfd_.reset(fds[0]);
    pair.wfd_.reset(fds[1]);
  }
  std::shared_ptr<pipe_pair> pair_{std::make_shared<pipe_pair>()};
};

class PipeReader : public Readable {
 public:
  PipeReader() = default;
  ~PipeReader() override = default;
  explicit PipeReader(Pipe& pipe) : pair_(pipe.pair_) {}

  PipeReader(PipeReader const& pipe) = delete;
  PipeReader& operator=(PipeReader const&) = delete;

  PipeReader(PipeReader&& other) = default;
  PipeReader& operator=(PipeReader&& other) = default;

  ssize_t read(void* data, size_t size) override {
    if (!pair_) {
      throw std::runtime_error("Invalid pipe");
    }
    return detail::read_some(pair_->rfd_, data, size);
  }

 private:
  std::shared_ptr<Pipe::pipe_pair> pair_;
};

class PipeWriter : public Writable {
 public:
  PipeWriter() = default;
  ~PipeWriter() override = default;
  explicit PipeWriter(Pipe& pipe) : pair_(pipe.pair_) {}

  PipeWriter(PipeWriter const& pipe) = delete;
  PipeWriter& operator=(PipeWriter const&) = delete;

  PipeWriter(PipeWriter&& other) = default;
  PipeWriter& operator=(PipeWriter&& other) = default;

  ssize_t write(void const* data, size_t size) override {
    if (!pair_) {
      throw std::runtime_error("Invalid pipe");
    }
    return detail::write_some(pair_->wfd_, data, size);
  }

  void close() { pair_->wfd_.close(); }

 private:
  std::shared_ptr<Pipe::pipe_pair> pair_;
};

class File {
 public:
  enum class OpenType {
    ReadOnly,
    WriteTruncate,
    WriteAppend,
  };

  explicit File(Device const& dev, OpenType read_or_write)
      : path_{dev.name_}, open_type_{read_or_write} {
    open_impl();
  }

  explicit File(std::string_view p, OpenType read_or_write)
      : path_{TO_NATIVE_STRING(p)}, open_type_{read_or_write} {
    open_impl();
  }
#if defined(_WIN32)
  explicit File(std::wstring_view p, OpenType read_or_write)
      : path_{p}, open_type_{read_or_write} {
    open_impl();
  }
#endif

  File(File&& o) noexcept
      : path_{std::move(o.path_)},
        open_type_{o.open_type_},
        fd_{std::move(o.fd_)} {}
  File& operator=(File&& o) noexcept {
    fd_ = std::move(o.fd_);
    path_ = std::move(o.path_);
    open_type_ = o.open_type_;
    return *this;
  }
  ~File() = default;
  File(File const&) = delete;
  File& operator=(File const&) = delete;

  void close() { fd_.close(); }
  [[nodiscard]] unique_fd const& fd() const { return fd_; }
  [[nodiscard]] unique_fd& fd() { return fd_; }

  ssize_t read_some(void* data, std::size_t size) const {
    return detail::read_some(fd_, data, size);
  }
  bool read_exact(void* data, std::size_t size) const {
    return detail::read_exact(fd_, data, size);
  }
  ssize_t write_some(void const* data, std::size_t size) const {
    return detail::write_some(fd_, data, size);
  }
  bool write_all(void const* data, std::size_t size) const {
    return detail::write_all(fd_, data, size);
  }

  File dup() const {
    File f;
    f.path_ = path_;
    f.open_type_ = open_type_;
    f.fd_ = fd_.dup();
    return f;
  }

  bool is_valid() const { return !!fd_; }

 private:
  File() {}
  void open_impl() {
    if (fd_) {
      return;
    }
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = FALSE;  // Default non-inheritable

    DWORD dwDesiredAccess = 0;
    DWORD dwCreationDisposition = 0;
    switch (open_type_) {
      case OpenType::ReadOnly:
        dwDesiredAccess = GENERIC_READ;
        dwCreationDisposition = OPEN_EXISTING;
        break;
      case OpenType::WriteTruncate:
        dwDesiredAccess = GENERIC_WRITE;
        dwCreationDisposition = CREATE_ALWAYS;
        break;
      case OpenType::WriteAppend:
        dwDesiredAccess = FILE_APPEND_DATA;
        dwCreationDisposition = OPEN_ALWAYS;
        break;
    }
    fd_.reset(CreateFileW(path_.c_str(), dwDesiredAccess, FILE_SHARE_READ, &sa,
                          dwCreationDisposition, FILE_ATTRIBUTE_NORMAL,
                          nullptr));
    if (!fd_) {
      return;
    }
#else
    int flag = O_CLOEXEC;
    switch (open_type_) {
      case OpenType::ReadOnly:
        flag |= O_RDONLY;
        break;
      case OpenType::WriteTruncate:
        flag |= O_WRONLY | O_CREAT | O_TRUNC;
        break;
      case OpenType::WriteAppend:
        flag |= O_WRONLY | O_CREAT | O_APPEND;
        break;
    }
    fd_.reset(
        ::open(path_.c_str(), flag, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH));
    if (!fd_) {
      return;
    }
#endif
  }
  NativeString path_;
  OpenType open_type_{OpenType::ReadOnly};
  unique_fd fd_{INVALID_NATIVE_HANDLE_VALUE};
};

class FileHandler {
 public:
  explicit FileHandler(NativeHandle f) : fd_{f} {}
  explicit FileHandler(unique_fd&& f) : fd_{std::move(f)} {}

  FileHandler(FileHandler&& o) = default;
  FileHandler& operator=(FileHandler&& o) = default;
  ~FileHandler() = default;
  FileHandler(FileHandler const&) = delete;
  FileHandler& operator=(FileHandler const&) = delete;

  [[nodiscard]] unique_fd const& fd() const { return fd_; }
  void close() { fd_.close(); }

  ssize_t read_some(void* data, std::size_t size) const {
    return detail::read_some(fd_, data, size);
  }
  bool read_exact(void* data, std::size_t size) const {
    return detail::read_exact(fd_, data, size);
  }
  ssize_t write_some(void const* data, std::size_t size) const {
    return detail::write_some(fd_, data, size);
  }
  bool write_all(void const* data, std::size_t size) const {
    return detail::write_all(fd_, data, size);
  }

  [[nodiscard]] FileHandler dup() const {
    auto f = fd_.dup();
    return FileHandler(f.release());
  }

 private:
  unique_fd fd_;
};

class Buffer {
  using buffer_container_type = buffer;
  using value_type = std::variant<std::reference_wrapper<buffer_container_type>,
                                  buffer_container_type>;

 public:
  explicit Buffer(buffer_container_type& buf)
      : buf_{std::make_shared<value_type>(std::ref(buf))},
        pipe_{Pipe::create()} {}
  Buffer()
      : buf_{std::make_shared<value_type>(buffer_container_type{})},
        pipe_{Pipe::create()} {}

  Buffer(Buffer&& o) = default;
  Buffer& operator=(Buffer&& o) = default;
  ~Buffer() = default;
  Buffer(Buffer const&) = delete;
  Buffer& operator=(Buffer const&) = delete;

  [[nodiscard]] ssize_t read_some() const {
    char tmp[1024];
    auto size = pipe_.read_some(tmp, sizeof(tmp));
    if (size > 0) {
      std::visit(
          visitor{
              [tmp, size](buffer_container_type& buf) {
                buf.append(tmp, size);
              },
              [tmp, size](std::reference_wrapper<buffer_container_type> ref) {
                ref.get().append(tmp, size);
              },
          },
          *buf_);
    }
    return size;
  }

  ssize_t write_some() {
    return std::visit(
        visitor{
            [this](const buffer_container_type& buf) {
              if (buf.size() <= written_size_) {
                return static_cast<ssize_t>(0);  // EOF0;
              }
              const auto written = pipe_.write_some(buf.data() + written_size_,
                                                    buf.size() - written_size_);
              if (written > 0) {
                written_size_ += written;
              }
              return written;
            },
            [this](const std::reference_wrapper<buffer_container_type> ref) {
              const auto& buf = ref.get();
              if (buf.size() <= written_size_) {
                return static_cast<ssize_t>(0);  // EOF0;
              }
              const auto written = pipe_.write_some(buf.data() + written_size_,
                                                    buf.size() - written_size_);
              if (written > 0) {
                written_size_ += written;
              }
              return written;
            },
        },
        *buf_);
  }

  [[nodiscard]] bool empty() const {
    return std::visit(
        visitor{
            [this](buffer_container_type const& buf) {
              return buf.size() <= written_size_;
            },
            [this](std::reference_wrapper<buffer_container_type> const ref) {
              return ref.get().size() <= written_size_;
            },
        },
        *buf_);
  }

  buffer& buf() {
    return std::visit(
        visitor{
            [](buffer_container_type& value) -> buffer_container_type& {
              return value;
            },
            [](std::reference_wrapper<buffer_container_type>& value)
                -> buffer_container_type& { return value.get(); },
        },
        *buf_);
  }
  Pipe& pipe() { return pipe_; }
  unique_fd const& rfd() const { return pipe_.rfd(); }
  unique_fd const& wfd() const { return pipe_.wfd(); }
  unique_fd& rfd() { return pipe_.rfd(); }
  unique_fd& wfd() { return pipe_.wfd(); }

  void close_write() const { pipe_.close_write(); }
  void close_read() const { pipe_.close_read(); }

  Buffer dup() const { return Buffer{buf_, pipe_.dup()}; }

 private:
  Buffer(std::shared_ptr<value_type> buf, Pipe&& pipe)
      : buf_{std::move(buf)}, pipe_{std::move(pipe)} {}
  std::shared_ptr<value_type> buf_;
  std::size_t written_size_{0};
  Pipe pipe_;
};

class Redirector {
  using value_type = std::variant<Pipe, File, Buffer, FileHandler>;

 public:
  explicit Redirector() : redirect_(nullptr) {}
  explicit Redirector(Pipe const& p)
      : redirect_(std::make_unique<value_type>(p)) {}
  explicit Redirector(File f)
      : redirect_(std::make_unique<value_type>(std::move(f))) {}
  explicit Redirector(FileHandler f)
      : redirect_(std::make_unique<value_type>(std::move(f))) {}
  explicit Redirector(buffer& buf)
      : redirect_(std::make_unique<value_type>(Buffer(buf))) {}
  Redirector(Redirector&&) noexcept = default;
  Redirector& operator=(Redirector&&) noexcept = default;
  Redirector(Redirector const&) = delete;
  Redirector& operator=(Redirector const&) = delete;
  virtual ~Redirector() = default;

  [[nodiscard]] std::unique_ptr<value_type> dup() const {
    if (!redirect_) {
      return nullptr;
    }
    return std::visit(
        []<typename T>(T& value) -> std::unique_ptr<value_type> {
          return std::make_unique<value_type>(value.dup());
        },
        *redirect_);
  }

  // Parent prepares for child process startup, e.g., opening file handles,
  // pipes, etc.
  void prepare_for_child() {
    if (!redirect_) {
      return;
    }
    std::visit(
        visitor{
            [this]([[maybe_unused]] Pipe& value) {
              (void)this;
#if defined(_WIN32)
              // Default is non-inheritable; make only the child end
              // inheritable. stdin  (fileno 0): child reads  -> rfd is the
              // child end stdout (fileno 1): child writes -> wfd is the child
              // end stderr (fileno 2): child writes -> wfd is the child end
              if (auto& inheritable_handle =
                      fileno() == 0 ? value.rfd() : value.wfd();
                  inheritable_handle) {
                if (!SetHandleInformation(inheritable_handle.get(),
                                          HANDLE_FLAG_INHERIT,
                                          HANDLE_FLAG_INHERIT)) {
                  print_error(L"SetHandleInformation failed: " +
                              std::to_wstring(GetLastError()));
                  return;
                }
              }
#endif
            },
            []([[maybe_unused]] File& value) {
#if defined(_WIN32)
              // File handles are now non-inheritable by default; make them
              // inheritable so the child can use them.
              if (value.fd()) {
                if (!SetHandleInformation(value.fd().get(), HANDLE_FLAG_INHERIT,
                                          HANDLE_FLAG_INHERIT)) {
                  print_error(L"SetHandleInformation failed: " +
                              std::to_wstring(GetLastError()));
                  return;
                }
              }
#endif
            },
            []([[maybe_unused]] FileHandler& value) {
              // do nothing
            },
            [this]([[maybe_unused]] Buffer& value) {
              (void)this;
#if defined(_WIN32)
              // Default is non-inheritable; make only the child end
              // inheritable.
              if (auto& inheritable_handle =
                      fileno() == 0 ? value.rfd() : value.wfd();
                  inheritable_handle) {
                if (!SetHandleInformation(inheritable_handle.get(),
                                          HANDLE_FLAG_INHERIT,
                                          HANDLE_FLAG_INHERIT)) {
                  print_error(L"SetHandleInformation failed: " +
                              std::to_wstring(GetLastError()));
                  return;
                }
              }
#endif
            },
        },
        *redirect_);
  }
  // Parent closes handles needed by the child, which the parent does not need
  void close_child_end() {
    if (!redirect_) {
      return;
    }
    std::visit(visitor{
                   [this](Pipe& value) {
                     // For child: fd 0 is the read end, parent should close it
                     if (fileno() == 0) {
                       value.close_read();
                     } else {
                       value.close_write();
                     }
                   },
                   [](File& value) {
                     // The redirected file was passed to the child, parent no
                     // longer needs it
                     value.close();
                   },
                   []([[maybe_unused]] FileHandler& value) {
                     // Handle provided by another program to the parent; may
                     // still be useful, do nothing, let the original opener
                     // close it
                   },
                   [this](Buffer& value) {
                     // For child: fd 0 is the read end, parent should close it
                     if (fileno() == 0) {
                       value.close_read();
                     } else {
                       value.close_write();
                     }
                   },
               },
               *redirect_);
  }
  void close_all() {
    if (!redirect_) {
      return;
    }
    std::visit(visitor{
                   [](Pipe& value) { value.close_all(); },
                   [](File& value) { value.close(); },
                   []([[maybe_unused]] FileHandler& value) {
                     // Handle provided by another program to the parent; may
                     // still be useful, do nothing, let the original opener
                     // close it
                   },
                   [](Buffer& value) { value.pipe().close_all(); },
               },
               *redirect_);
  }
#if defined(_WIN32)
  std::optional<NativeHandle> child_inherit_handle() {
    if (!redirect_) {
      return std::nullopt;
    }
    return std::visit(
        visitor{
            [this](Pipe& value) -> std::optional<NativeHandle> {
              return fileno() == 0 ? value.rfd().get() : value.wfd().get();
            },
            [](File& value) -> std::optional<NativeHandle> {
              return value.fd().get();
            },
            [](FileHandler& value) -> std::optional<NativeHandle> {
              return value.fd().get();
            },
            [this](Buffer& value) -> std::optional<NativeHandle> {
              return fileno() == 0 ? value.rfd().get() : value.wfd().get();
            },
        },
        *redirect_);
  }
#endif
#if !defined(_WIN32)
  // Child closes handles needed by the parent, which the child does not need
  void close_parent_end() {
    if (!redirect_) {
      return;
    }
    std::visit(visitor{
                   [this](Pipe& value) {
                     // Child: dup stdin as the pipe's read end, stdout as the
                     // pipe's write end
                     dup2(fileno() == 0 ? value.rfd().get() : value.wfd().get(),
                          fileno());
                     value.close_all();
                   },
                   [this](File& value) {
                     if (value.fd().get() != fileno()) {
                       dup2(value.fd().get(),
                            fileno());  // dup to the opened file handle
                       value.close();   // prevent leak
                     }
                   },
                   [this](FileHandler& value) {
                     if (value.fd().get() != fileno()) {
                       dup2(value.fd().get(),
                            fileno());  // dup to the fd provided by the
                                        // parent's program
                       value.close();   // prevent leak
                     }
                   },
                   [this](Buffer& value) {
                     // Child: dup stdin as the pipe's read end, stdout as the
                     // pipe's write end
                     dup2(fileno() == 0 ? value.rfd().get() : value.wfd().get(),
                          fileno());
                     value.pipe().close_all();
                   },
               },
               *redirect_);
  }
#endif  // !_WIN32
  [[nodiscard]] virtual int fileno() const = 0;
  [[nodiscard]] bool inherit() const { return !redirect_; }

  template <typename T>
  bool is() const {
    return redirect_ && std::holds_alternative<T>(*redirect_);
  }
  template <typename T>
  T& get() {
    return std::get<T>(*redirect_);
  }
  template <typename T>
  const T& get() const {
    return std::get<T>(*redirect_);
  }

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
  [[nodiscard]] int fileno() const override { return 0; }
  ~StdinRedirector() override {}
};
class StdoutRedirector : public Redirector {
 public:
  using Redirector::Redirector;
  StdoutRedirector(StdoutRedirector&&) noexcept = default;
  StdoutRedirector& operator=(StdoutRedirector&&) noexcept = default;
  StdoutRedirector(StdoutRedirector const&) = delete;
  StdoutRedirector& operator=(StdoutRedirector const&) = delete;
  [[nodiscard]] int fileno() const override { return 1; }
  ~StdoutRedirector() override {}
};
class StderrRedirector : public Redirector {
 public:
  using Redirector::Redirector;
  StderrRedirector(StderrRedirector&&) noexcept = default;
  StderrRedirector& operator=(StderrRedirector&&) noexcept = default;
  StderrRedirector(StderrRedirector const&) = delete;
  StderrRedirector& operator=(StderrRedirector const&) = delete;
  [[nodiscard]] int fileno() const override { return 2; }
  ~StderrRedirector() override {}
};

#if !defined(_WIN32)
[[maybe_unused]]
inline void read_write_to_buffer_use_poll(StdinRedirector& in,
                                          StdoutRedirector& out,
                                          StderrRedirector& err,
                                          pid_t child_pid,
                                          int* child_status = nullptr) {
  if (in.is<Buffer>()) {
    in.get<Buffer>().wfd().set_nonblocking();
  }
  if (out.is<Buffer>()) {
    out.get<Buffer>().rfd().set_nonblocking();
  }
  if (err.is<Buffer>()) {
    err.get<Buffer>().rfd().set_nonblocking();
  }

  struct pollfd fds[3]{{.fd = in.is<Buffer>() ? in.get<Buffer>().wfd().get()
                                              : INVALID_NATIVE_HANDLE_VALUE,
                        .events = POLLOUT,
                        .revents = 0},
                       {.fd = out.is<Buffer>() ? out.get<Buffer>().rfd().get()
                                               : INVALID_NATIVE_HANDLE_VALUE,
                        .events = POLLIN,
                        .revents = 0},
                       {.fd = err.is<Buffer>() ? err.get<Buffer>().rfd().get()
                                               : INVALID_NATIVE_HANDLE_VALUE,
                        .events = POLLIN,
                        .revents = 0}};

  // If a child pid was provided, use a timed poll loop so we can
  // periodically check whether the direct child has exited. This
  // avoids hanging forever when grandchildren inherit and hold open
  // the write ends of stdout/stderr pipes.
  const bool monitor_child = (!invalid_handle(child_pid));
  // After the child exits, drain any remaining buffered pipe data
  // before giving up. This gives us 2 seconds to collect final output.
  auto drain_deadline = std::chrono::steady_clock::time_point::max();
  bool child_exited = false;

  while (!invalid_handle(fds[0].fd) || !invalid_handle(fds[1].fd) ||
         !invalid_handle(fds[2].fd)) {
    // Use a 200ms timeout when monitoring the child so we can
    // periodically check liveness; otherwise block indefinitely.
    int poll_count = poll(fds, 3, monitor_child ? 200 : -1);
    if (poll_count == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      print_error("poll() failed");
      break;
    }

    // Check if the direct child has exited. We use waitpid(WNOHANG)
    // rather than kill(pid, 0) because a zombie process still exists
    // in the process table and responds to kill(), giving a false
    // positive that the child is still running.
    if (monitor_child && !child_exited) {
      int status = 0;
      pid_t result = ::waitpid(static_cast<pid_t>(child_pid), &status, WNOHANG);
      if (result > 0) {
        // Child has exited — reap it and store the status.
        child_exited = true;
        if (child_status != nullptr) {
          *child_status = status;
        }
        // Start the drain timer — collect any last buffered data
        // from the pipes for a limited time, then stop.
        drain_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
      }
    }

    // If the child exited and the drain deadline has passed, stop
    // waiting for more pipe data — grandchildren may still be running,
    // but we don't want to block the caller indefinitely.
    if (child_exited && std::chrono::steady_clock::now() >= drain_deadline) {
      break;
    }

    if (poll_count == 0) {
      // Timeout only — check child again on next iteration.
      continue;
    }
    if (!invalid_handle(fds[0].fd) && (fds[0].revents & POLLOUT)) {
      ssize_t write_count;
      while ((write_count = in.get<Buffer>().write_some()) > 0) {
      }
      if (write_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          print_error("write() error: " + std::to_string(errno));
          in.get<Buffer>().close_write();
          fds[0].fd = INVALID_NATIVE_HANDLE_VALUE;
          break;
        }
      }
      if (in.get<Buffer>().empty()) {
        in.get<Buffer>().close_write();
        fds[0].fd = INVALID_NATIVE_HANDLE_VALUE;
      }
    }
    if (!invalid_handle(fds[1].fd) && (fds[1].revents & POLLIN)) {
      ssize_t read_count;
      do {
        read_count = out.get<Buffer>().read_some();
      } while (read_count > 0);
      if (read_count == 0) {
        out.get<Buffer>().close_read();
        fds[1].fd = INVALID_NATIVE_HANDLE_VALUE;
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          print_error(get_last_error_message());
          out.get<Buffer>().close_read();
          fds[1].fd = INVALID_NATIVE_HANDLE_VALUE;
          break;
        }
      }
    } else if (!invalid_handle(fds[1].fd) &&
               (fds[1].revents & (POLLHUP | POLLERR))) {
      out.get<Buffer>().close_read();
      fds[1].fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    if (!invalid_handle(fds[2].fd) && (fds[2].revents & POLLIN)) {
      ssize_t read_count;
      do {
        read_count = err.get<Buffer>().read_some();
      } while (read_count > 0);
      if (read_count == 0) {
        err.get<Buffer>().close_read();
        fds[2].fd = INVALID_NATIVE_HANDLE_VALUE;
      }
      if (read_count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          print_error(get_last_error_message());
          err.get<Buffer>().close_read();
          fds[2].fd = INVALID_NATIVE_HANDLE_VALUE;
          break;
        }
      }
    } else if (!invalid_handle(fds[2].fd) &&
               (fds[2].revents & (POLLHUP | POLLERR))) {
      err.get<Buffer>().close_read();
      fds[2].fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    if (!invalid_handle(fds[0].fd) &&
        (fds[0].revents & (POLLNVAL | POLLHUP | POLLERR))) {
      in.get<Buffer>().close_write();
      fds[0].fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    if (!invalid_handle(fds[1].fd) &&
        (fds[1].revents & (POLLNVAL | POLLHUP | POLLERR))) {
      out.get<Buffer>().close_read();
      fds[1].fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    if (!invalid_handle(fds[2].fd) &&
        (fds[2].revents & (POLLNVAL | POLLHUP | POLLERR))) {
      err.get<Buffer>().close_read();
      fds[2].fd = INVALID_NATIVE_HANDLE_VALUE;
    }
    if (invalid_handle(fds[0].fd) && invalid_handle(fds[1].fd) &&
        invalid_handle(fds[2].fd)) {
      break;
    }
  }
  if (in.is<Buffer>()) {
    in.get<Buffer>().close_write();
  }
  if (out.is<Buffer>()) {
    out.get<Buffer>().close_read();
  }
  if (err.is<Buffer>()) {
    err.get<Buffer>().close_read();
  }
}

#endif

inline std::vector<std::thread> read_write_to_buffer_with_threads(
    StdinRedirector& in, StdoutRedirector& out, StderrRedirector& err) {
  std::vector<std::thread> threads;
  if (in.is<Buffer>()) {
    threads.emplace_back(
        [](Buffer& buf) {
          ssize_t write_size;
          do {
            write_size = buf.write_some();
          } while (write_size > 0);
          buf.close_write();
        },
        std::ref(in.get<Buffer>()));
  }
  if (out.is<Buffer>()) {
    threads.emplace_back(
        [](const Buffer& buf) {
          ssize_t read_size;
          do {
            read_size = buf.read_some();
          } while (read_size > 0);
          buf.close_read();
        },
        std::ref(out.get<Buffer>()));
  }
  if (err.is<Buffer>()) {
    threads.emplace_back(
        [](const Buffer& buf) {
          ssize_t read_size;
          do {
            read_size = buf.read_some();
          } while (read_size > 0);
          buf.close_read();
        },
        std::ref(err.get<Buffer>()));
  }

  return threads;
}

struct stdin_redirector {
  StdinRedirector operator<(Pipe p) const {
    return StdinRedirector{std::move(p)};
  }
  StdinRedirector operator<(std::string_view file) const {
    return StdinRedirector{File{file, File::OpenType::ReadOnly}};
  }
  StdinRedirector operator<(Device file) const {
    return StdinRedirector{File{file, File::OpenType::ReadOnly}};
  }
#if defined(_WIN32)
  StdinRedirector operator<(std::wstring_view file) const {
    return StdinRedirector{File{file, File::OpenType::ReadOnly}};
  }
#endif
  StdinRedirector operator<(buffer& buf) const { return StdinRedirector{buf}; }
};

struct stdout_redirector {
  StdoutRedirector operator>(Pipe const& p) const {
    return StdoutRedirector{p};
  }
  StdoutRedirector operator>(std::string_view file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteTruncate}};
  }
  StdoutRedirector operator>(Device file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteTruncate}};
  }
#if defined(_WIN32)
  StdoutRedirector operator>(std::wstring_view file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteTruncate}};
  }
#endif
  StdoutRedirector operator>(buffer& buf) const {
    buf.clear();
    return StdoutRedirector{buf};
  }
  StdoutRedirector operator>>(buffer& buf) const {
    return StdoutRedirector{buf};
  }

  StdoutRedirector operator>>(std::string_view file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteAppend}};
  }
  StdoutRedirector operator>>(Device file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteAppend}};
  }
#if defined(_WIN32)
  StdoutRedirector operator>>(std::wstring_view file) const {
    return StdoutRedirector{File{file, File::OpenType::WriteAppend}};
  }
#endif
};

struct stderr_redirector {
  StderrRedirector operator>(Pipe p) const {
    return StderrRedirector{std::move(p)};
  }
  StderrRedirector operator>(std::string_view file) const {
    return StderrRedirector{File{file, File::OpenType::WriteTruncate}};
  }
  StderrRedirector operator>(Device file) const {
    return StderrRedirector{File{file, File::OpenType::WriteTruncate}};
  }
#if defined(_WIN32)
  StderrRedirector operator>(std::wstring_view file) const {
    return StderrRedirector{File{file, File::OpenType::WriteTruncate}};
  }
#endif
  StderrRedirector operator>(buffer& buf) const {
    buf.clear();
    return StderrRedirector{buf};
  }
  StderrRedirector operator>>(buffer& buf) const {
    return StderrRedirector{buf};
  }
  StderrRedirector operator>>(std::string_view file) const {
    return StderrRedirector{File{file, File::OpenType::WriteAppend}};
  }
  StderrRedirector operator>>(Device file) const {
    return StderrRedirector{File{file, File::OpenType::WriteAppend}};
  }
#if defined(_WIN32)
  StderrRedirector operator>>(std::wstring_view file) const {
    return StderrRedirector{File{file, File::OpenType::WriteAppend}};
  }
#endif
};

struct Cwd {
  NativeString cwd;
};

// Set environment variables, replacing existing ones.
struct Env {
  std::map<NativeString, NativeString> env;
};

// Append environment variables to the existing environment.
struct EnvAppend {
  std::map<NativeString, NativeString> env;
};

// Append or prepend a value to a specific environment variable, e.g., PATH.
struct EnvItemAppend {
  EnvItemAppend& operator+=(std::string_view val) {
    std::get<1>(kv) = TO_NATIVE_STRING(val);
    std::get<2>(kv) = true;
    return *this;
  }
  EnvItemAppend& operator<<=(std::string_view val) {
    std::get<1>(kv) = TO_NATIVE_STRING(val);
    std::get<2>(kv) = false;
    return *this;
  }

#if defined(_WIN32)
  EnvItemAppend& operator+=(std::wstring_view val) {
    std::get<1>(kv) = val;
    std::get<2>(kv) = true;
    return *this;
  }
  EnvItemAppend& operator<<=(std::wstring_view val) {
    std::get<1>(kv) = val;
    std::get<2>(kv) = false;
    return *this;
  }
#endif

  // name, value, is_append
  std::tuple<NativeString, NativeString, bool> kv;
};

struct Timeout {
  std::chrono::milliseconds timeout;
};

struct Newgroup {
  bool newgroup{false};
};

struct newgroup_operator {
  Newgroup operator=(bool b) const { return Newgroup{b}; }
};

struct timeout_operator {
  Timeout operator=(std::chrono::milliseconds ms) const { return Timeout{ms}; }
  Timeout operator=(int seconds) const {
    return Timeout{std::chrono::seconds(seconds)};
  }
};

struct cwd_operator {
  Cwd operator=(std::string_view p) const {
    return Cwd{TO_NATIVE_STRING(std::string(p))};
  }
#if defined(_WIN32)
  Cwd operator=(std::wstring_view p) const { return Cwd{std::wstring{p}}; }
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
  EnvItemAppend operator[](std::string_view key) const {
#if defined(_WIN32)
    return EnvItemAppend{{utf8_to_utf16(key), L"", true}};
#else
    return EnvItemAppend{{std::string(key), "", true}};
#endif
  }
};

class Shell {
 public:
#if defined(_WIN32)
  using argv_to_command_line_fn_ptr =
      std::vector<wchar_t> (*)(std::wstring, std::vector<std::wstring> const&);
#endif
  NativeString shell_cmd() const {
    auto cmds = shell_prefix_commands();
    return cmds[0];
  }
  std::vector<NativeString> shell_args() const {
    auto cmds = shell_prefix_commands();
    cmds.erase(cmds.begin());
    return cmds;
  }

#if defined(_WIN32)
  argv_to_command_line_fn_ptr argv_to_command_line_func() {
    return nullptr == argv_to_command_line_fn ? &detail::argv_to_command_line
                                              : argv_to_command_line_fn;
  }
#endif
#if defined(_WIN32)
  constexpr Shell cmd() const {
    Shell shell;
    shell.shell_prefix_commands = &Shell::cmd_cmds;
    shell.argv_to_command_line_fn = &detail::argv_to_command_line_for_cmd;
    return shell;
  }
  constexpr static Shell Cmd() { return Shell().cmd(); }

  constexpr Shell powershell() const {
    Shell shell;
    shell.shell_prefix_commands = &Shell::powershell_cmds;
    return shell;
  }
  constexpr static Shell Powershell() { return Shell().powershell(); }
#endif
  constexpr Shell posix() const {
    Shell shell;
    shell.shell_prefix_commands = &Shell::sh_cmds;
    return shell;
  }
  constexpr static Shell Posix() { return Shell().posix(); }
  constexpr Shell bash() const {
    Shell shell;
    shell.shell_prefix_commands = &Shell::bash_cmds;
    return shell;
  }
  constexpr static Shell Bash() { return Shell().bash(); }

  Shell() = default;

 private:
#if defined(_WIN32)
  static std::vector<NativeString> cmd_cmds() {
    std::vector<NativeString> ret;
    // %COMSPEC% and %SystemRoot%\System32\cmd.exe
    auto compspec = detail::getenv(L"COMSPEC");
    if (compspec.has_value()) {
      ret.push_back(compspec.value());
    } else if (auto system_root = detail::getenv(L"SystemRoot");
               system_root.has_value()) {
      ret.push_back(system_root.value() + L"\\System32\\cmd.exe");
    } else if (auto cmd = find_executable(L"cmd"); cmd.has_value()) {
      ret.push_back(cmd.value());
    } else {
      ret.push_back(L"cmd.exe");
    }
    ret.push_back(L"/d");
    ret.push_back(L"/s");
    ret.push_back(L"/c");
    return ret;
  }
#endif
  static std::vector<NativeString> sh_cmds() {
#if defined(_WIN32)
    return std::vector<NativeString>{L"sh.exe", L"-c"};
#else
    return std::vector<NativeString>{"/bin/sh", "-c"};
#endif
  }

  static std::vector<NativeString> bash_cmds() {
#if defined(_WIN32)
    auto is_file = [](std::wstring const& path) {
      DWORD const attr = GetFileAttributesW(path.c_str());
      return attr != INVALID_FILE_ATTRIBUTES &&
             !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };
    std::wstring bash_path;
    std::wstring bash_exe{L"bash.exe"};
    auto bashs = find_all_command_path(bash_exe);
    for (auto const& b : bashs) {
      if (b.ends_with(L"bin\\bash.exe") || b.ends_with(L"bin/bash.exe")) {
        bash_path = b;
        break;
      }
    }
    if (bash_path.empty()) {
      std::wstring git_exe{L"git.exe"};
      auto gits = find_all_command_path(git_exe);
      for (auto const& g : gits) {
        if (auto bash = g.substr(0, g.size() - git_exe.size()) + bash_exe;
            is_file(bash)) {
          bash_path = bash;
          break;
        }
        if (auto bash = g.substr(0, g.size() - git_exe.size()) + L"..\\bin\\" +
                        bash_exe;
            is_file(bash)) {
          bash_path = bash;
          break;
        }
      }
    }
    if (bash_path.empty()) {
      bash_path = L"bash";
    }
    std::vector<NativeString> ret{bash_path, L"-c"};
#else
    std::vector<NativeString> ret{"bash", "-c"};
#endif
    return ret;
  }

#if defined(_WIN32)
  static std::vector<NativeString> powershell_cmds() {
    return std::vector<NativeString>{L"powershell", L"-NoProfile", L"-Command"};
  }
#endif
  std::vector<NativeString> (*shell_prefix_commands)() = nullptr;

#if defined(_WIN32)
  argv_to_command_line_fn_ptr argv_to_command_line_fn = nullptr;
#endif
};

namespace named_args {
#if defined(_WIN32)
[[maybe_unused]] inline constexpr static auto devnull = Device{L"NUL"};
[[maybe_unused]] inline constexpr static auto devttyout = Device{L"CONOUT$"};
[[maybe_unused]] inline constexpr static auto devttyin = Device{L"CONIN$"};
[[maybe_unused]] inline constexpr static Shell powershell = Shell::Powershell();
[[maybe_unused]] inline constexpr static Shell shell{Shell::Cmd()};
#else
[[maybe_unused]] inline constexpr static auto devnull = Device{"/dev/null"};
[[maybe_unused]] inline constexpr static auto devtty = Device{"/dev/tty"};
[[maybe_unused]] inline constexpr static auto devttyout = devtty;
[[maybe_unused]] inline constexpr static auto devttyin = devtty;
[[maybe_unused]] inline constexpr static Shell shell{Shell::Posix()};
#endif
[[maybe_unused]] inline constexpr static stdin_redirector std_in;
[[maybe_unused]] inline constexpr static stdout_redirector std_out;
[[maybe_unused]] inline constexpr static stderr_redirector std_err;
[[maybe_unused]] inline constexpr static cwd_operator cwd;
[[maybe_unused]] inline constexpr static env_operator env;
[[maybe_unused]] inline constexpr static timeout_operator timeout;
[[maybe_unused]] inline constexpr static int timeout_infinite = INT_MAX;
[[maybe_unused]] inline constexpr static newgroup_operator newgroup;
[[maybe_unused]] inline constexpr static Shell bash{Shell::Bash()};

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
#if defined(_WIN32)
[[maybe_unused]] inline constexpr static Shell $powershell =
    Shell::Powershell();
[[maybe_unused]] inline constexpr static Shell $shell{Shell::Cmd()};
#else
[[maybe_unused]] inline constexpr static auto $devtty = devtty;
[[maybe_unused]] inline constexpr static Shell $shell{Shell::Posix()};
#endif
[[maybe_unused]] inline constexpr static auto $devnull = devnull;
[[maybe_unused]] inline constexpr static auto $devttyout = devttyout;
[[maybe_unused]] inline constexpr static auto $devttyin = devttyin;
[[maybe_unused]] inline constexpr static stdin_redirector $stdin;
[[maybe_unused]] inline constexpr static stdout_redirector $stdout;
[[maybe_unused]] inline constexpr static stderr_redirector $stderr;
[[maybe_unused]] inline constexpr static cwd_operator $cwd;
[[maybe_unused]] inline constexpr static env_operator $env;
[[maybe_unused]] inline constexpr static timeout_operator $timeout;
[[maybe_unused]] inline constexpr static int $timeout_infinite = INT_MAX;
[[maybe_unused]] inline constexpr static newgroup_operator $newgroup;
[[maybe_unused]] inline constexpr static Shell $bash{Shell::Bash()};
#endif
}  // namespace named_args
template <typename T>
concept named_argument_type = std::same_as<Env, std::decay_t<T>> ||
                              std::same_as<StdinRedirector, std::decay_t<T>> ||
                              std::same_as<StdoutRedirector, std::decay_t<T>> ||
                              std::same_as<StderrRedirector, std::decay_t<T>> ||
                              std::same_as<Cwd, std::decay_t<T>> ||
                              std::same_as<Timeout, std::decay_t<T>> ||
                              std::same_as<Newgroup, std::decay_t<T>> ||
                              std::same_as<EnvAppend, std::decay_t<T>> ||
                              std::same_as<EnvItemAppend, std::decay_t<T>>;
template <typename ToCharT, string_like_type From>
std::basic_string<ToCharT> convert_to_string(From&& from) {
  if constexpr (std::is_same_v<get_char_type_t<From>, ToCharT>) {
    return std::basic_string<ToCharT>(std::forward<From>(from));
#if defined(_WIN32)
  } else if constexpr (!std::is_same_v<get_char_type_t<From>, ToCharT>) {
    if constexpr (std::is_same_v<ToCharT, wchar_t>) {
      return utf8_to_utf16(std::string(std::forward<From>(from)));
    } else {
      return utf16_to_utf8(std::wstring(std::forward<From>(from)));
    }
#endif
  } else {
    static_assert(sizeof(From) == 0, "invalid type");
  }
}

template <typename T>
concept run_args_type = named_argument_type<T> || string_like_type<T>;

template <typename T>
concept named_argument_for_capture_type =
    named_argument_type<T> &&
    !std::same_as<StdoutRedirector, std::decay_t<T>> &&
    !std::same_as<StderrRedirector, std::decay_t<T>>;

template <typename T>
concept capture_run_args_type =
    named_argument_for_capture_type<T> || string_like_type<T>;

template <typename T>
concept named_argument_for_detach_type =
    std::same_as<Env, std::decay_t<T>> || std::same_as<Cwd, std::decay_t<T>> ||
    std::same_as<EnvAppend, std::decay_t<T>> ||
    std::same_as<EnvItemAppend, std::decay_t<T>>;

template <typename T>
concept detach_run_args_type =
    named_argument_for_detach_type<T> || string_like_type<T>;

template <typename T1, typename T2>
struct tuple_concat;

template <typename... T1, typename... T2>
struct tuple_concat<std::tuple<T1...>, std::tuple<T2...>> {
  using type = std::tuple<T1..., T2...>;
};

template <typename... T1>
struct tuple_concat<std::tuple<T1...>, std::tuple<>> {
  using type = std::tuple<T1...>;
};

template <typename...>
struct named_arg_type_list;

template <>
struct named_arg_type_list<> {
  using type = std::tuple<>;
};
template <typename Head, typename... Tails>
struct named_arg_type_list<Head, Tails...> {
  using type = std::conditional_t<
      named_argument_type<Head>,
      typename tuple_concat<std::tuple<Head>,
                            typename named_arg_type_list<Tails...>::type>::type,
      typename named_arg_type_list<Tails...>::type>;
};
template <typename... T>
using named_arg_type_list_t = named_arg_type_list<T...>::type;

class process {
 public:
#if defined(_WIN32)
  process(unique_fd process_handle, unique_fd thread_handle,
          StdinRedirector _stdin, StdoutRedirector _stdout,
          StderrRedirector _stderr,
          std::optional<std::chrono::milliseconds> timeout,
          std::shared_ptr<unique_fd> job_handle)
      : process_handle_(std::move(process_handle)),
        thread_handle_(std::move(thread_handle)),
        job_handle_(job_handle),
        stdin_(std::move(_stdin)),
        stdout_(std::move(_stdout)),
        stderr_(std::move(_stderr)),
        timeout_(timeout) {
    if (!process_handle_) {
      return;
    }
    launch_watchdog();
    pump_pipe_data();
  }
#else
  process(unique_pid pid, unique_pid pgid, StdinRedirector _stdin,
          StdoutRedirector _stdout, StderrRedirector _stderr,
          std::optional<std::chrono::milliseconds> timeout)
      : pid_(std::move(pid)),
        pgid_(std::move(pgid)),
        stdin_(std::move(_stdin)),
        stdout_(std::move(_stdout)),
        stderr_(std::move(_stderr)),
        timeout_(std::move(timeout)) {
    if (!pid_) {
      return;
    }
    launch_watchdog();
    pump_pipe_data();
  }
#endif

  process(process const&) = delete;
  process& operator=(process const&) = delete;

  process(process&& other) = default;
  process& operator=(process&& other) = default;

  ~process() {
    terminate();
#if defined(_WIN32)
    join_pump_threads();
#endif
  }

#if defined(_WIN32)
  [[nodiscard]] NativeHandle pid() const { return process_handle_.get(); }
#else
  [[nodiscard]] pid_t pid() const { return pid_.get(); }
#endif
  explicit operator bool() const { return is_valid(); }
  bool is_valid() const {
#if defined(_WIN32)
    return !!process_handle_;
#else
    return !!pid_;
#endif
  }
  [[nodiscard]] bool running() {
    if (!is_valid()) {
      return false;
    }
    update_status();
    return !exited_;
  }
// TODO:
// void wait_for(std::chrono::milliseconds timeout) {}
#if defined(_WIN32)
  int decode(DWORD status) { return static_cast<int>(status); }
  int wait() {
    auto watchdog_guard = detail::make_scope_exit([this] { stop_watchdog(); });
    auto threads_guard =
        detail::make_scope_exit([this] { join_pump_threads(); });
    if (!process_handle_) {
      return 127;
    }
    if (exited_ && exit_code_ != kNotExited) {
      process_handle_.reset(INVALID_NATIVE_HANDLE_VALUE);
      return decode(exit_code_);
    }
    auto rc = WaitForSingleObject(process_handle_.get(), INFINITE);
    if (rc == WAIT_OBJECT_0) {
      GetExitCodeProcess(process_handle_.get(), &exit_code_);
      process_handle_.reset(INVALID_NATIVE_HANDLE_VALUE);
      return decode(exit_code_);
    }

    process_handle_.reset(INVALID_NATIVE_HANDLE_VALUE);
    return 127;
  }
#else
  int decode(int status) {
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
      return 128 + (WTERMSIG(status));
    }
    return status;
  }
  int wait() {
    auto watchdog_guard = detail::make_scope_exit([this] { stop_watchdog(); });
    if (!pid_) {
      return 127;
    }
    if (exited_ && exit_code_ != kNotExited) {
      pid_.reset(-1);
      return decode(exit_code_);
    }
    int wait_ret;
    do {
      wait_ret = waitpid(pid_.get(), &exit_code_, 0);
    } while (wait_ret == -1 && errno == EINTR);

    pid_.reset(-1);
    return decode(exit_code_);
  }
#endif

  void terminate() {
    auto close_all_guard = detail::make_scope_exit([this] {
      stdin_.close_all();
      stdout_.close_all();
      stderr_.close_all();
    });
#if defined(_WIN32)
    if (job_handle_ && *job_handle_) {
      TerminateJobObject(job_handle_->get(), 1);
      job_handle_->close();
    } else if (process_handle_) {
      TerminateProcess(process_handle_.get(), 1);
    }
#else
    if (-1 == pid_) {
      return;
    }
    auto kill_pid = pid_.get();
    if ((-1 != pgid_) && (pid_ == pgid_ || 0 == pgid_)) {
      kill_pid = -pid_.get();
    }
    kill(kill_pid, SIGTERM);
    auto sigkill_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < sigkill_deadline) {
      if (kill(kill_pid, 0) != 0) {
        return;  // all processes in the group have already exited
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(kill_pid, SIGKILL);
    // TODO: set pid_ to -1 ?
#endif
  }

 private:
#if defined(_WIN32)
  void join_pump_threads() {
    for (auto& t : pump_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    pump_threads_.clear();
  }
  // parent pump data to/from child processes
  void pump_pipe_data() {
    close_child_end();
    pump_threads_ = read_write_to_buffer_with_threads(stdin_, stdout_, stderr_);
  }
#else
  // parent pump data to/from child processes
  void pump_pipe_data() {
    close_child_end();
    read_write_to_buffer_use_poll(stdin_, stdout_, stderr_, pid_.get(),
                                  &exit_code_);
    {
      int status = 0;
      pid_t result;
      do {
        result = ::waitpid(pid_.get(), &status, WNOHANG);
      } while (result == -1 && errno == EINTR);
      if (result > 0) {
        exited_ = true;
        exit_code_ = status;
      } else if (result == -1 && errno == ECHILD) {
        exited_ = true;
      }
    }
  }
#endif
  void launch_watchdog() {
    if (!timeout_.has_value()) {
      return;
    }
    watchdog_ = Timer::after([this]() { terminate(); }, *timeout_);
  }

  void stop_watchdog() { watchdog_.stop(); }
  void close_child_end() {
    stdin_.close_child_end();
    stdout_.close_child_end();
    stderr_.close_child_end();
  }

  void update_status() {
    if (!is_valid() || exited_) {
      return;
    }
#if defined(_WIN32)
    auto rc = WaitForSingleObject(process_handle_.get(), 0);
    if (rc == WAIT_OBJECT_0) {
      exited_ = true;
      GetExitCodeProcess(process_handle_.get(), &exit_code_);
    }
#else
    int status;
    pid_t rc;
    do {
      rc = waitpid(pid_.get(), &status, WNOHANG);
    } while (rc == -1 && errno == EINTR);
    if (rc == pid_.get()) {
      exit_code_ = status;
      exited_ = true;
    }
#endif
  }

#if defined(_WIN32)
  unique_fd process_handle_{INVALID_NATIVE_HANDLE_VALUE};
  unique_fd thread_handle_{INVALID_NATIVE_HANDLE_VALUE};
  std::shared_ptr<unique_fd> job_handle_{std::make_shared<unique_fd>()};
  std::vector<unsigned char> proc_thread_attr_list_;
  std::vector<std::thread> pump_threads_;
#else
  unique_pid pid_{-1};
  unique_pid pgid_{-1};
#endif
  StdinRedirector stdin_;
  StdoutRedirector stdout_;
  StderrRedirector stderr_;
  std::optional<std::chrono::milliseconds> timeout_{std::nullopt};
  detail::Timer watchdog_;
  bool exited_{false};
#if defined(_WIN32)
  static constexpr DWORD kNotExited = 259;  // STILL_ACTIVE
  DWORD exit_code_{kNotExited};
#else
  static constexpr int kNotExited = 0x17f;
  int exit_code_{kNotExited};
#endif
};

class builder {
  friend class pipeline;

 public:
  template <named_argument_type... T>
  explicit builder(NativeString app, std::vector<NativeString> args,
                   T&&... named_args)
      : app_(std::move(app)), args_(std::move(args)) {
    std::map<NativeString, NativeString> environments;
    std::map<NativeString, NativeString> env_appends;
    std::vector<std::tuple<NativeString, NativeString, bool>> env_item_appends;
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
             if constexpr (std::is_same_v<ArgType, Newgroup>) {
#if defined(_WIN32)
               newgroup_ = arg.newgroup;
#else
               if (arg.newgroup) {
                 requested_pgid_ = 0;
               } else {
                 requested_pgid_ = INVALID_NATIVE_HANDLE_VALUE;
               }
#endif
             }
             if constexpr (std::is_same_v<ArgType, Timeout>) {
               if (arg.timeout ==
                   std::chrono::seconds(named_args::timeout_infinite)) {
                 timeout_ = std::nullopt;
               } else {
                 timeout_ = arg.timeout;
               }
             }
           }(std::forward<T>(named_args))));
    if ((!env_item_appends.empty() || !env_appends.empty()) &&
        environments.empty()) {
      environments = get_all_env_vars();
    }
    for (auto const& [key, val] : env_appends) {
#ifdef _WIN32
      auto it = std::find_if(
          environments.begin(), environments.end(), [&key](auto const& kv) {
            return to_lower_ascii(kv.first) == to_lower_ascii(key);
          });
      if (it != environments.end()) {
        it->second = val;
      } else {
        environments.insert({key, val});
      }
#else
      environments[key] = val;
#endif
    }
#ifdef _WIN32
    wchar_t path_env_sep = L';';
#else
    char path_env_sep = ':';
#endif
    for (auto const& [name, value, is_append] : env_item_appends) {
#ifdef _WIN32
      auto it = std::find_if(environments.begin(), environments.end(),
                             [name = to_lower_ascii(name)](auto const& kv) {
                               return to_lower_ascii(kv.first) == name;
                             });
#else
      auto it = environments.find(name);
#endif
      if (it == environments.end()) {
        environments.insert({name, value});
      } else {
        if (is_append) {
          it->second.push_back(path_env_sep);
          it->second.append(value);
        } else {
          it->second.insert(it->second.begin(), path_env_sep);
          it->second.insert(it->second.begin(), value.begin(), value.end());
        }
      }
    }
    env_ = environments;
  }

  template <named_argument_type... Args>
  builder(Shell shell, NativeString command, Args&&... named_args)
      : builder(
            shell.shell_cmd(),
            [&shell](NativeString command) {
              std::vector<NativeString> ret{shell.shell_args()};
              ret.emplace_back(std::move(command));
              return ret;
            }(std::move(command)),
            std::forward<Args>(named_args)...) {
#if defined(_WIN32)
    this->argv_to_command_line_fn = shell.argv_to_command_line_func();
#endif
  }

#if defined(_WIN32)
  template <named_argument_type... T>
  explicit builder(std::string const& app, std::vector<std::string> const& args,
                   T&&... named_args)
      : builder(
            TO_NATIVE_STRING(app),
            [](auto const& args) {
              std::vector<NativeString> ret;
              std::transform(args.begin(), args.end(), std::back_inserter(ret),
                             [](auto const& s) { return TO_NATIVE_STRING(s); });
              return ret;
            }(args),
            std::forward<T>(named_args)...) {}
  template <named_argument_type... Args>
  builder(Shell shell, std::string command, Args&&... named_args)
      : builder(shell, utf8_to_utf16(std::move(command)),
                std::forward<Args>(named_args)...) {}
#endif

  builder(builder&&) noexcept = default;
  builder& operator=(builder&&) noexcept = default;
  builder(const builder&) = delete;
  builder& operator=(const builder&) = delete;

  ~builder() = default;

  process spawn() {
    prepare_for_child();
    auto close_child_end_guard =
        detail::make_scope_exit([this]() { close_child_end(); });
#if defined(_WIN32)
    return spawn_win();
#else
    return spawn_posix();
#endif  // !_WIN32
  }

  bool detach_spawn() {
#if defined(_WIN32)
    bool success = detach_spawn_win();
#else
    bool success = detach_spawn_posix();
#endif  // !_WIN32
    close_child_end();
    return success;
  }

  int run() {
    auto p = spawn();
    if (!p) {
      return 127;
    }
    return p.wait();
  }

 private:
#if defined(_WIN32)
  process spawn_win() {
    process invalid_process(unique_fd(), unique_fd(), StdinRedirector{},
                            StdoutRedirector{}, StderrRedirector{},
                            std::nullopt, std::make_shared<unique_fd>());

    if (!(*job_handle_)) {
      HANDLE hJob = CreateJobObjectW(NULL, NULL);
      if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli,
                                sizeof(jeli));
        *job_handle_ = unique_fd(hJob);
      }
    }

    STARTUPINFOEXW startup_info{};
    startup_info.StartupInfo.cb = sizeof(startup_info);

    startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::vector<HANDLE> handles_to_inherit;
    auto add_unique_handle = [&](HANDLE h) {
      if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        return;
      }
      for (auto existing : handles_to_inherit) {
        if (existing == h) {
          return;
        }
      }
      handles_to_inherit.push_back(h);
    };

    auto stdin_handle = stdin_.child_inherit_handle();
    auto stdout_handle = stdout_.child_inherit_handle();
    auto stderr_handle = stderr_.child_inherit_handle();

    startup_info.StartupInfo.hStdInput =
        stdin_handle.value_or(GetStdHandle(STD_INPUT_HANDLE));
    startup_info.StartupInfo.hStdOutput =
        stdout_handle.value_or(GetStdHandle(STD_OUTPUT_HANDLE));
    startup_info.StartupInfo.hStdError =
        stderr_handle.value_or(GetStdHandle(STD_ERROR_HANDLE));

    add_unique_handle(startup_info.StartupInfo.hStdInput);
    add_unique_handle(startup_info.StartupInfo.hStdOutput);
    add_unique_handle(startup_info.StartupInfo.hStdError);

    // Set up PROC_THREAD_ATTRIBUTE_LIST for explicit handle inheritance
    // only when there are real inheritable handles. An empty list would
    // cause UpdateProcThreadAttribute to fail with ERROR_BAD_LENGTH.
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = nullptr;
    if (!handles_to_inherit.empty()) {
      SIZE_T attr_list_size = 0;
      InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);
      proc_thread_attr_list_.resize(attr_list_size);
      attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
          proc_thread_attr_list_.data());
      if (!InitializeProcThreadAttributeList(attr_list, 1, 0,
                                             &attr_list_size)) {
        print_error(L"InitializeProcThreadAttributeList failed: " +
                    std::to_wstring(GetLastError()));
        return invalid_process;
      }
      if (!UpdateProcThreadAttribute(
              attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
              handles_to_inherit.data(),
              handles_to_inherit.size() * sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attr_list);
        print_error(L"UpdateProcThreadAttribute failed: " +
                    std::to_wstring(GetLastError()));
        return invalid_process;
      }
      startup_info.lpAttributeList = attr_list;
    }

    auto app_path = find_executable(app_, cwd_);

    auto command = argv_to_command_line_fn(app_, args_);

    auto env_block = create_environment_string_data(env_);

    PROCESS_INFORMATION pi{};
    DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
    if (attr_list != nullptr) {
      creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    if (newgroup_) {
      creation_flags |= CREATE_NEW_PROCESS_GROUP;
    }
    auto success = CreateProcessW(
        app_path.has_value() ? app_path.value().c_str() : nullptr,
        command.data(), NULL, NULL, TRUE, creation_flags,
        env_block.empty() ? nullptr : env_block.data(),
        cwd_.empty() ? nullptr : cwd_.data(),
        reinterpret_cast<LPSTARTUPINFOW>(&startup_info), &pi);

    if (attr_list != nullptr) {
      DeleteProcThreadAttributeList(attr_list);
    }
    startup_info.lpAttributeList = nullptr;

    if (success) {
      if (job_handle_ && *job_handle_) {
        AssignProcessToJobObject(job_handle_->get(), pi.hProcess);
      }
      return process(unique_fd(pi.hProcess), unique_fd(pi.hThread),
                     std::move(stdin_), std::move(stdout_), std::move(stderr_),
                     timeout_, job_handle_);
    }
    print_error(get_last_error_message());
    stdin_.close_all();
    stdout_.close_all();
    stderr_.close_all();
    return invalid_process;
  }

  bool detach_spawn_win() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);

    auto app_path = find_executable(app_, cwd_);
    auto command = argv_to_command_line_fn(app_, args_);
    auto env_block = create_environment_string_data(env_);

    PROCESS_INFORMATION pi{};
    DWORD creation_flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP |
                           CREATE_UNICODE_ENVIRONMENT;

    auto success = CreateProcessW(
        app_path.has_value() ? app_path.value().c_str() : nullptr,
        command.data(), NULL, NULL, FALSE, creation_flags,
        env_block.empty() ? nullptr : env_block.data(),
        cwd_.empty() ? nullptr : cwd_.data(), &si, &pi);

    if (success) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return true;
    }
    print_error(get_last_error_message());
    return false;
  }
#else
  process spawn_posix() {
    process invalid_process{unique_pid(),       unique_pid(),
                            StdinRedirector{},  StdoutRedirector{},
                            StderrRedirector{}, std::nullopt};
    if (!requested_pgid_.has_value()) {
      File stdin_file{detail::named_args::devttyin, File::OpenType::ReadOnly};
      if (stdin_.inherit() && !stdin_file.fd().isatty()) {
        requested_pgid_ = 0;
      } else {
        requested_pgid_ = INVALID_NATIVE_HANDLE_VALUE;
      }
    }

    auto pid = fork();
    if (pid < 0) {
      print_error("fork() failed");
      return invalid_process;
    }
    if (pid == 0) {
      if (!invalid_handle(
              requested_pgid_.value_or(INVALID_NATIVE_HANDLE_VALUE))) {
        setpgid(0, requested_pgid_.value());
      }
      execute_command_in_child();
    } else {
      pid_t target_pgid{-1};
      if (!invalid_handle(
              requested_pgid_.value_or(INVALID_NATIVE_HANDLE_VALUE))) {
        target_pgid =
            requested_pgid_.value() == 0 ? pid : requested_pgid_.value();
        (void)setpgid(pid, target_pgid);
      }
      return process{unique_pid(pid),    unique_pid(target_pgid),
                     std::move(stdin_),  std::move(stdout_),
                     std::move(stderr_), std::move(timeout_)};
    }
    return invalid_process;
  }
  bool detach_spawn_posix() {
    auto pid = fork();
    if (pid < 0) {
      print_error("fork() failed");
      return false;
    }
    if (pid == 0) {
      // Double-fork: the intermediate child exits immediately so the
      // grandchild is re-parented to init (PID 1), avoiding a zombie.
      auto pid2 = fork();
      if (pid2 < 0) {
        _Exit(1);
      }
      if (pid2 == 0) {
        // Grandchild: detach from terminal and exec.
        if (setsid() < 0) {
          _Exit(1);
        }
        execute_command_in_child();
      }
      _Exit(0);
    }
    // Reap the intermediate child immediately.
    int status;
    waitpid(pid, &status, 0);
    return true;
  }
#endif

  void prepare_for_child() {
    stdin_.prepare_for_child();
    stdout_.prepare_for_child();
    stderr_.prepare_for_child();
  }
  void close_child_end() {
    stdin_.close_child_end();
    stdout_.close_child_end();
    stderr_.close_child_end();
  }

#if !defined(_WIN32)
  void execute_command_in_child() {
    stdin_.close_parent_end();
    stdout_.close_parent_end();
    stderr_.close_parent_end();

    std::vector<char*> cmd{app_.data()};
    std::ranges::transform(args_, std::back_inserter(cmd),
                           [](std::string& s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!cwd_.empty() && (-1 == ::chdir(cwd_.data()))) {
      print_error("chdir failed: " + get_last_error_message() + "\n");
      _Exit(126);
    }

    std::string executable_path = app_;
    const auto resolved_path = find_executable(executable_path);
    if (resolved_path.has_value()) {
      executable_path = resolved_path.value();
    }

    if (!env_.empty()) {
      std::vector<std::string> env_tmp{};

      std::ranges::transform(
          env_, std::back_inserter(env_tmp),
          [](auto& entry) { return entry.first + "=" + entry.second; });

      std::vector<char*> envs{};
      std::ranges::transform(env_tmp, std::back_inserter(envs),
                             [](auto& s) { return s.data(); });
      envs.push_back(nullptr);
      execve(executable_path.c_str(), cmd.data(), envs.data());
      print_error("execve(" + executable_path +
                  ") failed: " + get_last_error_message() + "\n");
    } else {
      execv(executable_path.c_str(), cmd.data());
      print_error("execv(" + executable_path +
                  ") failed: " + get_last_error_message() + "\n");
    }
    _Exit(127);
  }
#endif  // !_WIN32

  NativeString app_;
  std::vector<NativeString> args_;
  NativeString cwd_;
  std::map<NativeString, NativeString> env_;
  StdinRedirector stdin_;
  StdoutRedirector stdout_;
  StderrRedirector stderr_;
  std::optional<std::chrono::milliseconds> timeout_{std::nullopt};
#if defined(_WIN32)
  std::vector<unsigned char> proc_thread_attr_list_;
  bool newgroup_{false};
  std::vector<wchar_t> (*argv_to_command_line_fn)(
      std::wstring,
      std::vector<std::wstring> const&) = &detail::argv_to_command_line;
  std::shared_ptr<unique_fd> job_handle_{std::make_shared<unique_fd>()};
#else
  std::optional<pid_t> requested_pgid_{std::nullopt};
#endif
};

class pipeline {
 public:
  explicit pipeline(builder&& sub) { builders_.push_back(std::move(sub)); }
  pipeline(pipeline&&) = default;
  pipeline& operator=(pipeline&&) = default;
  pipeline(pipeline const&) = delete;
  pipeline& operator=(pipeline const&) = delete;
  pipeline& append(builder&& sub) {
    builders_.push_back(std::move(sub));
    return *this;
  }

  ~pipeline() { terminate(); }

  int run() {
    std::vector<Pipe> pipes;

    for (auto it = builders_.begin(); it != builders_.end() - 1; ++it) {
      pipes.push_back(Pipe::create());
      it->stdout_ = (named_args::std_out > pipes.back());
      (it + 1)->stdin_ = (named_args::std_in < pipes.back());
    }
    for (auto it = builders_.begin(); it != builders_.end(); ++it) {
#if defined(_WIN32)
      if (it != builders_.begin()) {
        it->job_handle_ = builders_.begin()->job_handle_;
      }
#else
      if (it == builders_.begin()) {
        it->requested_pgid_ = 0;
      } else {
        it->requested_pgid_ = subs_.begin()->pid();
      }
#endif
      subs_.emplace_back(it->spawn());
      auto& sub = subs_.back();
      if (!sub) {
        for (auto& p : pipes) {
          p.close_all();
        }
        break;
      }
    }
    for (auto& sub : subs_) {
      exit_codes_.push_back(sub.wait());
    }
    return exit_codes_.back();
  }

  void terminate() {
    if (subs_.empty()) {
      return;
    }
    subs_[0].terminate();
  }

  int exit_code() const { return exit_codes_.back(); }
  [[nodiscard]] std::vector<int> exit_codes() const { return exit_codes_; }

 private:
  std::vector<builder> builders_;
  std::vector<process> subs_;
  std::vector<int> exit_codes_;
};

inline pipeline operator|(builder&& lhs, builder&& rhs) {
  pipeline subs(std::move(lhs));
  subs.append(std::move(rhs));
  return subs;
}

inline pipeline operator|(pipeline lhs, builder&& rhs) {
  lhs.append(std::move(rhs));
  return lhs;
}
}  // namespace detail

using detail::buffer;

namespace named_arguments {
#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
using detail::named_args::$bash;
using detail::named_args::$cwd;
using detail::named_args::$devnull;
using detail::named_args::$devttyin;
using detail::named_args::$devttyout;
using detail::named_args::$env;
using detail::named_args::$newgroup;
using detail::named_args::$shell;
using detail::named_args::$stderr;
using detail::named_args::$stdin;
using detail::named_args::$stdout;
using detail::named_args::$timeout;
using detail::named_args::$timeout_infinite;
#if defined(_WIN32)
using detail::named_args::$powershell;
#endif
#endif
using detail::named_args::bash;
using detail::named_args::cwd;
using detail::named_args::devnull;
using detail::named_args::devttyin;
using detail::named_args::devttyout;
using detail::named_args::env;
using detail::named_args::newgroup;
using detail::named_args::shell;
using detail::named_args::std_err;
using detail::named_args::std_in;
using detail::named_args::std_out;
using detail::named_args::timeout;
using detail::named_args::timeout_infinite;
#if defined(_WIN32)
using detail::named_args::powershell;
#endif
}  // namespace named_arguments

template <detail::string_like_type T, detail::named_argument_type... Args>
inline int run(T&& app, std::vector<detail::to_string_t<T>> args,
               Args&&... named_args) {
  return detail::builder(detail::to_string_t<T>(std::forward<T>(app)),
                         std::move(args), std::forward<Args>(named_args)...)
      .run();
}

namespace detail {
template <typename... Args>
concept partition_args =
    []<size_t... I, size_t... N>(std::index_sequence<I...>,
                                 std::index_sequence<N...>) constexpr {
      return ((string_like_type<std::tuple_element_t<
                   I, std::tuple<std::decay_t<Args>...>>>) &&
              ...) &&
             ((named_argument_type<std::tuple_element_t<
                   N, typename detail::named_arg_type_list_t<
                          std::decay_t<Args>...>>>) &&
              ...);
    }(std::make_index_sequence<
          std::tuple_size_v<std::tuple<Args...>> -
          std::tuple_size_v<
              typename detail::named_arg_type_list_t<std::decay_t<Args>...>>>{},
      std::make_index_sequence<std::tuple_size_v<
          typename detail::named_arg_type_list_t<std::decay_t<Args>...>>>{});
}  // namespace detail

template <detail::string_like_type T, detail::run_args_type... Args>
  requires detail::partition_args<Args...>
inline int run(T&& app, Args... args) {
  std::tuple<Args...> args_tuple{std::move(args)...};
  using NamedArgTypeList =
      typename detail::named_arg_type_list_t<std::decay_t<Args>...>;
  return [&app, &args_tuple]<size_t... I, size_t... N>(
             std::index_sequence<I...>, std::index_sequence<N...>) {
    return run(detail::to_string_t<T>(std::forward<T>(app)),
               std::vector<detail::to_string_t<T>>{
                   detail::convert_to_string<detail::get_char_type_t<T>>(
                       std::move(std::get<I>(args_tuple)))...},
               std::move(std::get<std::tuple_size_v<std::tuple<Args...>> -
                                  std::tuple_size_v<NamedArgTypeList> + N>(
                   args_tuple))...);
  }(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>> -
                             std::tuple_size_v<NamedArgTypeList>>{},
         std::make_index_sequence<std::tuple_size_v<NamedArgTypeList>>{});
}

template <detail::string_like_type Command, detail::named_argument_type... Args>
inline int run(detail::Shell s, Command&& command, Args&&... args) {
  return detail::builder(
             s, detail::to_string_t<Command>(std::forward<Command>(command)),
             std::forward<Args>(args)...)
      .run();
}

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
template <detail::string_like_type T, detail::named_argument_type... Args>
inline int $(T&& app, std::vector<detail::to_string_t<T>> args,
             Args&&... named_args) {
  return run(std::forward<T>(app), std::move(args),
             std::forward<Args>(named_args)...);
}

template <detail::run_args_type... Args>
inline int $(Args... args) {
  return run(std::forward<Args>(args)...);
}
#endif  // USE_DOLLAR_NAMED_VARIABLES

template <detail::string_like_type T,
          detail::named_argument_for_capture_type... Args>
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    T&& app, std::vector<detail::to_string_t<T>> args, Args&&... named_args) {
  using namespace named_arguments;
  using namespace detail;
  std::tuple<int, buffer, buffer> result;
  auto& [exit_code_, std_out_, std_err_] = result;
  exit_code_ =
      run(detail::to_string_t<T>(std::forward<T>(app)), std::move(args),
          StdinRedirector(File{devnull, File::OpenType::ReadOnly}),
          std::forward<Args>(named_args)..., StdoutRedirector{std_out_},
          StderrRedirector{std_err_}, Newgroup{true});
  return result;
}

template <detail::string_like_type T, detail::capture_run_args_type... Args>
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    T&& app, Args... args) {
  using namespace named_arguments;
  std::tuple<Args...> args_tuple{std::move(args)...};
  using NamedArgTypeList =
      typename detail::named_arg_type_list_t<std::decay_t<Args>...>;

  return [&app, &args_tuple]<size_t... I, size_t... N>(
             std::index_sequence<I...>, std::index_sequence<N...>) {
    return capture_run(
        detail::to_string_t<T>(std::forward<T>(app)),
        std::vector<detail::to_string_t<T>>{
            detail::convert_to_string<detail::get_char_type_t<T>>(
                std::move(std::get<I>(args_tuple)))...},
        std::move(
            std::get<std::tuple_size_v<std::tuple<Args...>> -
                     std::tuple_size_v<NamedArgTypeList> + N>(args_tuple))...);
  }(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>> -
                             std::tuple_size_v<NamedArgTypeList>>{},
         std::make_index_sequence<std::tuple_size_v<NamedArgTypeList>>{});
}

template <detail::string_like_type Command,
          detail::capture_run_args_type... Args>
inline std::tuple<int, subprocess::buffer, subprocess::buffer> capture_run(
    detail::Shell s, Command&& command, Args&&... args) {
  using namespace named_arguments;
  using namespace detail;
  std::tuple<int, buffer, buffer> result;
  auto& [exit_code_, std_out_, std_err_] = result;
  exit_code_ =
      detail::builder(
          s, detail::to_string_t<Command>(std::forward<Command>(command)),
          StdinRedirector(File{devnull, File::OpenType::ReadOnly}),
          std::forward<Args>(args)..., StdoutRedirector{std_out_},
          StderrRedirector{std_err_}, Newgroup{true})
          .run();
  return result;
}

template <detail::string_like_type T,
          detail::named_argument_for_detach_type... Args>
inline bool detach_run(T app, std::vector<detail::to_string_t<T>> args,
                       Args&&... named_args) {
  using namespace named_arguments;
  using namespace detail;
  return detail::builder(
             detail::to_string_t<T>(std::forward<T>(app)), std::move(args),
             std::forward<Args>(named_args)...,
             StdinRedirector(File{devnull, File::OpenType::ReadOnly}),
             StdoutRedirector(File{devnull, File::OpenType::WriteTruncate}),
             StderrRedirector(File{devnull, File::OpenType::WriteTruncate}))
      .detach_spawn();
}

template <detail::string_like_type T, detail::detach_run_args_type... Args>
inline bool detach_run(T&& app, Args... args) {
  using namespace named_arguments;
  std::tuple<Args...> args_tuple{std::move(args)...};
  using NamedArgTypeList =
      typename detail::named_arg_type_list_t<std::decay_t<Args>...>;

  return [&app, &args_tuple]<size_t... I, size_t... N>(
             std::index_sequence<I...>, std::index_sequence<N...>) {
    return detach_run(
        detail::to_string_t<T>(std::forward<T>(app)),
        std::vector<detail::to_string_t<T>>{
            detail::convert_to_string<detail::get_char_type_t<T>>(
                std::move(std::get<I>(args_tuple)))...},
        std::move(
            std::get<std::tuple_size_v<std::tuple<Args...>> -
                     std::tuple_size_v<NamedArgTypeList> + N>(args_tuple))...);
  }(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>> -
                             std::tuple_size_v<NamedArgTypeList>>{},
         std::make_index_sequence<std::tuple_size_v<NamedArgTypeList>>{});
}
template <detail::string_like_type Command,
          detail::detach_run_args_type... Args>
inline bool detach_run(detail::Shell s, Command&& command, Args&&... args) {
  using namespace named_arguments;
  using namespace detail;
  return detail::builder(
             s, detail::to_string_t<Command>(std::forward<Command>(command)),
             std::forward<Args>(args)...,
             StdinRedirector(File{devnull, File::OpenType::ReadOnly}),
             StdoutRedirector(File{devnull, File::OpenType::WriteTruncate}),
             StderrRedirector(File{devnull, File::OpenType::WriteTruncate}))
      .detach_spawn();
}

}  // namespace subprocess

#if defined(USE_DOLLAR_NAMED_VARIABLES) && USE_DOLLAR_NAMED_VARIABLES
using subprocess::$;
using subprocess::named_arguments::$bash;
using subprocess::named_arguments::$cwd;
using subprocess::named_arguments::$devnull;
using subprocess::named_arguments::$devttyin;
using subprocess::named_arguments::$devttyout;
using subprocess::named_arguments::$env;
using subprocess::named_arguments::$newgroup;
using subprocess::named_arguments::$shell;
using subprocess::named_arguments::$stderr;
using subprocess::named_arguments::$stdin;
using subprocess::named_arguments::$stdout;
using subprocess::named_arguments::$timeout;
using subprocess::named_arguments::$timeout_infinite;
#if defined(_WIN32)
using subprocess::named_arguments::$powershell;
#endif
#endif

#endif  // __SUBPROCESS_SUBPROCESS_HPP__
