# run_coverage.cmake Executed via: cmake -D... -P run_coverage.cmake Expects
# these -D variables: COVERAGE_BINARY_DIR  - CMake binary directory
# ALL_TEST_BINARY      - path to the all_test executable LLVM_PROFDATA        -
# path to llvm-profdata LLVM_COV             - path to llvm-cov CTEST_COMMAND -
# path to ctest

# ── Resolve absolute paths early ─────────────────────────────────────
get_filename_component(COVERAGE_DIR "${COVERAGE_BINARY_DIR}/coverage" ABSOLUTE)
get_filename_component(ALL_TEST_BINARY "${ALL_TEST_BINARY}" ABSOLUTE)

# ── 1. Clean previous coverage data ──────────────────────────────────
file(REMOVE_RECURSE "${COVERAGE_DIR}")
file(MAKE_DIRECTORY "${COVERAGE_DIR}")

# ── 2. Run tests with profiling enabled ────────────────────────────── Use an
# absolute path so profiles land in the right place regardless of each test's
# working directory (ctest sets CWD = test executable dir).
set(ENV{LLVM_PROFILE_FILE} "${COVERAGE_DIR}/%p.profraw")

message(STATUS "Running tests with coverage instrumentation...")
execute_process(
  COMMAND "${CTEST_COMMAND}" --output-on-failure
  WORKING_DIRECTORY "${COVERAGE_BINARY_DIR}"
  RESULT_VARIABLE ctest_result
  OUTPUT_VARIABLE ctest_output
  ERROR_VARIABLE ctest_error)
message("${ctest_output}")
if(NOT ctest_result EQUAL 0)
  message(WARNING "Some tests failed (exit code ${ctest_result}). "
                  "Coverage data may be incomplete.")
endif()

# ── 3. Locate profraw files ──────────────────────────────────────────
file(GLOB profraw_files "${COVERAGE_DIR}/*.profraw")
list(LENGTH profraw_files profraw_count)

if(profraw_count EQUAL 0)
  # Also search one level deeper (belt-and-suspenders for ctest CWD issues)
  file(GLOB_RECURSE profraw_files "${COVERAGE_DIR}/*.profraw")
  list(LENGTH profraw_files profraw_count)
endif()

if(profraw_count EQUAL 0)
  message(FATAL_ERROR "No .profraw files found under ${COVERAGE_DIR}. "
                      "Did the tests actually run?")
endif()

message(STATUS "Found ${profraw_count} profile data file(s)")

# ── 4. Merge profile data ────────────────────────────────────────────
set(profdata "${COVERAGE_DIR}/coverage.profdata")

message(STATUS "Merging profile data...")
execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${profraw_files} -o "${profdata}"
  RESULT_VARIABLE merge_result
  OUTPUT_VARIABLE merge_output
  ERROR_VARIABLE merge_error)
if(NOT merge_result EQUAL 0)
  message(
    FATAL_ERROR
      "llvm-profdata merge failed (exit ${merge_result}):\n${merge_error}")
endif()

if(NOT EXISTS "${profdata}")
  message(FATAL_ERROR "llvm-profdata did not produce ${profdata}")
endif()

# Regex to exclude test infrastructure from coverage reports. Keeps only project
# source (subprocess.hpp). Adjust if you add more headers under include/.
set(exclude_regex "tests/|googletest|_deps|gmock|environment")

# ── 5. Summary report (terminal) ─────────────────────────────────────
message("\n========== Coverage Summary ==========")
execute_process(
  COMMAND "${LLVM_COV}" report "--instr-profile=${profdata}"
          "${ALL_TEST_BINARY}" "-ignore-filename-regex=${exclude_regex}"
  RESULT_VARIABLE report_result
  OUTPUT_VARIABLE report_output
  ERROR_VARIABLE report_error)
message("${report_output}")
if(NOT report_result EQUAL 0)
  message(
    WARNING "llvm-cov report failed (exit ${report_result}):\n${report_error}")
endif()

# ── 6. Line-by-line coverage → file ──────────────────────────────────
set(lines_file "${COVERAGE_DIR}/lines.txt")
message(STATUS "Writing line coverage to ${lines_file} ...")
execute_process(
  COMMAND
    "${LLVM_COV}" show "--instr-profile=${profdata}" "${ALL_TEST_BINARY}"
    --show-line-counts-or-regions "-ignore-filename-regex=${exclude_regex}"
  RESULT_VARIABLE lines_result
  OUTPUT_FILE "${lines_file}"
  ERROR_VARIABLE lines_error)
if(NOT lines_result EQUAL 0)
  message(
    WARNING
      "llvm-cov show (lines) failed (exit ${lines_result}):\n${lines_error}")
else()
  message(STATUS "Line coverage written to ${lines_file}")
endif()

# ── 7. HTML report ───────────────────────────────────────────────────
set(html_dir "${COVERAGE_DIR}/html")
message(STATUS "Generating HTML coverage report in ${html_dir} ...")
execute_process(
  COMMAND
    "${LLVM_COV}" show "--instr-profile=${profdata}" "${ALL_TEST_BINARY}"
    --format=html "--output-dir=${html_dir}"
    "-ignore-filename-regex=${exclude_regex}"
  RESULT_VARIABLE html_result
  ERROR_VARIABLE html_error)
if(NOT html_result EQUAL 0)
  message(
    WARNING "llvm-cov show (HTML) failed (exit ${html_result}):\n${html_error}")
else()
  message("HTML report: file://${html_dir}/index.html")
endif()

# ── 8. Done ──────────────────────────────────────────────────────────
message("\n========== Coverage Complete ==========")
message("Summary : (see above)")
message("Lines   : ${lines_file}")
message("HTML    : file://${html_dir}/index.html")
