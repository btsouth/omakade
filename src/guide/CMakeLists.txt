# Files in this folder default to omakade_guide_runtime, the guard's library. A new
# window-side file that needs Qt Quick or the core library goes in the omakade_core list below.
omakade_add_directory_sources(omakade_guide_runtime
  TO omakade-guide-guard
    guard.cpp
  # The Guide button listener builds its own copy of the client.
  TO omakade-guide-button
    GuideClient.cpp GuideClient.h
  # The window side of the guide and the resident guide build into the core library.
  TO omakade_core
    GuideClient.cpp GuideClient.h
    GuideEnvironment.cpp GuideEnvironment.h
    GuidePlugin.cpp GuidePlugin.h
    InGameGuide.cpp InGameGuide.h
    ResidentGuide.cpp ResidentGuide.h)
