omakade_add_directory_sources(omakade_core RECURSE EXCEPT
  steam/ValveKeyValues.cpp steam/ValveKeyValues.h)

# Shared with the library's Steam scanner; the recorder reads Steam's stats files with it.
target_sources(omakade_tracking PRIVATE steam/ValveKeyValues.cpp steam/ValveKeyValues.h)
