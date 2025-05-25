cmake_minimum_required(VERSION 3.15)
project(
  subprocess
  VERSION "0.1"
  LANGUAGES CXX)

set("PROJECT_DESCRIPTION" "A header-only lightweight subprocess for c++20")
set("PROJECT_HOMEPAGE_URL" "https://github.com/shediao/subprocess.hpp")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(SUBPROCESS_BUILD_TESTS "Set to ON to build tests" ON)
else()
  option(SUBPROCESS_BUILD_TESTS "Set to ON to build tests" OFF)
endif()

add_library(subprocess INTERFACE)

target_include_directories(
  subprocess INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                       $<INSTALL_INTERFACE:include>)

target_compile_features(subprocess INTERFACE cxx_std_20)
add_library(subprocess::subprocess ALIAS subprocess)

if(SUBPROCESS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(
  TARGETS subprocess
  EXPORT subprocess-targets
  INCLUDES
  DESTINATION include)

install(
  DIRECTORY include/
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.hpp")

install(
  EXPORT subprocess-targets
  FILE subprocess-targets.cmake
  NAMESPACE subprocess::
  DESTINATION lib/cmake/subprocess)
