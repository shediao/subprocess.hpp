# subprocess.hpp

[![CMAKE](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/cmake-multi-platform.yml)
[![MSYS2](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml/badge.svg)](https://github.com/shediao/subprocess.hpp/actions/workflows/msys2.yml)

A modern, cross-platform, header-only C++20 library for running and managing subprocesses. Inspired by Python's `subprocess` module and shell syntax, it provides a simple, intuitive, and powerful API.

Licensed under the **MIT License**.

## ✨ Core Features

- **Header-only**: Simply include `include/subprocess/subprocess.hpp` in your project.
- **Cross-Platform**: Works on Windows, Linux, and macOS.
- **C++20 Standard**: Leverages modern C++ features (concepts, `std::span`, designated initializers, etc.) for a cleaner syntax and compile-time type safety.
- **Easy to Use**: The API is designed to be simple and intuitive, resembling shell commands.
- **Powerful I/O Redirection**:
  - Easily redirect `stdin`, `stdout`, and `stderr` to files, `/dev/null`, or in-memory buffers.
  - Supports `>` (truncate) and `>>` (append) operators.
- **Flexible Environment Variable Control**:
  - Completely replace environment variables for the child process.
  - Add or modify variables on top of the existing environment.
  - Append or prepend values to specific variables (e.g., `PATH`).
- **Capture Output**: The `capture_run` function makes it easy to capture `stdout` and `stderr` from the subprocess.
- **Shell Support**: Execute commands through `bash`, `sh`, `cmd.exe`, or `powershell` with the `Shell` class.
- **Detached Processes**: Spawn daemonized/background processes with `detach_run`.
- **Chainable Pipelines**: Create process pipelines like `cmd1 | cmd2 | cmd3` using `operator|`.
- **Type-Safe**: Utilizes C++20 concepts to validate arguments at compile time.
- **Wide String Support**: Full support for `std::string`, `std::wstring`, `std::string_view`, `std::wstring_view`, `std::filesystem::path`, and raw string pointers on all platforms.

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
using subprocess::detach_run;

// Convenient alias, similar to shell
using subprocess::$;

// Named arguments
using namespace subprocess::named_arguments;
```

### 1. Running a Simple Command

Pass the command and its arguments directly. The `run` function returns the exit code of the subprocess. All string-like types are supported.

```cpp
// Equivalent to shell: ls -l /tmp
int exit_code = run("ls", "-l", "/tmp");

// Using std::string, wstring, string_view, wstring_view, filesystem::path, etc.
std::string cmd = "ls";
std::string arg = "-l";
int code = run(cmd, arg, "/tmp");

// On Windows, wide strings are also supported
// run(L"cmd", L"/c", L"echo hello");
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

// Use in-memory buffers
subprocess::buffer inbuf{"hello world"};
subprocess::buffer outbuf;
run("cat", $stdin < inbuf, $stdout > outbuf);
```

### 4. Setting the Working Directory

Use the `$cwd` argument to specify the current working directory for the subprocess.

```cpp
// Execute ls in the /tmp directory
auto [code, out, err] = capture_run("ls", $cwd = "/tmp");
```

### 5. Managing Environment Variables

Use the `$env` argument to flexibly manage the subprocess's environment variables.

```cpp
// Completely overwrite environment variables
run("printenv", $env = {{"MY_VAR", "123"}});

// Add/modify variables on top of the existing environment
run("printenv", $env += {{"NEW_VAR", "hello"}});

// Append a path to the PATH environment variable
// (Path separators are handled automatically: ';' on Windows, ':' on Unix)
run("my_program", $env["PATH"] += "/opt/my_app/bin");

// Prepend a path to PATH
run("my_program", $env["PATH"] <<= "/usr/local/bin");
```

### 6. Timeout Control

Use the `$timeout` argument to set a time limit for the child process. On timeout, the entire process tree is terminated.

```cpp
// 5-second timeout (using an integer for seconds)
run("some_command", $timeout = 5);

// Timeout using chrono milliseconds
using namespace std::chrono_literals;
run("some_command", $timeout = 5000ms);

// Disable timeout (default)
run("some_command", $timeout = $timeout_infinite);
```

### 7. Process Group Control

Use the `$newgroup` argument to control whether the child process is placed in a new process group / job object. When enabled, timeout termination will kill the entire process tree.

```cpp
run("some_command", $newgroup = true);
```

> **Note**: `capture_run` enables `$newgroup` internally by default, so manual specification is unnecessary.

### 8. Shell Execution

Execute commands through a shell interpreter. The library provides `Shell::Bash()`, `Shell::Posix()` (`sh`), `Shell::Cmd()` (Windows `cmd.exe`), and `Shell::Powershell()` (Windows PowerShell).

```cpp
// Execute through bash
run(Shell::Bash(), "echo $HOME && ls -la");

// Execute through sh (POSIX shell)
run(Shell::Posix(), "echo $SHELL");

// On Windows: execute through cmd.exe
// run(Shell::Cmd(), "echo %PATH% & exit /b 0");

// On Windows: execute through PowerShell
// run(Shell::Powershell(), "Get-ChildItem | Select-Object Name");

// Shell execution also works with capture_run and detach_run
auto [code, out, err] = capture_run(Shell::Bash(), "ls -l /tmp");
detach_run(Shell::Bash(), "my_daemon --foreground");
```

Pre-built shell constants are available as named arguments:

```cpp
using namespace subprocess::named_arguments;

run($bash, "echo hello");       // bash -c "echo hello"
run($shell, "dir");             // sh -c "dir" (Unix) or cmd.exe /d /s /c "dir" (Windows)
// run($powershell, "ls");      // PowerShell (Windows only)
```

### 9. Detached (Daemon) Processes

Use `detach_run` to spawn a background process that is detached from the parent. The child process continues running independently. Standard I/O is redirected to `/dev/null` (or `NUL` on Windows).

```cpp
// Spawn a detached process
bool success = detach_run("my_daemon", "--foreground");

// With a custom working directory
detach_run("my_daemon", $cwd = "/var/run/myapp");

// With custom environment variables
detach_run("my_daemon", $env += {{"DAEMON_MODE", "1"}});
```

> **Note**: On Unix, `detach_run` uses a double-fork technique and calls `setsid()` to create a proper daemon. On Windows, it uses `DETACHED_PROCESS` and `CREATE_NEW_PROCESS_GROUP`.

### 10. Pipelines

Chain multiple subprocesses together using `operator|`, just like shell pipes. Builders can be created implicitly when string-like arguments are passed to `run()`, or explicitly via `subprocess::detail::builder` for pipelines.

```cpp
using subprocess::detail::builder;

// Equivalent to: ls -l | grep ".cpp" | wc -l
auto pipeline = builder("ls", {"-l"}) | builder("grep", {".cpp"}) | builder("wc", {"-l"});
int exit_code = pipeline.run();

// Pipelines return the exit code of the last process
std::cout << "Exit code: " << exit_code << std::endl;

// You can also access all exit codes
auto pipe = builder("cmd1", {}) | builder("cmd2", {});
pipe.run();
auto all_codes = pipe.exit_codes();  // returns vector<int> of all exit codes
```

Pipelines automatically:

- Connect `stdout` of each process to `stdin` of the next via pipes.
- On Windows, share a single Job Object across all processes.
- On Unix, create a shared process group so signals are delivered to all members.

## 📚 API Reference

### Core Functions

| Function                                                                     | Description                                                                                                                       |
| ---------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `int run(app, args..., named_args...)`                                       | Execute a command, wait for it to complete, return exit code. Accepts variadic string-like arguments followed by named arguments. |
| `int run(Shell, command, named_args...)`                                     | Execute a command through a shell interpreter.                                                                                    |
| `int $(...)`                                                                 | Convenient alias for `run`. Controlled by the `USE_DOLLAR_NAMED_VARIABLES` macro (enabled by default).                            |
| `std::tuple<int, buffer, buffer> capture_run(app, args..., named_args...)`   | Execute a command, capture `stdout` and `stderr` into buffers. Returns `[exit_code, stdout, stderr]`.                             |
| `std::tuple<int, buffer, buffer> capture_run(Shell, command, named_args...)` | Shell variant of `capture_run`.                                                                                                   |
| `bool detach_run(app, args..., named_args...)`                               | Spawn a detached/daemonized process. Returns `true` on success.                                                                   |
| `bool detach_run(Shell, command, named_args...)`                             | Shell variant of `detach_run`.                                                                                                    |

### Named Arguments

Named arguments are used to control various aspects of process execution. They are resolved at compile time via C++20 concepts.

- **`$stdin < source`**: Redirect standard input from `source`.
  - `source` can be:
    - `std::string` / `std::wstring`: A file path.
    - `subprocess::buffer&`: A reference to an in-memory buffer.
    - `$devnull`: The system's null device.

- **`$stdout > target` / `$stderr > target`**: Redirect standard output/error to `target` (truncate mode).
- **`$stdout >> target` / `$stderr >> target`**: Redirect standard output/error to `target` (append mode).
  - `target` can be:
    - `std::string` / `std::wstring`: A file path.
    - `subprocess::buffer&`: A reference to an in-memory buffer.
    - `$devnull`: The system's null device.

- **`$cwd = path`**: Execute the command in the specified `path` (type: `std::string`, `std::wstring`, `std::string_view`, `std::wstring_view`).

- **`$env = map`**: Completely replace the child process's environment variables with the key-value pairs in `map`.
  - `map` type: `std::map<std::string, std::string>` (or `std::map<std::wstring, std::wstring>` on Windows).

- **`$env += map`**: Add the key-value pairs from `map` to the current environment. Existing variables will be overwritten.

- **`$env["VAR"] += value`**: Append `value` to the end of the environment variable named `VAR` (useful for `PATH`, etc.). Path separators are handled automatically.
- **`$env["VAR"] <<= value`**: Prepend `value` to the beginning of the environment variable named `VAR`.

- **`$timeout = duration`**: Set a timeout for the child process.
  - `duration` can be:
    - `int`: Seconds, e.g., `$timeout = 5`.
    - `std::chrono::milliseconds`: Milliseconds, e.g., `$timeout = 5000ms`.
    - `$timeout_infinite`: Disable timeout (default).

- **`$newgroup = bool`**: Place the child process in a new process group / job object. When enabled, timeout termination kills the entire process tree.

- **Device Constants**:
  - `$devnull`: The system null device (`/dev/null` on Unix, `NUL` on Windows).
  - `$devttyout`: Standard output terminal device (`/dev/tty` on Unix, `CONOUT$` on Windows).
  - `$devttyin`: Standard input terminal device (`/dev/tty` on Unix, `CONIN$` on Windows).

- **Shell Constants**:
  - `$bash`: `bash -c` (cross-platform).
  - `$shell`: `sh -c` on Unix, `cmd.exe /d /s /c` on Windows.
  - `$powershell`: `powershell -NoProfile -Command` (Windows only).

### The `subprocess::buffer` Type

A simple buffer class for exchanging data with subprocesses.

```cpp
// Construction
buffer buf1;                                    // empty buffer
buffer buf2("hello");                           // from string_view
buffer buf3([](const unsigned char* d, size_t s) {
    // callback invoked on each append
    std::cout << "received " << s << " bytes\n";
});

// Access
buf1.to_string();   // convert to std::string
buf1.data();        // raw pointer (const unsigned char*)
buf1.size();        // number of bytes
buf1.span();        // std::span<const unsigned char>
buf1.empty();       // check if empty
buf1.clear();       // clear contents

// Comparison (for testing)
assert(buf1 == "hello");
assert(buf1 == std::string_view("hello"));
assert(buf1 == std::wstring_view(L"hello"));  // cross-encoding comparison
```

### The `Shell` Type

The `Shell` class represents a shell interpreter. Factory methods create pre-configured instances.

| Method                | Shell        | Platform       | Command Line                     |
| --------------------- | ------------ | -------------- | -------------------------------- |
| `Shell::Bash()`       | `bash`       | Cross-platform | `bash -c`                        |
| `Shell::Posix()`      | `sh`         | Unix only      | `/bin/sh -c`                     |
| `Shell::Cmd()`        | `cmd.exe`    | Windows only   | `cmd.exe /d /s /c`               |
| `Shell::Powershell()` | `powershell` | Windows only   | `powershell -NoProfile -Command` |

### Pipelines

```cpp
using subprocess::detail::builder;

// Create pipelines with operator|
auto pipeline = builder("producer", {}) | builder("filter", {}) | builder("consumer", {});

// Run and get the last exit code
int code = pipeline.run();

// Get all exit codes
std::vector<int> codes = pipeline.exit_codes();

// Terminate the entire pipeline
pipeline.terminate();
```

### String Type Support

All public APIs accept the following string-like types (via the `string_like_type` C++20 concept):

- `char*`, `const char*`
- `std::string`, `std::string_view`
- `std::filesystem::path`
- **Windows only**: `wchar_t*`, `const wchar_t*`, `std::wstring`, `std::wstring_view`

Encoding conversion between UTF-8 and UTF-16 is handled automatically on Windows.

## ⚙️ Configuration Macros

| Macro                        | Default | Description                                                                                                                                                               |
| ---------------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `USE_DOLLAR_NAMED_VARIABLES` | `1`     | When enabled, exports `$`-prefixed aliases (`$stdin`, `$stdout`, `$()`, etc.) into the global namespace. Set to `0` before including the header to disable this behavior. |

## 📄 License

MIT License. See the header file for the full license text.
