cmake_minimum_required(VERSION 3.24)
project(NeversnoozeTF2 LANGUAGES CXX)

# Same reasoning as the cs2 tree: a bare `cmake -B build` must never silently
# produce a -O0 debug binary.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(Source)
