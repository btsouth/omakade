# Stats screen and year-in-review card

Written 2026-09-17. Forward execution plan for the stats screen and the shareable
year-in-review card, on branch `codex/stats-year-in-review` off `codex/1.11-sessions`.

## User outcome

Open a Stats view, pick a period, and see what you played and when: how much, on which
platforms and launchers, in what shape of sessions, at what hours, in what streaks, and
what you finished or unlocked. Export that period as a shareable card image, designed the
way the app looks, that a person can post anywhere.

Two rules hold the whole feature together:

1. **The numbers agree with the library.** Every playtime figure shown here is the same
   reconciled figure the library shows for the same game, produced by the same code. A
   second playtime resolver is not permitted; it is how the product starts contradicting
   itself.
2. **The card never overclaims.** Playtime comes from two different clocks and they are
   labelled everywhere they appear, on screen and on the card.

## What we have to build on (checked against the real database, 2026-09-17)

- `play_sessions`: one row per recorded session with `started_at`, `ended_at`, `seconds`,
  `source`, `game_path`, `session_key`. This is the only dated play data we have. It starts
  when recording starts: on the maintainer's machine, 2026-09-08, 73 sessions, 56 hours.
- Per-source imported counters (`ryujinx_games.playtime_seconds`, `owned_games.playtime_minutes`,
  and the other source tables) reconciled by `PlaySessionStore` into the library's
  `PlaytimeSeconds`, `PlaytimeText` and `PlaytimeProvenance` roles. These totals go back
  further than recording and **cannot be split by period**.
- `UnifiedGameModel` roles: `Title`, `Source`, `System`, `Genres`, `Year`, `Rating`,
  `AchievementsUnlocked`, `AchievementsTotal`, `CompletionStatus`, `Tags`, `Collections`,
  `LastPlayed`, `Favorite`, `PlaytimeSeconds`, `PlaytimeProvenance`.
- `achievements` and `achievement_summary`: 5,504 rows from three sources (Steam local,
  Steam web, RetroAchievements), 400 unlocked, every row carrying `unlock_time` and the
  Steam rows a `rarity`. RetroAchievements has 2,057 rows and 0 unlocked today.
- `game_insights`: HowLongToBeat-style `rushed_seconds`, `normal_seconds`,
  `complete_seconds` and a critic score, for 30 games.
- `launch_activity`: `launch_count` and `last_launched` for 43 games.
- `game_metadata` payloads: per game `genres`, `platform`, `year`, `developers`, `rating`,
  `aliases`, `igdbId`.
- `HomeModel` is the precedent for this shape of code: it takes `UnifiedGameModel*` plus a
  database path and exposes `QVariantList` properties to QML.

## The two clocks, and the honesty rule

| | Source | Dated? | Honest claim |
| --- | --- | --- | --- |
| Library total | reconciled per-game totals | no | "your library reports N hours" |
| Recorded play | `play_sessions` | yes | "Omakade recorded N hours across M days since <date>" |

Consequences, which the UI and the card must both respect:

- A period figure that mixes them says so. For "2026" the card shows recorded play for the
  year, and library totals separately, labelled.
- Anything only derivable from dated sessions (hours of day, session shape, streaks,
  first-time plays, returns after a gap) is drawn from the recorded window only, and the
  screen states the window when it is shorter than the period.
- When the recorded window is empty, the screen says what recording would show rather than
  rendering zeroes as if they were facts.

## What the screen shows

**1. Headline.** Recorded time for the period, library total, games played, days played,
sessions, and the top game with its share of recorded time.

**2. Time patterns (recorded).** Hours-of-day strip, day-of-week split, late-night share
(23:00 to 05:00), and the busiest single day.

**3. Session shape (recorded).** Average and longest session with the game that holds it,
sessions over two hours, and a length distribution (under 15 minutes, 15 to 60, 1 to 3
hours, over 3 hours).

**4. Streaks (recorded).** Longest run of consecutive days played, the current run, and
days off in the period.

**5. Where the time went.** By system and by source or launcher. This is the part Steam
cannot do: emulator platforms (Switch, PS2, GameCube, Wii, Wii U, PS4, Xbox 360, and
RetroArch cores) sit beside Steam, GOG, Epic, Amazon, Battle.net and Lutris in one picture.

**6. Finished and unlocked.** Games marked finished in the period, achievements unlocked,
the rarest one, and the unlock rate. RetroAchievements is included, so emulator play
counts, which no launcher-level recap can show.

**7. Backlog behaviour.** Games played for the first time in the period, one-and-done
games (played once, never again), and returns after a long gap ("came back to X after 42
days"), which the session history makes computable.

**8. The library in numbers.** Size, systems covered, genres by recorded time, top rated.

## The card

A portrait card, 1200 by 2000, rendered from a dedicated QML scene in the app's own visual
language, exported as a PNG with a preview and an "open folder" action. Content: the app
mark, the period, the top game's artwork, six to eight hero figures, an hours-of-day strip,
the platform split, and the provenance labels. No network, no upload, no format other than
PNG.

Why it is better than Steam's recap rather than a copy of it: it covers every launcher and
every emulator, it can talk about the shape of play (session lengths, hours, streaks,
returns) because we have per-session timestamps, it includes RetroAchievements, and it
states plainly what is measured versus reported instead of presenting a launcher counter as
measured play.

## Slices

1. **Slice 1, the data layer.** `PlayStats` next to `HomeModel`: takes `UnifiedGameModel*`
   and the database path, computes the period's aggregates, and exposes them to QML.
   Playtime-derived aggregates come from the model's roles; dated aggregates come from
   `play_sessions`. Unit tests cover an empty library, a library with no sessions, one game,
   one session, sessions spanning midnight, a session still open, missing metadata, and a
   period with no recorded play. **No UI in this slice.**
2. **Slice 2, the screen and its entry point.** Navigation entry on desktop, the headline
   section, top games, and the library/source breakdown, bound to real data, with honest
   empty and error states.
3. **Slice 3, the recorded shape.** Time patterns, session shape and streaks.
4. **Slice 4, outcomes.** Finished games, achievements, backlog behaviour, library in
   numbers, and the period selector.
5. **Slice 5, the card.** The card scene, the PNG export, the preview and the open-folder
   action, with tests that the export produces a real image of the right size and that the
   card's figures match the screen's for the same period.
6. **Slice 6, the couch and the edges.** Couch Mode treatment, keyboard and controller
   navigation with focus that survives model replacement, failure states (unreadable
   database, recording off, no metadata), and the acceptance pass.

Each slice: implement, cover with tests, run the full suite at the milestone, commit, push.
The plan doc's status section is updated in the same commit as the slice it describes.

## Acceptance journey

Open Stats from the nav, confirm the headline matches the library's own totals for a game
picked at random, walk every section on desktop, switch the period from this year to all
time and back, export a card and open the file, confirm the card's figures equal the
screen's, restart the app and confirm the chosen period is remembered, then repeat the walk
in Couch Mode with a controller and confirm focus reaches every section and returns.

## Out of scope, deliberately

- Splitting imported launcher counters by period. Impossible without dated data; the card
  says "reported" instead of pretending.
- Library growth by acquisition date. There is no trustworthy added-at timestamp.
- Any upload, sharing service, or non-PNG export.
- Per-game notes, a screenshots gallery, and other unrelated parity items.
- Store, install, cloud-save or plugin work. The plan defers all of it.

## Open decisions for the maintainer

- Version naming: M8 is "1.11" in `PLAN.md` and M9 is "from 1.12". If this work ships in the
  same release as M8, that release is really 1.12. Branch names and the changelog follow
  whichever is chosen.
- Whether Stats becomes a fifth top-level destination, or lives behind Library as a view.
  The slices assume a top-level destination, which is what a headline feature deserves and
  what the nav can carry; it is one file's worth of work to move it.

## Status

- 2026-09-17: plan written, branch `codex/stats-year-in-review` created off
  `codex/1.11-sessions` at `90d1f18`.
- 2026-09-17: **slice 1 done.** `src/library/PlayStats.{h,cpp}` plus six tests
  (`stats*` in `CoreTests`), all passing with no build warnings. No UI yet, and nothing calls it
  from the app.

Two things the tests settled against the design's assumptions, both worth keeping:

- Genres, the year and the rating reach the library through the **metadata layer**, not from a
  source model, and only for games whose identity is confirmed. So the fixtures seed
  `game_metadata` payloads rather than source models, and a game with no metadata reports no
  genres instead of inventing them.
- Completion is a **current state** kept in the library's own `game_organization` table, as a
  lowercase id (`backlog`, `playing`, `completed`, `abandoned`). It has no date on it, so it is
  reported as a state and never as "finished this period". The screen labels it from the id.

One defect was found and fixed while writing it: the backlog walk read a hash entry through an
iterator that the same statement's `insert` could invalidate by rehashing, which segfaulted the
first test run. The iterator is now read before the insert.

- 2026-09-17: **slice 2 done.** `qml/screens/StatsScreen.qml`, a STATS entry in the navigation
  row, the `Stats` context property, and a headless render test (`omakade_stats_screen`) that
  seeds a fixture of recorded sessions and fails on any QML error. The screen shows the period
  chips, the window note, the headline figures, the most played game, the system and launcher
  breakdowns and the library counts. Time patterns, outcomes and streaks are slices 3 and 4;
  the couch treatment is slice 6, so the screen hides itself in Couch Mode for now and entering
  Couch Mode closes it.

The render test earned its place on the first run: it passed while the screen said "nothing has
been recorded yet", because the overlay was missing from the list of overlays that get a
writable fixture database, so the fixture and the screen each opened their own empty in-memory
database. A test that passes is not the same as a screen that draws the data; the screenshot is
what caught it. Worth remembering for every remaining slice.

A fifth destination also broke `omakade_controller_navigation_narrow`, which renders at 600 by
800 and fails if any toolbar control leaves the window: the row no longer fit. The fix is in
`Main.qml`, not in the test: below 700 pixels the brand drops to its icon, because five
destinations matter more there than repeating a name the window title already shows. That is
under the window's own 820 pixel minimum, so it only ever appears in the harsher layout test,
and it is visible in the 600 pixel render as a clean row with every border intact.

- 2026-09-17: **slice 3 done.** Hours of day and weekday strips with a sentence naming the
  busiest hour, the busiest day and the late-night share; session shape (average, longest with
  the game that holds it, count over two hours, and the length distribution); and streaks.

Reading the render caught a defect the unit tests had blessed, which is the argument for
looking at the picture: **days off was counted from 1 January** in a year period, so a window
that began on 5 September reported 255 days off, 246 of them before the recorder existed. It
now counts only from the day recording starts, and a test pins the corrected window by failing
without it.

The check also renders at 1100 by 1700 as well as 900 by 900 now: the first screenshot showed
only the top half, and the sections below the fold were going unverified. That is how a
"missing" launcher row turned out to be an artifact of the crop rather than a defect, and it is
worth knowing which of the two you are looking at before reporting either.
