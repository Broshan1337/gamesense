target_sources(UnitTests PRIVATE
  ConfigFromStringTests.cpp
  ConfigSchemaTests.cpp
  ConfigToStringTests.cpp
  ConfigVariablesTests.cpp
)

target_sources(UnitTests PRIVATE ConfigWorkflowTests.cpp)
