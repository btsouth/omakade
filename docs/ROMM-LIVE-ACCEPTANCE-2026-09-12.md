# RomM live integration check

September 12, 2026. Tested Omakade's installed `3afe8190c90f` candidate against an
actual local RomM 5.2.0 server. No Omakade runtime fix was required.

## Environment

- Official `rommapp/romm:5.2.0` image, manifest digest
  `sha256:3512f2ca455782f90247271bed23116e6bc675bc74e379be2c41696e607ab11e`.
- MariaDB 11.4, temporary database, dedicated Docker Compose network.
- RomM bound only to `127.0.0.1:18765`, one CPU and 2 GiB memory maximum.
- Disposable local library containing `roms/snes/Omakade QA.sfc`.
- Dedicated test administrator and Client API Tokens with only `roms.read`.
- Isolated Omakade settings, cache, database, runtime directory, and temporary directory.
  The desktop keyring was exercised with a temporary server-scoped credential.

Setup followed the [official Compose example](https://github.com/rommapp/romm/blob/5.2.0/examples/docker-compose.example.yml)
and [Client API Token authentication](https://docs.romm.app/5.0.0/developers/api-authentication/).
RomM's own scanner populated its database; responses were not mocked or injected.

## Results

| Check | Result |
| --- | --- |
| Connect through Omakade's actual RommGameModel and desktop keyring | Pass |
| Read catalog with real read-only Client API Token | Pass |
| Resolve `fs_path` and `fs_name` to the mounted local file | Pass |
| Rename mount away, refresh, retain catalog and mark unavailable | Pass |
| Restore mount and recover | Pass |
| Invalid token, visible unauthorized state, cached entry retained | Pass |
| Favorite retained through refresh and authentication failure | Pass |
| Stop server, recreate Omakade model, retain cached identity and favorite | Pass |
| Restart server, reconnect, retain stable identity | Pass |
| Disconnect and forget token, verify keyring entry removed | Pass |
| Token without `roms.read` cannot read catalog | Pass, HTTP 403 |
| Read-only token cannot create additional credentials | Pass, HTTP 403 |
| Revoke token, reject subsequent catalog request | Pass, HTTP 401 |
| Installed Omakade `--play` with empty local cache fetches real catalog and hands correct file to controlled process | Pass |

The opt-in `RommCatalogTests::liveServerAcceptance` run passed in 18.954 seconds.
It uses actual HTTP and keyring operations and restarts only the dedicated QA container.
The ordinary RomM catalog and feature-workflow CTest targets also passed afterward.

The CLI test used an executable that records the received file path. It did not certify
emulator gameplay, RomM-specific play-session recording, or save capture during a real
RomM game session. Those integration components retain their existing automated fixture
coverage. A headless emulator preflight did not complete; once Cemu was running, no
further builds or interactive emulator testing were performed. The running game and its
recorder were left in place.

## Cleanup and repeatability

All temporary API tokens were revoked and keyring credentials removed. The dedicated
containers, network, downloaded QA images, temporary database, account credentials, and
disposable library were removed. No RomM service remains installed or running. Existing
containers, personal library settings, saves, and installed Omakade binaries were preserved.

The opt-in test skips during ordinary CTest runs. To repeat it, provision the dedicated
local server described above, name its app container `omakade-romm-qa-romm-1`, mount the
one-entry library at `/tmp/omakade-romm-live/library`, and create a `roms.read` token.
Save its creation response privately as JSON containing `raw_token`, then run:

```sh
OMAKADE_LIVE_ROMM_TOKEN_FILE=/path/to/private-token.json \
  QT_QPA_PLATFORM=offscreen \
  build/release/tests/omakade_romm_catalog_tests liveServerAcceptance
```

A working desktop Secret Service is required. The test deliberately stops/restarts its
named QA container and renames the temporary library mount, so use only disposable data.
Local evidence logs are under `build/romm-live/`; they contain no API token values.
