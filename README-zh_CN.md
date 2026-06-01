# subprocess.hpp

[![CMAKE](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![MSYS2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

一个用于在 C++ 中运行和管理子进程的现代、跨平台、仅头文件的 C++20 库。它的设计灵感来源于 Python 的 `subprocess` 模块和 shell 的语法，旨在提供一个简单、直观且强大的 API。

基于 **MIT 许可证** 发布。

## ✨ 核心特性

- **仅头文件**：只需将 `include/subprocess/subprocess.hpp` 包含到你的项目中即可。
- **跨平台**：在 Windows、Linux 和 macOS 上均可工作。
- **C++20 标准**：利用现代 C++ 特性（concepts、`std::span`、designated initializers 等）提供更简洁的语法和编译期类型安全。
- **易于使用**：API 设计简单直观，类似 shell 命令。
- **强大的 I/O 重定向**：
  - 轻松将 `stdin`、`stdout`、`stderr` 重定向到文件、`/dev/null` 或内存中的缓冲区。
  - 支持 `>`（截断）和 `>>`（追加）操作符。
- **灵活的环境变量控制**：
  - 为子进程完全替换环境变量。
  - 在现有环境的基础上添加或修改变量。
  - 向特定变量（如 `PATH`）追加或前插值。
- **捕获输出**：`capture_run` 函数可以轻松捕获子进程的 `stdout` 和 `stderr`。
- **Shell 支持**：通过 `Shell` 类使用 `bash`、`sh`、`cmd.exe` 或 `powershell` 执行命令。
- **分离进程**：使用 `detach_run` 创建守护/后台进程。
- **链式管道**：使用 `operator|` 创建类似 `cmd1 | cmd2 | cmd3` 的进程管道。
- **类型安全**：利用 C++20 concepts 在编译时验证参数。
- **宽字符串支持**：在所有平台上完整支持 `std::string`、`std::wstring`、`std::string_view`、`std::wstring_view`、`std::filesystem::path` 以及原生字符串指针。

## 🚀 接入到现有工程

### 方式一：直接包含（推荐）

这是一个仅头文件的库，最简单的方式是直接将 `include` 目录复制到你的项目中，然后包含头文件：

```cpp
#include "subprocess/subprocess.hpp"
```

### 方式二：使用 CMake `add_subdirectory`

你可以将此仓库作为子模块或直接下载到你的项目目录中，然后在你的 `CMakeLists.txt` 中添加：

```cmake
# 将 subprocess.hpp 添加到你的项目中
add_subdirectory(path/to/subprocess.hpp)

# 将其链接到你的目标
target_link_libraries(your_target PRIVATE subprocess)
```

### 方式三：使用 CMake `FetchContent`

如果你的项目使用 CMake 3.11 或更高版本，你可以使用 `FetchContent` 模块自动下载并集成此库。

```cmake
include(FetchContent)

FetchContent_Declare(
  subprocess
  GIT_REPOSITORY https://github.com/shediao/subprocess.hpp.git
  GIT_TAG main # 或指定的 commit/tag
)

FetchContent_MakeAvailable(subprocess)

target_link_libraries(your_target PRIVATE subprocess)
```

## 📖 基本使用参考

### 引入命名空间

为了方便使用，建议引入以下命名空间。本文档中的所有示例都假定已进行此操作。

```cpp
#include <subprocess/subprocess.hpp>

// 核心函数
using subprocess::run;
using subprocess::capture_run;
using subprocess::detach_run;

// 方便的别名，类似 shell
using subprocess::$;

// 命名参数
using namespace subprocess::named_arguments;
```

### 1. 执行简单命令

直接传递命令和参数，`run` 函数会返回子进程的退出码。支持所有字符串类类型。

```cpp
// 等同于 shell: ls -l /tmp
int exit_code = run("ls", "-l", "/tmp");

// 支持 std::string, wstring, string_view, wstring_view, filesystem::path 等类型
std::string cmd = "ls";
std::string arg = "-l";
int code = run(cmd, arg, "/tmp");

// 在 Windows 上也支持宽字符串
// run(L"cmd", L"/c", L"echo hello");
```

### 2. 捕获输出

使用 `capture_run` 函数可以方便地捕获 `stdout` 和 `stderr` 的输出。

```cpp
// capture_run 返回一个包含 [exit_code, stdout_buffer, stderr_buffer] 的 tuple
auto [exit_code, out, err] = capture_run("echo", "Hello, Subprocess!");

if (exit_code == 0) {
    std::cout << "Stdout: " << out.to_string() << std::endl;
}
```

### 3. I/O 重定向

API 设计得像 shell 一样直观。

```cpp
// 将 stdin 从文件读入
run("cat", $stdin < "input.txt");

// 将 stdout 重定向到文件（截断模式）
run("ls", "-l", $stdout > "output.txt");

// 将 stderr 重定向到文件（追加模式）
run("ls", "-l", "/non_existent_dir", $stderr >> "errors.log");

// 将所有输出重定向到 /dev/null（或 Windows 上的 NUL）
run("some_command", $stdout > $devnull, $stderr > $devnull);

// 使用内存缓冲区
subprocess::buffer inbuf{"hello world"};
subprocess::buffer outbuf;
run("cat", $stdin < inbuf, $stdout > outbuf);
```

### 4. 设置工作目录

使用 `$cwd` 参数来指定子进程的当前工作目录。

```cpp
// 在 /tmp 目录中执行 ls
auto [code, out, err] = capture_run("ls", $cwd = "/tmp");
```

### 5. 管理环境变量

使用 `$env` 参数来灵活地管理子进程的环境变量。

```cpp
// 完全覆盖环境变量
run("printenv", $env = {{"MY_VAR", "123"}});

// 在现有环境的基础上添加/修改变量
run("printenv", $env += {{"NEW_VAR", "hello"}});

// 向 PATH 环境变量追加路径
// （路径分隔符会自动处理：Windows 用 ';'，Unix 用 ':'）
run("my_program", $env["PATH"] += "/opt/my_app/bin");

// 向 PATH 环境变量前插路径
run("my_program", $env["PATH"] <<= "/usr/local/bin");
```

### 6. 超时控制

使用 `$timeout` 参数为子进程设置超时时间。超时后整个进程树会被终止。

```cpp
// 5 秒超时（使用整数表示秒）
run("some_command", $timeout = 5);

// 使用 chrono 毫秒超时
using namespace std::chrono_literals;
run("some_command", $timeout = 5000ms);

// 设置为无超时（默认）
run("some_command", $timeout = $timeout_infinite);
```

### 7. 进程组控制

使用 `$newgroup` 参数控制是否将子进程放入新的进程组 / Job Object。启用时，超时终止会终止整个进程树。

```cpp
run("some_command", $newgroup = true);
```

> **注意**：`capture_run` 内部默认启用 `$newgroup`，因此无需手动指定。

### 8. Shell 执行

通过 shell 解释器执行命令。库提供了 `Shell::Bash()`、`Shell::Posix()`（`sh`）、`Shell::Cmd()`（Windows `cmd.exe`）和 `Shell::Powershell()`（Windows PowerShell）。

```cpp
// 通过 bash 执行
run(Shell::Bash(), "echo $HOME && ls -la");

// 通过 sh（POSIX shell）执行
run(Shell::Posix(), "echo $SHELL");

// Windows：通过 cmd.exe 执行
// run(Shell::Cmd(), "echo %PATH% & exit /b 0");

// Windows：通过 PowerShell 执行
// run(Shell::Powershell(), "Get-ChildItem | Select-Object Name");

// Shell 执行也支持 capture_run 和 detach_run
auto [code, out, err] = capture_run(Shell::Bash(), "ls -l /tmp");
detach_run(Shell::Bash(), "my_daemon --foreground");
```

预构建的 shell 常量可作为命名参数使用：

```cpp
using namespace subprocess::named_arguments;

run($bash, "echo hello");       // bash -c "echo hello"
run($shell, "dir");             // sh -c "dir"（Unix）或 cmd.exe /d /s /c "dir"（Windows）
// run($powershell, "ls");      // PowerShell（仅 Windows）
```

### 9. 分离（守护）进程

使用 `detach_run` 创建与父进程分离的后台进程。子进程独立运行，标准 I/O 重定向到 `/dev/null`（或 Windows 上的 `NUL`）。

```cpp
// 创建分离进程
bool success = detach_run("my_daemon", "--foreground");

// 指定工作目录
detach_run("my_daemon", $cwd = "/var/run/myapp");

// 自定义环境变量
detach_run("my_daemon", $env += {{"DAEMON_MODE", "1"}});
```

> **注意**：在 Unix 上，`detach_run` 使用 double-fork 技术并调用 `setsid()` 来创建真正的守护进程。在 Windows 上，使用 `DETACHED_PROCESS` 和 `CREATE_NEW_PROCESS_GROUP`。

### 10. 管道

使用 `operator|` 将多个子进程串联起来，就像 shell 管道一样。

```cpp
using subprocess::detail::builder;

// 等同于：ls -l | grep ".cpp" | wc -l
auto pipeline = builder("ls", {"-l"}) | builder("grep", {".cpp"}) | builder("wc", {"-l"});
int exit_code = pipeline.run();

// 管道返回最后一个进程的退出码
std::cout << "Exit code: " << exit_code << std::endl;

// 你也可以获取所有进程的退出码
auto pipe = builder("cmd1", {}) | builder("cmd2", {});
pipe.run();
auto all_codes = pipe.exit_codes();  // 返回 vector<int>，包含所有退出码
```

管道会自动：

- 通过管道将每个进程的 `stdout` 连接到下一个进程的 `stdin`。
- 在 Windows 上，所有进程共享一个 Job Object。
- 在 Unix 上，创建共享的进程组，信号会传递给所有成员。

## 📚 API 接口说明

### 核心函数

| 函数                                                                         | 说明                                                                                 |
| ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| `int run(app, args..., named_args...)`                                       | 执行命令并等待完成，返回退出码。接受可变数量的字符串类参数后跟命名参数。             |
| `int run(Shell, command, named_args...)`                                     | 通过 shell 解释器执行命令。                                                          |
| `int $(...)`                                                                 | `run` 的便捷别名。由 `USE_DOLLAR_NAMED_VARIABLES` 宏控制（默认启用）。               |
| `std::tuple<int, buffer, buffer> capture_run(app, args..., named_args...)`   | 执行命令，将 `stdout` 和 `stderr` 捕获到缓冲区。返回 `[exit_code, stdout, stderr]`。 |
| `std::tuple<int, buffer, buffer> capture_run(Shell, command, named_args...)` | `capture_run` 的 shell 版本。                                                        |
| `bool detach_run(app, args..., named_args...)`                               | 创建分离/守护进程。成功返回 `true`。                                                 |
| `bool detach_run(Shell, command, named_args...)`                             | `detach_run` 的 shell 版本。                                                         |

### 命名参数 (Named Arguments)

命名参数用于控制进程执行的各个方面。通过 C++20 concepts 在编译时解析。

- **`$stdin < source`**：从 `source` 重定向标准输入。
  - `source` 可以是：
    - `std::string` / `std::wstring`：文件路径。
    - `subprocess::buffer&`：内存中的缓冲区引用。
    - `$devnull`：系统空设备。

- **`$stdout > target` / `$stderr > target`**：将标准输出/错误重定向到 `target`（截断模式）。
- **`$stdout >> target` / `$stderr >> target`**：将标准输出/错误重定向到 `target`（追加模式）。
  - `target` 可以是：
    - `std::string` / `std::wstring`：文件路径。
    - `subprocess::buffer&`：内存中的缓冲区引用。
    - `$devnull`：系统空设备。

- **`$cwd = path`**：在指定的 `path`（类型为 `std::string`、`std::wstring`、`std::string_view`、`std::wstring_view`）中执行命令。

- **`$env = map`**：完全使用 `map` 中的键值对作为子进程的环境变量。
  - `map` 类型：`std::map<std::string, std::string>`（Windows 上也可用 `std::map<std::wstring, std::wstring>`）。

- **`$env += map`**：将 `map` 中的键值对添加到当前的环境变量中。如果变量已存在，则覆盖。

- **`$env["VAR"] += value`**：将 `value` 追加到名为 `VAR` 的环境变量的末尾（主要用于 `PATH` 等）。路径分隔符会自动处理。
- **`$env["VAR"] <<= value`**：将 `value` 前插到名为 `VAR` 的环境变量的开头。

- **`$timeout = duration`**：设置子进程超时时间。
  - `duration` 可以是：
    - `int`：秒数，如 `$timeout = 5`。
    - `std::chrono::milliseconds`：毫秒数，如 `$timeout = 5000ms`。
    - `$timeout_infinite`：禁用超时（默认）。

- **`$newgroup = bool`**：是否将子进程放入新的进程组 / Job Object。超时终止时会终止整个进程树。

- **设备常量**：
  - `$devnull`：系统空设备（Unix 上为 `/dev/null`，Windows 上为 `NUL`）。
  - `$devttyout`：标准输出终端设备（Unix 上为 `/dev/tty`，Windows 上为 `CONOUT$`）。
  - `$devttyin`：标准输入终端设备（Unix 上为 `/dev/tty`，Windows 上为 `CONIN$`）。

- **Shell 常量**：
  - `$bash`：`bash -c`（跨平台）。
  - `$shell`：Unix 上为 `sh -c`，Windows 上为 `cmd.exe /d /s /c`。
  - `$powershell`：`powershell -NoProfile -Command`（仅 Windows）。

### `subprocess::buffer` 类型

一个简单的缓冲区类，用于和子进程进行数据交换。

```cpp
// 构造
buffer buf1;                                    // 空缓冲区
buffer buf2("hello");                           // 从 string_view 创建
buffer buf3([](const unsigned char* d, size_t s) {
    // 每次追加数据时调用的回调
    std::cout << "收到 " << s << " 字节\n";
});

// 访问
buf1.to_string();   // 转换为 std::string
buf1.data();        // 原始指针 (const unsigned char*)
buf1.size();        // 字节数
buf1.span();        // std::span<const unsigned char>
buf1.empty();       // 检查是否为空
buf1.clear();       // 清空

// 比较（用于测试）
assert(buf1 == "hello");
assert(buf1 == std::string_view("hello"));
assert(buf1 == std::wstring_view(L"hello"));  // 跨编码比较
```

### `Shell` 类型

`Shell` 类表示一个 shell 解释器。工厂方法创建预配置的实例。

| 方法                  | Shell        | 平台       | 命令行                           |
| --------------------- | ------------ | ---------- | -------------------------------- |
| `Shell::Bash()`       | `bash`       | 跨平台     | `bash -c`                        |
| `Shell::Posix()`      | `sh`         | 仅 Unix    | `/bin/sh -c`                     |
| `Shell::Cmd()`        | `cmd.exe`    | 仅 Windows | `cmd.exe /d /s /c`               |
| `Shell::Powershell()` | `powershell` | 仅 Windows | `powershell -NoProfile -Command` |

### 管道

```cpp
using subprocess::detail::builder;

// 使用 operator| 创建管道
auto pipeline = builder("producer", {}) | builder("filter", {}) | builder("consumer", {});

// 运行并获取最后一个退出码
int code = pipeline.run();

// 获取所有退出码
std::vector<int> codes = pipeline.exit_codes();

// 终止整个管道
pipeline.terminate();
```

### 字符串类型支持

所有公开 API 接受以下字符串类类型（通过 C++20 `string_like_type` concept）：

- `char*`、`const char*`
- `std::string`、`std::string_view`
- `std::filesystem::path`
- **仅 Windows**：`wchar_t*`、`const wchar_t*`、`std::wstring`、`std::wstring_view`

在 Windows 上，UTF-8 和 UTF-16 之间的编码转换会自动处理。

## ⚙️ 配置宏

| 宏                           | 默认值 | 说明                                                                                                                  |
| ---------------------------- | ------ | --------------------------------------------------------------------------------------------------------------------- |
| `USE_DOLLAR_NAMED_VARIABLES` | `1`    | 启用时，将 `$` 前缀别名（`$stdin`、`$stdout`、`$()` 等）导出到全局命名空间。在包含头文件之前设置为 `0` 可禁用此行为。 |

## 📄 许可证

MIT 许可证。完整许可证文本请参见头文件。
