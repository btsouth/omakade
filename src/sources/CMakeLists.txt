# Shared with the library's Steam scanner; the recorder reads Steam's stats files with it.
omakade_add_directory_sources(omakade_core RECURSE
  TO omakade_tracking steam/ValveKeyValues.cpp steam/ValveKeyValues.h)
