cmake_minimum_required(VERSION 3.28)
project(coroutini)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(PROJECT_IS_TOP_LEVEL)
  set(COROUTINI_DEV ON)
else()
  set(COROUTINI_DEV OFF)
endif()

add_subdirectory(lib)

if(COROUTINI_DEV)
  # Use Catch2 as the testing framework.
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1 # or a later release
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_DIR}/extras)
  include(CTest)
  include(Catch)

  add_subdirectory(tests)

  # Target "format" will invoke clang-format.
  find_program(CLANG_FORMAT clang-format)
  if(CLANG_FORMAT)
    file(GLOB_RECURSE files_to_format
      "lib/*.[ch]"
      "lib/*.[ch]h"
      "lib/*.[ch]pp"
      "lib/*.[ch]xx"
      "lib/*.cppm"
      "lib/*.cxxm"
      "tests/*.[ch]"
      "tests/*.[ch]h"
      "tests/*.[ch]pp"
      "tests/*.[ch]xx"
      "tests/*.cppm"
      "tests/*.cxxm"
    )
    message("files to format: ${files_to_format}")
    add_custom_target(
      format
      COMMAND ${CLANG_FORMAT} -i --style=file ${files_to_format}
      COMMENT "Formatting code with clang-format"
      VERBATIM
    )
  else()
    message(STATUS "clang-format not found. Skipping format target.")
  endif()
endif()
