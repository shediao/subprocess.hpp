# subprocess.hpp

[![CMAKE](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![MSYS2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

A modern, cross-platform, header-only C++ library for running and managing subprocesses. Inspired by Python's `subprocess` module and shell syntax, it aims to provide a simple, intuitive, and powerful API.

## ✨ Core Features

- **Header-only**: Simply include `include/subprocess/subprocess.hpp` in your project.
- **Cross-Platform**: Works on Windows, Linux, and macOS.
- **C++20 Standard**: Leverages modern C++ features for a cleaner syntax.
- **Easy to Use**: The API is designed to be simple and intuitive, resembling shell commands.
- **Powerful I/O Redirection**:
  - Easily redirect `stdin`, `stdout`, and `stderr` to files, /dev/null, or in-memory buffers.
  - Supports `>` (truncate) and `>>` (append) operators.
- **Flexible Environment Variable Control**:
  - Completely replace environment variables for the child process.
  - Add or modify variables on top of the existing environment.
- **Capture Output**: The `capture_run` function makes it easy to capture `stdout` and `stderr` from the subprocess.
- **Chainable Pipelines**: Supports creating process pipelines like `cmd1 | cmd2` (TODO).
- **Type-Safe**: Utilizes the C++ type system to catch errors at compile time.

## 🚀 Getting Started

### Option 1: Direct Include (Recommended)

As a header-only library, the simplest way is to copy the `include` directory into your project and then include the header:

```cpp
#include "subprocess/subprocess.hpp"
```

### Option 2: Using CMake `add_subdirectory`

You can add this repository as a submodule or download it directly into your project directory, then add the following to your `CMakeLists.txt`:

```cmake
# Add subprocess.hpp to your project
add_subdirectory(path/to/subprocess.hpp)

# Link it to your target
target_link_libraries(your_target PRIVATE subprocess)
```

### Option 3: Using CMake `FetchContent`

If your project uses CMake 3.11 or later, you can use the `FetchContent` module to automatically download and integrate this library.

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

## 📖 Usage Reference

### Importing Namespaces

For convenience, it's recommended to import the following namespaces. All examples in this documentation assume this has been done.

```cpp
#include <subprocess/subprocess.hpp>

// Core functions
using subprocess::run;
using subprocess::capture_run;

// Convenient alias, similar to shell
using subprocess::$;

// Named arguments
using namespace subprocess::named_arguments;
```

### 1. Running a Simple Command

Pass the command and its arguments directly. The `run` function returns the exit code of the subprocess.

```cpp
// Equivalent to shell: ls -l /tmp
int exit_code = run("ls", "-l", "/tmp");

if (exit_code == 0) {
    // success
} else {
    // failed
}
```

### 2. Capturing Output

Use the `capture_run` function to easily capture the output of `stdout` and `stderr`. `capture_run` returns a tuple containing `[exit_code, stdout_buffer, stderr_buffer]`.

```cpp
auto [exit_code, out, err] = capture_run("echo", "Hello, Subprocess!");

if (exit_code == 0) {
    std::cout << "Stdout: " << out.to_string() << std::endl;
}
```

### 3. I/O Redirection

The API is designed to be as intuitive as the shell.

```cpp
// Read stdin from a file
run("cat", $stdin < "input.txt");

// Redirect stdout to a file (truncate mode)
run("ls", "-l", $stdout > "output.txt");

// Redirect stderr to a file (append mode)
run("ls", "-l", "/non_existent_dir", $stderr >> "errors.log");

// Redirect all output to /dev/null (or NUL on Windows)
run("some_command", $stdout > $devnull, $stderr > $devnull);
```

### 4. Piping

You can create process pipelines using `subprocess::Pipe` objects.

```cpp
TODO:

```

### 5. Setting the Working Directory

Use the `$cwd` argument to specify the current working directory for the subprocess.

```cpp
// Execute ls in the /tmp directory
auto [code, out, err] = capture_run("ls", $cwd = "/tmp");
```

### 6. Managing Environment Variables

Use the `$env` argument to flexibly manage the subprocess's environment variables.

```cpp
// Completely overwrite environment variables
run("printenv", $env = {{"MY_VAR", "123"}});

// Add/modify variables on top of the existing environment
run("printenv", $env += {{"NEW_VAR", "hello"}});

// Append a path to the PATH environment variable
// (Path separators are handled automatically on Windows)
run("my_program", $env["PATH"] += "/opt/my_app/bin");
```

## 📚 API Reference

### Core Functions

- `int run(...)`: Executes a command and waits for it to complete, returning the exit code. Arguments can be any combination of command parts, string arguments, and named arguments.
- `int $(...)`: A convenient alias for `run`.
- `std::tuple<int, buffer, buffer> capture_run(...)`: Executes a command, waits for completion, and returns a `std::tuple` containing the exit code, `stdout` buffer, and `stderr` buffer.

### Named Arguments

Named arguments are used to control various aspects of process execution.

- **`$stdin < source`**: Redirect standard input from `source`.
  - `source` can be:
    - `std::string`: A file path.
    - `subprocess::buffer`: An in-memory buffer.
    - `subprocess::Pipe`: Pipe output from another process.
    - `$devnull`: The system's null device.

- **`$stdout > target` / `$stderr > target`**: Redirect standard output/error to `target` (truncate mode).
- **`$stdout >> target` / `$stderr >> target`**: Redirect standard output/error to `target` (append mode).
  - `target` can be:
    - `std::string`: A file path.
    - `subprocess::buffer&`: A reference to an in-memory buffer.
    - `subprocess::Pipe`: For pipeline connections.
    - `$devnull`: The system's null device.

- **`$cwd = path`**: Execute the command in the specified `path` (of type `std::string` or `std::wstring`).

- **`$env = map`**: Completely replace the child process's environment variables with the key-value pairs in `map`.
  - `map` type: `std::map<std::string, std::string>`.

- **`$env += map`**: Add the key-value pairs from `map` to the current environment. Existing variables will be overwritten.

- **`$env["VAR"] += value`**: Append `value` to the end of the environment variable named `VAR` (useful for `PATH`, etc.).
- **`$env["VAR"] <<= value`**: Prepend `value` to the beginning of the environment variable named `VAR`.


### The `subprocess::buffer` Type

A simple buffer class for exchanging data with subprocesses.

- `buffer()`: Creates an empty buffer.
- `buffer(std::string_view)`: Creates a buffer from a string view.
- `to_string()`: Converts buffer content to a `std::string`.
- `data()`: Gets the raw `char*` data.
- `size()`: Gets the buffer size.
- `clear()`: Clears the buffer.

### Helper Functions

The `subprocess` namespace also provides several useful cross-platform helper functions, which are aliased under the `process` namespace for convenience.

- `process::getenv(name)`: Gets an environment variable.
- `process::environs()`: Gets all environment variables.
- `process::getcwd()`: Gets the current working directory.
- `process::chdir(path)`: Changes the current working directory.
- `process::home()`: Gets the user's home directory.
- `process::pid()`: Gets the current process ID.

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/shediao/subprocess.hpp)

---
Happy Coding!
