add_library(hooks STATIC
  vac_hook.cpp
  analyze_pe_module_hook.cpp
  integrity_audit.cpp
  game_state_resolver.cpp
  memory_search/memory_search.cpp
  memory_search/byte_pattern.cpp
  memory_search/pattern_finder_simd.cpp
  memory_search/pattern_search_result.cpp
  memory_search/byte_pattern_storage.cpp
  memory_search/byte_pattern_literal.cpp
  memory_search/byte_pattern_converter.cpp
)

target_include_directories(hooks PRIVATE ..)
target_link_libraries(hooks PRIVATE ${CMAKE_DL_LIBS})

target_compile_definitions(hooks PRIVATE
  IMGUI_DEFINE_MATH_OPERATORS
  IMGUI_DISABLE_DEFAULT_ALLOCATORS
  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
  IMGUI_DISABLE_FILE_FUNCTIONS
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

target_link_libraries(hooks PUBLIC imgui m)