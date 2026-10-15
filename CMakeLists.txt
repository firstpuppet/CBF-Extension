cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ClickBetweenFrames VERSION 1.0.0)

if (NOT WIN32)
    message(FATAL_ERROR "This mod only supports Windows. On Linux, cross-compile for Windows (see BUILDING.md).")
endif()

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/input.cpp
    src/game_layer.cpp
    src/player.cpp
    src/patches.cpp
    src/results.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
