add_subdirectory(SniperRifles)

target_sources(UnitTests PRIVATE CombatCommandTests.cpp)

target_sources(UnitTests PRIVATE RagebotRegressionTests.cpp)

target_sources(UnitTests PRIVATE AutowallTests.cpp)
