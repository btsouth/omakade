# Settings, the keyring helper and the single-instance lock build into the core library.
omakade_add_directory_sources(omakade
  TO omakade_core
    AppSettings.cpp AppSettings.h
    SecretService.h
    SingleInstance.cpp SingleInstance.h)
