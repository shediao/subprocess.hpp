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

This is a **header-only** C++20 library (`include/subprocess/subprocess.hpp`) for running child processes on Windows, Linux, and macOS. The entire implementation lives in a single header.

### Core class: `detail::subprocess`

The main implementation class. Holds the command vector, I/O redirectors, environment, cwd, timeout, and newgroup flags. Key methods:

- `async_run()` — spawns the child process (CreateProcessW on Windows, fork+exec or posix_spawn on Unix), then enters `pump_pipe_data()` to shuttle I/O between parent and child.
- `run()` — calls `async_run()` then `wait_for_exit()`.
- `wait_for_exit()` — blocks until the child exits, then returns the exit code.
- `terminate()` — kills the process tree on timeout. Uses `TerminateJobObject` (Windows) or `kill(-pid, SIGTERM)` → `SIGKILL` (Unix).

### I/O redirection: `detail::Redirector` hierarchy

`StdinRedirector` (fd 0), `StdoutRedirector` (fd 1), `StderrRedirector` (fd 2) each wrap a `std::variant<Pipe, File, Buffer, FileHandler>`:

- **`detail::Pipe`** — anonymous pipe between parent and child. Uses `pipe2(O_CLOEXEC)` on Linux, `fcntl(FD_CLOEXEC)` elsewhere. On Windows, handles are created non-inheritable; only the child-end is made inheritable.
- **`detail::File`** — a filesystem file opened for read or write.
- **`detail::Buffer`** — an in-memory buffer with a backing pipe; used by `capture_run()`.
- **`detail::FileHandler`** — wraps an already-open native handle/fd provided by the caller.

The `prepare_for_child()` / `close_child_end()` / `close_parent_end()` protocol ensures the correct end of each pipe is inherited by the child and closed by the parent (and vice versa).

### Named arguments (the `$` syntax)

Defined in `detail::named_args` and re-exported in `subprocess::named_arguments`. The operators return typed wrapper structs:

- `$stdin < source`, `$stdout > target`, `$stderr > target` — produce `StdinRedirector` / `StdoutRedirector` / `StderrRedirector`.
- `$cwd = path` → `Cwd`, `$env = map` → `Env`, `$env += map` → `EnvAppend`, `$env["KEY"] += val` → `EnvItemAppend`.
- `$timeout = duration` → `Timeout`, `$newgroup = true` → `Newgroup`.

### Public API functions

- **`run(...)`** — variadic template that partitions arguments into command parts (string-like) and named arguments (typed wrappers). Returns `int` exit code.
- **`capture_run(...)`** — like `run()` but returns `std::tuple<int, buffer, buffer>` (exit_code, stdout, stderr). Internally forces `$stdin < $devnull` and `$newgroup = true`, then attaches stdout/stderr to buffers.
- **`$(...)`** — alias for `run()` (controlled by `USE_DOLLAR_NAMED_VARIABLES` macro).

### Pipeline support

`detail::pipeline` chains multiple `subprocess` objects with pipes (`subproc1 | subproc2`). Uses job objects (Windows) or process groups (Unix) for unified lifecycle management.

### I/O pump strategy

- **Windows**: `read_write_to_buffer_with_threads()` — dedicated threads for stdin write, stdout read, stderr read.
- **Unix**: `read_write_to_buffer_use_poll()` — single-threaded `poll()` loop. When a child pid is provided (which it always is), the loop monitors child liveness via periodic `waitpid(WNOHANG)` and drains remaining pipe data for 2 seconds after the child exits. This prevents hanging when grandchildren inherit pipe write-ends.

### Timeout mechanism

A watchdog thread is launched by `async_run()`. If the timeout expires before `stop_watchdog()` is called (by `wait_for_exit()`), the watchdog calls `terminate()` which kills the entire process tree (Windows Job Object / Unix process group).

### Platform abstraction

The header uses `#if defined(_WIN32)` throughout. On Windows, all strings are converted to UTF-16 (`utf8_to_utf16`). Public APIs accept both `std::string` and `std::wstring`. Environment variables use the platform-native wide/narrow string types internally. POSIX has two spawn paths: traditional `fork`+`exec` and an optional `posix_spawn` path (opt-in via `SUBPROCESS_USE_POSIX_SPAWN`).

### Tests

Each `.cc` file in `tests/` becomes its own test executable plus there is an `all_test` target that links all of them together. Tests use Google Test (v1.15.2 fetched via FetchContent). The helper `tests/utils.h` provides `TempFile` for creating temporary files in cross-platform tests.
