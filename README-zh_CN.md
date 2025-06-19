
[![cmake-multi-platform](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![msys2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

## 基本用法

```cpp
#include <subprocess/subprocess.hpp>

using subprocess::run;
using namespace subprocess::named_arguments;  // 用于命名参数: std_in,
                                           // std_out, std_err, cwd, env

// 1. 简单用法
int exit_code = run("/bin/echo", "-n", "123");
int exit_code = run({"/bin/echo", "-n", "123"});  // 命令可以是 vector

// 2. 捕获 stdout 和 stderr
subprocess::buffer stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out > stdout_buf,   // > 是重定向
    std_err > stderr_buf    // > 是重定向
);

// 3. 重定向到文件
subprocess::buffer stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out > "/tmp/out.txt",
    std_err > "/tmp/err.txt"
);

subprocess::buffer stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out >> "/tmp/out.txt",  // >> 是追加到文件
    std_err >> "/tmp/err.txt"   // >> 是追加到文件
);

// 4. 设置环境变量和追加环境变量

// env= 是覆盖环境变量
run("/usr/bin/printenv", env={
//                          ^ ~~~覆盖环境变量
  {"e1", "v1"},
  {"e2", "e2"}
});

// env+= 是追加到环境变量
run("/usr/bin/printenv", env+={
//                          ^ ~~~ 追加环境变量
  {"e1", "v1"},
  {"e2", "e2"}
});


// 5. 设置当前工作目录(cwd)
run("/bin/pwd", cwd="/home/my");



// 6. 命令是 vector
std::vector<std::string> cmd;
cmd.push_back("echo");
cmd.push_back("-n");
cmd.push_back("hello");
cmd.push_back("subprocess");

run(cmd);

```

## 如何使用

### 1. CMake

```cmake
FetchContent_Declare(
    subprocess
    GIT_REPOSITORY https://github.com/shediao/subprocess.hpp
    GIT_TAG v0.0.1
)
FetchContent_MakeAvailable(subprocess)

target_link_libraries(你的目标 PRIVATE subprocess::subprocess ...)
```

或者

```cmake
git clone https://github.com/shediao/subprocess.hpp /path/to/subprocess.hpp
add_subdirectory(/path/to/subprocess.hpp)

#include <subprocess/subprocess.hpp>
```

### 2. 其他方式

将 `subprocess.hpp` 文件复制到你的项目的 `include/subprocess/` 目录下。

## TODO (待办事项)

1. 设置进程优先级
2. Windows Unicode 支持
3. 管道运行子进程
4. 并行运行子进程

## docs
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/shediao/subprocess.hpp)
