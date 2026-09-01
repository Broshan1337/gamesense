cmake_minimum_required(VERSION 3.24)
# C is enabled for the vendored LuaJIT static library (Source/ThirdParty/luajit) - it is a C
# codebase; without the C language enabled CMake silently generates an EMPTY archive.
project(Neversneeze LANGUAGES CXX C)

# No build type configured (bare `cmake -B build`, or an IDE defaulting to Debug) must never
# silently produce a -O0 debug binary: besides being ~4x larger, a Debug build asserts at
# injection on any duplicate pattern ("Pattern should be unique!"). Default to Release.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  enable_language(ASM_MASM)
elseif(UNIX)
  # ASM (not just ASM-ATT): the vendored LuaJIT's lj_vm.S is a preprocessed-assembly source and
  # CMake 4 only assigns a language to .S files when the generic ASM language is enabled.
  enable_language(ASM)
  enable_language(ASM-ATT)
endif()

set(ENABLE_TESTS CACHE STRING "List of test types to enable e.g. \"unit;functional\"")

add_subdirectory(Source)

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
