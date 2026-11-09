# The window's composition and its helpers build into omakade_app, which the omakade binary
# and the omakade-uitest harness both link. Settings, the keyring helper and the
# single-instance lock build into the core library, and main.cpp is the shipped binary's entry.
omakade_add_directory_sources(omakade_app
  TO omakade_core
    AppSettings.cpp AppSettings.h
    SecretService.h
    SingleInstance.cpp SingleInstance.h
  TO omakade
    main.cpp)
