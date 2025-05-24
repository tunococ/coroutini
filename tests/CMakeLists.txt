if(COROUTINI_COVERAGE)
  add_compile_options(--coverage -O0)
  add_link_options(--coverage)

  add_custom_command(
    OUTPUT tests/CMakeFiles/coroutini_test.dir/blank.cpp.gcda
    COMMAND cmake --build . --target=coroutini_test
    COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
    COMMAND ctest -T Test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM
  )

  find_program(LCOV lcov)
  if(NOT LCOV)
    message(WARNING "lcov not found. Please install it first.")
  else()
    add_custom_target(lcov
      COMMAND ${LCOV} -c -d .
        --ignore-errors inconsistent,negative
        -o coverage.info
      COMMAND ${LCOV} -r coverage.info '/usr/*'
        --ignore-errors inconsistent
        -o coverage.info
      COMMAND ${LCOV} -r coverage.info 'build/_deps/*'
        --ignore-errors inconsistent
        -o coverage.info
      COMMAND genhtml coverage.info
        -o coverage_html
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report..."
      DEPENDS tests/CMakeFiles/coroutini_test.dir/blank.cpp.gcda
    )
  endif()
endif()

add_executable(coroutini_test)
target_sources(coroutini_test
  PRIVATE
    blank.cpp
    future.cpp
    thread_pool/thread_pool.cpp
    utils/defer.cpp
    utils/manual_clock.cpp
)
target_link_libraries(coroutini_test
  PRIVATE
    coroutini
    coroutini_utils
    Catch2::Catch2WithMain
)
catch_discover_tests(coroutini_test)

