# coroutini

C++ task management utilities

## User guide

TODO: Write this section.

## Developer guide

### Prerequisites

- CMake

- Ninja

- GCC or Clang

### Setting up `build` directory

There are a few ways to initialize the repository for development:

1. Using CMake configure preset

   At the root of the repository, call

   ```shell
   cmake --preset dev-gcc
   ```

   if you are using GCC, or

   ```shell
   cmake --preset dev-clang
   ```

   if you are using Clang.

2. Using a full CMake workflow

   At the root of the repository, call

   ```shell
   cmake --workflow --preset dev-gcc
   ```

   if you are using GCC, or

   ```shell
   cmake --workflow --preset dev-clang
   ```

   if you are using Clang.

   This is similar to option 1, but it will also try to compile the code and
   run test cases.

3. Using a traditional CMake configuration

   At the root of the repository, call

   ```shell
   cmake -B build -G Ninja -S .
   ```

   Note: `Ninja` is required for C++ modules at the time of this writing.

   You can set additional options and/or environment variables for this
   command.
   For example, you can pick the compiler to use by setting the environment
   variables `CC` and `CXX`.

   See [this](https://cmake.org/cmake/help/latest/manual/cmake.1.html#generate-a-project-buildsystem)
   for more information, or see [CMakePresets.json](CMakePresets.json) for an
   example of how Clang or GCC can be chosen.

Afterwards, a subdirectory named `build` will be created, in which you can
perform all the CMake operations.

### Inside `build` directory

#### Building code

Once you are inside the `build` subdirectory, you can (re)build code simply by
calling:

```shell
cmake --build .
```

#### Running tests

You can run tests by calling the test executable directly or by calling
`ctest`.
Note that `ctest` does not support command-line arguments the same way that the
test executable does.
For example, if you want to run only one section inside a test case, you should
use the test executable directly.

The build process will create the unit test executable named
[`build/tests/coroutini_test`](`build/tests/coroutini_test`).
Our test code is made with Catch2, so please consult [their documentation](
https://github.com/catchorg/Catch2/blob/devel/docs/command-line.md#top)
for information on command-line arguments.

#### Code coverage summary from `ctest`

Prerequisites:

- You must have lcov installed on your system.

- You must use GCC as the compiler.

The following `ctest` command can produce the code coverage summary:

```shell
ctest -T Coverage
```

This must be executed after the test code was run.

Alternatively, you can build the `lcov` target to get the coverage information
with

```shell
cmake --build . --target=lcov
```

This will build the code, run the test, and create an HTML report in the
directory [`build/coverage_html`](build/coverage_html).
The entry point is [`index.html`](build/coverage_html/index.html) in that
directory.
