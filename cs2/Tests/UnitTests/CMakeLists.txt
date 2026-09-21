add_executable(UnitTests)

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
