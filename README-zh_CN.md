# subprocess.hpp

[![CMAKE](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![MSYS2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

一个用于在 C++ 中运行和管理子进程的现代、跨平台、仅头文件的库. 它的设计灵感来源于 Python 的 `subprocess` 模块和 shell 的语法, 旨在提供一个简单、直观且强大的 API.

## ✨ 核心特性

- **仅头文件**: 只需将 `include/subprocess/subprocess.hpp` 包含到你的项目中即可.
- **跨平台**: 在 Windows、Linux 和 macOS 上均可工作.
- **C++20 标准**: 利用现代 C++ 的特性提供更简洁的语法.
- **易于使用**: API 设计简单直观, 类似 shell 命令.
- **强大的 I/O 重定向**:
  - 轻松将 `stdin`, `stdout`, `stderr` 重定向到文件、/dev/null 或内存中的缓冲区.
  - 支持 `>` (截断) 和 `>>` (追加) 操作符.
- **灵活的环境变量控制**:
  - 为子进程完全替换环境变量.
  - 在现有环境的基础上添加或修改变量.
- **捕获输出**: `capture_run` 函数可以轻松捕获子进程的 `stdout` 和 `stderr`.
- **链式管道**: 支持创建类似 `cmd1 | cmd2` 的进程管道(TODO).
- **类型安全**: 利用 C++ 类型系统在编译时捕获错误.

## 🚀 接入到现有工程

### 方式一: 直接包含 (推荐)

这是一个仅头文件的库, 最简单的方式是直接将 `include` 目录复制到你的项目中, 然后包含头文件:

```cpp
#include "subprocess/subprocess.hpp"
```

### 方式二: 使用 CMake `add_subdirectory`

你可以将此仓库作为子模块或直接下载到你的项目目录中, 然后在你的 `CMakeLists.txt` 中添加:

```cmake
# 将 subprocess.hpp 添加到你的项目中
add_subdirectory(path/to/subprocess.hpp)

# 将其链接到你的目标
target_link_libraries(your_target PRIVATE subprocess)
```

### 方式三: 使用 CMake `FetchContent`

如果你的项目使用 CMake 3.11 或更高版本, 你可以使用 `FetchContent` 模块自动下载并集成此库.

```cmake
include(FetchContent)

FetchContent_Declare(
  subprocess
  GIT_REPOSITORY https://github.com/shediao/subprocess.hpp.git
  GIT_TAG main # or a specific commit/tag
)

FetchContent_MakeAvailable(subprocess)

target_link_libraries(your_target PRIVATE subprocess)
```

## 📖 基本使用参考

### 引入命名空间

为了方便使用, 建议引入以下命名空间. 本文档中的所有示例都假定已进行此操作.

```cpp
#include <subprocess/subprocess.hpp>

// 核心函数
using subprocess::run;
using subprocess::capture_run;

// 方便的别名, 类似 shell
using subprocess::$;

// 命名参数
using namespace subprocess::named_arguments;
```

### 1. 执行简单命令

直接传递命令和参数, `run` 函数会返回子进程的退出码.

```cpp
// 等同于 shell: ls -l /tmp
int exit_code = run("ls", "-l", "/tmp");

if (exit_code == 0) {
    // success
} else {
    // failed
}
```

### 2. 捕获输出

使用 `capture_run` 函数可以方便地捕获 `stdout` 和 `stderr` 的输出.

```cpp
// capture_run 返回一个包含 [exit_code, stdout_buffer, stderr_buffer] 的 tuple
auto [exit_code, out, err] = capture_run("echo", "Hello, Subprocess!");

if (exit_code == 0) {
    std::cout << "Stdout: " << out.to_string() << std::endl;
}
```

### 3. I/O 重定向

API 设计得像 shell 一样直观.

```cpp
// 将 stdin 从文件读入
run("cat", $stdin < "input.txt");

// 将 stdout 重定向到文件 (截断模式)
run("ls", "-l", $stdout > "output.txt");

// 将 stderr 重定向到文件 (追加模式)
run("ls", "-l", "/non_existent_dir", $stderr >> "errors.log");

// 将所有输出重定向到 /dev/null (或 Windows 上的 NUL)
run("some_command", $stdout > $devnull, $stderr > $devnull);
```

### 4. 管道 (Piping)

你可以通过 `subprocess::Pipe` 对象创建进程管道.

```cpp
TODO:

```

### 5. 设置工作目录

使用 `$cwd` 参数来指定子进程的当前工作目录.

```cpp
// 在 /tmp 目录中执行 ls
auto [code, out, err] = capture_run("ls", $cwd = "/tmp");
```

### 6. 管理环境变量

使用 `$env` 参数来灵活地管理子进程的环境变量.

```cpp
// 完全覆盖环境变量
run("printenv", $env = {{"MY_VAR", "123"}});

// 在现有环境的基础上添加/修改变量
run("printenv", $env += {{"NEW_VAR", "hello"}});

// 向 PATH 环境变量追加路径
// (在 Windows 上会自动处理路径分隔符)
run("my_program", $env["PATH"] += "/opt/my_app/bin");
```

## 📚 API 接口说明

### 核心函数

- `int run(...)`: 执行一个命令并等待其完成, 返回退出码. 参数可以是命令、字符串参数和命名参数的任意组合.
- `int $(...)`: `run` 的一个方便的别名.
- `std::tuple<int, buffer, buffer> capture_run(...)`: 执行命令, 等待其完成, 并返回一个包含退出码、`stdout` 缓冲区和 `stderr` 缓冲区的 `std::tuple`.

### 命名参数 (Named Arguments)

命名参数用于控制进程执行的各个方面.

- **`$stdin < source`**: 从 `source` 重定向标准输入.
  - `source` 可以是:
    - `std::string`: 文件路径.
    - `subprocess::buffer`: 内存中的缓冲区.
    - `subprocess::Pipe`: 另一个进程的管道输出.
    - `$devnull`: 系统空设备.

- **`$stdout > target` / `$stderr > target`**: 将标准输出/错误重定向到 `target` (截断模式).
- **`$stdout >> target` / `$stderr >> target`**: 将标准输出/错误重定向到 `target` (追加模式).
  - `target` 可以是:
    - `std::string`: 文件路径.
    - `subprocess::buffer&`: 内存中的缓冲区引用.
    - `subprocess::Pipe`: 用于管道连接.
    - `$devnull`: 系统空设备.

- **`$cwd = path`**: 在指定的 `path` (类型为 `std::string` 或 `std::wstring`) 中执行命令.

- **`$env = map`**: 完全使用 `map` 中的键值对作为子进程的环境变量.
  - `map` 类型: `std::map<std::string, std::string>`.

- **`$env += map`**: 将 `map` 中的键值对添加到当前的环境变量中. 如果变量已存在, 则覆盖.

- **`$env["VAR"] += value`**: 将 `value` 追加到名为 `VAR` 的环境变量的末尾 (主要用于 `PATH` 等).
- **`$env["VAR"] <<= value`**: 将 `value` 前插到名为 `VAR` 的环境变量的开头.


### `subprocess::buffer` 类型

一个简单的缓冲区类, 用于和子进程进行数据交换.

- `buffer()`: 创建一个空缓冲区.
- `buffer(std::string_view)`: 从字符串视图创建缓冲区.
- `to_string()`: 将缓冲区内容转换为 `std::string`.
- `data()`: 获取原始 `char*` 数据.
- `size()`: 获取缓冲区大小.
- `clear()`: 清空缓冲区.

### 辅助函数

`subprocess` 命名空间下还提供了一些有用的跨平台辅助函数, 它们也被别名到了 `process` 命名空间下.

- `process::getenv(name)`: 获取一个环境变量.
- `process::environs()`: 获取所有环境变量.
- `process::getcwd()`: 获取当前工作目录.
- `process::chdir(path)`: 更改当前工作目录.
- `process::home()`: 获取用户主目录.
- `process::pid()`: 获取当前进程 ID.




[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/shediao/subprocess.hpp)

---
Happy Coding!
