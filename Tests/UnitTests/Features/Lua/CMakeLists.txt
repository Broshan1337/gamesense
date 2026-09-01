target_sources(UnitTests PRIVATE
  LuaManagerTests.cpp
  # The framework implementation is a translation unit (see LuaManager.h for why) - the tests
  # link it directly. Default test flags already have unwind tables.
  "${CMAKE_SOURCE_DIR}/Source/Features/Lua/Lua.cpp"
)
target_include_directories(UnitTests PRIVATE "${CMAKE_SOURCE_DIR}/Source/ThirdParty/luajit/src")
set_source_files_properties("${CMAKE_SOURCE_DIR}/Source/Features/Lua/Lua.cpp" PROPERTIES
  COMPILE_OPTIONS "-funwind-tables;-fasynchronous-unwind-tables")
