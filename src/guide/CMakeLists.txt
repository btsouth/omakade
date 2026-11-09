omakade_add_directory_sources(omakade_guide_runtime EXCEPT
  guard.cpp
  GuideClient.cpp GuideClient.h
  GuideEnvironment.cpp GuideEnvironment.h
  GuidePlugin.cpp GuidePlugin.h
  InGameGuide.cpp InGameGuide.h
  ResidentGuide.cpp ResidentGuide.h)

target_sources(omakade-guide-guard PRIVATE guard.cpp)

# The Guide button listener builds its own copy of the client.
target_sources(omakade-guide-button PRIVATE GuideClient.cpp GuideClient.h)

# The window side of the guide and the resident guide build into the core library.
target_sources(omakade_core PRIVATE
  GuideClient.cpp GuideClient.h
  GuideEnvironment.cpp GuideEnvironment.h
  GuidePlugin.cpp GuidePlugin.h
  InGameGuide.cpp InGameGuide.h
  ResidentGuide.cpp ResidentGuide.h)
