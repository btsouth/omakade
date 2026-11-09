omakade_app_test(omakade_source_rescan_enablement
  TIMEOUT 20
  ARGS --render-overlay=source-rescan --render-size=900x720
       --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/source-rescan.png)
