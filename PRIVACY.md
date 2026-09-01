# Omakade privacy

Omakade is local-first. It has no analytics, advertising, telemetry, account
system, or required online service.

## Local data

Omakade reads Steam library manifests, artwork caches, playtime, recent-play
state, and achievement caches. It also reads installed-game manifests and
cached artwork from Lutris and Heroic, plus configured playlists, thumbnails,
and runtime logs from RetroArch. It never writes into source launcher directories.

Omakade retains:

- Owned Steam games in the same database
- Library, source records, favorites, hidden state, and achievements in
  `$XDG_DATA_HOME/omakade/library.sqlite3`
- User-created links between duplicate installations in the same database
- Completion states, tags, collections, and collection memberships in the same
  database
- Steam ID, public IGDB client ID, cache limit, and reduced-motion preference in
  `$XDG_CONFIG_HOME/omakade/config.toml`
- Downloaded covers and achievement icons in `$XDG_CACHE_HOME/omakade/`
- Copies of covers selected by the user in `$XDG_DATA_HOME/omakade/artwork/`

The Steam ID is an account identifier, not a credential. A Steam Web API key
is stored only through the desktop Secret Service under
`io.github.tsouth89.Omakade.Steam`. Older preview keys stored as
`io.github.omakade.Steam` remain readable. The key is never written to Omakade's config, database,
logs, or process arguments.

Optional IGDB game insights require a Twitch developer client ID and client
secret supplied by the user. The public client ID is stored in Omakade's config.
The client secret is stored through Secret Service as
`io.github.tsouth89.Omakade.IGDB` and is never written to config, the database,
logs, or process arguments. Omakade sends these credentials to Twitch only to
obtain an app access token, then sends the token and client ID to IGDB.

## Network requests

Omakade may request missing covers and achievement icons from Steam's public
HTTPS artwork hosts. Responses are size-limited and the artwork cache is
bounded by the configured limit.

Omakade requests the account's owned games from Valve's documented
IPlayerService/GetOwnedGames endpoint when a Steam Web API key is stored and the
owned-games setting is on. It retains the app ID, title, playtime, and last-played
time for each owned game in `owned_games` in the local library database. Turning
the setting off hides those entries; removing the API key stops the requests.

Steam Web API requests occur only after the user stores a key. Omakade refreshes
stale achievement data when Steam game details open or when the user selects
Refresh Steam. It requests player achievements, the game's achievement schema,
and global rarity from Valve's documented HTTPS endpoints. Failed requests do
not remove cached data.

IGDB requests occur only after the user supplies their own Twitch developer
credentials. Omakade maps a Steam App ID to an IGDB game, then requests IGDB's
external critic aggregate and game-length estimates. Responses are cached in
the local library database for offline use and refreshed after 30 days.

## Removal

The settings panel can clear downloaded achievement art and remove Steam or
IGDB credentials from Secret Service. Removing Omakade does not remove its XDG
data by default, so users can preserve settings across reinstallations.
Resetting a custom cover removes Omakade's private copy and restores the
source-provided artwork. It does not change the original selected image.
