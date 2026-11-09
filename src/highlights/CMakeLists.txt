# Files in this folder default to omakade_tracking, the library the recorder links. The clip
# list on a game's page builds into the core library; a new window-side file goes there too.
omakade_add_directory_sources(omakade_tracking
  TO omakade_core HighlightClips.cpp HighlightClips.h)
