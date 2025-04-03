#ifndef __SUBPROCESS_HPP__
#define __SUBPROCESS_HPP__

#if __cplusplus < 201703L
#error "This code requires C++17 or later."
#endif

#pragma once
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

// POSIX (Linux, macOS, etc.) implementation
extern char **environ;

namespace process {

using std::nullptr_t;
using data_container = std::vector<char>;
using path_t = std::filesystem::path;

inline path_t search_path(std::string const &exe_file) {
#ifdef _WIN32
  char separator = '\\';
  char path_env_sep = ';';
#else
  char separator = '/';
  char path_env_sep = ':';
#endif
  auto split_env = [](const char *env, char sep) {
    std::istringstream stream;
    stream.str(getenv(env));
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
      if (exe_file.find('.') == std::string::npos) {
        if (auto f = path_t(p) / exe_file; exists(f)) {
          return f;
        } else {
          for (auto &ext : path_exts) {
            if (auto f = path_t(p) / (exe_file + ext); exists(f)) {
              return f;
            }
          }
        }
      } else {
        auto f = path_t(p) / exe_file;
        if (exists(f)) {
          return f;
        }
      }
#else
      auto f = path_t(p) / exe_file;
      if (0 == access(f.c_str(), X_OK)) {
        return f;
      }
#endif
    }
  }
  return {};
}

inline std::map<std::string, std::string> environments() {
  std::map<std::string, std::string> envMap;

#ifdef _WIN32
  // Windows implementation
  char *envBlock = GetEnvironmentStrings();
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

  FreeEnvironmentStrings(envBlock);

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

class Stdio {
  friend class subprocess;
  friend struct stdin_operator;
  friend struct stdout_operator;

 public:
  Stdio(int fileno) : fileno(fileno) {}
  Stdio &operator=(Stdio const &s) {
    io = s.io;
    append = s.append;
    return *this;
  }
  const int fileno;

 private:
  std::variant<nullptr_t, int, std::reference_wrapper<data_container>, path_t>
      io{nullptr};
  bool append{false};
  int pipe_fds[2];
};

struct Cwd {
  path_t cwd;
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

struct stdin_operator {
  Stdio operator<(int fd) {
    Stdio ret{this->fileno};
    ret.io = fd;
    return ret;
  }
  Stdio operator<(data_container &c) {
    Stdio ret{this->fileno};
    ret.io = std::ref(c);
    return ret;
  }
  Stdio operator<(path_t const &f) {
    Stdio ret{this->fileno};
    ret.io = f;
    return ret;
  }
  int fileno{-1};
};

struct stdout_operator {
  Stdio operator>(int fd) {
    Stdio ret{this->fileno};
    ret.io = fd;
    return ret;
  }
  Stdio operator>(data_container &c) {
    Stdio ret{this->fileno};
    ret.io = std::ref(c);
    return ret;
  }
  Stdio operator>(path_t const &f) {
    Stdio ret{this->fileno};
    ret.io = f;
    return ret;
  }
  Stdio operator>>(path_t const &f) {
    Stdio ret{this->fileno};
    ret.io = f;
    ret.append = true;
    return ret;
  }
  int fileno{-1};
};

struct cwd_operator {
  Cwd operator=(std::string const &p) { return Cwd{p}; }
};
struct env_operator {
  Env operator=(std::map<std::string, std::string> env) {
    return Env{std::move(env)};
  }
  Env operator+=(std::map<std::string, std::string> env) {
    std::map<std::string, std::string> env_tmp{environments()};
    env_tmp.insert(env.begin(), env.end());
    return Env{std::move(env_tmp)};
  }
  EnvItemAppend operator[](std::string key) { return EnvItemAppend{{key, ""}}; }
};

[[maybe_unused]] auto static devnull = path_t("/dev/null");
[[maybe_unused]] static stdin_operator std_in{0};
[[maybe_unused]] static stdout_operator std_out{1};
[[maybe_unused]] static stdout_operator std_err{2};
[[maybe_unused]] static cwd_operator cwd;
[[maybe_unused]] static env_operator env;

class subprocess {
  template <typename... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };
  template <typename... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;

 public:
  subprocess(std::vector<std::string> cmd, Stdio in = Stdio{0},
             Stdio out = Stdio{1}, Stdio err = Stdio{2}, std::string cwd = {},
             std::map<std::string, std::string> env = {})
      : _cmd{cmd},
        _cwd{cwd},
        _env{env},
        _stdin{in},
        _stdout{out},
        _stderr{err} {}

  int run() {
    setup();
    auto pid = fork();
    if (pid < 0) {
      throw std::runtime_error("fork failed");
    } else if (pid == 0) {
      child_run();
      return 0;
    } else {
      return wait_child(pid);
    }
  }

 private:
  void setup() {
    std::visit(
        overloaded{[](nullptr_t) {}, [](int) {},
                   [this](std::reference_wrapper<data_container> &) {
                     if (-1 == pipe(this->_stdin.pipe_fds)) {
                       throw std::runtime_error{"pipe failed"};
                     }
                   },
                   [this](path_t &f) {
                     auto fd = open(f.c_str(), O_RDONLY);
                     if (fd == -1) {
                       throw std::runtime_error{"open failed: " + f.string()};
                     }
                     this->_stdin.io = fd;
                   }},
        _stdin.io);

    std::visit(overloaded{[](nullptr_t) {}, [](int) {},
                          [this](std::reference_wrapper<data_container> &) {
                            if (-1 == pipe(this->_stdout.pipe_fds)) {
                              throw std::runtime_error{"pipe failed"};
                            }
                          },
                          [this](path_t &f) {
                            auto fd = open(f.c_str(),
                                           O_WRONLY | O_CREAT | O_TRUNC, 0666);
                            this->_stdout.io = fd;
                          }},
               _stdout.io);

    std::visit(
        overloaded{[](nullptr_t) {}, [](int) {},
                   [this](std::reference_wrapper<data_container> &) {
                     if (-1 == pipe(this->_stderr.pipe_fds)) {
                       throw std::runtime_error{"pipe failed"};
                     }
                   },
                   [this](path_t &f) {
                     auto fd = open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                    S_IWUSR | S_IRUSR);
                     if (fd == -1) {
                       throw std::runtime_error{"open failed: " + f.string()};
                     }
                     this->_stderr.io = fd;
                   }},
        _stderr.io);
  }
  int wait_child(int pid) {
    std::visit(overloaded{[](nullptr_t) {}, [](int fd) {},
                          [this](std::reference_wrapper<data_container> &) {
                            close(this->_stdin.pipe_fds[0]);
                          },
                          [](path_t &f) {}},
               _stdin.io);

    std::visit(overloaded{[](nullptr_t) {}, [](int fd) {},
                          [this](std::reference_wrapper<data_container> &) {
                            close(this->_stdout.pipe_fds[1]);
                          },
                          [](path_t &f) {}},
               _stdout.io);

    std::visit(overloaded{[](nullptr_t) {}, [](int) {},
                          [this](std::reference_wrapper<data_container> &) {
                            close(this->_stderr.pipe_fds[1]);
                          },
                          [](path_t &f) {}},
               _stderr.io);

    struct pollfd fds[3]{{-1, POLLOUT}, {-1, POLLIN}, {-1, POLLIN}};
    std::string_view stdin_str{};
    if (std::holds_alternative<std::reference_wrapper<data_container>>(
            _stdin.io)) {
      fds[0].fd = _stdin.pipe_fds[1];
      auto &tmp =
          std::get<std::reference_wrapper<data_container>>(_stdin.io).get();
      stdin_str = {tmp.data(), tmp.size()};
    }
    if (std::holds_alternative<std::reference_wrapper<data_container>>(
            _stdout.io)) {
      fds[1].fd = _stdout.pipe_fds[0];
    }
    if (std::holds_alternative<std::reference_wrapper<data_container>>(
            _stderr.io)) {
      fds[2].fd = _stderr.pipe_fds[0];
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
            stdin_str.remove_prefix(len);
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
          auto &tmp =
              std::get<std::reference_wrapper<data_container>>(_stdout.io)
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
          auto &tmp =
              std::get<std::reference_wrapper<data_container>>(_stderr.io)
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
  }
  void child_run() {
    std::visit(
        overloaded{[](nullptr_t) {}, [](int fd) { dup2(fd, STDIN_FILENO); },
                   [this](std::reference_wrapper<data_container> &) {
                     close(this->_stdin.pipe_fds[1]);
                     dup2(this->_stdin.pipe_fds[0], STDIN_FILENO);
                   },
                   [](path_t &f) {}},
        _stdin.io);

    std::visit(
        overloaded{[](nullptr_t) {}, [](int fd) { dup2(fd, STDOUT_FILENO); },
                   [this](std::reference_wrapper<data_container> &) {
                     close(this->_stdout.pipe_fds[0]);
                     dup2(this->_stdout.pipe_fds[1], STDOUT_FILENO);
                   },
                   [](path_t &f) {}},
        _stdout.io);

    std::visit(
        overloaded{[](nullptr_t) {}, [](int fd) { dup2(fd, STDERR_FILENO); },
                   [this](std::reference_wrapper<data_container> &) {
                     close(this->_stderr.pipe_fds[0]);
                     dup2(this->_stderr.pipe_fds[1], STDERR_FILENO);
                   },
                   [](path_t &f) {}},
        _stderr.io);
    std::vector<char *> cmd{};
    std::transform(_cmd.begin(), _cmd.end(), std::back_inserter(cmd),
                   [](std::string &s) { return s.data(); });
    cmd.push_back(nullptr);
    if (!_cwd.empty() && (-1 == chdir(_cwd.data()))) {
      throw std::runtime_error("chdir failed: " + _cwd);
    }
    auto exe_file = _cmd[0];
    if (exe_file.find('\\') == std::string::npos) {
      auto exe_path = search_path(exe_file).string();
      if (!exe_path.empty()) {
        exe_file = exe_path;
      }
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
      execve(exe_file.c_str(), cmd.data(), envs.data());
    } else {
      execv(exe_file.c_str(), cmd.data());
    }
    throw std::runtime_error(std::string("execv failed: ") + strerror(errno));
  }

 private:
  std::vector<std::string> _cmd;
  std::string _cwd{};
  std::map<std::string, std::string> _env;
  Stdio _stdin{0};
  Stdio _stdout{1};
  Stdio _stderr{2};
};

template <typename... T>
int run(std::vector<std::string> cmd, T... args) {
  Stdio stdin{0};
  Stdio stdout{1};
  Stdio stderr{2};
  std::string cwd;
  std::map<std::string, std::string> env;
  std::vector<std::pair<std::string, std::string>> env_appends;
  (void)(..., ([&](auto arg) {
           using ArgType = std::decay_t<decltype(arg)>;
           if constexpr (std::is_same_v<ArgType, Stdio>) {
             if (arg.fileno == 0) {
               stdin = arg;
             } else if (arg.fileno == 1) {
               stdout = arg;
             } else if (arg.fileno == 2) {
               stderr = arg;
             }
           } else if constexpr (std::is_same_v<ArgType, Env>) {
             env.insert(arg.env.begin(), arg.env.end());
           } else if constexpr (std::is_same_v<ArgType, EnvItemAppend>) {
             env_appends.push_back(arg.kv);
           } else if constexpr (std::is_same_v<ArgType, Cwd>) {
             cwd = arg.cwd.string();
           } else {
             static_assert(false,
                           "Invalid argument type passed to run function.");
           }
         }(args)));
  return subprocess(cmd, stdin, stdout, stderr, cwd, env).run();
}

}  // namespace process
#endif  // __SUBPROCESS_HPP__
