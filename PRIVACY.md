# Omakade privacy

Omakade is local-first. It has no analytics, advertising, telemetry, account
system, or required online service.

## Local data

Omakade reads Steam library manifests, artwork caches, playtime, recent-play
state, and achievement caches. It also reads installed-game manifests and
cached artwork from Lutris and Heroic, Heroic's sideloaded game list and play
timestamps, configured playlists, thumbnails, and runtime logs from RetroArch,
and Battle.net Agent product databases plus last-played stamps inside Wine,
Proton, and Bottles prefixes. It also reads configured emulator paths and game metadata from PCSX2,
Ryujinx, Cemu, shadPS4, and Dolphin. Switch title/icon extraction reads installed
Ryujinx keys locally; those keys and game files are not uploaded. It never writes
into source launcher directories.

Omakade retains:

- Library, source records, favorites, hidden state, and achievements in
  `$XDG_DATA_HOME/omakade/library.sqlite3`
- User-created links and preferred installations in the same database
- Manual game titles, executable paths, arguments, working directories, and
  saved filter queries in the same database
- Completion states, tags, collections, collection memberships, game-identification
  choices, provider IDs, ratings, and popularity scores in the same database
- Owned Steam App IDs, titles, and account playtime after an explicit library
  sync in the same database
- Steam ID, RetroAchievements username, public IGDB client ID, cache limit, and
  reduced-motion preference, console-view overrides, and cover sizes in
  `$XDG_CONFIG_HOME/omakade/config.toml`
- Downloaded covers and achievement icons in `$XDG_CACHE_HOME/omakade/`
- SteamGridDB portraits in `$XDG_DATA_HOME/omakade/portrait-covers/`
- Copies of covers, heroes, and logos selected by the user in
  `$XDG_DATA_HOME/omakade/artwork/`
- Configured GOG folders and desktop/Couch Mode preferences in the settings file
- Private restore jobs and recovery copies in
  `$XDG_DATA_HOME/omakade/restore-recovery/`

The Steam ID is an account identifier, not a credential. A Steam Web API key
is stored only through the desktop Secret Service under
`io.github.tsouth89.Omakade.Steam`. Older preview keys stored as
`io.github.omakade.Steam` remain readable. The key is never written to Omakade's config, database,
logs, or process arguments.

A RetroAchievements Web API key is stored only through the desktop Secret
Service under `io.github.tsouth89.Omakade.RetroAchievements`. It is never
written to Omakade's config, database, logs, or process arguments.

Optional IGDB game insights require a Twitch developer client ID and client
secret supplied by the user. The public client ID is stored in Omakade's config.
The client secret is stored through Secret Service as
`io.github.tsouth89.Omakade.IGDB` and is never written to config, the database,
logs, or process arguments. Omakade sends these credentials to Twitch only to
obtain an app access token, then sends the token and client ID to IGDB.

A SteamGridDB API key is stored through Secret Service under
`io.github.tsouth89.Omakade.SteamGridDB`. It is never written to config,
the database, logs, or process arguments.

## Backup and restore

Export creates a local archive at the path you choose. It includes personal
library choices, manual launch details, saved filters, custom artwork, and
supported preferences. It excludes API credentials, Omakade account-service
identifiers, game files, launcher databases, and downloaded caches. Paths and
manual arguments can contain personal information; an export is not encrypted.
Omakade does not upload it.

Restore keeps the incoming archive and a pre-restore recovery archive locally.
It also keeps an exact copy of the local settings file for interrupted-restore
recovery, so the private recovery folder can contain local account identifiers
that portable exports omit. Recovery files have owner-only access. Completed
recovery jobs are retained; they are not automatically deleted after success.
Restoring does not reconnect accounts or launch imported entries.

## Network requests

Omakade may request missing covers and achievement icons from Steam's public
HTTPS artwork hosts, missing Battle.net covers and banners from Lutris's
public game-art URLs, and missing RetroArch box art from Libretro's public
thumbnail CDN (`thumbnails.libretro.com`). Missing Dolphin covers may be requested
from GameTDB (`art.gametdb.com`) using the disc's game ID. Libretro requests send a
playlist name and game label in the URL. They do not upload ROMs or hashes.
Responses are size-limited, accepted only if they decode as an image, and
the artwork cache is bounded by the configured limit.

Steam Web API requests occur only after the user stores a key. Omakade refreshes
stale achievement data when Steam game details open or when the user selects
Refresh Steam. It requests player achievements, the game's achievement schema,
and global rarity from Valve's documented HTTPS endpoints. Failed requests do
not remove cached data.

Owned-library requests occur only when the user selects Sync Owned Steam
Library. Omakade requests the public game list and playtime for the configured
Steam ID, then caches it locally. A failed or private-profile response does not
replace the previous cache. Steam remains responsible for installation.

IGDB requests occur only after the user supplies their own Twitch developer
credentials. Omakade maps a Steam App ID to an IGDB game, then requests IGDB's
external critic aggregate and game-length estimates. Responses are cached in
the local library database for offline use and refreshed after 30 days.

Selecting Update Ratings & Portraits in Settings or searching in game details
sends game titles and platform IDs to IGDB, or Steam App IDs when available.
Library ratings use IGDB's combined `total_rating` field. Popularity uses its
IGDB-visit metric. Manual identification choices stay in the local database.

SteamGridDB requests occur only after the user supplies an API key and asks
for a library update or portrait selection. Requests send game titles or
provider game IDs, never ROM files, installation paths, or ROM hashes.
Portraits are downloaded from SteamGridDB's HTTPS image hosts, decoded, and
cached locally. The configured artwork limit bounds the portrait cache and
achievement cache separately. Removing a connection keeps cached artwork;
Settings can clear downloaded portraits without deleting chosen custom covers.

RetroAchievements requests occur only after the user supplies a username and
Web API key. Omakade downloads supported game hashes and matches ROM hashes
locally. It sends the matched game identifier and configured username to
RetroAchievements to retrieve progress. Responses are cached in the local
library database for offline use.

## Removal

The settings panel can clear downloaded achievement art and remove Steam,
RetroAchievements, IGDB, or SteamGridDB credentials from Secret Service. Removing Omakade does not remove its XDG
data by default, so users can preserve settings across reinstallations.
Resetting a custom artwork slot removes its unused Omakade copy and restores
the source-provided artwork. It does not change the original selected image.
cached SteamGridDB portrait or source-provided artwork. It does not change the
original selected image.

## Console keys

To show the icon and name stored inside a Switch dump, Omakade reads the
`prod.keys` and `title.keys` files that Ryujinx (or yuzu) already keeps in its
own configuration folder. The keys are used in memory to decrypt only the
small control section of each dump. They are never copied, written to
Omakade's config or database, logged, or sent anywhere. Without those files
Switch artwork falls back to files next to the dump and to Ryujinx's own
covers folder.

## Cover downloads

When a card becomes visible and no local artwork exists, Omakade may fetch a
cover from the Libretro thumbnail server (RetroArch systems) or from GameTDB
(GameCube and Wii, the same source Dolphin uses). Only the game's name or disc
id is sent. Downloads are cached under `~/.cache/omakade/covers` and a system
setting for the artwork cache size limits them. Games without a match are
remembered for a week so they are not requested on every launch.
