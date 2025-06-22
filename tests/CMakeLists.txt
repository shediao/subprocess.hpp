# Set the C++ standard to 20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch and configure googletest.
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

# --- Compiler and Platform Specific Configuration ---

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
if(NOT CMAKE_CROSSCOMPILING)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    list(
      APPEND
      SUBPROCESS_SANITIZER_CXX_FLAGS
      "-fsanitize=address,undefined"
      "-fno-omit-frame-pointer"
      "-fno-optimize-sibling-calls"
      "-g"
      "-O0")
    list(APPEND SUBPROCESS_SANITIZER_LINK_FLAGS "-fsanitize=address,undefined")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    list(APPEND SUBPROCESS_SANITIZER_CXX_FLAGS "/fsanitize=address")
  endif()
endif()

# --- Helper Function for Creating Tests ---

# A helper function to create test targets. It handles the logic for creating
# standard and, on Windows, ANSI targets.
function(add_subprocess_test_suite test_name)
  set(sources ${ARGN})
  set(variants "")
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(variants "unicode" "ansi")
  else()
    set(variants "default")
  endif()

  foreach(variant IN LISTS variants)
    set(target_name ${test_name})
    set(test_name_suffix "")

    if(variant STREQUAL "ansi")
      set(target_name "${test_name}_ansi")
      set(test_name_suffix "_ansi")
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

    # Set compile definitions for the UNICODE variant on Windows
    if(variant STREQUAL "unicode")
      target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE)
    endif()

    # Link libraries
    target_link_libraries(${target_name} PRIVATE subprocess::subprocess
                                                 GTest::gtest GTest::gtest_main)

    # Add to CTest
    add_test(NAME ${test_name}${test_name_suffix} COMMAND ${target_name})
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

# Create a single test executable containing all tests.
add_subprocess_test_suite(all_tests ${test_sources})
