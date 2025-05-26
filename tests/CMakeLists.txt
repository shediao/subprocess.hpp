set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2)

FetchContent_MakeAvailable(googletest)

function(add_subprocess_test test_name)
  add_executable(${test_name} ${test_name}.cc)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
     OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
     OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(NOT MSVC)
      target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror)
    endif()
  endif()
  if(MSVC)
    target_compile_options(${test_name} PRIVATE /W4 /WX /utf-8 /EHsc)
  endif()
  target_link_libraries(${test_name} PRIVATE subprocess::subprocess
                                             GTest::gtest GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

file(GLOB test_files "*.cc")

foreach(test_file ${test_files})
  get_filename_component(test_name ${test_file} NAME_WE)
  add_subprocess_test(${test_name})
endforeach()
