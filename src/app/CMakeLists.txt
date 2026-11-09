omakade_add_directory_sources(omakade EXCEPT
  AppSettings.cpp AppSettings.h
  SecretService.h
  SingleInstance.cpp SingleInstance.h)

# Settings, the keyring helper and the single-instance lock build into the core library.
target_sources(omakade_core PRIVATE
  AppSettings.cpp AppSettings.h
  SecretService.h
  SingleInstance.cpp SingleInstance.h)
