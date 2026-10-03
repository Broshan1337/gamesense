cmake_minimum_required(VERSION 3.24)
# C is enabled for the vendored LuaJIT static library (Source/ThirdParty/luajit) - it is a C
# codebase; without the C language enabled CMake silently generates an EMPTY archive.
project(Neversnooze LANGUAGES CXX C)

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

# Ship hardening (Phase 1 of the anti-reversing pass): when ON, the Release link is fully
# stripped (.symtab removed) and the symbol table is preserved only in a local
# <build>/Source/ship-debug/<name>.debug copy + gnu-debuglink. Set at configure time:
#   cmake -B build -DNEVERSNOOZE_SHIP:BOOL=ON ...
option(NEVERSNOOZE_SHIP "Ship hardening: fully strip module, keep local .debug copy" ON)

# OLLVM obfuscation (Arkari/goron-derived clang): configure the tree with
#   cmake -B build -DNEVERSNOOZE_OBFUSCATE=ON -DCMAKE_C_COMPILER=<arkari-clang> -DCMAKE_CXX_COMPILER=<arkari-clang++> ...
# Per-FUNCTION policy comes from the annotate attributes in Utils/ObfAnnotations.h
# (+irobf-cff / +irobf-cse / ... on cold, secret-carrying code only - NEVER hot paths).
# The -arkari-cfg file carries the random seed; global obfuscation flags stay OFF so
# unannotated functions are compiled clean.
option(NEVERSNOOZE_OBFUSCATE "Arkari OLLVM: enable per-function obfuscation annotations" ON)
set(NEVERSNOOZE_ARKARI_CFG "$ENV{HOME}/obfuscator/arkari-cfg/cs2.cfg" CACHE FILEPATH "Arkari config (random seed)")
if(NEVERSNOOZE_OBFUSCATE)
  add_compile_definitions(NEVERSNOOZE_OBFUSCATE)
  add_compile_options("-mllvm" "-arkari-cfg=${NEVERSNOOZE_ARKARI_CFG}")
  # the pattern TypeList instantiates 463-argument fold expressions - clang's default
  # expression nesting limit (256) is far below GCC's; lift it (numbers are generous caps)
  add_compile_options("-fconstexpr-depth=4096" "-fbracket-depth=1024" "-ftemplate-depth=2048")
endif()

add_subdirectory(Source)

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
