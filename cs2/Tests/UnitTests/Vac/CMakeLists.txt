# Fake-VAC harness: automated adversary for the production VAC hooks.
#
# VacHarnessBin links the REAL Source/hooks/vac_hook.cpp +
# Source/hooks/integrity_audit.cpp and patches a stand-in "steamclient"
# (VacFixture.c, recognised by the production resolver via the
# "steamclient.so" filename substring), so every assertion below exercises
# production code paths. Run with ctest -V (or the binary directly) for the
# full scanner transcript. Manual service-module capture:
#   ./VacHarnessBin --scan /tmp

add_library(vacfixture SHARED VacFixture.c)
set_target_properties(vacfixture PROPERTIES
  OUTPUT_NAME "vacfixture_steamclient"
  C_VISIBILITY_PRESET default
  POSITION_INDEPENDENT_CODE ON)
# Mirror the production target: real steamclient.so is BIND_NOW + RELRO, so
# its GOT pages are already read-only when we patch them. A lazy fixture
# would leave a writable GOT page that patch/restore visibly re-protects.
target_link_options(vacfixture PRIVATE "LINKER:-z,relro,-z,now")

# Hunter-test decoys, named like real VAC drops (<checksum>-<size>.so).
add_library(vacdecoy_runfunc SHARED DecoyRunfunc.c)
set_target_properties(vacdecoy_runfunc PROPERTIES
  OUTPUT_NAME "deadbeef-4096"
  PREFIX ""
  C_VISIBILITY_PRESET default
  POSITION_INDEPENDENT_CODE ON)

add_library(vacdecoy_plain SHARED DecoyPlain.c)
set_target_properties(vacdecoy_plain PROPERTIES
  OUTPUT_NAME "cafef00d-1234"
  PREFIX ""
  C_VISIBILITY_PRESET default
  POSITION_INDEPENDENT_CODE ON)

add_executable(VacHarness VacHarness.cpp VacExpose.cpp
  "${CMAKE_SOURCE_DIR}/Source/hooks/vac_hook.cpp"
  "${CMAKE_SOURCE_DIR}/Source/hooks/integrity_audit.cpp")

target_include_directories(VacHarness PRIVATE
  "${CMAKE_SOURCE_DIR}/Tests"
  "${CMAKE_SOURCE_DIR}/Source"
  "${CMAKE_SOURCE_DIR}/Source/hooks")

target_compile_definitions(VacHarness PRIVATE
  VAC_FIXTURE_PATH="$<TARGET_FILE:vacfixture>"
  VAC_DECOY_RUNFUNC_PATH="$<TARGET_FILE:vacdecoy_runfunc>"
  VAC_DECOY_PLAIN_PATH="$<TARGET_FILE:vacdecoy_plain>")

# The harness dlopens these at runtime: make sure they exist first.
add_dependencies(VacHarness vacfixture vacdecoy_runfunc vacdecoy_plain)

# Custom main (supports --scan): gtest without gtest_main.
target_link_libraries(VacHarness PRIVATE gtest Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(VacHarness PROPERTIES OUTPUT_NAME VacHarnessBin)
gtest_discover_tests(VacHarness DISCOVERY_TIMEOUT 60 TEST_PREFIX Vac_)
