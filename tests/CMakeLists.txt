qt_add_executable(omakade_core_tests
  CoreTests.cpp
)
target_compile_definitions(omakade_core_tests PRIVATE
  OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME omakade_details_recovery_isolation COMMAND ${CMAKE_COMMAND}
  -DOMAKADE=$<TARGET_FILE:omakade>
  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/details-recovery-isolation
  -P ${CMAKE_CURRENT_SOURCE_DIR}/DetailsRecoveryIsolation.cmake)
set_tests_properties(omakade_details_recovery_isolation PROPERTIES TIMEOUT 20)

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
set_tests_properties(omakade_details_directions omakade_details_directions_couch PROPERTIES
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

add_test(NAME omakade_couch_thousand_game_startup
  COMMAND omakade --couch --stress-test --benchmark --benchmark-max-ms=500
)
set_tests_properties(omakade_couch_thousand_game_startup PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  RUN_SERIAL TRUE
  TIMEOUT 5
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
  TIMEOUT 10
)

add_test(NAME omakade_couch_owned_navigation
  COMMAND omakade --couch --couch-navigation-test --owned-layout-test
          --uninstalled-layout-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_owned_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE=;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-owned-navigation"
  TIMEOUT 10
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
set_tests_properties(omakade_artwork_editor omakade_artwork_editor_couch
  omakade_artwork_editor_render omakade_artwork_editor_couch_render PROPERTIES
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
foreach(info_case IN ITEMS game-info game-info-expanded game-info-empty)
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
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)

add_test(NAME omakade_library_return_recent
  COMMAND omakade --owned-layout-test --render-overlay=library-reflow-return --render-size=2024x1104
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/library-return-recent.png)
set_tests_properties(omakade_library_return_recent PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  FAIL_REGULAR_EXPRESSION "Binding loop;TypeError;ReferenceError" TIMEOUT 20)

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
