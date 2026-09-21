add_library(Neversnooze SHARED dllmain.cpp)

set_target_properties(Neversnooze PROPERTIES
  # Output file name must stay libMangoHud.so: it is disguised as the legit MangoHud overlay
  # library (a normal thing to see in a game's maps), and inject.sh, inject_memfd, self-unload
  # (SONAME matching) plus the module-hiding code all key on it. The CMake target keeps the
  # project branding.
  OUTPUT_NAME "MangoHud"
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(Neversnooze PRIVATE "${CMAKE_SOURCE_DIR}/Source" "${CMAKE_SOURCE_DIR}/Source/hooks")
target_link_libraries(Neversnooze PRIVATE ${CMAKE_DL_LIBS})

# ---- Steam Module (separate target for Steam process injection) --------------------
add_library(SteamModule SHARED SteamModule/steam_module.cpp)

set_target_properties(SteamModule PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME "SteamModule")

target_include_directories(SteamModule PRIVATE "${CMAKE_SOURCE_DIR}/Source")
target_link_libraries(SteamModule PRIVATE ${CMAKE_DL_LIBS} pthread)

# ---- Steam Module 32-bit (Steam runs as 32-bit process) ---------------------------
# Built in its OWN tree (build-steam, see inject.sh), NOT here: both modules use the source file
# and output name, so defining both targets in one build directory makes two rules generate
# Source/libSteamModule.so - ninja aborts every build of the tree on it. Configure with
# -DNEVERSNOOZE_BUILD_STEAM_MODULE32=ON (as build-steam is) to get this target.
option(NEVERSNOOZE_BUILD_STEAM_MODULE32 "Also define the 32-bit SteamModule target (build-steam tree only)" OFF)
if(NEVERSNOOZE_BUILD_STEAM_MODULE32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  add_library(SteamModule32 SHARED SteamModule/steam_module.cpp)
  
  set_target_properties(SteamModule32 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "SteamModule")
  
  target_include_directories(SteamModule32 PRIVATE "${CMAKE_SOURCE_DIR}/Source")
  target_link_libraries(SteamModule32 PRIVATE ${CMAKE_DL_LIBS} pthread)
  
  target_compile_options(SteamModule32 PRIVATE -m32)
  target_link_options(SteamModule32 PRIVATE -m32)
endif()

# ---- SDL3 headers (UI input backend) ----------------------------------------------------
#
# Headers only - nothing is linked. At runtime the backend resolves the handful of SDL functions
# it needs (window size, performance counter, text input, ...) from the game's own libSDL3
# through the existing SdlDll dlsym pattern, exactly like every other SDL use in this tree. The
# SOURCE_SUBDIR trick points MakeAvailable at a directory without a CMakeLists.txt so the SDL
# build system is never configured - we only want the headers.
include(FetchContent)
FetchContent_Declare(sdl3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL
  GIT_TAG release-3.2.18
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR include
  UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(sdl3)

add_library(sdl3_headers INTERFACE)
target_include_directories(sdl3_headers INTERFACE "${sdl3_SOURCE_DIR}/include")
target_link_libraries(Neversnooze PRIVATE sdl3_headers)

# ---- Dear ImGui (freestanding bridging model) ------------------------------------------
#
# The UI migration replaces Panorama with ImGui (Source/UI/ImGui/**). ImGui is bridged into
# this -nostdlib freestanding target instead of linking the CRT:
#   * core imgui only (no demo, no misc/cpp imgui_stdlib): no exceptions, no RTTI, no
#     std:: containers, no operator new - allocations go through ImGui::SetAllocatorFunctions
#     bridged to CS2's IMemAlloc (Source/UI/ImGui/ImGuiMemAllocBridge.h).
#   * IMGUI_DISABLE_FILE_FUNCTIONS keeps imgui off stdio file IO (fonts are loaded through
#     LinuxPlatformApi in our own code).
#   * the Vulkan render backend is compiled with IMGUI_IMPL_VULKAN_NO_PROTOTYPES: all vk*
#     functions are pointers resolved via ImGui_ImplVulkan_LoadFunctions from the game's
#     already-loaded libvulkan, so nothing references libvulkan at link time and the
#     LINKER:--no-undefined guarantee below stays intact.
#   * libm is linked explicitly below: ImGui's own math (fmodf/powf/atan2f/... in cold UI
#     paths) calls it. libm.so.6 is already mapped by the game, so this adds one DT_NEEDED
#     that resolves to the existing mapping. -fno-math-errno on this target is unaffected
#     (it only controls how THIS target's own code compiles; Trig.h's SQRTSS assumption holds).
#   * the same IMGUI_* defines are PUBLIC so Source/UI/ImGui/** sees a consistent imgui build
#     regardless of which target includes the headers.
#
# Patches under ${CMAKE_SOURCE_DIR}/Patches/ImGUI/ are carried over from the UI donor
# (FORFUTURETESTS/somecs2baseforlinux): the two overlay-draw-list patches are prerequisites for
# future ImGui-based world overlay drawing, the two rounding patches are pure visual polish.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_PROGRESS TRUE
  GIT_TAG v1.91.7
  GIT_SHALLOW TRUE
  PATCH_COMMAND git apply "${CMAKE_SOURCE_DIR}/Patches/ImGUI/FullyRoundedTabs.patch"
                          "${CMAKE_SOURCE_DIR}/Patches/ImGUI/RoundedImageButtons.patch"
                          "${CMAKE_SOURCE_DIR}/Patches/ImGUI/PrependDrawList.patch"
                          "${CMAKE_SOURCE_DIR}/Patches/ImGUI/ExposeSetupDrawListSharedData.patch"
  UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(imgui)

file(GLOB IMGUI_SOURCE_FILES "${imgui_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM IMGUI_SOURCE_FILES "${imgui_SOURCE_DIR}/imgui_demo.cpp") # never built
list(APPEND IMGUI_SOURCE_FILES "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

add_library(imgui STATIC ${IMGUI_SOURCE_FILES})
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(imgui PUBLIC
  IMGUI_DEFINE_MATH_OPERATORS
  IMGUI_DISABLE_DEFAULT_ALLOCATORS
  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
  IMGUI_DISABLE_FILE_FUNCTIONS
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
# Header-only: provides Vulkan::Headers (vulkan_core.h etc.); no Vulkan library is linked -
# the backend loads vk* functions at runtime (IMGUI_IMPL_VULKAN_NO_PROTOTYPES).
find_package(Vulkan REQUIRED)
target_link_libraries(imgui PUBLIC Vulkan::Headers)
set_target_properties(imgui PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON
  LINKER_LANGUAGE CXX)

# Mirror this target's own freestanding flags onto imgui so none of its objects can reference
# exception/RTTI runtime machinery that a -nostdlib link would reject (imgui itself is
# compatible with all of these).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(UNIX)
    target_compile_options(imgui PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-math-errno>)
  endif()
endif()

target_link_libraries(Neversnooze PRIVATE imgui m)

# ---- LuaJIT 2.1 (vendored, pre-generated codegen) ---------------------------------------
# Vendored copy of github.com/LuaJIT/LuaJIT (commit 1ee778a4, 2026-08-19, rolling 2.1) in
# Source/ThirdParty/luajit. The buildvm/minilua host-tool codegen steps were run ONCE at
# vendor time; their outputs (lj_vm.S, lj_bcdef.h, lj_ffdef.h, lj_libdef.h, lj_recdef.h,
# lj_folddef.h, jit/vmdef.lua, luajit.h) are committed inside src/, so our build needs no host
# tools and no git. Regenerate only when updating the vendored LuaJIT (build it with its own
# Makefile and copy the generated files back).
# GC64 is the rolling-release default on x64 (see lj_arch.h) - required for a DSO injected at
# arbitrary addresses: non-GC64 packs pointers into 47-bit values and breaks outside the
# assumptions it makes about heap address ranges.
file(GLOB LUAJIT_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src/lib_*.c")
message(STATUS "LUAJIT lib sources: ${LUAJIT_LIB_SOURCES}")
file(GLOB LUAJIT_LJ_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src/lj_*.c")
message(STATUS "LUAJIT at add_library: LIB=${LUAJIT_LIB_SOURCES}")
add_library(luajit STATIC
  ${LUAJIT_LIB_SOURCES} ${LUAJIT_LJ_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src/lj_vm.S")
get_target_property(LUAJIT_SOURCES luajit SOURCES)
list(GET LUAJIT_SOURCES 0 LUAJIT_FIRST_SOURCE)
get_source_file_property(LUAJIT_VM_LANGUAGE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src/lj_vm.S" LANGUAGE)
get_source_file_property(LUAJIT_VM_HEADER_ONLY "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src/lj_vm.S" HEADER_FILE_ONLY)
message(STATUS "LUAJIT lj_vm.S language: ${LUAJIT_VM_LANGUAGE} headerOnly=${LUAJIT_VM_HEADER_ONLY}")
get_source_file_property(LUAJIT_FIRST_GENERATED ${LUAJIT_FIRST_SOURCE} GENERATED)
get_source_file_property(LUAJIT_FIRST_EXTERNAL ${LUAJIT_FIRST_SOURCE} EXTERNAL_OBJECT)
message(STATUS "LUAJIT first source: ${LUAJIT_FIRST_SOURCE} headerOnly=${LUAJIT_FIRST_HEADER_ONLY} generated=${LUAJIT_FIRST_GENERATED} external=${LUAJIT_FIRST_EXTERNAL}")
target_include_directories(luajit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/luajit/src")
target_compile_definitions(luajit PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE
  # x64 LuaJIT errors unwind through libgcc's DWARF unwinder (LJ_UNWIND_EXT, see lj_arch.h /
  # lj_err.c) - without this define lj_err.c refuses to compile on x64, and with it the
  # _Unwind_* symbols must be linked (libgcc_s is always mapped alongside the game's C++ modules).
  LUAJIT_UNWIND_EXTERNAL)
target_compile_options(luajit PRIVATE -fno-stack-protector)
set_target_properties(luajit PROPERTIES
  C_VISIBILITY_PRESET hidden
  LINKER_LANGUAGE CXX
  POSITION_INDEPENDENT_CODE ON)
# pthread: lj_profile.c (referenced once the profiler path is pulled in); m: LuaJIT's own math;
# gcc_s: the _Unwind_* external-unwinder functions LUAJIT_UNWIND_EXTERNAL binds to.
target_link_libraries(luajit PUBLIC m pthread gcc_s)
target_link_libraries(Neversnooze PRIVATE luajit)

# UI / hook translation units (the tree is otherwise header-only; these carry the ImGui menu
# implementation and the Vulkan presentation hook).
target_sources(Neversnooze PRIVATE
  UI/ImGui/GUI.cpp
  UI/ImGui/Neverlose/Neverlose.cpp
  Hooks/Graphics/VulkanHook.cpp
  ThirdParty/stb/stb_image.cpp
  hooks/vac_hook.cpp
  hooks/integrity_audit.cpp
  hooks/game_state_resolver.cpp
)

# Lua scripting framework: the lua_CFunction bindings are unwound THROUGH by LuaJIT foreign
# exceptions (LUAJIT_UNWIND_EXTERNAL on x64), so this one TU must keep unwind tables even in
# the release build (which sets -fno-unwind-tables target-wide). Per-source options come after
# the target flags, so these win.
set_source_files_properties(Features/Lua/Lua.cpp PROPERTIES
  COMPILE_OPTIONS "-funwind-tables;-fasynchronous-unwind-tables")
target_sources(Neversnooze PRIVATE Features/Lua/Lua.cpp)

# Neverlose menu fonts, embedded into the binary (objcopy) so the UI has no filesystem
# dependency. Symbols follow the objcopy convention: _binary_<name with ./- replaced>_start/_end.
if(UNIX)
  set(NL_FONT_DIR "${CMAKE_SOURCE_DIR}/Source/UI/ImGui/Neverlose/fonts")
  foreach(FONT IN ITEMS Inter-Medium Inter-SemiBold fa-solid-900 NotoCJK-subset)
    set(FONT_OBJ "${CMAKE_CURRENT_BINARY_DIR}/font_${FONT}.o")
    add_custom_command(OUTPUT "${FONT_OBJ}"
      COMMAND ${CMAKE_OBJCOPY} -I binary -O elf64-x86-64 -B i386:x86-64 "${FONT}.ttf" "${FONT_OBJ}"
      WORKING_DIRECTORY "${NL_FONT_DIR}" # relative input -> short _binary_<name>_start symbols
      DEPENDS "${NL_FONT_DIR}/${FONT}.ttf"
      VERBATIM)
    target_sources(Neversnooze PRIVATE "${FONT_OBJ}")
    set_source_files_properties("${FONT_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  endforeach()
endif()

if(WIN32)
  target_sources(Neversnooze PRIVATE Platform/Windows/Syscalls/WindowsSyscall.asm)
endif()

if(MSVC)
  set_target_properties(Neversnooze PROPERTIES MSVC_RUNTIME_LIBRARY "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>")
  string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  target_sources(Neversnooze PRIVATE $<$<CONFIG:Release>:Platform/Windows/CRTWindows.cpp>)
  target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 $<$<CONFIG:Release>:/sdl- /GS->>)
  target_compile_definitions(Neversnooze PRIVATE $<$<CONFIG:Release>:_USE_STD_VECTOR_ALGORITHMS=0>)
  target_link_options(Neversnooze PRIVATE $<$<CONFIG:Release>:/nodefaultlib /ENTRY:"DllMain">)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall>)
    # -fno-gnu-unique is load-bearing for unload. GCC emits static template variables (e.g. the
    # inline std::atomic/atomic_flag objects in Utils/RetAddrSpoofer.h) with the STB_GNU_UNIQUE
    # symbol binding, which -fvisibility=hidden does NOT suppress. glibc marks any DSO that
    # DEFINES a unique-bound symbol as permanently nodelete: dlclose() then decrements the open
    # count to zero, silently skips the unmap, and reports success - so unload "works", every log
    # stage passes, and yet /proc/<pid>/maps keeps showing libMangoHud.so, re-injection finds the
    # existing copy and never re-runs .init_array. With this flag those symbols are emitted as
    # ordinary weak COMDAT symbols instead, weak definitions do not pin the DSO, and the unmap
    # works. Verified offline (see the unload repro): UNIQUE bind -> stays mapped; WEAK -> clean.
    # -fno-gnu-unique is load-bearing for unload. GCC emits static template variables (e.g. the
    # inline std::atomic/atomic_flag objects in Utils/RetAddrSpoofer.h) with the STB_GNU_UNIQUE
    # symbol binding, which -fvisibility=hidden does NOT suppress. glibc marks any DSO that
    # DEFINES a unique-bound symbol as permanently nodelete: dlclose() then decrements the open
    # count to zero, silently skips the unmap, and reports success - so unload "works", every log
    # stage passes, and yet /proc/<pid>/maps keeps showing libMangoHud.so, re-injection finds the
    # existing copy and never re-runs .init_array. With this flag those symbols are emitted as
    # ordinary weak COMDAT symbols instead, weak definitions do not pin the DSO, and the unmap
    # works. Verified offline (see the unload repro): UNIQUE bind -> stays mapped; WEAK -> clean.
    # Clang never emits STB_GNU_UNIQUE for these (its flag only exists since clang 16 and is a
    # no-op there), so the flag is GCC-only.
    target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall>)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-gnu-unique>)
    endif()
    # clang's default expression/template depth limits (256/1024/900) are far below GCC's:
    # the pattern TypeList instantiates 463-argument fold expressions. Harmless for stock
    # clang, required for the Arkari OLLVM build.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fconstexpr-depth=4096 -fbracket-depth=1024 -ftemplate-depth=2048>)
    endif()
    target_link_options(Neversnooze PRIVATE LINKER:--no-undefined -z noexecstack)
  endif()

  target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-braces>)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
  target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-dangling-reference>) # false positive
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(UNIX)
    target_compile_options(Neversnooze PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-math-errno -g1>)
    target_link_options(Neversnooze PRIVATE -nostdlib)
    target_link_libraries(Neversnooze PRIVATE c)

    target_compile_options(SteamModule PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables>)
    if(TARGET SteamModule32)
      target_compile_options(SteamModule32 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables>)
    endif()
    
    # Crash triage paths:
    # - Default (NEVERSNOOZE_SHIP=OFF): --strip-debug keeps .symtab + the -g1 line tables, so a
    #   crash dump's module+offset decodes to function + file:line with nm/addr2line against the
    #   shipped binary.
    # - Ship (NEVERSNOOZE_SHIP=ON, set at CONFIGURE time in the tree used for the distributed
    #   artifact): full strip of the distributed .so - no .symtab, no local symbol names. The
    #   full symbol table is preserved in a local <build>/ship-debug/<name>.debug copy (never
    #   shipped) and referenced via a gnu-debuglink, so the offline triage path still works on
    #   the builder's machine: addr2line -e ship-debug/libMangoHud.so.debug <pc>.
    #   Runtime is unaffected: dlopen/RELRO/GOT-patching only need .dynsym, which strip keeps.
    if(CMAKE_STRIP)
      if(NEVERSNOOZE_SHIP AND CMAKE_OBJCOPY)
        set(_ns_ship_debug_dir ${CMAKE_CURRENT_BINARY_DIR}/ship-debug)
        foreach(_ns_target Neversnooze SteamModule)
          if(TARGET ${_ns_target})
            add_custom_command(TARGET ${_ns_target} POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E make_directory ${_ns_ship_debug_dir}
              COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${_ns_target}>"
                      "${_ns_ship_debug_dir}/$<TARGET_FILE_NAME:${_ns_target}>.debug"
              COMMAND ${CMAKE_STRIP} --strip-all "$<TARGET_FILE:${_ns_target}>"
              COMMAND ${CMAKE_OBJCOPY}
                      --add-gnu-debuglink="${_ns_ship_debug_dir}/$<TARGET_FILE_NAME:${_ns_target}>.debug"
                      "$<TARGET_FILE:${_ns_target}>")
          endif()
        endforeach()
        if(TARGET SteamModule32)
          add_custom_command(TARGET SteamModule32 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_ns_ship_debug_dir}
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:SteamModule32>"
                    "${_ns_ship_debug_dir}/$<TARGET_FILE_NAME:SteamModule32>.debug"
            COMMAND ${CMAKE_STRIP} --strip-all "$<TARGET_FILE:SteamModule32>"
            COMMAND ${CMAKE_OBJCOPY}
                    --add-gnu-debuglink="${_ns_ship_debug_dir}/$<TARGET_FILE_NAME:SteamModule32>.debug"
                    "$<TARGET_FILE:SteamModule32>")
        endif()
      else()
        add_custom_command(TARGET Neversnooze POST_BUILD COMMAND ${CMAKE_STRIP} --strip-debug "$<TARGET_FILE:Neversnooze>")
        add_custom_command(TARGET SteamModule POST_BUILD COMMAND ${CMAKE_STRIP} --strip-debug "$<TARGET_FILE:SteamModule>")
        if(TARGET SteamModule32)
          add_custom_command(TARGET SteamModule32 POST_BUILD COMMAND ${CMAKE_STRIP} --strip-debug "$<TARGET_FILE:SteamModule32>")
        endif()
      endif()
    endif()
  endif()
endif()
