target_sources(UnitTests PRIVATE
  LuaManagerTests.cpp
  # The framework implementation is a translation unit (see LuaManager.h for why) - the tests
  # link it directly. Default test flags already have unwind tables.
  "${CMAKE_SOURCE_DIR}/Source/Features/Lua/Lua.cpp"
  # renderer.load_image decodes through the vendored stb_image (linked by the main target; the
  # tests compile Lua.cpp standalone, so the implementation TU comes along here too).
  "${CMAKE_SOURCE_DIR}/Source/ThirdParty/stb/stb_image.cpp"
  # gui_log::write (the Lua anomaly line) reaches LinuxPlatformApi::write/rename/pid/tid;
  # SharedObjectTests.cpp mocks only the dlopen family, so the rest is provided here.
  LinuxPlatformApiTestImpl.cpp
)
target_include_directories(UnitTests PRIVATE "${CMAKE_SOURCE_DIR}/Source/ThirdParty/luajit/src")
set_source_files_properties("${CMAKE_SOURCE_DIR}/Source/Features/Lua/Lua.cpp" PROPERTIES
  COMPILE_OPTIONS "-funwind-tables;-fasynchronous-unwind-tables")
