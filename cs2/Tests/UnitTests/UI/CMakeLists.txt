target_sources(UnitTests PRIVATE
  ShadowStampTests.cpp
  FeatureBindTests.cpp
  MenuInputTests.cpp
  ViewmodelRotationTests.cpp
)

target_link_libraries(UnitTests sdl3_headers)

target_sources(UnitTests PRIVATE PopupSurfaceTests.cpp)
