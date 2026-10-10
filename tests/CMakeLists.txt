find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME omakade_tv_game_launcher
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/TvGameLauncherTests.py
)

qt_add_executable(omakade_core_tests
  CoreTests.cpp
)
target_compile_definitions(omakade_core_tests PRIVATE
  OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME omakade_details_recovery_isolation COMMAND ${CMAKE_COMMAND}
  -DOMAKADE=$<TARGET_FILE:omakade>
  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/details-recovery-isolation
  -P ${CMAKE_CURRENT_SOURCE_DIR}/DetailsRecoveryIsolation.cmake)
set_tests_properties(omakade_details_recovery_isolation PROPERTIES TIMEOUT 20)

qt_add_executable(omakade_game_mode_tests GameModeTests.cpp)
target_link_libraries(omakade_game_mode_tests PRIVATE omakade_core Qt6::Test)
foreach(shard RANGE 0 1)
  add_test(NAME omakade_game_mode_tests_${shard}
    COMMAND omakade_game_mode_tests --test-shard=${shard})
  set_tests_properties(omakade_game_mode_tests_${shard} PROPERTIES TIMEOUT 60)
endforeach()

qt_add_executable(omakade_backup_recovery_tests BackupRecoveryTests.cpp)
target_link_libraries(omakade_backup_recovery_tests PRIVATE omakade_core Qt6::Test)
target_compile_definitions(omakade_backup_recovery_tests PRIVATE
  OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME omakade_backup_recovery_tests COMMAND omakade_backup_recovery_tests)
set_tests_properties(omakade_backup_recovery_tests PROPERTIES TIMEOUT 60)

foreach(preference_case IN ITEMS gog-settings linked-preference)
  add_test(NAME omakade_${preference_case}_controls COMMAND omakade --${preference_case}-test)
  add_test(NAME omakade_${preference_case}_couch_controls COMMAND omakade --${preference_case}-test --couch)
  set_tests_properties(omakade_${preference_case}_controls omakade_${preference_case}_couch_controls PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
foreach(preference_overlay IN ITEMS gog-folders linked-preference linked-preference-missing)
  add_test(NAME omakade_${preference_overlay}_desktop_render COMMAND omakade
    --render-overlay=${preference_overlay} --render-size=1000x850
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${preference_overlay}-desktop.png)
  add_test(NAME omakade_${preference_overlay}_couch_render COMMAND omakade --couch
    --render-overlay=${preference_overlay} --render-size=1920x1080
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${preference_overlay}-couch.png)
  set_tests_properties(omakade_${preference_overlay}_desktop_render omakade_${preference_overlay}_couch_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

add_test(NAME omakade_details_directions COMMAND omakade --details-direction-test
  --render-size=1400x1100)
add_test(NAME omakade_details_directions_couch COMMAND omakade --couch --details-direction-test
  --render-size=1920x1080)
add_test(NAME omakade_details_directions_single_column COMMAND omakade --details-direction-test
  --render-size=320x800)
add_test(NAME omakade_details_directions_two_columns COMMAND omakade --details-direction-test
  --render-size=600x800)
add_test(NAME omakade_details_directions_four_columns COMMAND omakade --details-direction-test
  --render-size=1200x900)
add_test(NAME omakade_details_directions_five_columns COMMAND omakade --details-direction-test
  --render-size=1920x1080)
set_tests_properties(omakade_details_directions omakade_details_directions_couch
  omakade_details_directions_single_column omakade_details_directions_two_columns
  omakade_details_directions_four_columns omakade_details_directions_five_columns PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30)

add_test(NAME omakade_backup_editor COMMAND omakade --backup-editor-test)
add_test(NAME omakade_backup_editor_couch COMMAND omakade --backup-editor-test --couch)
add_test(NAME omakade_backup_editor_render COMMAND omakade --render-overlay=backup-editor
  --render-size=900x720 --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/backup-editor-desktop.png)
add_test(NAME omakade_backup_editor_couch_render COMMAND omakade --couch --render-overlay=backup-editor
  --render-size=1920x1080 --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/backup-editor-couch.png)
set_tests_properties(omakade_backup_editor omakade_backup_editor_couch
  omakade_backup_editor_render omakade_backup_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

foreach(restore_case IN ITEMS success retry undo close)
  add_test(NAME omakade_restore_startup_${restore_case}
    COMMAND omakade --restore-startup-test=${restore_case})
  set_tests_properties(omakade_restore_startup_${restore_case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
add_test(NAME omakade_restore_startup_couch
  COMMAND omakade --restore-startup-test=undo --couch --render-size=1920x1080
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/restore-startup-couch.png)
add_test(NAME omakade_restore_startup_desktop
  COMMAND omakade --restore-startup-test=retry --render-size=900x620
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/restore-startup-desktop.png)
set_tests_properties(omakade_restore_startup_couch omakade_restore_startup_desktop PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

target_link_libraries(omakade_core_tests PRIVATE
  omakade_core
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)

add_dependencies(omakade_core_tests omakade-sessiond)
add_test(NAME omakade_core_tests COMMAND omakade_core_tests)
set_tests_properties(omakade_core_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(NAME omakade_qml_smoke COMMAND omakade --smoke-test)
set_tests_properties(omakade_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation
  COMMAND omakade --controller-navigation-test --uninstalled-layout-test
)
set_tests_properties(omakade_controller_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_console_portal_view
  COMMAND omakade --console-portal-test --render-size=1600x900
)
set_tests_properties(omakade_console_portal_view PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation_tiled
  COMMAND omakade --controller-navigation-test --owned-layout-test --uninstalled-layout-test
          --render-size=820x590
)
set_tests_properties(omakade_controller_navigation_tiled PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation_narrow
  COMMAND omakade --controller-navigation-test --owned-layout-test --uninstalled-layout-test
          --render-size=600x800)
set_tests_properties(omakade_controller_navigation_narrow PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30)

add_test(NAME omakade_controller_navigation_wide
  COMMAND omakade --controller-navigation-test --owned-layout-test --uninstalled-layout-test
          --render-size=1600x900
)

add_test(NAME omakade_couch_navigation
  COMMAND omakade --couch --couch-navigation-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-couch-navigation"
  TIMEOUT 30
)

# Exercise every rendered directional edge with keyboard, SDL D-pad and analog input.
# The narrow case wraps the filter bar; the other cases cover TV and ultrawide layouts.
foreach(size IN ITEMS 900x700 1280x720 1920x1080 2560x1080 3840x2160)
  add_test(NAME omakade_couch_navigation_contract_${size}
    COMMAND omakade --couch --couch-navigation-contract --render-size=${size}
                   --reduced-motion
  )
  set_tests_properties(omakade_couch_navigation_contract_${size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 90
    RUN_SERIAL TRUE
  )
endforeach()

# Every direction from every control on each couch screen, and Back one screen at a time.
foreach(size IN ITEMS 900x700 1280x720 1920x1080 3840x2160)
  add_test(NAME omakade_couch_navigation_sweep_${size}
    COMMAND omakade --couch --couch-navigation-sweep --render-size=${size} --reduced-motion
  )
  add_test(NAME omakade_desktop_navigation_sweep_${size}
    COMMAND omakade --couch-navigation-sweep --render-size=${size} --reduced-motion
  )
  set_tests_properties(omakade_couch_navigation_sweep_${size} omakade_desktop_navigation_sweep_${size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 300
  )
endforeach()

foreach(layout IN ITEMS desktop couch-detail couch-grid)
  foreach(input IN ITEMS keyboard controller)
    foreach(state IN ITEMS populated empty delayed)
      if(layout STREQUAL "desktop" AND state STREQUAL "delayed")
        continue()
      endif()
      set(startup_args --startup-navigation-test --reduced-motion)
      if(NOT layout STREQUAL "desktop")
        list(APPEND startup_args --couch)
      endif()
      if(layout STREQUAL "couch-grid")
        list(APPEND startup_args --startup-grid)
      endif()
      if(input STREQUAL "controller")
        list(APPEND startup_args --startup-controller)
      endif()
      if(NOT state STREQUAL "populated")
        list(APPEND startup_args --startup-${state})
      endif()
      add_test(NAME omakade_startup_navigation_${layout}_${input}_${state}
        COMMAND omakade ${startup_args})
      set_tests_properties(omakade_startup_navigation_${layout}_${input}_${state} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
        TIMEOUT 15
        RUN_SERIAL TRUE)
    endforeach()
  endforeach()
endforeach()

add_test(NAME omakade_couch_thousand_game_startup
  COMMAND omakade --couch --stress-test --benchmark --benchmark-max-ms=500
)
set_tests_properties(omakade_couch_thousand_game_startup PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  RUN_SERIAL TRUE
  TIMEOUT 5
)

add_test(NAME omakade_couch_ten_thousand_game_startup
  COMMAND omakade --couch --stress-test --stress-count=10000 --benchmark --benchmark-max-ms=1000
)
set_tests_properties(omakade_couch_ten_thousand_game_startup PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  RUN_SERIAL TRUE
  TIMEOUT 10
)

foreach(benchmark_case IN ITEMS text empty missing nonpositive)
  if(benchmark_case STREQUAL "text")
    set(benchmark_limit_argument --benchmark-max-ms=fast)
  elseif(benchmark_case STREQUAL "empty")
    set(benchmark_limit_argument --benchmark-max-ms=)
  elseif(benchmark_case STREQUAL "missing")
    set(benchmark_limit_argument --benchmark-max-ms)
  else()
    set(benchmark_limit_argument --benchmark-max-ms=-1)
  endif()
  add_test(NAME omakade_benchmark_rejects_${benchmark_case}_limit
    COMMAND omakade --benchmark ${benchmark_limit_argument}
  )
  set_tests_properties(omakade_benchmark_rejects_${benchmark_case}_limit PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    WILL_FAIL TRUE
    TIMEOUT 5
  )
endforeach()

add_test(NAME omakade_cli_rejects_missing_play_value COMMAND omakade --play)
add_test(NAME omakade_cli_rejects_missing_screenshot_path COMMAND omakade --render-screenshot)
add_test(NAME omakade_cli_rejects_invalid_render_size
  COMMAND omakade --smoke-test --render-size=wide
)
set_tests_properties(
  omakade_cli_rejects_missing_play_value
  omakade_cli_rejects_missing_screenshot_path
  omakade_cli_rejects_invalid_render_size
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    WILL_FAIL TRUE
    TIMEOUT 5
)

add_test(NAME omakade_couch_thousand_game_navigation
  COMMAND omakade --couch --stress-test --couch-navigation-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_thousand_game_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE=;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-thousand-navigation"
  RUN_SERIAL TRUE
  TIMEOUT 30
)

add_test(NAME omakade_couch_owned_navigation
  COMMAND omakade --couch --couch-navigation-test --owned-layout-test
          --uninstalled-layout-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_owned_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE=;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-owned-navigation"
  RUN_SERIAL TRUE
  TIMEOUT 30
)
set_tests_properties(omakade_controller_navigation_wide PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_render_smoke
  COMMAND omakade --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-render.png
          --render-size=820x590
)

add_test(NAME omakade_couch_render
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_reduced_motion_render
  COMMAND omakade --couch --reduced-motion
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-reduced-motion.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_reduced_motion_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_light_opaque_render
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-light-opaque.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_light_opaque_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDG_STATE_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/light-state;XDG_CONFIG_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/empty-config;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30
)

foreach(couch_case IN ITEMS 720p 1440p ultrawide 4k)
  if(couch_case STREQUAL "720p")
    set(couch_size 1280x720)
  elseif(couch_case STREQUAL "1440p")
    set(couch_size 2560x1440)
  elseif(couch_case STREQUAL "ultrawide")
    set(couch_size 2560x1080)
  else()
    set(couch_size 3840x2160)
  endif()
  add_test(NAME omakade_couch_render_${couch_case}
    COMMAND omakade --couch
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-${couch_case}.png
            --render-size=${couch_size}
  )
  set_tests_properties(omakade_couch_render_${couch_case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

foreach(grid_size IN ITEMS 720p 1080p 4k)
  if(grid_size STREQUAL "720p")
    set(grid_dimensions 1280x720)
  elseif(grid_size STREQUAL "1080p")
    set(grid_dimensions 1920x1080)
  else()
    set(grid_dimensions 3840x2160)
  endif()
  add_test(NAME omakade_couch_grid_${grid_size}_render
    COMMAND omakade --couch --owned-layout-test
            --render-overlay=couch-grid
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-grid-${grid_size}.png
            --render-size=${grid_dimensions}
  )
  set_tests_properties(omakade_couch_grid_${grid_size}_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

foreach(couch_overlay IN ITEMS couch-search couch-browse couch-settings-bottom)
  string(REPLACE "-" "_" couch_overlay_test ${couch_overlay})
  add_test(NAME omakade_${couch_overlay_test}_4k_render
    COMMAND omakade --couch --owned-layout-test
            --render-overlay=${couch_overlay}
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-${couch_overlay}-4k.png
            --render-size=3840x2160
  )
  set_tests_properties(omakade_${couch_overlay_test}_4k_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

add_test(NAME omakade_couch_detail_4k_render
  COMMAND omakade --couch --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-detail-4k.png
          --render-size=3840x2160
)
set_tests_properties(omakade_couch_detail_4k_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_render_200_percent
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-200-percent.png
          --render-size=1280x720
)
set_tests_properties(omakade_couch_render_200_percent PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QT_SCALE_FACTOR=2"
  TIMEOUT 30
)

add_test(NAME omakade_couch_detail_render
  COMMAND omakade --couch --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-detail.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_detail_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_keyboard_render
  COMMAND omakade --couch
          --render-overlay=couch-search
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-keyboard.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_keyboard_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_browse_render
  COMMAND omakade --couch
          --render-overlay=couch-browse
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-browse.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_browse_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_text_entry_render
  COMMAND omakade --couch
          --render-overlay=couch-entry
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-text-entry.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_text_entry_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_settings_render
  COMMAND omakade --couch --owned-layout-test
          --render-overlay=couch-settings-top
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-settings.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_settings_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_settings_720p_render
  COMMAND omakade --couch --owned-layout-test
          --render-overlay=couch-settings-bottom
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-settings-720p.png
          --render-size=1280x720
)
set_tests_properties(omakade_couch_settings_720p_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)
set_tests_properties(omakade_render_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_render_space_separated_options
  COMMAND omakade --render-screenshot ${CMAKE_CURRENT_BINARY_DIR}/omakade-render-spaced.png
          --render-size 820x590
)
set_tests_properties(omakade_render_space_separated_options PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_owned_layout_render
  COMMAND omakade --owned-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-owned-layout.png
          --render-size=820x590
)
set_tests_properties(omakade_owned_layout_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_uninstalled_detail_render
  COMMAND omakade --owned-layout-test --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-uninstalled-detail.png
          --render-size=820x590
)
set_tests_properties(omakade_uninstalled_detail_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

foreach(size_case IN ITEMS small large)
  add_test(NAME omakade_cover_size_${size_case}
    COMMAND omakade --owned-layout-test --render-overlay=cover-size-${size_case}
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/cover-size-${size_case}.png
            --render-size=820x590)
  set_tests_properties(omakade_cover_size_${size_case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)
endforeach()
foreach(settings_section IN ITEMS sources library connections controls about)
  add_test(NAME omakade_settings_${settings_section}
    COMMAND omakade --render-overlay=settings-${settings_section}
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/settings-${settings_section}.png
            --render-size=820x590)
  set_tests_properties(omakade_settings_${settings_section} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)
endforeach()

foreach(wheel_mode IN ITEMS animated reduced)
  set(wheel_options --expand-scroll-test)
  if(wheel_mode STREQUAL "reduced")
    list(APPEND wheel_options --reduced-motion)
  endif()
  add_test(NAME omakade_filtered_wheel_${wheel_mode}
    COMMAND omakade --console-portal-test --render-size=1600x900 ${wheel_options})
  set_tests_properties(omakade_filtered_wheel_${wheel_mode} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)
endforeach()

foreach(couch_grid_variant IN ITEMS normal small large)
  set(couch_grid_overlay couch-grid)
  if(NOT couch_grid_variant STREQUAL "normal")
    set(couch_grid_overlay couch-grid-${couch_grid_variant})
  endif()
  add_test(NAME omakade_couch_grid_centered_${couch_grid_variant}
    COMMAND omakade --couch --owned-layout-test
      --render-overlay=${couch_grid_overlay}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/couch-grid-centered-${couch_grid_variant}.png
      --render-size=2048x1152)
  set_tests_properties(omakade_couch_grid_centered_${couch_grid_variant} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)
endforeach()
add_test(NAME omakade_manual_editor COMMAND omakade --manual-editor-test)
add_test(NAME omakade_manual_editor_couch COMMAND omakade --manual-editor-test --couch)
add_test(NAME omakade_manual_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/manual-editor.png
  --render-overlay=manual-editor --render-size=820x590)
add_test(NAME omakade_manual_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/manual-editor-couch.png
  --render-overlay=manual-editor --render-size=1920x1080)
set_tests_properties(omakade_manual_editor omakade_manual_editor_couch
  omakade_manual_editor_render omakade_manual_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_artwork_editor COMMAND omakade --artwork-editor-test)
add_test(NAME omakade_artwork_editor_couch COMMAND omakade --artwork-editor-test --couch)
add_test(NAME omakade_artwork_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-editor.png
  --render-overlay=artwork-editor --render-size=820x590)
add_test(NAME omakade_artwork_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-editor-couch.png
  --render-overlay=artwork-editor --render-size=1920x1080)
add_test(NAME omakade_artwork_editor_narrow_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-editor-narrow.png
  --render-overlay=artwork-editor --render-size=380x800)
set_tests_properties(omakade_artwork_editor omakade_artwork_editor_couch
  omakade_artwork_editor_render omakade_artwork_editor_couch_render
  omakade_artwork_editor_narrow_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

# Narrowing the library leaves the grid holding delegates for the games that just left. Moving
# down from the organize row must reach the game on screen, never one of those.
add_test(NAME omakade_stale_selection
  COMMAND omakade --stale-selection-test --owned-layout-test --render-size=1600x900)
set_tests_properties(omakade_stale_selection PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

# Back walks out of a console, then a search, then a source, then anything else narrowing the
# view, in both Desktop and Couch Mode.
add_test(NAME omakade_filter_back COMMAND omakade --filter-back-test --render-size=1600x900)
add_test(NAME omakade_filter_back_couch
  COMMAND omakade --filter-back-test --couch --render-size=1920x1080)
set_tests_properties(omakade_filter_back omakade_filter_back_couch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

# Game Mode keeps Couch Mode for its session and returns the window to the mode it had,
# whether it was started from Desktop or from Couch Mode.
add_test(NAME omakade_game_mode COMMAND omakade --game-mode-test --render-size=1600x900)
add_test(NAME omakade_game_mode_couch
  COMMAND omakade --game-mode-test --couch --render-size=1920x1080)
set_tests_properties(omakade_game_mode omakade_game_mode_couch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

foreach(shard RANGE 0 1)
  add_test(NAME omakade_game_mode_startup_lifecycle_${shard}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/GameModeStartupTests.py
            $<TARGET_FILE:omakade> --shard=${shard}/2)
  set_tests_properties(omakade_game_mode_startup_lifecycle_${shard} PROPERTIES TIMEOUT 60)
endforeach()

add_test(NAME omakade_random_selection COMMAND omakade --random-selection-test)
add_test(NAME omakade_random_selection_couch COMMAND omakade --random-selection-test --couch)
add_test(NAME omakade_random_selection_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/random-selection.png
  --render-overlay=random-selection --render-size=820x590)
set_tests_properties(omakade_random_selection omakade_random_selection_couch
  omakade_random_selection_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_saved_filters COMMAND omakade --saved-filter-test)
add_test(NAME omakade_saved_filters_couch COMMAND omakade --saved-filter-test --couch)
add_test(NAME omakade_saved_filters_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/saved-filters.png
  --render-overlay=saved-filters --render-size=820x590)
add_test(NAME omakade_saved_filters_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/saved-filters-couch.png
  --render-overlay=saved-filters --render-size=1920x1080)
set_tests_properties(omakade_saved_filters omakade_saved_filters_couch
  omakade_saved_filters_render omakade_saved_filters_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_bulk_editor COMMAND omakade --bulk-editor-test)
add_test(NAME omakade_bulk_editor_couch COMMAND omakade --bulk-editor-test --couch)
add_test(NAME omakade_bulk_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/bulk-editor.png
  --render-overlay=bulk-editor --render-size=820x590)
add_test(NAME omakade_bulk_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/bulk-editor-couch.png
  --render-overlay=bulk-editor --render-size=1920x1080)
set_tests_properties(omakade_bulk_editor omakade_bulk_editor_couch
  omakade_bulk_editor_render omakade_bulk_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

# Narrow editor controls must remain inside the window and usable with a controller.
foreach(editor_case IN ITEMS saved-filter bulk-editor manual-editor)
  foreach(editor_mode IN ITEMS desktop couch)
    set(editor_args)
    if(editor_mode STREQUAL "couch")
      list(APPEND editor_args --couch)
    endif()
    add_test(NAME omakade_${editor_case}_${editor_mode}_narrow
      COMMAND omakade --${editor_case}-test ${editor_args} --render-size=380x800)
    set_tests_properties(omakade_${editor_case}_${editor_mode}_narrow PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
      TIMEOUT 30)
  endforeach()
endforeach()

foreach(settings_page IN ITEMS sources library connections controls storage appearance streaming about categories connection-0 connection-1 connection-2 connection-3)
  foreach(settings_mode IN ITEMS desktop couch)
    set(settings_args)
    if(settings_mode STREQUAL "couch")
      list(APPEND settings_args --couch)
    endif()
    add_test(NAME omakade_settings_${settings_page}_${settings_mode}_narrow
      COMMAND omakade ${settings_args} --render-size=380x800
      --render-overlay=settings-${settings_page}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/settings-${settings_page}-${settings_mode}-narrow.png)
    set_tests_properties(omakade_settings_${settings_page}_${settings_mode}_narrow PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
      TIMEOUT 30)
  endforeach()
endforeach()

# Populated and empty provider details, including keyboard expand/collapse.
foreach(info_case IN ITEMS game-info game-info-expanded game-info-empty game-info-rom-only)
  foreach(info_mode IN ITEMS desktop couch)
    set(info_args --render-overlay=${info_case})
    if(info_mode STREQUAL "couch")
      list(APPEND info_args --couch --render-size=1920x1080)
    else()
      list(APPEND info_args --render-size=900x720)
    endif()
    add_test(NAME omakade_${info_case}_${info_mode} COMMAND omakade ${info_args}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${info_case}-${info_mode}.png)
    set_tests_properties(omakade_${info_case}_${info_mode} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      TIMEOUT 20)
  endforeach()
endforeach()
add_test(NAME omakade_game_info_rating_only COMMAND omakade
  --render-overlay=game-info-rating-only --render-size=900x720
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/game-info-rating-only.png)
set_tests_properties(omakade_game_info_rating_only PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

# The real organization controls must remain navigable when detail rows wrap.
add_test(NAME omakade_game_info_navigation_narrow
  COMMAND omakade --render-overlay=game-info --render-size=600x800
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/game-info-navigation-narrow.png)
add_test(NAME omakade_game_info_navigation_couch_720p
  COMMAND omakade --owned-layout-test --couch --render-overlay=game-info-expanded --render-size=1280x720
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/game-info-navigation-couch-720p.png)
set_tests_properties(omakade_game_info_navigation_narrow omakade_game_info_navigation_couch_720p
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

foreach(discovery_size IN ITEMS 600x800 1280x720)
  add_test(NAME omakade_metadata_filters_${discovery_size} COMMAND omakade
    --render-overlay=metadata-filters --render-size=${discovery_size}
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/metadata-filters-${discovery_size}.png)
  set_tests_properties(omakade_metadata_filters_${discovery_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

foreach(home_mode IN ITEMS desktop couch)
  set(home_args)
  if(home_mode STREQUAL "couch")
    list(APPEND home_args --couch)
  endif()
  foreach(home_size IN ITEMS 600x800 1280x720)
    add_test(NAME omakade_home_${home_mode}_${home_size} COMMAND omakade ${home_args}
      --render-overlay=home --render-size=${home_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-${home_mode}-${home_size}.png)
    set_tests_properties(omakade_home_${home_mode}_${home_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      TIMEOUT 20)
  endforeach()
endforeach()

foreach(hero_case IN ITEMS legacy screenshot custom)
  foreach(hero_size IN ITEMS 600x800 1920x1080)
    add_test(NAME omakade_hero_${hero_case}_${hero_size} COMMAND omakade
      --render-overlay=game-info-hero-${hero_case} --render-size=${hero_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/hero-${hero_case}-${hero_size}.png)
    set_tests_properties(omakade_hero_${hero_case}_${hero_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      TIMEOUT 20)
  endforeach()
endforeach()

# Responsive menu previews also validate QML loading on the installed candidate.
foreach(nav_menu IN ITEMS library-sources library-filters library-view library-actions detail-manage)
  add_test(NAME omakade_menu_${nav_menu}
    COMMAND omakade --owned-layout-test --render-size=600x800 --render-overlay=${nav_menu}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${nav_menu}.png)
  set_tests_properties(omakade_menu_${nav_menu} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
add_test(NAME omakade_menu_detail_manage_4k
  COMMAND omakade --couch --owned-layout-test --render-size=3840x2160 --render-overlay=detail-manage
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/detail-manage-4k.png)
set_tests_properties(omakade_menu_detail_manage_4k PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

foreach(home_overview_size IN ITEMS 600x800 1280x720 2048x1152)
  add_test(NAME omakade_home_overview_${home_overview_size} COMMAND omakade
    --render-overlay=home-overview --render-size=${home_overview_size}
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-overview-${home_overview_size}.png)
  set_tests_properties(omakade_home_overview_${home_overview_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

add_test(NAME omakade_home_empty COMMAND omakade --render-overlay=home-empty --render-size=600x800
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-empty.png)
set_tests_properties(omakade_home_empty PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1" TIMEOUT 20)

foreach(home_delayed_mode IN ITEMS desktop couch)
  set(home_delayed_args)
  if(home_delayed_mode STREQUAL "couch")
    list(APPEND home_delayed_args --couch)
  endif()
  foreach(home_delayed_size IN ITEMS 600x800 1280x720 1920x1080 3840x2160)
    add_test(NAME omakade_home_delayed_${home_delayed_mode}_${home_delayed_size} COMMAND omakade ${home_delayed_args}
      --render-overlay=home-delayed --render-size=${home_delayed_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-delayed-${home_delayed_mode}-${home_delayed_size}.png)
    set_tests_properties(omakade_home_delayed_${home_delayed_mode}_${home_delayed_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      FAIL_REGULAR_EXPRESSION "polish.*loop;Binding loop;TypeError;ReferenceError"
      TIMEOUT 20)
  endforeach()
endforeach()

foreach(home_wheel_motion IN ITEMS animated reduced)
  set(home_wheel_args)
  if(home_wheel_motion STREQUAL "reduced")
    list(APPEND home_wheel_args --reduced-motion)
  endif()
  foreach(home_wheel_size IN ITEMS 600x800 1920x1080)
    add_test(NAME omakade_home_wheel_${home_wheel_motion}_${home_wheel_size} COMMAND omakade ${home_wheel_args}
      --render-overlay=home-wheel --render-size=${home_wheel_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-wheel-${home_wheel_motion}-${home_wheel_size}.png)
    set_tests_properties(omakade_home_wheel_${home_wheel_motion}_${home_wheel_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      FAIL_REGULAR_EXPRESSION "polish.*loop;Binding loop;TypeError;ReferenceError" TIMEOUT 20)
  endforeach()
endforeach()

foreach(home_stream_size IN ITEMS 600x800 1920x1080)
  add_test(NAME omakade_home_wheel_stream_${home_stream_size} COMMAND omakade
    --render-overlay=home-wheel-stream --render-size=${home_stream_size}
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-wheel-stream-${home_stream_size}.png)
  set_tests_properties(omakade_home_wheel_stream_${home_stream_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "polish.*loop;Binding loop;TypeError;ReferenceError" TIMEOUT 20)
endforeach()

# Opening details keeps identity, activity and launch actions in the viewport.
foreach(detail_overview_mode IN ITEMS desktop couch)
  set(detail_overview_args)
  if(detail_overview_mode STREQUAL "couch")
    list(APPEND detail_overview_args --couch)
  endif()
  foreach(detail_overview_size IN ITEMS 600x800 1280x720 1920x1080)
    add_test(NAME omakade_detail_overview_${detail_overview_mode}_${detail_overview_size}
      COMMAND omakade --owned-layout-test ${detail_overview_args}
        --render-overlay=game-info-overview-long --render-size=${detail_overview_size}
        --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/detail-overview-${detail_overview_mode}-${detail_overview_size}.png)
    set_tests_properties(omakade_detail_overview_${detail_overview_mode}_${detail_overview_size}
      PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
  endforeach()
endforeach()

foreach(identify_size IN ITEMS 600x800 1280x720)
  add_test(NAME omakade_identify_panel_${identify_size}
    COMMAND omakade --owned-layout-test --render-overlay=game-info-identify --render-size=${identify_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/identify-panel-${identify_size}.png)
  set_tests_properties(omakade_identify_panel_${identify_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
endforeach()

foreach(panel_size IN ITEMS 600x800 1280x720)
  add_test(NAME omakade_artwork_matched_${panel_size}
    COMMAND omakade --owned-layout-test --render-overlay=game-info-identify-matched --render-size=${panel_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-matched-${panel_size}.png)
  set_tests_properties(omakade_artwork_matched_${panel_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
endforeach()

foreach(panel_size IN ITEMS 600x800 1280x720)
  add_test(NAME omakade_artwork_late_resize_${panel_size}
    COMMAND omakade --owned-layout-test --render-overlay=game-info-identify-late-matched --render-size=${panel_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-late-${panel_size}.png)
  set_tests_properties(omakade_artwork_late_resize_${panel_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
endforeach()

foreach(tooltip_mode IN ITEMS desktop narrow couch)
  if(tooltip_mode STREQUAL "narrow")
    set(tooltip_size 600x800)
  else()
    set(tooltip_size 1280x720)
  endif()
  set(tooltip_args)
  if(tooltip_mode STREQUAL "couch")
    list(APPEND tooltip_args --couch)
  endif()
  foreach(tooltip_target IN ITEMS platform date rating missing)
    add_test(NAME omakade_rating_tooltip_${tooltip_mode}_${tooltip_target}
      COMMAND omakade --owned-layout-test ${tooltip_args}
        --render-overlay=game-info-tooltip-${tooltip_target} --render-size=${tooltip_size}
        --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/tooltip-${tooltip_mode}-${tooltip_target}.png)
    set_tests_properties(omakade_rating_tooltip_${tooltip_mode}_${tooltip_target} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
  endforeach()
endforeach()

add_test(NAME omakade_library_reflow
  COMMAND omakade --owned-layout-test --render-overlay=library-reflow --render-size=1255x1000
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/library-reflow.png)
set_tests_properties(omakade_library_reflow PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 45)

add_test(NAME omakade_library_return_recent
  COMMAND omakade --owned-layout-test --render-overlay=library-reflow-return --render-size=2024x1104
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/library-return-recent.png)
set_tests_properties(omakade_library_return_recent PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 45)

foreach(launch_mode IN ITEMS desktop couch)
  set(launch_args --render-size=600x800)
  if(launch_mode STREQUAL "couch")
    set(launch_args --couch --render-size=1280x720)
  endif()
  add_test(NAME omakade_launch_feedback_${launch_mode} COMMAND omakade ${launch_args}
    --render-overlay=launch-feedback
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/launch-feedback-${launch_mode}.png)
  set_tests_properties(omakade_launch_feedback_${launch_mode} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

foreach(queue_mode IN ITEMS desktop couch)
  set(queue_args)
  if(queue_mode STREQUAL "couch")
    list(APPEND queue_args --couch)
  endif()
  add_test(NAME omakade_home_full_queue_${queue_mode} COMMAND omakade ${queue_args}
    --render-overlay=home-full-queue --render-size=1280x720
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/home-full-queue-${queue_mode}.png)
  set_tests_properties(omakade_home_full_queue_${queue_mode} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "polish.*loop;Binding loop;TypeError;ReferenceError" TIMEOUT 30)
endforeach()

foreach(recorder_overlay IN ITEMS recorder-details settings-recorder-on settings-recorder-off)
  foreach(recorder_size IN ITEMS 600x800 1920x1080)
    set(recorder_args)
    if(recorder_size STREQUAL "1920x1080")
      list(APPEND recorder_args --couch)
    endif()
    add_test(NAME omakade_${recorder_overlay}_${recorder_size} COMMAND omakade ${recorder_args}
      --render-overlay=${recorder_overlay} --render-size=${recorder_size}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${recorder_overlay}-${recorder_size}.png)
    set_tests_properties(omakade_${recorder_overlay}_${recorder_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
      FAIL_REGULAR_EXPRESSION "polish.*loop;Binding loop;TypeError;ReferenceError" TIMEOUT 20)
  endforeach()
endforeach()

# Explicitly built offline audit adapter; no provider credentials or library writes.
add_executable(omakade_metadata_audit EXCLUDE_FROM_ALL ../tools/MetadataAudit.cpp)
target_link_libraries(omakade_metadata_audit PRIVATE omakade_core Qt6::Core)

qt_add_executable(omakade_protondb_tests ProtonDbTests.cpp)
target_link_libraries(omakade_protondb_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_protondb_tests COMMAND omakade_protondb_tests)
set_tests_properties(omakade_protondb_tests PROPERTIES TIMEOUT 30)

qt_add_executable(omakade_protondb_ui_tests ProtonDbUiTests.cpp)
target_link_libraries(omakade_protondb_ui_tests PRIVATE omakade_core Qt6::Quick Qt6::Test)
target_compile_definitions(omakade_protondb_ui_tests PRIVATE OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME omakade_protondb_ui_tests COMMAND omakade_protondb_ui_tests)
set_tests_properties(omakade_protondb_ui_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)

foreach(review_size IN ITEMS 640x600 1280x900)
  add_test(NAME omakade_review_filters_${review_size} COMMAND omakade
    --render-overlay=review-filters --render-size=${review_size}
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/review-filters-${review_size}.png)
  set_tests_properties(omakade_review_filters_${review_size} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
add_test(NAME omakade_review_filters_couch COMMAND omakade --couch
  --render-overlay=review-filters --render-size=1920x1080
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/review-filters-couch.png)
set_tests_properties(omakade_review_filters_couch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

qt_add_executable(omakade_automatic_launch_tests AutomaticLaunchTests.cpp)
target_link_libraries(omakade_automatic_launch_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_automatic_launch_tests COMMAND omakade_automatic_launch_tests)
set_tests_properties(omakade_automatic_launch_tests PROPERTIES TIMEOUT 30)

add_executable(omakade_launch_audit EXCLUDE_FROM_ALL ../tools/LaunchAudit.cpp)
target_link_libraries(omakade_launch_audit PRIVATE omakade_core Qt6::Core)

add_test(NAME omakade_library_empty_review COMMAND omakade
  --render-overlay=library-empty-review --render-size=640x600
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/library-empty-review.png)
set_tests_properties(omakade_library_empty_review PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

add_test(NAME omakade_session_history_desktop COMMAND omakade
  --render-overlay=session-history --render-size=640x600
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/session-history-desktop.png)
add_test(NAME omakade_session_history_couch COMMAND omakade --couch
  --render-overlay=session-history --render-size=1920x1080
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/session-history-couch.png)
set_tests_properties(omakade_session_history_desktop omakade_session_history_couch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

add_test(NAME omakade_now_playing_desktop COMMAND omakade
  --render-overlay=now-playing --render-size=900x700
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/now-playing-desktop.png)
add_test(NAME omakade_now_playing_couch COMMAND omakade --couch
  --render-overlay=now-playing --render-size=1920x1080
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/now-playing-couch.png)
# The panel is where a couch player acts, so it has to hold together at the sizes
# a TV actually reports, not only at 1080p.
add_test(NAME omakade_now_playing_couch_720p COMMAND omakade --couch
  --render-overlay=now-playing --render-size=1280x720
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/now-playing-couch-720p.png)
add_test(NAME omakade_now_playing_couch_4k COMMAND omakade --couch
  --render-overlay=now-playing --render-size=3840x2160
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/now-playing-couch-4k.png)
set_tests_properties(omakade_now_playing_desktop omakade_now_playing_couch
  omakade_now_playing_couch_720p omakade_now_playing_couch_4k PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError"
  TIMEOUT 20)

# The stats screen renders against a fixture of recorded sessions, so the sections have real
# figures to draw and any QML error in them fails the test rather than waiting for a person to
# open the view.
add_test(NAME omakade_stats_screen COMMAND omakade
  --render-overlay=stats --render-size=900x900
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-desktop.png)
# The same overview in a taller window, with separate checks for the other views below.
add_test(NAME omakade_stats_screen_full COMMAND omakade
  --render-overlay=stats --render-size=1100x1700
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-full.png)
set_tests_properties(omakade_stats_screen omakade_stats_screen_full PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError"
  TIMEOUT 20)
foreach(stats_view IN ITEMS patterns library empty error)
  add_test(NAME omakade_stats_${stats_view}_render COMMAND omakade
    --render-overlay=stats-${stats_view} --render-size=900x900
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-${stats_view}.png)
  set_tests_properties(omakade_stats_${stats_view}_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
endforeach()
add_test(NAME omakade_stats_narrow_render COMMAND omakade
  --render-overlay=stats --render-size=600x800
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-narrow.png)
set_tests_properties(omakade_stats_narrow_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)
# The card, open in its preview.
add_test(NAME omakade_stats_card COMMAND omakade
  --render-overlay=year-in-review --render-size=1200x1000
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-card-preview.png)
# The card written out. The app exits with the outcome of the write, so a run that cannot produce
# the file fails here rather than leaving an export path nobody ever exercised.
add_test(NAME omakade_card_export COMMAND omakade --stats-fixture
  --export-card=${CMAKE_CURRENT_BINARY_DIR}/stats-card.png)
add_test(NAME omakade_card_export_dimensions COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/CheckPngSize.py
  ${CMAKE_CURRENT_BINARY_DIR}/stats-card.png 1200 2000)
set_tests_properties(omakade_card_export_dimensions PROPERTIES DEPENDS omakade_card_export)
# The same screen in Couch Mode, which is how it reaches a television.
add_test(NAME omakade_stats_couch COMMAND omakade --couch
  --render-overlay=stats --render-size=1920x1080
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/stats-couch.png)
set_tests_properties(omakade_stats_card omakade_card_export omakade_stats_couch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError"
  TIMEOUT 30)

qt_add_executable(omakade_save_backups_tests SaveBackupsTests.cpp)
target_link_libraries(omakade_save_backups_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_save_backups_tests COMMAND omakade_save_backups_tests)
set_tests_properties(omakade_save_backups_tests PROPERTIES TIMEOUT 30)

foreach(save_overlay IN ITEMS save-backups save-backups-confirm save-backups-shared-confirm
                              save-backups-delete)
  add_test(NAME omakade_${save_overlay}_desktop COMMAND omakade
    --render-overlay=${save_overlay} --render-size=640x600
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${save_overlay}-desktop.png)
  add_test(NAME omakade_${save_overlay}_couch COMMAND omakade --couch
    --render-overlay=${save_overlay} --render-size=1920x1080
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${save_overlay}-couch.png)
  set_tests_properties(omakade_${save_overlay}_desktop omakade_${save_overlay}_couch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

qt_add_executable(omakade_save_audit EXCLUDE_FROM_ALL ../tools/SaveAudit.cpp)
target_link_libraries(omakade_save_audit PRIVATE omakade_core Qt6::Core)

qt_add_executable(omakade_save_set_tests SaveSetTests.cpp)
target_link_libraries(omakade_save_set_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_save_set_tests COMMAND omakade_save_set_tests)
set_tests_properties(omakade_save_set_tests PROPERTIES TIMEOUT 60)

qt_add_executable(omakade_save_coverage_audit EXCLUDE_FROM_ALL ../tools/SaveCoverageAudit.cpp)
target_link_libraries(omakade_save_coverage_audit PRIVATE omakade_core)

qt_add_executable(omakade_cover_artwork_tests CoverArtworkTests.cpp)
target_link_libraries(omakade_cover_artwork_tests PRIVATE omakade_core Qt6::Test Qt6::Quick)
target_compile_definitions(omakade_cover_artwork_tests PRIVATE OMAKADE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME omakade_cover_artwork_tests COMMAND omakade_cover_artwork_tests)
set_tests_properties(omakade_cover_artwork_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)

qt_add_executable(omakade_romm_catalog_tests RommCatalogTests.cpp)
target_link_libraries(omakade_romm_catalog_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_romm_catalog_tests COMMAND omakade_romm_catalog_tests)
set_tests_properties(omakade_romm_catalog_tests PROPERTIES TIMEOUT 30)

qt_add_executable(omakade_feature_workflow_tests FeatureWorkflowTests.cpp)
target_link_libraries(omakade_feature_workflow_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_feature_workflow_tests COMMAND omakade_feature_workflow_tests)
set_tests_properties(omakade_feature_workflow_tests PROPERTIES TIMEOUT 60)

foreach(feature_overlay IN ITEMS library-repair library-repair-controls library-repair-relocate library-repair-manual library-repair-manual-runtime launch-setup settings-romm settings-save-overview)
  foreach(feature_size IN ITEMS desktop couch)
    if(feature_size STREQUAL "couch")
      set(feature_args --couch --render-size=1920x1080)
    elseif(feature_overlay STREQUAL "library-repair-relocate")
      set(feature_args --render-size=960x1080)
    else()
      set(feature_args --render-size=640x600)
    endif()
    if(feature_overlay STREQUAL "library-repair-controls")
      list(APPEND feature_args --repair-navigation-fixture)
    endif()
    add_test(NAME omakade_${feature_overlay}_${feature_size} COMMAND omakade ${feature_args}
      --render-overlay=${feature_overlay} --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${feature_overlay}-${feature_size}.png)
    set_tests_properties(omakade_${feature_overlay}_${feature_size} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1" TIMEOUT 20)
  endforeach()
endforeach()

add_test(NAME omakade_library-repair_controls_380 COMMAND omakade
  --render-size=380x700 --render-overlay=library-repair-controls
  --repair-navigation-fixture
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/library-repair-controls-380.png)
set_tests_properties(omakade_library-repair_controls_380 PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1" TIMEOUT 20)

add_test(NAME omakade_launch_setup_controller_entry COMMAND omakade --couch
  --render-overlay=launch-setup-entry --render-size=1920x1080
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/launch-setup-entry.png)
set_tests_properties(omakade_launch_setup_controller_entry PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1" TIMEOUT 20)

# Attributing a running game to a target list, without signalling anything.
qt_add_executable(omakade_game_stop_tests GameStopTests.cpp)
target_link_libraries(omakade_game_stop_tests PRIVATE omakade_core Qt6::Test)
target_compile_definitions(omakade_game_stop_tests PRIVATE
  OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME omakade_game_stop_tests COMMAND omakade_game_stop_tests)
set_tests_properties(omakade_game_stop_tests PROPERTIES TIMEOUT 30)

# Sending the signals a plan asks for, through a sink a test stands in for.
qt_add_executable(omakade_game_stopper_tests GameStopperTests.cpp)
target_link_libraries(omakade_game_stopper_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_game_stopper_tests COMMAND omakade_game_stopper_tests)
set_tests_properties(omakade_game_stopper_tests PROPERTIES TIMEOUT 30)

# The interface half: a library row to an identity, a preview, and a stop run
# off the calling thread.
qt_add_executable(omakade_game_stop_service_tests GameStopServiceTests.cpp)
target_link_libraries(omakade_game_stop_service_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_game_stop_service_tests COMMAND omakade_game_stop_service_tests)
set_tests_properties(omakade_game_stop_service_tests PROPERTIES TIMEOUT 60)

# The stop controls: the confirmation a per-game stop opens, and the global one.
# Both render against the overlay fixture, whose sink signals nothing.
set(stop_profiles_config "${CMAKE_CURRENT_BINARY_DIR}/stop-profiles-config")
file(MAKE_DIRECTORY "${stop_profiles_config}/omakade")
configure_file("${CMAKE_SOURCE_DIR}/resources/sessiond-profiles.json"
  "${stop_profiles_config}/omakade/sessiond-profiles.json" COPYONLY)
foreach(stop_mode IN ITEMS desktop couch)
  set(stop_args --render-size=600x800)
  if(stop_mode STREQUAL "couch")
    set(stop_args --couch --render-size=1920x1080)
  endif()
  foreach(stop_case IN ITEMS stop-game stop-game-apply stop-game-refused stop-all)
    add_test(NAME omakade_${stop_case}_${stop_mode} COMMAND omakade ${stop_args}
      --owned-layout-test --render-overlay=${stop_case}
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${stop_case}-${stop_mode}.png)
    set_tests_properties(omakade_${stop_case}_${stop_mode} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDG_CONFIG_HOME=${stop_profiles_config}"
      TIMEOUT 20)
  endforeach()
endforeach()

add_executable(omakade_release_hardening_tests ReleaseHardeningTests.cpp)
target_link_libraries(omakade_release_hardening_tests PRIVATE omakade_core Qt6::Test)
add_test(NAME omakade_release_hardening_tests COMMAND omakade_release_hardening_tests)
# The durability cases do real file and synchronization work, and this suite runs beside the other
# suites rather than alone: measured at 38.5 seconds on its own and 54 seconds while its own
# neighbours (`omakade_core_tests` and `omakade_backup_recovery_tests`) are in flight. A 45 second
# budget was therefore killing a run that was still passing, which is worse than useless: it hides a
# real regression in the noise. The budget covers the machine, not the code.
set_tests_properties(omakade_release_hardening_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "TZ=America/New_York;QT_FORCE_STDERR_LOGGING=1")

foreach(history_mode IN ITEMS desktop couch)
  set(history_args --render-size=640x600)
  if(history_mode STREQUAL "couch")
    set(history_args --couch --render-size=1920x1080)
  endif()
  add_test(NAME omakade_history_pages_${history_mode} COMMAND omakade ${history_args}
    --render-overlay=session-history-pages --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/history-pages-${history_mode}.png)
  set_tests_properties(omakade_history_pages_${history_mode} PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError")
endforeach()

foreach(card_case IN ITEMS empty paused error)
  add_test(NAME omakade_card_export_${card_case} COMMAND omakade --stats-fixture
    --render-overlay=year-in-review-${card_case}
    --export-card=${CMAKE_CURRENT_BINARY_DIR}/stats-card-${card_case}.png)
  set_tests_properties(omakade_card_export_${card_case} PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
endforeach()
set_tests_properties(omakade_card_export_error PROPERTIES WILL_FAIL TRUE)

add_test(NAME omakade_card_export_requires_fixture_flag COMMAND omakade
  --export-card=${CMAKE_CURRENT_BINARY_DIR}/unmarked-demo-card.png)
set_tests_properties(omakade_card_export_requires_fixture_flag PROPERTIES WILL_FAIL TRUE TIMEOUT 10
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")

# The overlay clicks the real action with a temporary Heroic CLI, checks that it
# opens management without tracking/recording play, and survives close-after-launch.
foreach(heroic_owned_mode IN ITEMS desktop couch)
  set(heroic_owned_args)
  if(heroic_owned_mode STREQUAL "couch")
    list(APPEND heroic_owned_args --couch)
  endif()
  add_test(NAME omakade_heroic_owned_${heroic_owned_mode}
    COMMAND omakade ${heroic_owned_args} --render-overlay=heroic-owned
      --render-size=1400x1000
      --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/heroic-owned-${heroic_owned_mode}.png)
  set_tests_properties(omakade_heroic_owned_${heroic_owned_mode} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError"
    TIMEOUT 20)
endforeach()

# The controller Guide button: the press policy, the evdev reader on fake device nodes, and
# the Settings switch over a stand-in systemctl.
qt_add_executable(omakade_guide_button_tests GuideButtonTests.cpp)
target_link_libraries(omakade_guide_button_tests PRIVATE omakade_core omakade_guide Qt6::Test)
add_test(NAME omakade_guide_button_tests COMMAND omakade_guide_button_tests)
set_tests_properties(omakade_guide_button_tests PROPERTIES TIMEOUT 30)
add_test(NAME omakade_guide_button_list COMMAND omakade-guide-button --list
  --dev-dir=${CMAKE_CURRENT_BINARY_DIR} --sys-dir=${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(omakade_guide_button_list PROPERTIES TIMEOUT 10
  PASS_REGULAR_EXPRESSION "No controllers with a Guide or Home button are connected")

qt_add_executable(omakade_in_game_guide_tests InGameGuideTests.cpp)
target_link_libraries(omakade_in_game_guide_tests PRIVATE omakade_core Qt6::Test Qt6::Qml)
target_compile_definitions(omakade_in_game_guide_tests PRIVATE
  OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  OMAKADE_GUIDE_GUARD="$<TARGET_FILE:omakade-guide-guard>")
add_dependencies(omakade_in_game_guide_tests omakade-guide-guard)
add_test(NAME omakade_in_game_guide_tests COMMAND omakade_in_game_guide_tests)
set_tests_properties(omakade_in_game_guide_tests PROPERTIES TIMEOUT 30)

qt_add_executable(omakade_resident_guide_tests ResidentGuideTests.cpp)
target_link_libraries(omakade_resident_guide_tests PRIVATE omakade_core Qt6::Test)
target_compile_definitions(omakade_resident_guide_tests PRIVATE
  OMAKADE_SESSIOND="$<TARGET_FILE:omakade-sessiond>"
  OMAKADE_APP="$<TARGET_FILE:omakade>"
  OMAKADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  OMAKADE_GUIDE_BUTTON="$<TARGET_FILE:omakade-guide-button>")
add_dependencies(omakade_resident_guide_tests omakade-sessiond omakade omakade-guide-button)
add_test(NAME omakade_resident_guide_tests COMMAND omakade_resident_guide_tests)
# The guide's clip and a screen recording started from Omarchy never take each other's place.
add_test(NAME omakade_guide_recorder COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/GuideRecorderTests.py ${PROJECT_SOURCE_DIR}/omarchy-plugin/guide-files.py)
set_tests_properties(omakade_resident_guide_tests PROPERTIES TIMEOUT 30)
