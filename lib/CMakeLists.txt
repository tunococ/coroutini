add_library(coroutini)
target_sources(coroutini
  PUBLIC FILE_SET coroutini TYPE CXX_MODULES
  FILES
    main.cppm
    future/future.cppm
    future/utils.cppm
)

