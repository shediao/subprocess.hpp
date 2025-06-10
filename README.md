
[![cmake-multi-platform](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![msys2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

## Basics

```cpp
#include <subprocess/subprocess.hpp>

using subprocess::run;
using namespace subprocess::named_arguments;  // for named arguments: std_in,
                                           // std_out, std_err, cwd, env

// 1. simple usage
int exit_code = run("/bin/echo", "-n", "123");
int exit_code = run({"/bin/echo", "-n", "123"});  // command is a vector

// 2. capture stdout&stderr
std::vector<char> stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out > stdout_buf,   // > is redirect
    std_err > stderr_buf    // > is redirect
);

// 3. redirect to file
std::vector<char> stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out > "/tmp/out.txt",
    std_err > "/tmp/err.txt"
);

std::vector<char> stdout_buf, stderr_buf;
run(
    "/bin/bash", "-c", "echo -n 123; echo -n '345' >&2",
    std_out >> "/tmp/out.txt",  // >> is append to file
    std_err >> "/tmp/err.txt"   // >> is append to file
);

// 4. set environments && append environments

// env= is override environments
run("/usr/bin/printenv", env={
//                          ^ ~~~override environments
  {"e1", "v1"},
  {"e2", "e2"}
});

// env+= is append to environments
run("/usr/bin/printenv", env+={
//                          ^ ~~~ append environments
  {"e1", "v1"},
  {"e2", "e2"}
});


// 5. set cwd
run("/bin/pwd", cwd="/home/my");



// 6. command is a vector
std::vector<std::string> cmd;
cmd.push_back("echo");
cmd.push_back("-n");
cmd.push_back("hello");
cmd.push_back("subprocess");

run(cmd);

```

## Usage

### 1. cmake

```
FetchContent_Declare(
    subprocess
    GIT_REPOSITORY https://github.com/shediao/subprocess.hpp
    GIT_TAG v0.0.1
)
FetchContent_MakeAvailable(subprocess)

target_link_libraries(xxx PRIVATE subprocess::subprocess ...)
```

OR

```
git clone https://github.com/shediao/subprocess.hpp /path/to/subprocess.hpp
add_subdirectory(/path/to/subprocess.hpp)

#include <subprocess/subprocess.hpp>
```

### 2. others

copy `subprocess.hpp` to myproject/dir/include/subprocess/

## TODO

1. set process priority
2. windows unicode
3. pipe run subprocess
4. parallel run subprocess

## docs
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/shediao/subprocess.hpp)
