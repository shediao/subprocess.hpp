set(CMAKE_CXX_STANDARD 20)

# ── Coverage: validate LLVM toolchain when enabled ────────────────────
if(SUBPROCESS_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(
      FATAL_ERROR
        "SUBPROCESS_ENABLE_COVERAGE=ON requires Clang (LLVM) compiler. "
        "Current compiler: ${CMAKE_CXX_COMPILER_ID}")
  endif()
  find_program(LLVM_PROFDATA_EXECUTABLE llvm-profdata REQUIRED)
  find_program(LLVM_COV_EXECUTABLE llvm-cov REQUIRED)
  message(STATUS "Coverage enabled")
  message(STATUS "  llvm-profdata: ${LLVM_PROFDATA_EXECUTABLE}")
  message(STATUS "  llvm-cov:      ${LLVM_COV_EXECUTABLE}")
endif()

# Default ENABLE_ASAN to ON only for Debug builds, unless user explicitly sets
# it.
if(NOT DEFINED SUBPROCESS_ENABLE_ASAN)
  if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(SUBPROCESS_ENABLE_ASAN
        ON
        CACHE BOOL "build asan for unittests")
  else()
    set(SUBPROCESS_ENABLE_ASAN
        OFF
        CACHE BOOL "build asan for unittests")
  endif()
endif()

set(INSTALL_GTEST OFF)

include(FetchContent)

if(NOT CMAKE_HOST_SYSTEM_NAME MATCHES "Windows" AND MSVC)
  set(gtest_disable_pthreads
      ON
      CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  environment
  GIT_REPOSITORY https://github.com/shediao/environment.hpp
  GIT_TAG v0.0.6)
FetchContent_MakeAvailable(environment)

# Suppress -Wcharacter-conversion for gtest/gmock when using Clang and C++20.
# char8_t (C++20) to char32_t implicit conversion in gtest-printers.h triggers
# this warning, which becomes an error under -WX.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  foreach(gtarget gtest gtest_main gmock gmock_main)
    if(TARGET ${gtarget})
      target_compile_options(${gtarget} PRIVATE -Wno-character-conversion)
    endif()
  endforeach()
endif()
foreach(gtarget gtest gmock gmock_main gtest_main)
  set_target_properties(${gtarget} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
endforeach()

# --- Compiler and Platform Specific Configuration ---

set(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR
    ""
    CACHE INTERNAL "Have posix_spawn_file_actions_addchdir")
set(HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP
    ""
    CACHE INTERNAL "Have posix_spawn_file_actions_addchdir_np")

if(NOT WINDOWS)
  include(CheckSymbolExists)
  check_symbol_exists(posix_spawn_file_actions_addchdir "spawn.h"
                      HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
  check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h"
                      HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)

  message(
    STATUS
      "Checking for posix_spawn_file_actions_addchdir_np: ${HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP}"
  )
  message(
    STATUS
      "Checking for posix_spawn_file_actions_addchdir: ${HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR}"
  )
endif()

# Define common compile options based on the compiler.
set(SUBPROCESS_MSVC_COMPILE_OPTIONS "/W4" "/WX" "/utf-8" "/EHsc")
set(SUBPROCESS_GCC_CLANG_COMPILE_OPTIONS "-Wall" "-Wextra" "-Werror")

if(MSVC)
  set(SUBPROCESS_COMMON_COMPILE_OPTIONS ${SUBPROCESS_MSVC_COMPILE_OPTIONS})
else()
  set(SUBPROCESS_COMMON_COMPILE_OPTIONS ${SUBPROCESS_GCC_CLANG_COMPILE_OPTIONS})
endif()

# Configure sanitizer flags for Debug builds on Linux and macOS when not
# cross-compiling.
set(SUBPROCESS_SANITIZER_CXX_FLAGS "")
set(SUBPROCESS_SANITIZER_LINK_FLAGS "")
if(SUBPROCESS_ENABLE_ASAN)
  if(NOT CMAKE_CROSSCOMPILING AND CMAKE_HOST_SYSTEM_NAME MATCHES "Linux|Darwin")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
      list(
        APPEND
        SUBPROCESS_SANITIZER_CXX_FLAGS
        "-fsanitize=address,undefined"
        "-fno-omit-frame-pointer"
        "-fno-optimize-sibling-calls"
        "-g"
        "-O0")
      list(APPEND SUBPROCESS_SANITIZER_LINK_FLAGS
           "-fsanitize=address,undefined")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
      list(APPEND SUBPROCESS_SANITIZER_CXX_FLAGS "/fsanitize=address")
    endif()
  endif()
endif()

# --- Helper Function for Creating Tests ---

# A helper function to create test targets. It handles the logic for creating
# standard and, on Windows, ANSI targets.
function(add_subprocess_test_suite test_name)
  set(sources ${ARGN})
  set(variants "")
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(variants "default" "ansi")
  else()
    set(variants "default")
  endif()

  foreach(variant IN LISTS variants)
    set(target_name ${test_name})

    if(NOT variant STREQUAL "default")
      set(target_name "${test_name}_${variant}")
    endif()

    # Add the executable target
    add_executable(${target_name} ${sources})

    # Set compile options
    target_compile_options(${target_name}
                           PRIVATE ${SUBPROCESS_COMMON_COMPILE_OPTIONS})

    if(SUBPROCESS_SANITIZER_CXX_FLAGS)
      target_compile_options(
        ${target_name}
        PRIVATE "$<$<CONFIG:Debug>:${SUBPROCESS_SANITIZER_CXX_FLAGS}>")
    endif()
    if(SUBPROCESS_SANITIZER_LINK_FLAGS)
      target_link_options(
        ${target_name} PRIVATE
        "$<$<CONFIG:Debug>:${SUBPROCESS_SANITIZER_LINK_FLAGS}>")
    endif()

    # Coverage flags (Clang source-based instrumentation)
    if(SUBPROCESS_ENABLE_COVERAGE)
      target_compile_options(${target_name} PRIVATE -fprofile-instr-generate
                                                    -fcoverage-mapping)
      target_link_options(${target_name} PRIVATE -fprofile-instr-generate)
    endif()

    # Set compile definitions for the UNICODE variant on Windows
    if(NOT variant STREQUAL "ansi")
      target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE)
    endif()

    if(NOT WINDOWS AND "${HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR}" STREQUAL "1")
      target_compile_definitions(${target_name}
                                 PRIVATE HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
    endif()
    if(NOT WINDOWS AND "${HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP}" STREQUAL
                       "1")
      target_compile_definitions(
        ${target_name} PRIVATE HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    endif()

    # Link libraries
    target_link_libraries(
      ${target_name} PRIVATE subprocess::subprocess GTest::gtest
                             GTest::gtest_main environment::environment)

    # Add to CTest
    add_test(NAME ${target_name} COMMAND ${target_name})
  endforeach()
endfunction()

# --- Test Target Definitions ---

# Glob all test source files.
file(
  GLOB test_sources
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*.cc")

# Create a separate test executable for each source file.
foreach(test_source IN LISTS test_sources)
  get_filename_component(test_name ${test_source} NAME_WE)
  add_subprocess_test_suite(${test_name} ${test_source})
endforeach()

add_subprocess_test_suite(all_test "${test_sources}")

# ── Coverage target ──────────────────────────────────────────────────
if(SUBPROCESS_ENABLE_COVERAGE)
  add_custom_target(
    coverage
    COMMAND
      ${CMAKE_COMMAND} -D "COVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}" -D
      "ALL_TEST_BINARY=$<TARGET_FILE:all_test>" -D
      "LLVM_PROFDATA=${LLVM_PROFDATA_EXECUTABLE}" -D
      "LLVM_COV=${LLVM_COV_EXECUTABLE}" -D
      "CTEST_COMMAND=${CMAKE_CTEST_COMMAND}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/run_coverage.cmake"
    DEPENDS all_test
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running tests and generating code coverage reports..."
    VERBATIM)
endif()
