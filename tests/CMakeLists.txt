# standalone test project for the pure timing code, doesn't need Geode or Windows:
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
cmake_minimum_required(VERSION 3.21)
project(CBFTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(timing_tests test_timing.cpp)
target_include_directories(timing_tests PRIVATE ../src)
if (MSVC)
    target_compile_options(timing_tests PRIVATE /W4)
else()
    target_compile_options(timing_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME timing COMMAND timing_tests)
