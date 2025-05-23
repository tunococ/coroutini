if(COROUTINI_COVERAGE)
  add_compile_options(--coverage)
endif()

add_library(coroutini)
target_sources(coroutini
  PUBLIC FILE_SET coroutini TYPE CXX_MODULES
  FILES
    main.cppm
    future/future.cppm
    future/main.cppm
    future/utils.cppm
    thread_pool/thread_pool.cppm
)

add_library(coroutini_utils)
target_sources(coroutini_utils
  PUBLIC FILE_SET coroutini_utils TYPE CXX_MODULES
  FILES
    utils/main.cppm
    utils/manual_clock.cppm
    utils/defer.cppm
)

