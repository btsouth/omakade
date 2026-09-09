# Matching and artwork audit, September 9, 2026

The local library snapshot contains 1,387 SNES games. Of these, 180 need identification and 243 have no usable source cover, downloaded portrait, or custom cover. This audit compared the actual ROM titles and saved identities with live IGDB, SteamGridDB, and Libretro data. It did not change the live library or installed application.

| Result | Games |
| --- | ---: |
| Previously unidentified, recovered by the new automatic matching rules | 68 |
| Still ambiguous or search results truncated | 10 |
| Still without a confident match from the tested searches | 102 |
| Missing covers with an identified artwork route | 191 |
| Missing covers still needing review or additional artwork | 52 |

The 191 artwork routes include 107 reachable Libretro box images, 20 SteamGridDB portrait candidates for identified or recoverable games, and 76 reachable IGDB fallback images. Sources overlap. These are availability findings, not a claim that the running app has downloaded or displayed them. SteamGridDB cannot guarantee that a shared game entry's physical box depicts the desired platform or region.

## Game-by-game results

- [All 180 unidentified SNES games](unidentified-snes.csv): automatic recovery decisions, selected IGDB records, and unconfirmed suggestions for the remaining games.
- [All 243 missing-cover SNES games](missing-covers-snes.csv): provider links, HTTP checks, portrait counts, and cases requiring manual selection.
- [Other unidentified games](other-unidentified.csv): the five non-SNES entries from the metadata snapshot.

Unconfirmed suggestions are search aids. Some are unrelated games. A failed search is not proof that IGDB lacks the game. The 102 unresolved names include alternate romanizations, renamed titles, incomplete aliases, prototypes, homebrew, and unofficial games. No arbitrary spelling-distance match was applied to the library.

## Confirmed causes and local fixes

- Automatic IGDB searches missed names that differ in spacing, punctuation, compounds, or “Part II.” A bounded, platform-filtered discovery query now retrieves those candidates. Acceptance still requires an equivalent full name or catalog alias, with number boundaries preserved.
- Mods sometimes list the original game's name as an alias. An explicitly named original can now beat a differently named mod that only matches through that alias. Same-named editions remain ambiguous.
- Separate SNES and Super Famicom catalog records competed even when a ROM had an explicit region. A single North American, European, or Japanese tag can now distinguish those platform records. It does not decide between editions for the same machine.
- SteamGridDB matching now handles explicit Japanese long vowels and known SNES platform qualifiers. Mickey's “Daibouken” versus “Daibōken” and Alien vs. Predator “(Nintendo)” are regression cases. The Peace Keepers' duplicate undated entries still require a choice.
- Libretro filename lookup now recognizes regions combined with revisions and tries the same complete title under standard region labels. Translations frequently replaced the original region tag. Of 110 newly generated filename candidates present in the repository, 107 served images; three returned HTTP 404.
- Transient download errors were treated as missing artwork. Only HTTP 404/410 now contribute to persistent negative caching. Rate limits, server failures, invalid images, and save failures are retryable. The old negative-cache markers are invalidated.
- The cover queue and viewport prefetch are larger. Visible empty cards retry dropped requests and transient failures in desktop and Couch Mode.
- IGDB cover images now provide a cached fallback after custom covers, downloaded portraits, and source artwork. Image IDs, hosts, sizes, and image decoding are validated. Provider API credentials are not sent to image hosts.
- The manual search fields were rebound when metadata changed. Editing or clearing a query now survives those notifications. Manually entered parentheses are passed through to searches.
- Corrupt Ryujinx display titles containing the replacement character now fall back to the ROM filename while valid Japanese titles remain intact.

## The examples in the screenshots

| Example | Finding |
| --- | --- |
| Super Back to the Future, Part II | Recovered as IGDB 8520, Super Back to the Future II. A SteamGridDB portrait is available. |
| Mr. Bloppy Saves the World | ROM title appears misspelled. IGDB 42659 is Mr. Bloopy Saves the World. Listed for review, not automatically guessed. |
| Pocket Monster (TW) | Actual SNES file and an identified unofficial game. Its IGDB cover is reachable. |
| Pokémon Stadium (TW) | Discovery finds the SNES bootleg, IGDB 163098, with its own reachable cover. SteamGridDB's same-name official-game result is not automatically used. |
| Pokémon Gold & Silver (TW) | Actual SNES file. Remains unidentified; official Game Boy titles must not be substituted. |
| Super Air Diver 2 | Already identified. IGDB supplies the missing fallback cover. |
| Street Fighter V (NA) | Remains unresolved for SNES. A modern Street Fighter V identity would be incorrect. |

Unlicensed/Taiwan-tagged ROMs now require a manual SteamGridDB choice when no portrait already exists. Their exact names can collide with official games on other platforms. An identified IGDB cover can still be used.

All 1,207 existing identified SNES IDs occur in the current SNES/Super Famicom catalog. Their ROM names match a catalog name or alias under the checked normalization, except Timon & Pumbaa's Jungle Games, which differs only by the supported “Disney's” prefix. This supports the existing title/platform matches, but is not ROM-hash verification of every revision or edition.

## Evidence and validation

The audit used a consistent, read-only SQLite backup, 3,093 IGDB SNES/Super Famicom catalog records, 3,755 Libretro box-art paths, and live searches for every unidentified SNES title and every missing-cover title. Additional alias searches were performed for unresolved artwork with an identified or recoverable IGDB record. The raw snapshots and scripts remain local in `build/matching-audit-20260909`; credentials were not saved with the results.

The 68 recovery decisions are captured as provider-response fixtures and exercised through the production matching code. The offline adapter is `tools/MetadataAudit.cpp`, built with `cmake --build build/dev --target omakade_metadata_audit`.

Validation and the exact local candidate are recorded in [candidate.md](candidate.md). The running library still needs a refresh under that candidate, followed by an actual scrolling and artwork-selection check. First-time downloads remain dependent on provider latency and rate limits.
