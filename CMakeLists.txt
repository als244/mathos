cmake_minimum_required(VERSION 3.18)

project(mathos)

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_C_STANDARD 99)

set(PROJ_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(PROJ_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(src)
