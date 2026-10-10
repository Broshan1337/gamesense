add_executable(UnitTests)
# Match Source/CMakeLists.txt depth limits: the ConfigVariableTypes tuple instantiates
# recursively and GCC's default template depth (900) is too shallow for it (2026-10-10).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(UnitTests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fconstexpr-depth=4096 -ftemplate-depth=4096>)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(UnitTests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fconstexpr-depth=4096 -fbracket-depth=1024 -ftemplate-depth=2048>)
endif()

if(MSVC)
  target_compile_options(UnitTests PRIVATE "/MP")
endif()

target_include_directories(UnitTests PRIVATE "${CMAKE_SOURCE_DIR}/Tests" "${CMAKE_SOURCE_DIR}/Source")
# imgui is header-only from the tests' point of view (include path + public compile defines for
# Source/UI/ImGui/** headers); linking the static lib costs nothing but a few objects.
# luajit is linked for the same reason: the Lua scripting framework tests need the real runtime.
target_link_libraries(UnitTests gtest_main gmock Threads::Threads imgui luajit)
set_target_properties(UnitTests PROPERTIES OUTPUT_NAME UnitTestsBin)
gtest_discover_tests(UnitTests DISCOVERY_TIMEOUT 60 TEST_PREFIX Unit_)

add_subdirectory(Config)
add_subdirectory(Features)
add_subdirectory(GameClient)
add_subdirectory(MemorySearch)
add_subdirectory(Platform)
add_subdirectory(UI)
add_subdirectory(Utils)
add_subdirectory(Vac)
