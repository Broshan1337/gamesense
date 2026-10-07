add_subdirectory(Combat)
add_subdirectory(Hud)
add_subdirectory(Lua)
add_subdirectory(Radio)
add_subdirectory(Visuals)

target_sources(UnitTests PRIVATE Game/AirStrafeTests.cpp)
