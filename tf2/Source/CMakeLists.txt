add_library(NeversnoozeTF2 SHARED
  Tf2Module.cpp
  Tf2Diagnostics.cpp
  Tf2Log.cpp
  Vulkan/VkPresentHook.cpp
  Vulkan/VkPresentSlotHook.cpp)

set_target_properties(NeversnoozeTF2 PROPERTIES
  # Output file name must stay libMangoHud.so: same disguise as the CS2 module (a normal
  # thing to see in a game's maps), and the Loader's mapped-state checks key on it.
  OUTPUT_NAME "MangoHud"
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(NeversnoozeTF2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(NeversnoozeTF2 PRIVATE ${CMAKE_DL_LIBS})
