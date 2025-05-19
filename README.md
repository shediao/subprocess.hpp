## Basics

```cpp
#include <subprocess/subprocess.hpp>

using namespace subprocess;

// 1. simple usage
int exit_code = run({"/bin/echo", "-n", "123"});

// 2. capture stdout&stderr
std::vector<char> stdout_buf, stderr_buf;
run(
    {"/bin/bash", "-c", "echo -n 123; echo -n '345' >&2"},
    std_out > stdout_buf,   // > is redirect
    std_err > stderr_buf    // > is redirect
);

// 3. redirect to file
std::vector<char> stdout_buf, stderr_buf;
run(
    {"/bin/bash", "-c", "echo -n 123; echo -n '345' >&2"},
    std_out > "/tmp/out.txt",
    std_err > "/tmp/err.txt"
);

std::vector<char> stdout_buf, stderr_buf;
run(
    {"/bin/bash", "-c", "echo -n 123; echo -n '345' >&2"},
    std_out >> "/tmp/out.txt",  // >> is append to file
    std_err >> "/tmp/err.txt"   // >> is append to file
);

// 4. set environments && append environments
run({"/usr/bin/printenv"}, env={
//                            ^ ~~~this is override environment
  {"e1", "v1"},
  {"e2", "e2"}
});

run({"/usr/bin/printenv"}, env+={
//                            ^ ~~~this is append environment
  {"e1", "v1"},
  {"e2", "e2"}
});


// 5. set cwd
run({"/bin/pwd"}, cwd="/home/my");

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

1. redirect append to file
2. support windows、bsd
