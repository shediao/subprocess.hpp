# Ai.md

This file provides guidance to Ai Coder when working with code in this repository.

## Build Commands

```bash
# Configure (from repo root)
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DSUBPROCESS_BUILD_TESTS=ON

# Build
cmake --build cmake-build-debug

# Run all tests
cd cmake-build-debug && ctest --output-on-failure

# Run a single test by name
cd cmake-build-debug && ctest -R TestName --output-on-failure

# Run a single test executable directly
./cmake-build-debug/tests/test_basic
```

AddressSanitizer is enabled by default in Debug builds on Linux/macOS. To disable: `-DSUBPROCESS_ENABLE_ASAN=OFF`.

## Cross-Compilation

> **Note:** Cross-compilation environment is currently only available on macOS and Linux platforms.

When directories matching `build/{platform}-{arch}` exist (e.g., `build/darwin-arm64`, `build/linux-x86_64`, `build/mingw64-x86_64`, `build/windows-arm64`), the cross-compilation environment is already configured. Build directly with:

```bash
cmake --build build/{platform}-{arch}
```

Supported build directories:

| Directory              | Target                 |
| ---------------------- | ---------------------- |
| `build/darwin-arm64`   | macOS ARM64            |
| `build/darwin-x86_64`  | macOS x86_64           |
| `build/linux-arm64`    | Linux ARM64            |
| `build/linux-x86_64`   | Linux x86_64           |
| `build/mingw64-x86_64` | Windows x86_64 (MinGW) |
| `build/windows-arm64`  | Windows ARM64 (MSVC)   |
| `build/windows-x86_64` | Windows x86_64 (MSVC)  |

When source files are added/removed or CMake options change (making it necessary to re-run CMake configuration), simply `touch CMakeLists.txt` and the next `cmake --build` will automatically re-generate the build system:

```bash
touch CMakeLists.txt
cmake --build build/{platform}-{arch}
```

## Code Formatting

clang-format (Google style) is configured. A pre-commit hook auto-formats staged C/C++ and CMake files. The repo enforces `-Wall -Wextra -Werror` (or `/W4 /WX` for MSVC).

## Coding Style

### 1. Language Standard & Tooling

- **C++20** is the baseline (`CMAKE_CXX_STANDARD 20`). Use modern C++ idioms (concepts, `std::span`, designated initializers, etc.) where appropriate.
- **clang-format** — `.clang-format` at repo root: Google style, `InsertBraces: true`, `InsertNewlineAtEOF: true`.
- **clang-tidy** — `.clang-tidy` at repo root with a curated set of checks covering resource management (`cppcoreguidelines-*`, `bugprone-*`), modern C++ (`modernize-*`), performance, readability, and portability. Run before submitting PRs.
- **`.clangd`** — Not present in the repo (it's in `.gitignore`). Developers may create their own; `.clang-format` and `.clang-tidy` are picked up automatically. A `compile_commands.json` can be generated with `cmake -B cmake-build-debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

### 2. Resource Management (RAII)

- **No bare resource handles or owning raw pointers** in type members. All native handles (file descriptors, `HANDLE`, `pid_t`, etc.) must be wrapped in RAII types that close/release in their destructor.
- Use `std::unique_ptr`, `std::shared_ptr`, or project-internal RAII wrappers (e.g., `detail::unique_fd_base` CRTP pattern) for ownership. The `.clang-tidy` configuration enforces `cppcoreguidelines-owning-memory`, `cppcoreguidelines-no-malloc`, and `cppcoreguidelines-special-member-functions`.
- Follow the Rule of Five: if a class manages a resource, either define or `= default` / `= delete` the destructor, copy/move constructors, and copy/move assignment operators.

### 3. Line Endings

- **All source files must use LF (`\n`) line endings exclusively.** Never introduce CRLF (`\r\n`).
- The repo does not have a `.gitattributes` file; editors and tools should be configured to use Unix-style line endings on all platforms. Verify with `file <filename>` or `dos2unix` if needed.

### 4. Testing Requirements

- **Every new function, class, or public API must have corresponding tests.** No exceptions.
- Tests live in `tests/`, one `.cc` file per feature area (e.g., `test_basic.cc`, `test_io.cc`, `test_pipeline.cc`). Each `.cc` file compiles into its own test executable (via `add_subprocess_test_suite()`) and is also linked into the `all_test` aggregate target. On Windows, each test gets a Unicode variant (the default) and an ANSI variant (suffixed `_${name}_ansi`).
- Tests use **Google Test** (v1.15.2, fetched via `FetchContent`). Write tests with `TEST()` / `TEST_F()` macros; use `ASSERT_*` for fatal conditions and `EXPECT_*` for non-fatal ones.
- Include `tests/utils.h` for shared utilities like `TempFile`.
- When fixing a bug, add a regression test that reproduces it before applying the fix.
- The `environment` library (v0.0.6, from `shediao/environment.hpp`) is also available in tests for cross-platform environment variable testing.

### 5. Cross-Platform Support

- The library **must compile and work on Windows, Linux, and macOS**, plus other POSIX/BSD systems where feasible.
- Use `#if defined(_WIN32)` / `#else` for platform-specific code. Avoid `#ifdef` unless necessary.
- Platform-specific helpers (e.g., `utf8_to_utf16` on Windows, `pipe2` on Linux) should be isolated behind clear abstraction boundaries within the `detail` namespace.
- String handling: public APIs accept both narrow (`char*`, `std::string`, `std::string_view`) and wide (`wchar_t*`, `std::wstring`, `std::wstring_view`) string types. On Windows, internally convert to UTF-16 as needed. The `string_like_type` concept governs which types are accepted.
- Never commit code that breaks the build on any supported platform. Use the cross-compilation environments in `build/{platform}-{arch}/` for verification when available.

## CI

If the remote repository URL is `http://github.com/*` or `git@github.com:*`, then GitHub Actions (configuration files located at `.github/workflows/*.yml`) is used as CI.

Browse and manage GitHub Actions via the `gh` command:

```bash
# View recent workflow runs
gh run list

# View details of a specific run
gh run view <run-id>

# View logs of a specific run
gh run view <run-id> --log

# Manually trigger a workflow
gh workflow run <workflow-name>

# List all workflows
gh workflow list

# View workflow file contents
gh workflow view <workflow-name>
```

**When the user wants to resolve GitHub Actions failures, they should first use `gh` commands to get logs and analyze the problem**, rather than blindly guessing the cause. How to get logs:

```bash
# Get logs of the latest run (usually the failed run)
gh run list --limit 1 --json databaseId -q '.[].databaseId' | xargs gh run view --log

# Get logs of a specific run-id (including failed steps)
gh run view <run-id> --log

# Get logs of the run corresponding to a specific commit
gh run list --commit <commit-sha> --limit 1 --json databaseId -q '.[].databaseId' | xargs gh run view --log

# Only view failed runs
gh run list --status failure --limit 5

# View logs of a failed job in a run (if the run has multiple jobs)
gh run view <run-id> --log --job <job-id>
```

**When fetching logs with `gh` commands, always prefer getting only the key information rather than the full logs unless absolutely necessary.** For example:

- Use `gh run view <run-id> --log --failed` to get only failed step logs (if supported).
- Pipe through `grep` to filter for error messages, failed test names, or compilation errors: `gh run view <run-id> --log 2>&1 | grep -E '(error:|FAILED|failure)'`.
- For test failures, look for the specific test output rather than the entire build log.
- Only fetch full logs when the filtered output does not provide enough context to diagnose the issue.

After getting the logs, identify the specific failure cause based on the error messages in the logs (compilation errors, test failures, environment issues, etc.), then make targeted fixes.

**Fix Verification**: If the failure occurs on a platform different from the current machine (e.g., currently on macOS ARM64, but the failure is on Linux x86_64), and a corresponding cross-compilation environment `build/{platform}-{arch}/` exists locally, after fixing, you **must** verify the build through that cross-compilation environment to ensure the fix also passes on the target platform:

```bash
# For example: fixed a compilation error on Linux x86_64, with build/linux-x86_64/ available locally
cmake --build build/linux-x86_64
```

If the corresponding cross-compilation environment does not exist locally, just push the fix directly and let CI verify.

## Architecture

This is a **header-only** C++20 library (`include/subprocess/subprocess.hpp`) for running child processes on Windows, Linux, and macOS. The entire implementation lives in a single header (~3492 lines).

### Namespace & Macro Controls

- All library code lives under `namespace subprocess`.
- Implementation details are in `namespace subprocess::detail`.
- Named argument constants are in `subprocess::named_arguments` (aliased from `detail::named_args`).
- `USE_DOLLAR_NAMED_VARIABLES` macro (default `1`): when set, enables `$`-prefixed aliases (`$stdin`, `$stdout`, `$cwd`, `$env`, `$timeout`, `$newgroup`, `$shell`, `$bash`, `$powershell` (Windows), `$()` function).

### RAII & Platform Primitives (`detail` namespace)

- **`detail::unique_fd_base`** — CRTP base class template for RAII handle wrappers. Templated on handle type, derived type, and deleter function. Provides move semantics, invalid-value tracking, and comparison operators.
- **`detail::unique_fd`** — RAII wrapper over `NativeHandle` (Windows `HANDLE` / Unix `int` fd). Inherits from `unique_fd_base` with `close_native_handle` deleter. Supports `dup()`, `close()`, `isatty()`, and `set_nonblocking()` (Unix only).
- **`detail::unique_pid`** — (Unix only) RAII wrapper over `pid_t` with a no-op deleter, using the `unique_fd_base` CRTP. Used for child process and process group IDs.
- **`detail::scope_exit`** — scope guard utility, similar to `std::experimental::scope_exit`. Used for cleanup: watchdog thread stop, pump thread join. Create with `make_scope_exit(fn)`.
- **`detail::visitor`** — overloaded lambda visitor pattern for `std::visit` over `std::variant`.
- **`detail::ssize_t`** — `ptrdiff_t` alias on Windows (where POSIX `ssize_t` is unavailable).
- **`detail::INVALID_NATIVE_HANDLE_VALUE`** — `INVALID_HANDLE_VALUE` on Windows, `-1` on Unix.
- **`detail::invalid_handle()`** — checks if a handle equals the invalid value.
- **`detail::close_native_handle()`** — `CloseHandle` on Windows, `close` on Unix.
- **`detail::dup_native_handle()`** — `DuplicateHandle` on Windows, `fcntl(F_DUPFD_CLOEXEC)` on Unix.
- **`detail::set_nonblocking()`** — (Unix only) sets `O_NONBLOCK` on a file descriptor.
- **`detail::print_error()`** — platform-specific error printing: on Windows, attempts VT-aware `WriteConsoleW`, falls back to `WriteFile` with UTF-8; on Unix, writes to stderr with EINTR retry.
- **`detail::get_last_error_message()`** — returns `GetLastError()` / `errno` message as a string/wstring.

### Timer Class

- **`detail::Timer`** — general-purpose async timer using `std::thread` + `condition_variable`. Supports `start(fn, duration)`, `stop()`, `running()`, and static `after(fn, duration)`. Uses `std::shared_ptr<State>` for safe move semantics and destruction-from-own-thread handling. Used on Unix as the watchdog timer instead of a raw thread.

### String & Command-Line Utilities

- **`utf8_to_utf16()` / `utf16_to_utf8()`** — (Windows only) conversion between UTF-8 `string_view` and UTF-16 `wstring_view`.
- **`split()` / `split_to_if()`** — generic string splitting with delimiter predicate; supports `compress_tokens` for collapsing consecutive delimiters. `split_to_if` is templated to work with any container that has `emplace_back` or `insert`.
- **`argv_to_command_line()`** — (Windows only) builds a properly escaped command-line string from a vector of `wstring` arguments, following MSVC quoting rules (backslash-doubling before quotes, wrapping whitespace-containing args in double quotes). This is the default for all subprocesses.
- **`argv_to_command_line_for_cmd()`** — (Windows only) a separate command-line builder for `cmd.exe` specifically. It handles `/c` and `/s` flag ordering differently: switches (`/X`) precede the command string, and the command+args are wrapped in a single double-quoted string. Used automatically when the executable is `cmd.exe`.
- **`create_environment_string_data()`** — (Windows only) builds the null-delimited environment block required by `CreateProcessW`.
- **`get_all_env_vars()`** — returns a map of the current process environment. On Windows, handles hidden env vars (starting with `=`), and uppercases keys for case-insensitive matching.
- **`getenv()`** — platform-specific helper to retrieve a single environment variable; returns `std::optional`.
- **`find_executable()`** — resolves an executable name to a full path.
  - On Windows: uses `SearchPathW`, checks `PATHEXT` extensions, resolves relative paths against the child's CWD using `GetFullPathNameW`. Accepts an optional `cwd` parameter.
  - On Unix: walks `PATH` entries, checks with `stat()` + `access(X_OK)`, resolves paths containing `/` via `realpath`.
- **`convert_to_string<ToCharT>()`** — template helper that converts between narrow and wide string types. On Windows, uses `utf8_to_utf16`/`utf16_to_utf8` for cross-encoding conversion.

### Core class: `detail::subprocess`

The main implementation class. Holds the command vector, I/O redirectors, environment, cwd, timeout, newgroup flags, and platform-specific process handles.

**Key fields:**

- `app_`, `args_`: `wstring` + `vector<wstring>` on Windows, `string` + `vector<string>` on Unix.
- `cwd_`: `wstring` on Windows, `string` on Unix.
- `env_`: `map<wstring,wstring>` on Windows, `map<string,string>` on Unix.
- `stdin_`, `stdout_`, `stderr_`: `StdinRedirector`, `StdoutRedirector`, `StderrRedirector` respectively.
- `timeout_`: `optional<chrono::milliseconds>` on both platforms.
- **Windows-specific**: `shared_ptr<unique_fd> job_handle_` (Job Object), `unique_fd process_handle_` + `thread_handle_`, `vector<unsigned char> proc_thread_attr_list_` (for explicit handle inheritance), `vector<thread> pump_threads_`, `bool newgroup_`, `argv_to_command_line_fn` function pointer.
- **Unix-specific**: `detail::Timer watchdog_`, `unique_pid pid_` + `pgid_`, `optional<pid_t> requested_pgid_`, `int early_exit_status_` + `bool child_reaped_early_` (for handling grandchildren that hold pipe write-ends open).

**Key methods:**

- `spawn()` — the main entry point for running a subprocess. Calls `prepare_for_child()` → platform-specific spawn → `close_child_end()` → `pump_pipe_data()`.
- `detach_spawn()` — spawns a detached/daemonized process. On Unix: double-fork + `setsid()`. On Windows: `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`. stdin/stdout/stderr are redirected to `/dev/null` (or `NUL` on Windows). Returns `true` if spawn succeeded (but exec failures are invisible on Unix).
- `run()` — calls `spawn()` then `wait()`.
- `wait()` — blocks until the child exits, then returns the exit code.
  - **Windows**: `WaitForSingleObject` on process handle, with optional timeout; calls `terminate()` on timeout. Joins pump threads via scope guard.
  - **Unix**: Calls `waitpid()`, unless the child was already reaped early by the I/O pump (stored in `early_exit_status_`). Converts signal exit to `128 + signo`. Stops watchdog via scope guard.
- `terminate()` — kills the process tree.
  - **Windows**: `TerminateJobObject` first (closes child's inherited pipe handles, unblocking I/O threads), falls back to `TerminateProcess`. Then closes all redirectors.
  - **Unix**: `kill(-pid, SIGTERM)` with 100ms grace period polling, then `kill(-pid, SIGKILL)`. Only uses process group kill when the child is the process group leader (`pid == pgid`).
- `pid()` — returns the native process ID (`HANDLE` on Windows, `pid_t` on Unix).

**Constructor**: The subprocess constructor takes an executable name, argument vector, and named arguments. It processes all named argument types (stdin/stdout/stderr redirectors, `Env`, `EnvAppend`, `EnvItemAppend`, `Cwd`, `Newgroup`, `Timeout`) and builds the final environment via a cascade: base environment → `Env` (replacement) → `EnvAppend` (merge) → `EnvItemAppend` (append/prepend to specific keys). On Windows, env keys are upper-cased for case-insensitive matching.

### Shell Support

- **`detail::Shell`** — represents a shell interpreter. Provides `shell_cmd()` and `shell_args()` to get the shell executable and its prefix arguments. Factory methods:
  - `.bash()` / `Shell::Bash()` — `bash -c` (Unix: `/bin/bash`, Windows: `bash`)
  - `.sh()` / `Shell::Sh()` — `sh -c` (Unix only: `/bin/sh`)
  - `.cmd()` / `Shell::Cmd()` — `cmd.exe /d /s /c` (Windows only, resolves via `COMSPEC` or `SystemRoot`)
  - `.powershell()` / `Shell::Powershell()` — `powershell -NoProfile -Command` (Windows only)
- **Pre-built instances**: `detail::named_args::shell`, `detail::named_args::bash`, `detail::named_args::powershell` (Windows). Dollar-named versions: `$shell`, `$bash`, `$powershell`.
- **`subprocess(Shell, command, named_args...)` constructor** — builds the subprocess with the shell's executable and prefix args, appending the user's command as the final argument. On Windows with cmd.exe, automatically uses `argv_to_command_line_for_cmd()`.
- **Public API overloads**: `run(Shell, command, args...)`, `capture_run(Shell, command, args...)`, `detach_run(Shell, command, args...)` — accept a `Shell` instance as the first argument.

### I/O Primitives

- **`detail::Device`** — a platform-specific device name struct: `NUL`/`CONOUT$`/`CONIN$` on Windows, `/dev/null`/`/dev/tty` on Unix. Predefined constants: `$devnull`, `$devttyout`, `$devttyin` (and `$devtty` as alias for `$devttyout` on Unix).
- **`detail::Pipe`** — anonymous pipe between parent and child. Uses `pipe2(O_CLOEXEC)` on Linux, `pipe` + `fcntl(FD_CLOEXEC)` on other Unix, and `CreatePipe` with non-inheritable `SECURITY_ATTRIBUTES` on Windows. Wraps `rfd_`/`wfd_` as `unique_fd` in a `shared_ptr<pipe_pair>` for cheap copying. Supports `read_some`, `read_exact`, `write_some`, `write_all`, `dup`, `close_read`, `close_write`, `close_all`.
- **`detail::File`** — a filesystem file with `OpenType` enum: `ReadOnly`, `WriteTruncate`, `WriteAppend`. Opens eagerly in the constructor via `open_impl()`. On Windows uses `CreateFileW` with `FILE_SHARE_READ`; on Unix uses `::open` with `O_CLOEXEC`. The path is stored as `wstring` on Windows, `string` on Unix. Has `is_valid()`, `dup()`.
- **`detail::FileHandler`** — wraps an already-open `NativeHandle` or `unique_fd`. Used for passing existing file descriptors/handles. Supports `dup`, `read`/`write` operations.
- **`detail::Buffer`** (capital B, the internal implementation detail) — an in-memory buffer with a backing `Pipe`. Holds a `shared_ptr<variant<buffer, ref<buffer>>>`. The variant allows referencing an external `buffer` or owning one internally. Tracks `written_size_` for stdin writing progress. `read_some()` reads from the pipe and appends to the buffer; `write_some()` writes from the buffer to the pipe; `empty()` checks if all data has been consumed from the buffer.
- **`detail::Readable`** / **`detail::Writable`** — abstract interfaces with virtual `read(void*, size_t)` / `write(const void*, size_t)` methods.
- **`detail::PipeReader`** / **`detail::PipeWriter`** — concrete implementations of `Readable`/`Writable` that hold a `shared_ptr<Pipe::pipe_pair>`, allowing read/write operations to survive the original `Pipe` object's lifetime.
- **`subprocess::buffer`** (lowercase b, the public type) — a `vector<unsigned char>` with an optional callback (`function<void(const unsigned char*, size_t)>`) invoked on each append. Provides `span()`, `to_string()`, `empty()`, `clear()` and extensive `operator==` overloads against `buffer`, `std::span<T>`, `std::basic_string_view<CharT>`, `std::basic_string<CharT>`, and raw character pointers (for `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`).

### I/O Redirection: `detail::Redirector` Hierarchy

`Redirector` is the abstract base. `StdinRedirector` (fd 0), `StdoutRedirector` (fd 1), `StderrRedirector` (fd 2) each wrap a `unique_ptr<variant<Pipe, File, Buffer, FileHandler>>`.

The redirector lifecycle protocol:

1. **`prepare_for_child()`** — parent calls before spawning. On Windows: makes the child-end handle inheritable via `SetHandleInformation`. Opens files (already opened eagerly, but this step handles inheritability).
2. **`close_child_end()`** — parent calls after spawning. Parent closes the end meant for the child (e.g., for stdin, parent closes the read end since the child reads).
3. **`close_parent_end()`** — (Unix only) child calls after fork. Uses `dup2()` to redirect to the target fd, then closes the pipe/file.

Additional redirector methods:

- **`child_inherit_handle()`** — (Windows only) returns the `HANDLE` the child should inherit.
- **`get_buffer()`** — returns `optional<reference_wrapper<Buffer>>` if the redirector contains a Buffer.
- **`get_file_fd()`** — returns `optional<reference_wrapper<const unique_fd>>` for File/FileHandler types.
- **`inherit()`** — returns `true` if no redirect is configured (i.e., child inherits parent's fd).
- **`close_all()`** — closes all handles in the redirector.
- **`dup()`** — deep-copies the redirector's contents.

### Named Arguments (the `$` syntax)

Defined in `detail::named_args` and re-exported in `subprocess::named_arguments`. Operator structs produce typed wrapper structs:

| Syntax                             | Produces                    | Notes                                    |
| ---------------------------------- | --------------------------- | ---------------------------------------- |
| `$stdin < pipe`                    | `StdinRedirector(Pipe)`     |                                          |
| `$stdin < "file"`                  | `StdinRedirector(File)`     | ReadOnly                                 |
| `$stdin < $devnull`                | `StdinRedirector(File)`     | ReadOnly from device                     |
| `$stdin < buffer`                  | `StdinRedirector(buffer&)`  |                                          |
| `$stdout > pipe`                   | `StdoutRedirector(Pipe)`    |                                          |
| `$stdout > "file"`                 | `StdoutRedirector(File)`    | WriteTruncate                            |
| `$stdout >> "file"`                | `StdoutRedirector(File)`    | WriteAppend                              |
| `$stdout > buffer`                 | `StdoutRedirector(buffer&)` | Clears buffer first                      |
| `$stdout >> buffer`                | `StdoutRedirector(buffer&)` | Appends (does not clear)                 |
| `$stderr > ...` / `$stderr >> ...` | `StderrRedirector(...)`     | Same semantics as stdout                 |
| `$cwd = path`                      | `Cwd`                       | string_view to platform-native string    |
| `$env = map`                       | `Env`                       | Replaces entire environment              |
| `$env += map`                      | `EnvAppend`                 | Merges into existing env                 |
| `$env["KEY"] += val`               | `EnvItemAppend`             | Appends to env var (with path separator) |
| `$env["KEY"] <<= val`              | `EnvItemAppend`             | Prepends to env var                      |
| `$timeout = ms`                    | `Timeout`                   | `chrono::milliseconds` or `int` seconds  |
| `$timeout_infinite`                | —                           | `INT_MAX`, disables timeout              |
| `$newgroup = true/false`           | `Newgroup`                  | Creates new process group / job object   |
| `$shell` / `$bash` / `$powershell` | `Shell`                     | Shell interpreter constants (see above)  |

On Windows, env keys are case-insensitive (uppercased for lookup). Path separator for `EnvItemAppend` is `;` on Windows, `:` on Unix.

### Public API Functions

All public functions reside in `namespace subprocess`.

The **variadic template functions** partition arguments at compile time using C++20 concepts: all string-like arguments must come first (they form the command + argument vector), followed by all named argument types.

**C++20 Concepts:**

| Concept                            | Matches                                                                 |
| ---------------------------------- | ----------------------------------------------------------------------- |
| `string_like_type<T>`              | `char*`, `const char*`, `string`, `string_view`, `filesystem::path`, and on Windows: `wchar_t*`, `const wchar_t*`, `wstring`, `wstring_view` |
| `named_argument_type<T>`           | `StdinRedirector`, `StdoutRedirector`, `StderrRedirector`, `Cwd`, `Env`, `EnvAppend`, `EnvItemAppend`, `Timeout`, `Newgroup` |
| `run_args_type<T>`                 | `named_argument_type \|\| string_like_type`                             |
| `named_argument_for_capture_type<T>` | `named_argument_type` excluding `StdoutRedirector` and `StderrRedirector` (capture_run manages its own stdout/stderr) |
| `capture_run_args_type<T>`         | `named_argument_for_capture_type \|\| string_like_type`                 |
| `named_argument_for_detach_type<T>` | `Env`, `Cwd`, `EnvAppend`, `EnvItemAppend` (detach_run does not accept I/O redirectors) |
| `detach_run_args_type<T>`          | `named_argument_for_detach_type \|\| string_like_type`                  |
| `partition_args<Args...>`          | Validates that all string-like args precede all named args              |

**Type traits for cross-encoding support:**
- `get_char_type<T>` / `get_char_type_t<T>` — extracts the character type from a string-like type.
- `to_string_view_t<T>` / `to_string_t<T>` — maps to `basic_string_view` / `basic_string` of the extracted character type.
- `convert_to_string<ToCharT>(from)` — converts between narrow and wide strings.

**Functions:**

- **`run(cmd_vector, named_args...)`** — takes a `vector<NativeString>` plus named arguments. Returns `int` exit code.
- **`run(app, args...)`** — variadic template that partitions arguments using `partition_args`. Returns `int` exit code.
- **`run(Shell, command, named_args...)`** — executes a command through a shell interpreter. Returns `int` exit code.
- **`capture_run(cmd_vector, named_args...)`** — like `run()` but returns `std::tuple<int, buffer, buffer>` (exit_code, stdout, stderr). Internally forces `$stdin < $devnull` and `$newgroup = true`, then attaches stdout/stderr to internal buffers. Accepts only non-stdout/stderr named arguments.
- **`capture_run(app, args...)`** — variadic overload, same partitioning logic.
- **`capture_run(Shell, command, args...)`** — shell variant of `capture_run`.
- **`detach_run(cmd_vector, named_args...)`** — spawns a detached/daemonized process. I/O is redirected to `/dev/null` (or `NUL`). Returns `bool` (spawn success). Only accepts `Env`, `Cwd`, `EnvAppend`, `EnvItemAppend` as named arguments.
- **`detach_run(app, args...)`** — variadic overload.
- **`detach_run(Shell, command, args...)`** — shell variant of `detach_run`.
- **`$(...)`** — alias for `run()` (controlled by `USE_DOLLAR_NAMED_VARIABLES` macro; enabled by default). Both `$(app, args...)` and `$(app, vector, named_args...)` overloads exist.

### Pipeline Support

`detail::pipeline` chains multiple `subprocess` objects with pipes using `operator|`:

```cpp
auto exit_code = (subprocess("prog1") | subprocess("prog2") | subprocess("prog3")).run();
```

- On construction/append, pipes are created between adjacent subprocesses: `subproc_n.stdout` → pipe → `subproc_{n+1}.stdin`.
- On Windows, all subprocesses share the first subprocess's `job_handle_`.
- On Unix, the first subprocess creates a new process group; subsequent subprocesses join that group (`requested_pgid_ = first.pid()`).
- `run()` launches all subprocesses sequentially via `spawn()`, then waits for all to exit. Returns the last subprocess's exit code.
- `exit_codes()` returns all exit codes as a `vector<int>`.
- `terminate()` delegates to the first subprocess's `terminate()`, which kills the entire job/process group.

### I/O Pump Strategy

- **Windows**: `read_write_to_buffer_with_threads()` — dedicated `std::thread` for each active redirector (stdin write loop, stdout read loop, stderr read loop). Each thread loops on `read_some()`/`write_some()` until EOF (returns 0) or error. Threads are joined in `wait()`.
- **Unix**: `read_write_to_buffer_use_poll()` — single-threaded `poll()` loop over up to 3 fds (stdin wfd, stdout rfd, stderr rfd). All fds are set to non-blocking. Key behavior:
  - Polls with 200ms timeout when monitoring the child, so it can periodically `waitpid(WNOHANG)` to detect child exit.
  - When the child exits, stores the status in `early_exit_status_` and starts a 2-second drain timer to collect final buffered pipe output from grandchildren.
  - Handles `POLLHUP`, `POLLERR`, `POLLNVAL` to detect pipe closure.
  - After the poll loop, does a final `waitpid(WNOHANG)` check and sets `child_reaped_early_` flag. This flag is checked by `wait()`.

### Timeout Mechanism

- **Unix**: Uses `detail::Timer` (a `shared_ptr<State>` based async timer with `condition_variable`). On timeout, calls `terminate()`. `stop_watchdog()` signals the timer to cancel in `wait()`.
- **Windows**: No separate watchdog thread. Instead, `WaitForSingleObject` in `wait()` directly uses the timeout value. If it times out, `terminate()` is called (which closes child handles, unblocking the I/O threads).

### Detach Mechanism

- **Unix**: `detach_spawn_posix()` does a **double-fork**: the parent forks, the child forks again, the child exits immediately, and the grandchild calls `setsid()` to become a session leader before exec. The parent calls `waitpid()` to reap the intermediate child, preventing zombies. stdin/stdout/stderr are redirected to `/dev/null`.
- **Windows**: `detach_spawn_win()` calls `CreateProcessW` with `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`. stdin/stdout/stderr are set to `NUL`. Both `hProcess` and `hThread` are closed immediately after spawn.

### Platform Abstraction Summary

| Feature              | Windows                                            | Unix                                                                        |
| -------------------- | -------------------------------------------------- | --------------------------------------------------------------------------- |
| Spawn API            | `CreateProcessW`                                   | `fork()` + `execv`/`execve`                                                 |
| Detach spawn         | `DETACHED_PROCESS \| CREATE_NEW_PROCESS_GROUP`      | Double-fork + `setsid()`                                                    |
| Executable search    | `SearchPathW` + `PATHEXT`, cwd-aware               | Manual `PATH` walk via `stat` + `access(X_OK)`, `realpath` for path-containing |
| Process tree mgmt    | Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)  | Process group (`setpgid` + `kill(-pgid)`)                                   |
| Handle inheritance   | Explicit `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`       | FDs set `O_CLOEXEC` / `FD_CLOEXEC` by default; parent-end closed after fork |
| Pipe creation        | `CreatePipe` (non-inheritable)                     | `pipe2(O_CLOEXEC)` on Linux, `pipe` + `fcntl(FD_CLOEXEC)` elsewhere         |
| I/O pump             | Multi-threaded `WriteFile`/`ReadFile`              | Single-threaded `poll()` + non-blocking I/O                                 |
| Timeout              | `WaitForSingleObject` timeout                      | `detail::Timer` + `condition_variable`                                      |
| String encoding      | UTF-16 (`wstring`) everywhere internally           | UTF-8 (`string`) internally                                                 |
| Environment          | `GetEnvironmentStringsW`, null-delimited block     | `environ`, null-delimited strings                                           |
| Error output         | `WriteConsoleW` (VT-aware) fallback to `WriteFile` | `write(STDERR_FILENO, ...)` with EINTR retry                                |
| Device paths         | `NUL`, `CONOUT$`, `CONIN$`                         | `/dev/null`, `/dev/tty`                                                     |
| Path separator (env) | `;`                                                | `:`                                                                         |

### Environment Handling

- Environment cascade in the `subprocess` constructor: base (current env) → `Env` (full replacement) → `EnvAppend` (merge) → `EnvItemAppend` (append/prepend to specific keys). If `EnvAppend` or `EnvItemAppend` are used without `Env`, the current process environment is used as the base.
- On Windows, env keys are case-insensitive: they are uppercased during lookup.

### Tests

Located in `tests/`. 22+ `.cc` test files each become their own test executable (with Unicode + ANSI variants on Windows) via `add_subprocess_test_suite()`, plus there is an `all_test` aggregate target linking all of them together. Tests use **Google Test** (v1.15.2, fetched via `FetchContent`) and the **environment** library (v0.0.6, from `shediao/environment.hpp`).

Key test files:

- `test_basic.cc` — basic spawn/exit code
- `test_io.cc` — stdin/stdout/stderr redirection
- `test_pipeline.cc` — pipeline chaining
- `test_timeout.cc` — timeout and termination
- `test_environment.cc` — environment variable manipulation
- `test_buffer_callback.cc` — buffer callback mechanism
- `test_capture_stdin.cc` — capture_run with stdin
- `test_error_handling.cc` — error paths (invalid commands, etc.)
- `test_concept.cc` — C++20 concept validation
- `test_variadic_types.cc` — variadic template argument handling
- `test_string_like_arguments.cc` — all string-like types (char*, string, string_view, wchar_t*, wstring, wstring_view) for run/capture_run/detach_run
- `test_split.cc` — string splitting utilities
- `test_dup.cc` — handle/fd duplication
- `test_multiplex.cc` — concurrent operations
- `test_newgroup.cc` — process group behavior
- `test_read_write_some.cc` — partial read/write
- `test_filename_with_space.cc` — paths with spaces
- `test_background.cc` — background process behavior
- `test_filesystem_path.cc` — `std::filesystem::path` support
- `test_executable_resolution.cc` — executable search path resolution
- `test_detach.cc` — detach_run comprehensive tests (smoke, CWD, env, variadic types, POSIX setsid/PPID checks)
- `test_shell_command.cc` — Shell class: bash/cmd/powershell, capture_run, complex commands, functions, pipelines
- `test_pipe_reader_writer.cc` — PipeReader/PipeWriter unit tests (round-trip, EOF, lifetime, polymorphism)

Test utility: `tests/utils.h` provides `TempFile` for creating cross-platform temporary files (via `GetTempFileNameA` on Windows, `mkstemps` on Unix) and `getTempFilePath()` for temporary path generation.

### Test CMakeLists Details

- `add_subprocess_test_suite(name sources...)` — creates test targets with proper compile options, sanitizer flags, coverage flags, and platform-specific defines. On Windows, creates both a Unicode variant (default) and an ANSI variant (`_ansi` suffix, without `UNICODE`/`_UNICODE` defines).
- Checks for `posix_spawn_file_actions_addchdir` and `posix_spawn_file_actions_addchdir_np` at configure time.
- Suppresses `-Wcharacter-conversion` for gtest/gmock targets when using Clang + C++20 (due to `char8_t` to `char32_t` implicit conversion in gtest-printers.h).
- Supports LLVM source-based code coverage via `SUBPROCESS_ENABLE_COVERAGE`.

### CI Configuration

Two GitHub Actions workflows:

1. **`cmake-multi-platform.yml`** — Matrix build across `ubuntu-latest`, `windows-latest`, `macos-latest` by `[Release, Debug]` by `[gcc, clang, cl]`. MSVC on Windows, GCC/Clang on Linux, AppleClang on macOS. Runs cmake configure, build, ctest with `--rerun-failed --output-on-failure`. Uses `actions/checkout@v5`.

2. **`msys2.yml`** — Matrix build on `windows-latest` across `[MSYS, UCRT64, MINGW64]` by `[Release, Debug]` by `[gcc, clang]`. Uses `msys2/setup-msys2@v2` action. Builds with Ninja generator. Uses `actions/checkout@v5`.

### Coverage Support

`SUBPROCESS_ENABLE_COVERAGE` option enables LLVM source-based code coverage (`-fprofile-instr-generate -fcoverage-mapping`). A `coverage` custom target runs tests via ctest, then generates summary, line coverage, and HTML reports with `llvm-profdata` + `llvm-cov`. Requires Clang compiler. The coverage script (`tests/run_coverage.cmake`) excludes test infrastructure (`tests/`, `googletest`, `gmock`, `environment`) from reports.

# Ai Coding Guidelines

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:

- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:

- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:

- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
