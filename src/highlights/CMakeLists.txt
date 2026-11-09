omakade_add_directory_sources(omakade_tracking EXCEPT HighlightClips.cpp HighlightClips.h)

# The clip list on a game's page builds into the core library. The rest of the directory
# is in the tracking library that the recorder links.
target_sources(omakade_core PRIVATE HighlightClips.cpp HighlightClips.h)
