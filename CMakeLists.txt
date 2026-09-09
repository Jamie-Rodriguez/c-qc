# Windows build (MSVC and clang-cl). Unix builds use the Makefile instead.
cmake_minimum_required(VERSION 3.15)
project(qc LANGUAGES C)

if(NOT WIN32)
  message(WARNING "CMake is the Windows build only; use GNU Make on Unix.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(smoke tests/smoke.c)
if(MSVC)
  target_compile_options(smoke PRIVATE /W4 /WX)
endif()

enable_testing()
add_test(NAME smoke COMMAND smoke)
