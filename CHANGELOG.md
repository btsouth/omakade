# Changelog

## Unreleased

- In the Game Mode library, the large game shown above the cards now always matches the
  focused card. After a sort, a filter or a return from a game's page, it could show the
  first game while focus stayed on another. The large cover also shrinks to fit above
  the cards on small displays, such as 1920x1080 at scale 1.6, instead of covering them.
- If Omakade is closed by force during Game Mode, its next start puts back the workspace
  you were on before Game Mode when the screen was left on the empty Game Mode
  workspace. Leaving the next Game Mode session then returns there instead of to that
  empty workspace.
- The installed `omakade` no longer carries the UI test harness: the screenshot render
  paths, test fixtures, self-tests, navigation and layout checks, and the stress and
  start-up timing runs. They moved to `omakade-uitest`, which is built with the tests and
  never installed. `omakade --smoke-test` still checks the installed app.
- OmaStore installs can now use the controller Home button. OmaStore sets up its service,
  which stays off until it is turned on in Settings > Controls. Its service starts before
  Hyprland at login, so it usually needs turning off and on again after each login.
- The installed `omakade-sessiond` and `omakade-guide-button` user services now start the
  binaries from the install prefix, so a build installed under `/usr/local` no longer looks
  for them in `/usr/bin`.
- Settings > Appearance has a Text size choice: 90%, 100%, 115%, 130% or 150%. It sizes
  text, buttons and fields on the desktop, and by default follows the desktop's text
  scaling factor. Game Mode keeps sizing its text to the display. The small 8 to 11 px
  labels and explanations in Settings, game details and the library now use the shared
  text sizes, so they are easier to read and match each other.
- The package now installs Troubleshooting, the in-game guide page, Architecture and the
  README's screenshots, so every link in the installed docs opens. Troubleshooting now
  says a source that is on but not found is listed under Available, and the user guides
  no longer carry test checklists.
- Games on a Steam library drive that is unplugged or unreadable now stay in the library,
  marked as on a disconnected drive, instead of disappearing until the drive is back.
  Settings > Sources > Steam lists such libraries with Forget for one that is gone for good.
  Recheck in Repair Library rescans Steam, so a drive that is plugged back in clears there.
- A damaged or crafted Wii U `.wua` archive can no longer make a Cemu scan use gigabytes of
  memory by giving thousands of files the same long name. Such archives are skipped.
- Omakade's library database, settings and cache folders are now private to your user.
  Other accounts on the same computer can no longer read your play history, game paths or
  account names, and folders an earlier version created are tightened on the next start.
- When an emulator, manual game, native port or GOG game closes within a few seconds of
  starting, Omakade now says it closed right after starting and offers Open Log, on the
  desktop and in Game Mode, instead of only having said it was opening. Such a launch no
  longer counts as recently played. A game closed with Stop is not reported. Neither is a
  launch that leaves something running, or a manual game or port that starts a script or
  a wrapper such as env or flatpak, or hands a program a link, since those may only pass
  the game to a launcher. What these games print goes to a log per game, cut above 2 MiB
  while Omakade follows the game, at its next launch and when Omakade starts. No log is
  kept for a launch Auto-close closes Omakade after, or for a game `omakade --play`
  starts while Omakade is closed. Steam, Heroic and other launchers that hand the game off
  are not affected. MangoHud settings written for a launch are removed when the game
  closes, and ones left from earlier runs at startup.
- The Year in Review card preview now opens on its own panel, like Settings, so its title
  and Save image and Close buttons no longer draw over the Omakade logo and header buttons.
- Settings > Backup & Storage now uses the same text sizes and section headings as the
  other Settings pages, grouped as Save protection, Backup & restore and Downloaded artwork.
- Button labels now sit in the middle of their buttons instead of at the left edge. The
  main action on a screen, such as Play or Set Up Sources, is a solid accent button with
  text picked to stay readable on every theme, so it stands out from selected toggles like
  Favorited. Delete and other confirmations that cannot be undone are solid red, with a
  fixed red on themes whose red is not red or sits too close to the accent.
- Screen readers now hear a game card's favorite, completion status and install state in
  the library, Home and Game Mode, and can open the game from the card. Hidden achievements
  no longer reveal their name to screen readers before they are unlocked.
- The controller favorite button no longer favorites the game behind an open editor or
  dialog, such as the artwork editor opened from the game details.
- Reduced motion now follows the desktop by default. Omakade reduces motion when
  Hyprland animations or GTK animations are off, and Settings > Appearance offers System,
  Reduced and Full. An earlier Reduced choice is kept. The library grid selection also
  stops sliding when motion is reduced.
- The app and package are about 80 MB smaller. Console cards are now bundled as 768x1152
  WebP, only for consoles Omakade supports, instead of every full-size PNG master, and they
  decode faster. The Nintendo DS card now shows its console art instead of a plain gradient.
- Play recording now covers Steam. A Steam game installed in your library gets dated
  play sessions while it runs, whether it was started from Omakade or from Steam, so it
  shows in Stats, Year in Review and the game's Play History. Steam's own total is
  unchanged. Non-Steam shortcuts are not recorded, an emulator started from Steam is
  recorded once as the emulator, and Pause When Unfocused follows the game's window.
  Recording starts about ten seconds after launch and only while the Steam source is on.
  Discord Status now shows Steam games too, by their Steam title. The recording text in
  Settings and a game's Play History now mention Steam.
- A favorite now looks like one: the game details and Game Mode favorite button shows a
  filled accent heart with the selected fill and says FAVORITED, and screen readers hear it
  as checked. Library, Home and Game Mode cards mark favorites with a filled heart on an
  accent badge. The theme fonts drew both hearts as outlines before.
- Counts read correctly for a single item, such as "1 GAME" in the library toolbar
  instead of "1 GAMES", and the same on Home, Settings, game details and save panels.
- An empty Stats now says when play recording is off, which it is on new installations,
  and offers Turn On Recording with a line on what is recorded and which emulators give
  dated sessions. With recording on, it still says nothing has been recorded yet.
- Restoring a save backup is now refused while PPSSPP runs as PPSSPPSDL or PPSSPPQt, as it
  already was for other emulators, so a restore no longer writes under a running game.
- The restore preview lists every manual game launch command a backup adds or changes,
  with its program, full arguments and working folder, and warns when a shell or
  interpreter is told to run code from its arguments, including through wrappers
  like gamemoderun or a terminal. Merge and Replace confirmations
  say these commands will be able to run when you choose Play.
- Choosing a Settings category, or opening Settings on one as Connect in game details
  does, now puts focus on its first control instead of the second. On Home, Down from
  Play and the buttons beside it goes to the first card on the shelf below rather than
  the nearest one.
- Settings > Connections explains that a connected IGDB or SteamGridDB looks up the
  whole library in the background, and a new Identify Library Automatically switch
  turns that off. Privacy now describes these lookups as they really happen.
- The Sources picker lists only sources that have games, and choosing Emulated shows one
  Emulated chip instead of one per emulator. Settings > Sources lists a source as In use
  only once it is found; sources that are on but not found move to Available.
- An empty Library or Home, as on a first run, now has **Set Up Sources**, which opens
  Settings > Sources, and a link to the guide, next to Rescan. When two or more filters
  together leave nothing to show, the message names them instead of only the search, with
  the chosen sources counted as one filter.
- Keyboard and controller focus now has its own thicker ring around buttons, fields,
  checkboxes, lists and library cards, in a color that stands out on every theme. Hover only changes the fill,
  and the current card keeps a quieter outline while focus is elsewhere, such as the
  search field.
- Selected tabs, choices and switched-on settings now have an accent fill and marker
  that stay visible while hovered or focused. Screen readers hear them as checked, and
  the bare ON/OFF settings say which setting they change.
- Play time recording no longer stops for the whole session when its storage is
  unavailable at login. The recorder keeps retrying for about an hour and starts as soon
  as the storage is back.
- Game Mode reports an interrupted session it could not fully undo as a failure and
  tries again later, instead of saying it was undone. A damaged recovery record is set
  aside with a note on what to check, so Game Mode can start again. A record from a newer
  Omakade is kept for that version.
- Big libraries stay responsive while covers and metadata load. A change to one game, such
  as a finished cover download, now updates only that game's card instead of checking
  every game, and the in-game guide no longer scans the whole library every second
  while a game runs.
- An unfinished save restore no longer stops every emulator game from launching. Only the
  game it belongs to waits, nothing waits with save protection off (the rollback is still
  tried), and Save Backups now offers Keep Current Saves, which asks first and moves the
  recovery copies to a dated folder.
- A backup can be restored over a library database that is damaged and cannot be read.
  Omakade keeps an exact copy of the damaged file in the recovery folder, and Undo puts
  it back.
- When the play-session recorder blocks a restore, the startup screen offers **Stop
  Recorder and Restore** instead of asking you to run systemctl. Omakade starts the
  recorder again when the restore finishes.
- The package no longer starts `omakade-sessiond` for every account on the computer.
  Omakade enables it for your account the first time it opens, on Omarchy or not, so
  after updating, open Omakade once to keep the recorder and guide starting at login.
  The package says so when it updates from an earlier version. Opening Omakade also
  starts the recorder right away, on Omarchy always and elsewhere while Record Playtime
  or Highlights is on, so play is recorded before the next login. Disabling it with
  `systemctl --user disable omakade-sessiond` keeps it from starting at login, though
  opening Omakade still starts it, and so does the guide button service while that
  service is enabled.
- The bundled in-game guide plugin is licensed GPL-3.0-or-later like the rest of Omakade,
  and its manifest carries the Omakade version instead of 0.1.0.
- Omakade's single-instance socket now lives in your owner-only runtime directory
  instead of the shared temp directory, so another user on the same machine cannot
  claim it first. A window opened before the update still gets the command.
- On a Nintendo controller the in-game guide now selects with A and goes back with B, as
  the library does. It had them the other way round.
- RomM settings show whether the server connection is encrypted. Plain HTTP works
  without asking for local network and loopback addresses; for any other server,
  Omakade asks before it sends the token unencrypted.
- Status pills on library cards are larger and readable on every theme. Their label
  color is picked by contrast against the pill, so Catppuccin Latte and other light
  themes no longer show dark text on a saturated fill. On the smallest covers the label
  shrinks so the pill stays clear of the favorite heart.
- Game titles, achievement names, launch window titles and other library text on Home, Stats
  and elsewhere show as typed. A title holding markup, such as one from a RomM server, is no
  longer drawn as rich text and can no longer make Omakade fetch an image it names.
- Removing a library filter by its chip moves focus back to the Filters button, as Clear All
  does, instead of leaving it nowhere.
- Stats places play by the intervals the recorder keeps, so time a game spent paused no
  longer shows up in the hour, weekday and day charts. Sessions recorded before intervals
  were kept are still spread over their start and end, and the note says how many are
  estimated. Stats and Home also do their work in the background, and Stats no longer
  computes anything while it is closed.

## 1.18.0

Game Pass games, highlight clips and one Game Mode.

### Xodus Nexus

- Microsoft Store and PC Game Pass games installed through
  [Xodus Nexus](https://github.com/domsvoboda/xodus-nexus) appear in the library with
  their covers and banners and launch from Omakade, including from Game Mode. Omakade
  starts them through Nexus's own shortcut and stops them through their Wine prefix.
- Nexus shows nothing while a game starts, so Omakade says the game is starting until its
  window opens, and Game Mode's Starting screen recognizes the game by its prefix.
- The in-game guide opens over Xodus games.
- Play time is recorded for Xodus games and shows on their page and in Recent.
- Xbox achievements show on an Xodus game's page, on its library card and in the in-game
  guide. Nexus's own backend reads them from the Microsoft account it is signed in to;
  Omakade only caches what it reports.

### IGDB

- Critic scores and play time estimates from IGDB show for every game matched to IGDB,
  not only Steam games.
- The in-game guide lists RetroAchievements for RetroArch games, as it already did Steam
  achievements.

### Highlights

Save the last moments of a game. See [Highlights](docs/HIGHLIGHTS.md).

- **Settings → Highlights** keeps a short recording of the game's display in memory while a
  game is in view, with gpu-screen-recorder. It is off until you turn it on.
- **Clip 30 s** in the in-game guide saves a clip that ends where the game was when you
  summoned the guide, and shows the result on the card.
- **Ctrl + Print**, added from Settings, and `omakade --clip` save a clip from the keyboard.
- The **Share** button on Xbox Series controllers and the **Capture** button on Nintendo
  controllers save a clip while the controller Home button is on.
- **Steam achievement clips** save the moment an achievement unlocks, named after it.
- Clips are saved in Videos/Omakade, one folder per game, and listed on each game's page
  as thumbnail cards, where they play and move to the trash.
- The guide's Save N s tile for an outside replay buffer leaves Omakade's own buffer to the
  Clip tile.

### One Game Mode

Couch Mode is gone: fullscreen is always Game Mode.

- **F11, Start and the GAME MODE button** start Game Mode on the display you are using.
  Sound and notifications are left alone, and the screen is kept awake only while a game
  runs, so a handheld can still sleep while you browse. `omakade --couch`, a Moonlight
  stream and a fullscreen start Omakade remembers do the same. If Game Mode cannot start,
  the library opens fullscreen without it and a message says why.
- **The controller Home button** does what Start does when no game is running, so it no
  longer moves Omakade to the Game Mode display. Over a game it opens the in-game guide,
  now also for a game started while Game Mode is parked. Holding it works as before.
- **The guide's status row** says "Xbox controller", "PlayStation controller" and
  "Nintendo controller" instead of "Xbox pad" and the like.
- **Super + Ctrl + G and Start Game Mode** still use the display and sound output chosen
  in Settings. Those choices stay open to change while
  Game Mode started with F11 is running.
- Game Mode opens on Home, and Home and the library share one header. The desktop window
  keeps opening on the library.
- **LEAVE**, Start and F11 end Game Mode at once when no game is running. With a game
  running they open the Game Mode controls, as before.
- `omakade --couch` and the `couch_mode_enabled` setting keep working.
- `omakade --quit` during Game Mode, as a Moonlight stream sends when it ends, and closing
  Omakade's window now put the display, sound and notifications back before Omakade closes.
  Before, the window closed first and the session was only undone at the next start.
- Unplugging or switching off the Game Mode display now ends the session cleanly. Before,
  the desktop came back but the session stayed recorded and was undone at the next start.
- Return to Desktop shows the page the desktop had before Game Mode, and resuming goes
  back to the Game Mode page you left.

### Native ports and Kyty

- Discover PlayStation 5 dumps through Kyty and preserve per-game launcher settings.
- Find installed native ports such as SpartanPort and bbport, grouped with matching
  console dumps as alternate installations. Unlinked pairs stay separate.
- Link game details to known ports you have not installed, including their Linux availability.
- Propose monthly catalog updates through a reviewable pull request.

## 1.17.0

Game Mode keeps launchers out of the way while a game starts.

- **Hold launcher windows**: updates, splash screens, sign-ins and the setup Steam runs
  on a game's first launch no longer take focus from Omakade. A Starting screen lists
  them, and any of them can be opened from there with a controller or the keyboard. When
  the game opens it goes fullscreen and the rest stay out of sight. Publisher launchers that run
  under Proton, such as Rockstar's and EA's, are told apart from the game. Listed windows
  wait as long as a slow update takes. Turn it on in Settings → Controls.
- The Starting screen shows the game's cover and artwork while it starts.
- **Start Steam in the background** when Game Mode starts, so Steam games open without
  waiting for Steam to load. With launcher windows held, Steam's own windows wait on the
  Starting screen too. Turn it on in Settings → Controls.
- Controller buttons now navigate and activate controls directly, avoiding duplicate
  presses and actions reaching the wrong control. Keyboard controls stay the same.
- A game's page keeps every action in view while the game runs. Stop Game no longer
  pushes Manage past the edge in Game Mode or in a narrow window.
- In the Game Mode search, Enter on a keyboard after typing finishes the search, and a
  search with no match leaves you on Search to change it.
- Closing a game started from its page in Game Mode returns to Play, not Back.

## 1.16.2

- The in-game guide works on Omarchy 4.0 stable releases. It needed a shell
  surface that only newer Omarchy builds have, so on stable the guide did not load.

## 1.16.1

- The in-game guide's Record clip action now uses its own recorder. A screen recording you
  start from Omarchy keeps running on its own: the guide no longer shows it as its
  clip or stops it, and Omarchy's stop leaves the guide's clip alone.
- The in-game guide shows GPU load and temperature on NVIDIA cards too.
- Controller navigation is fixed across the app. A press could move two steps, which made
  some buttons and Settings sections hard or impossible to reach. Every screen is now
  tested from every control in every direction, in couch and desktop layouts.
- Home: the top buttons stay on one row, Down moves straight between shelves, and X adds
  a game to Up next. Going back up to the top buttons shows the top of the page.
- Settings: Back from Settings opened on Home returns to Home. Sliders change only with
  Left and Right, dropdowns no longer trap the controller, and Left from a page goes to
  the section list.
- With Steam running, each controller press counted twice, because Steam Input adds a
  copy of the pad. B in Settings on Home went past Home to the library. Omakade now
  ignores Steam's copy while the real pad is connected.
- In Game Mode, DESKTOP on Home goes straight to the desktop, as holding Home does.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.16.1)

## 1.16.0

Press Home or Super + Ctrl + G over a running game to open the in-game guide, a
panel at the edge of the screen in your Omarchy theme. The game pauses while it is
open. Resume, take a screenshot or record a clip, change the volume or sound output,
check performance, or quit the game.

- **Desktop** (in Game Mode) takes you to the desktop with the game hidden and muted.
  Press Home to come back to the game, paused, with the guide open.
- Hold Home for half a second to go to the desktop and back without the guide, as
  Home did in 1.15.
- RetroArch started by Omakade: Home opens only the guide. Click both sticks
  (L3 + R3) for RetroArch's own menu. Your `retroarch.cfg` is left as it was.
- The press that closes the guide never reaches the game.
- Game Mode opens straight onto its own workspace at full size, and the library
  stays behind your game.
- Prefer the old Home button? Set **Home button in games** to **Game Mode** in
  Settings. The guide comes with the package; `omarchy plugin disable
  omakade.guide` turns it off.

Fixes

- Sunshine apps exported by Omakade launch from Moonlight again.
- Heroic libraries with an empty Epic or Amazon cache no longer block GOG imports.
- OmaStore installs record playtime.
- x86_64 packages are built against Omarchy stable, so they start on stable, RC and edge.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.16.0) ·
[Guide documentation](https://github.com/btsouth/omakade/blob/v1.16.0/docs/GAME-GUIDE.md)

## 1.15.0

Press the Xbox, PlayStation or Home button to start Game Mode, return to the desktop
and resume.

- New **Controller Home button** setting in Settings → Controls, off by default. A short
  press on any controller does what Super + Ctrl + G does, from inside a game or with
  Omakade closed.
- It runs as an optional user service, `omakade-guide-button`, that only reads
  controllers. Nothing is grabbed, remapped or emulated, and Steam Input keeps working.
- Long holds, chords with other buttons or triggers, the press that switches a
  controller on and duplicate reports from Steam Input's pad are ignored. Presses while the screen is
  locked are ignored.
- With Steam open, turn off "Guide button focuses Steam" and "Enable Guide Button
  Chords for controllers" in Steam's controller settings.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.15.0) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.15.0/docs/GAME-MODE.md)

## 1.14.2

Game Mode remembers your place. Return to the desktop, then resume your library
or running game with the same shortcut.

- Super + Ctrl + G returns to the desktop and resumes the complete Game Mode session,
  with or without a game. Details, selection, focus and scroll stay in place until
  End Game Mode, Stop Games and Leave, or closing Omakade.
- Running games stay on their workspace with attributable game audio muted while away.
  A game ending releases its game/audio records and keeps the library session available.
- Prepare the Game Mode layout and fullscreen state before showing the window,
  avoiding the windowed-to-fullscreen flash when entering or resuming.
- Keep Resume available during background session checks and prevent late focus
  requests from pulling you away from the restored desktop.
- Restore the desktop even when game audio recovery fails, retaining the recovery
  record for a retry.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.2) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.2/docs/GAME-MODE.md)

## 1.14.1

Fixes the Game Mode shortcut leaving a new Omakade window on your desktop.
When the shortcut launches Omakade, pressing it again leaves Game Mode and closes
Omakade after restoring the desktop. An already-open window stays open in its
previous layout.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.1) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.1/docs/GAME-MODE.md)

## 1.14.0

Game Mode brings Omakade to the couch. Choose a display and sound output, then
start from Settings or `omakade --game-mode`. Omakade opens fullscreen, silences
notifications and keeps the screen awake. Leaving restores the settings it changed.

- Add Super + Ctrl + G from Settings with one click. The same key leaves Game Mode,
  or opens the Game Mode controls over a game that is still running.
- Return Omakade to the exact spot it held in a tiled layout.
- Return to your library, leave games running, or confirm Stop Games and Leave. A game
  left running comes back to your desktop.
- Keep controls responsive with large libraries and when an emulator hangs.
- Recover session settings after an unexpected exit.
- Find setup instructions in the shorter README and new feature guide.
- Sort installed games first, then alphabetically, and dim uninstalled covers in
  desktop and Couch Mode. Other sort choices remain available.
- Import owned Epic, GOG and Amazon games from Heroic, including uninstalled
  titles. Heroic handles installation and launch; refresh its libraries before rescanning.
- Keep ROM filename details accessible when metadata and descriptions are missing.
- Add `layer-shell-qt` as a required dependency for the in-game overlay and
  declare `libpulse` as optional for Game Mode sound switching.
- Show wide box art whole on the details page, name consoles in full, and keep
  setup hints and playtime bookkeeping off the TV.

Game Mode switches desktop audio as a whole. Gamescope, HDR and per-game audio
routing are outside this release.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.0) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.0/docs/GAME-MODE.md)

## 1.13.0

Clearer browsing, library repair and save relocation, with controller and keyboard
navigation checks built into the test suite.

- Rework Home, Library, Details and Settings layouts. Keep actions reachable in
  narrow windows, theme native controls, and use shared spacing and the Omarchy font.
- Make Couch navigation follow the visible rows. Up and Down reach adjacent toolbar
  rows, including wrapped filters, and return between games and their entry control.
  Search, Filters and Details restore focus when closed. Tab and Shift+Tab follow
  the visible controls and games.
- Give keyboard and controller input usable focus on a cold launch, including
  empty and delayed libraries. Opening Omakade no longer needs a mouse click first.
  Clear Filters also clears search stored in the library model.
- Add five navigation contracts covering 4,320 directional links and 16 cold-start
  cases to normal CI. Check keyboard, D-pad and analog input through production
  controller polling, both Couch layouts, empty results and wrapped rows.
- Match Nintendo controller actions to their button prompts. Show Stop Game only
  for attributable running targets and repair focus when actions change.
- Improve Stats Overview, Play patterns and Library snapshot, with clearer labels
  for recorded periods and imported lifetime totals.
- Clarify repair, artwork, launch setup and save actions. Evaluate library review
  availability in the background and keep review and undo controls navigable.
  Recheck superseded background work, group disconnected folders correctly, and
  keep relocation dialog focus inside its visible controls. Removing a search
  chip clears Couch searches too; relocation accepts real filenames containing `#`.
- Copy verified save backups when relocating a game's path. Preserve the original
  backups and copies on undo, reuse existing verified copies on repeated repairs,
  and refuse destination conflicts, incomplete recovery or unverified save mappings.
- Fix Home shelf scrolling and a cached-artwork geometry binding loop.

## 1.12.0

Play history and local statistics, safer game stopping, durable recording, and new Nintendo DS,
PlayStation 3, and PSP sources.

### Play history and statistics

- Open Stats for recorded play, top games, hourly patterns, streaks, achievements,
  and backlog habits. Export a local PNG card with the recording period clearly labelled.
  Imported lifetime totals stay separate. Desktop and Couch Mode are supported.
- See running sessions on Home, browse older history, and delete finished sessions
  with confirmation. Deletion preserves imported playtime and later recorded play.
- Stop one game or all attributable games. Previews explain shared Wine/Flatpak scopes;
  idle installations and documents open in editors are excluded. Recognize Steam Proton
  games whose process paths use Steam's container drive, and stop their verified prefix
  processes when no system wineserver is installed. Wait briefly for a forced process to
  exit before reporting the result.
- Record supported file-picker loads on Hyprland. Optionally pause recording when the
  emulator loses focus, or show the current game through Discord Rich Presence.
- Fix imported/recorded playtime overlap, recovery timestamps, stale title matching,
  Discord reconnects, and partial history deletion on storage failure.
- Keep Stats totals and charts consistent across midnight, New Year and DST. Paused
  historical sessions use labelled timing estimates. Storage errors prevent card export.
- Remember the selected Stats period, count linked installations consistently, and fit
  longer notes inside the exported card. Headless demo exports require an explicit fixture flag.

### Recording reliability and emulator support

- Make refused session writes durable. Sessions get a stable identity before their first
  write, and an insert, close or active-session checkpoint the database refuses is journaled
  to a bounded file beside the database and replayed, both after a recorder restart and
  during ordinary polling. Replay is idempotent, so a crash between the database commit and
  the journal acknowledgment cannot double a session. A torn final append keeps the verified
  prefix, and a partial header is repaired rather than swallowing the next record. A replayed
  checkpoint keeps its observation time as the heartbeat and never lowers recorded progress.
  A game-wide clear, a single-session deletion and a database restore each invalidate only the
  records they should, checked in the same transaction as the write, and a temporary database
  read failure retries instead of discarding. An unresolved identity remains retryable under
  a durable journal-owner binding, while Replace restore invalidates and removes prior
  journal work. A recovered session whose game has exited or changed is closed at the last
  observation, and a surviving same-game process is adopted under one stable key. A failed
  final close remains retryable, and checkpoint replacement reports write, flush, sync,
  rename and directory-persistence failures instead of a false acknowledgement. Repeated
  checkpoints replace rather than fill the journal, and a full or damaged journal is shown in
  recorder status instead of dropping accepted records. This replaces the earlier in-memory
  retry limitation.

- Preserve artwork when a Steam app has no published capsule. Metadata keys with their embedded
  source/runner/app-id separators are now decoded losslessly when the library reopens, and a
  Steam preload whose capsule request fails falls back to its identified IGDB cover. A real
  Steam capsule still takes priority whenever one exists.

- Attribute a game Dolphin loaded from its own file picker. Dolphin rewrites a playtime file
  while emulation runs, keyed by disc id, so it is read for a live Dolphin process and a game
  whose total has advanced is recorded against the right game. A total that has not advanced
  attributes nothing, a disc id the library does not know or one that matches two games is
  refused rather than guessed at, and two games advancing between two polls is refused for that
  poll. A record that stops advancing keeps the game it last confirmed instead of ending the
  session, which is what a paused game looks like. A session first attributed from Dolphin's
  window title ends when the record takes over, so the same play is never billed twice. Evidence
  from a command line still wins over the record, and stop actions still use verified command
  line evidence only. No other supported emulator writes a record that proves the loaded game,
  so their file-picker loads keep the window-title path.

- Add native melonDS discovery, launching, recording, title-index and save protection for
  Nintendo DS and DSi dumps. Omakade scans the ROM folders the user marked as DS, reads the
  same game-code and header fields melonDS reads, groups the games under a Nintendo DS console
  entry, and launches one ROM as a structured argument. The default battery save beside the ROM
  and a `SaveFilePath` override from `melonDS.toml` are protected as a shared ROM-folder save
  set; save states and relocated `<state>.sav` files are not treated as in-game saves. The
  native source was accepted with the MIT-licensed DS-Craft beta 1.7.1 homebrew release under
  melonDS 1.1. The Flatpak route launched the same homebrew and closed one 14-second session.
  melonDS still has no current-session evidence for a game loaded inside its own picker.

- Add RPCS3 discovery, PS3 grouping, launching, recording profile, title-index support and
  PARAM.SFO-aware per-title save resolution. Omakade reads `games.yml`, installed
  `dev_hdd0/game` directories, the `vfs.yml` auto-detection folder and explicit PS3 ROM
  folders, validates categories rather than importing updates or save data, and uses the
  verified `rpcs3 --no-gui <path>`/Flatpak argv. The save resolver reads each candidate save
  directory's `PARAM.SFO` instead of trusting a title-id prefix. Native execution was accepted
  after installing supplied PS3 firmware 4.93 in the isolated RPCS3 root: the GPL-2.0 iPSX3
  homebrew booted as `IPSX30001` and closed one 79-second session.

- Add PPSSPP discovery, PSP grouping, launching, recording and save protection. Omakade reads
  PARAM.SFO from ISO and PBP images, derives `DISC_ID`, `DISC_VERSION` and region, accepts
  homebrew ELF entries with a stable path identity, and scans explicit PSP folders plus the
  bounded Recent and PinnedPaths entries in `ppsspp.ini`. The native source was accepted with
  the MIT-licensed 2048PSP release under PPSSPP 1.20.4 and closed one 59-second session over a
  60-second wall span. A real-title run with `God of War: Ghost of Sparta` (`NPUG80508`) under
  PPSSPP 1.20.4 closed one 79-second attributed session. Save protection selects only
  `PSP/SAVEDATA` folders prefixed by the disc ID, keeps the shared-container warning, and never
  treats `PPSSPP_STATE` files as game saves.

- Read PCSX2's live `logs/emulog.txt` to identify the serial of the game actually loaded, including
  titles started inside PCSX2's own file picker. The adapter tracks appended data, handles log
  truncation and disc changes, keeps the last confirmed game while the log is quiet, and refuses
  an unknown serial instead of falling back to the previous game. A real `Black (USA)` run
  identified `SLUS-21376` and closed one 75-second attributed session. Recorder Settings now
  states which sources have verified live-game evidence and which remain command-line/title only.

- Persist observed activity intervals for each session, including pause spans, through the
  database and the durable recovery journal. Existing sessions retain aggregate provenance; new
  interval-backed sessions are marked observed. The existing proportional Stats allocator is
  intentionally unchanged in 1.12.

## 1.10.0

Xenia (Xbox 360) support and a TV setup for the couch.

- Xbox 360 games from Xenia Canary: discovery from Xenia's recent-games list and storage
  root, save backup, playtime recording, and launching through the `xenia_canary` binary.
  Thanks to @Salt-555 for the Xenia source work.
- Xbox 360 titles identified with IGDB and shown under their own console card, including
  names Xenia writes with trademark marks.
- Xenia pinned to X11 on Wayland, where its window otherwise stayed grey while audio kept
  playing.
- An optional TV gaming agent skill with a Gamescope launcher that checks the TV output,
  workspace, and audio sink before starting a game. It stays inert unless you install it.
  Thanks to @LucasOl1337 for the guide and helper.

## 1.9.2

- Add an optional ProtonDB tier badge on library cards, off by default. Turn on both
  community reports and card badges in Settings to see the tier beside playtime and
  rating. Fixes #39.

## 1.9.1

This patch fixes RomM catalog refresh for large libraries.

- Ask RomM to omit the result-set index and filter data it repeats on every page
  by default, which is what pushed large catalogs past the response limit.
- Raise the whole-refresh total while keeping each page bounded in memory, so
  libraries with tens of thousands of entries finish loading. A failed refresh
  still keeps the existing offline catalog.

Fixes #46.

## 1.9.0

- Connect an optional read-only RomM library with secure credentials, locally mounted
  games, and a catalog that remains available offline.
- Save per-installation emulator/core choices, inspect launch diagnostics, and repair
  missing paths. Keep explicit choices consistent across launch entry points.
- Protect supported emulator saves before launch, including memory cards and clock data.
  Restore and undo with shared-storage warnings and interrupted-restore recovery.
- Review save coverage, configure custom file layouts and retention, and preview backup
  cleanup. Refresh the selected backup list after automatic capture.
- Work through a persistent library repair queue with source/reason filters, selected
  retries, and separate identity and artwork undo.
- Improve ROM identification and artwork recovery while preserving manual choices,
  editions, and edits in searches. Prevent cached cover loading loops.
- Keep archive entries on RetroArch and report missing archives, cores, or runtimes
  without silently replacing a selected setup.
- Add optional ProtonDB community reports in Steam game details, with report counts
  and cache dates. Keep library captions uncluttered and launching independent of the service.
- Recognize Cemu `.wua` games in session recording.
- Clarify empty library views, source errors, and repository versus direct package installs.

## 1.8.0

- Add optional Home, persistent Up Next, and suggestions from the local library.
  Improve Home wheel scrolling during background metadata updates.
  Put game shelves before shortcuts and add direct Play beside Details.
- Filter by genre, decade, and platform, including saved filters.
- Show regional release dates, title evidence, genres, credits, and descriptions.
  Preserve manual identity choices and leave ambiguous matches correctable.
- Bring matching and cover selection together under Game & Artwork. Preserve
  existing portraits during refresh and cache maintenance, and recover covers
  through verified aliases. Keep Done visible while the panel scrolls.
- Prevent overlapping desktop library captions after returning from a game.
  Keep existing cards stable during unchanged startup console scans.
- Improve popup keyboard navigation, controller focus, and narrow details layouts.
  Restore the original Home action after closing details. Show immediate launch
  feedback, suppress repeated presses briefly, and keep launch errors visible.
  Limit the rating-count tooltip to the rating and put credits before regional details.
- Add optional local session recording for configured emulator process profiles.
  Show recorder status and separate imported time from recorded time. New installs
  opt in; existing recording preferences and history are preserved.
  Attribution requires a recognizable game path in process arguments. Internal
  emulator game changes and wrapper handoffs still need adapter-specific testing.
- Back up explicit metadata choices, recorded sessions, baselines, and preferences
  in archive format 2. Format 1 remains readable. Emulator saves are excluded.
- Report persistence failures and protect referenced artwork during cache cleanup.
- Keep ROM Folders on the Sources overview. Fixes #40.

## 1.7.0

Omakade 1.7 brings console libraries, more ways to organize your games, and a
reworked controller keyboard.

### Console libraries

- Discover Dolphin, Cemu, and shadPS4 games alongside existing sources. Launch
  Dreamcast games through Flycast and scan ROM folders and EmuDeck layouts.
- Browse console cards or individual games, pin games outside their console
  card, and choose a layout for each system.
- Read titles and icons from supported Switch dumps using locally installed
  keys, and artwork from Wii U archives. Filter out Switch updates and DLC.

### Artwork and settings

- Identify games with IGDB, display ratings, and sort by rating or popularity.
  Matching handles common region, revision, and translation tags in ROM names.
- Add optional SteamGridDB portrait covers, or choose your own cover, hero,
  and logo images. Each artwork slot can be reset separately.
- Browse settings by Sources, Library, Connections, Controls & streaming, and
  About & storage. Adjust desktop and couch cover sizes independently.

### Organization and backups

- Add native games and desktop entries with custom arguments and a working
  directory. Removing an entry leaves its game files alone.
- Choose a preferred installation for linked games and add extra GOG folders.
- Bulk-edit games, save named filters, and pick a random game from your results.
- Export personal library settings and artwork to a local backup. Preview,
  merge, or replace data, with recovery and undo for interrupted restores.
- Preserve console pins and favorite/hidden choices for the new emulator sources
  in backups.

### Controller and reliability fixes

- Use a QWERTY on-screen keyboard with A to select, B to cancel, X to delete,
  Y for space, and Start to finish. Confirm stays on the bottom face button,
  including controllers connected in Switch mode.
- Reach search and text fields with a controller, keep keyboard input inside
  its dialog, and return to the field after editing.
- Keep focus on Show, Source, and View while changing them. Source cycles through
  choices; Filters opens the full filter panel.
- Fix details-page navigation back to the title, status alignment, clipped
  collection controls, and narrow-window editor and settings layouts.
- Fix disappearing filtered libraries and preserve cached Dolphin and Cemu games
  when their configuration cannot be read.
- Track manually added games through launch and exit. Request Wayland idle
  inhibition while a tracked game process is running.
- Handle unavailable crypto when reading Switch artwork.

### Upgrading

Existing IGDB credentials carry over. SteamGridDB requires its own optional API
key in Settings → Connections. Use Update Ratings & Portraits to fetch metadata.

Backups containing the new console-pin data may not open in older Omakade
versions. Keep an older backup if you plan to downgrade.

## 1.6.1

- Launch, manage, and install Steam games through the Steam client itself,
  native first and then Flatpak, and only fall back to the desktop `steam://`
  URL handler when neither is available. Steam packages that register no
  handler sent Play to the web browser. Thanks @radiohost-cloud for the report
  and the Apple Silicon test.
- Stop matching the Omakade desktop entry when searching for "Steam" or
  "RetroArch" in the app launcher. Thanks @gmickel for the report.
- Remember the library sort order between launches.
- Show every game's cover at the same compact size on the details screen
  instead of letting portrait covers render larger than landscape ones.
- Share QML role-name definitions across nine game models without changing
  their role IDs, names, or behavior.

## 1.6.0

### Couch Mode

- Browse your library in detail or grid view with controller navigation,
  search, filters, and an on-screen keyboard.
- Open Couch Mode with F11, controller Start, or `omakade --couch`. Sunshine
  sessions open it automatically, and you can make it your startup view.
- Hold the stick or directional pad to move through games. Selection stays on
  the same game when switching layouts.
- Use a controller throughout Settings and game organization, including text
  entry, case, and symbols.
- Let games keep controller focus after launching; ignore controller input while
  Omakade is in the background.
- Hide the cursor during keyboard and controller navigation and restore it
  when the mouse moves.

### GOG and compatibility

- Discover and launch direct GOG installations. Native Linux games launch
  directly; Windows games use UMU with a separate prefix for each game.
- Keep Heroic-managed GOG games launching through Heroic with their existing
  settings.
- Remove uninstalled direct GOG games on rescan and preserve cached entries when
  a Heroic GOG inventory cannot be read.
- Fix Proton detection and launching for Omarchy Battle.net prefixes. Thanks
  @TheAirick for the report.
- Add aarch64 packages alongside x86_64, with checksums and provenance.

## 1.5.0

### RetroAchievements

- Added optional RetroAchievements support for RetroArch games, including
  compatible ROM hashing, achievement progress, unlock details, rarity, and
  account-aware caching.
- Kept network, hashing, and database work off the interface thread and handled
  sign-out, stale data, unsupported systems, and malformed responses safely.

### Battle.net

- Added Battle.net as a library source. Omakade finds the Windows Battle.net
  client in Wine, Proton, and Bottles prefixes, imports installed games from
  `product.db`, and launches them through Battle.net.
- Downloads missing Battle.net covers and banners from Lutris's public artwork
  hosts, including Heroes of the Storm.

### PCSX2 and Ryujinx

- Added PCSX2 as a game source: imports disc-based games from the current
  gamelist cache (v34) for native and Flatpak installs, with cover
  art, playtime, last-played, and region metadata, and delegated launching
  through the owning PCSX2 install. Sources are discovered automatically and
  appear once the emulator is detected.
- Added Ryujinx as a game source: discovers XCI, NSP, and NRO games from the
  configured game directories for native and Flatpak installs, with custom
  titles, playtime, and last-played metadata, and delegated launching.
- Added per-source filter chips, status rows, and rescan controls for both
  emulators in Settings.

### Steam

- Imported non-Steam shortcuts from `shortcuts.vdf`, including Wine/Proton
  games added to Steam, and launched them with the 64-bit shortcut ID Steam
  expects.
- Kept cached shortcuts available when `shortcuts.vdf` is temporarily
  unreadable.

### Interface and reliability

- Improved game-details layouts across narrow, standard, and ultrawide windows,
  including cover sizing, action widths, and the insights grid.
- Fixed keyboard and controller movement between Play, Favorite, Manage, and
  Hide in both two-column and four-column layouts.
- Preserved cached launcher games when an optional source is unavailable and
  expanded automated coverage for the new integrations and navigation paths.
- Updated project, support, download, and package links after the repository
  account rename.

Thanks to @karem505 for PCSX2 and Ryujinx, @HowieDuhzit for
RetroAchievements, @Nitemaeric for Battle.net, @Aweiward for Steam non-Steam
shortcuts, and @jeanmrx1 for the responsive game-details improvements.

## 1.4.0

### Sunshine and Moonlight

- Added optional Sunshine app export for Omakade and individual installed games, including
  cover art, while preserving existing Sunshine apps and keeping a one-time backup.
- Added a Restart Sunshine action in Settings.
- Added `omakade --play Source:runner:id` and `omakade --quit` for Sunshine app entries and
  other integrations.
- Used the installed Omakade executable for native Sunshine entries and waited for a fresh
  library scan when a game starts before the cache is ready.
- Opened Omakade fullscreen on Sunshine's streamed display for Moonlight sessions and used
  each game's normal launcher.

### Library and organization

- Replaced the Status, Collection, and Tag filter cycles with picker lists that open on the
  current value and work with keyboard, mouse, and controller.
- Kept the grid and details on the correct game during unchanged Steam rescans, filtered
  edits, and cover changes.
- Preserved unfinished text in tags and credential fields when background refreshes finish.
- Reported private Steam profiles correctly and showed scanning state for every library
  source.

### Navigation and interface

- Made keyboard arrows use the same spatial navigation as controllers in game details,
  Settings, and dialogs, without taking arrow keys from text fields.
- Moved focus to Clear Filters when the last visible game leaves a filtered grid and returned
  focus to the collection button when its editor closes.
- Kept focus in place when the window is reactivated and added accessible names to text fields.
- Rendered titles as plain text, kept toasts inside the window, shortened long card subtitles,
  and avoided unnecessary cover reloads while resizing.

### Performance and reliability

- Sped up linked-game searches, Steam artwork scans, controller detection, and theme updates.
- Pruned unused covers first, retried covers removed by the cache limit, and remembered IGDB
  misses instead of requesting them repeatedly.
- Delayed keyring access until credentials are configured and reduced unnecessary database
  and cover work during unchanged scans.
- Kept running when the single-instance socket is unavailable and restored normal SIGTERM,
  logout, and service-stop behavior.

## 1.3.0

### Steam

- Added optional owned Steam library sync, installed and ready-to-install
  filters, and Steam installation handoff.
- Loads owned-game covers as they enter the visible library instead of fetching
  an entire account at once.
- Kept the Steam library when a configured library path is missing or a manifest is
  unreadable instead of showing an empty or frozen library.
- Skipped unusable entries and Steam tools during owned-library sync instead of failing
  the whole sync.
- Remembered games without Steam achievements instead of re-requesting them on every
  visit, and reported that state plainly.
- Required a 17-digit Steam ID, reported when Steam is still busy, and stopped
  re-requesting covers Steam does not have.

### Heroic, RetroArch, and Lutris

- Added games sideloaded into Heroic, plus Heroic playtime and last-played activity.
- Resolved the RetroArch Flatpak's sandbox paths so its playlists, thumbnails, and
  playtime logs are found, and matched playtime logs by the core's short name and
  archived content name.
- Ignored a leftover Lutris database whose native or Flatpak launcher is no longer
  installed, and checked Flatpak launchers without blocking the interface.

### Navigation and library

- Added controller navigation across library modes, source filters,
  organization controls, Settings, and game details.
- Moved keyboard and controller Up from the top row of games into the filters and
  toolbar, with arrow keys between those controls and Down back into the grid.
- Kept detail-page controller movement in content order below collections.
- Added controller and keyboard scrolling through Steam achievement cards.
- Kept the highlighted card and the open game details on the same game when a
  background rescan rebuilds the library.
- Named the search or organization filter behind an empty library and offered a Clear
  Filters action, and made the Collection and Tag filters say how to create the first
  one instead of doing nothing.
- Added a visible whole-library Rescan action.
- Made Escape close the new-collection editor before closing game details.
- Made Return, Enter, and the controller confirm button press every button on desktops
  whose Qt platform theme does not map them.
- Clarified that automatic closing after launch is an opt-in setting.

### Fixes and housekeeping

- Fixed narrow game-details layouts and prerelease owned-library cache upgrades.
- Reused the IGDB access token within a session and declared the Qt SVG and image
  format plugins the package needs.
- Prevented space-separated screenshot options from creating an invisible main instance.
- Pinned third-party workflow actions and enabled monthly dependency updates.
- Added end-to-end navigation coverage and owned-library regression tests.

Thanks to @destx0 for the stale Steam library fix, and to @8uff3r, @bscott, and
@Zedster07 for the reports that shaped this release.

## 1.2.3

- Restored controller navigation on game details.
- Kept controller focus inside the game grid at the top library row.
- Added an end-to-end controller navigation test for the library and details.

## 1.2.2

- Fixed controller Up and Down navigation in the game library.
- Made controller focus movement follow the actual screen direction on details
  and overlay screens.
- Fixed absent launchers reporting database errors in Settings.
- Reflowed Settings actions so they remain visible in tiled windows.

## 1.2.1

- Improved keyboard and controller navigation across game details, settings,
  filters, and dialogs.
- Preserved favorites and hidden state when launcher games disappear and return.
- Moved Lutris, Heroic, and Faugus scans off the interface thread.
- Fixed stale Steam achievement rows and controller repeat after disconnecting.
- Added stricter limits for artwork, achievement caches, and Steam metadata parsing.

## 1.2.0

- Added native and Flatpak RetroArch playlist discovery and launch delegation.
- Added RetroArch box art, screenshots, core names, playtime, and recent activity.
- Added safe handling for archived ROM paths and missing core associations.
- Added RetroArch source filters, diagnostics, and settings.

## 1.1.1

- Refreshes stale Steam achievements automatically when game details open.
- Hides the new collection field behind a compact action.
- Moves achievement sorting into the Achievements header.
- Uses the selected Steam installation for linked-game achievements and insights.

## 1.1.0

- Added native and Flatpak Faugus library discovery, artwork, playtime, filters, and settings.
- Added safe launch delegation and management handoff to Faugus.
- Preferred high-resolution Steam hero artwork over small store headers.
- Fixed missing achievement icons from Steam's legacy image CDN.
- Added achievement sorting by status or unlock date.
- Increased smooth mouse-wheel travel and added proportional touchpad scrolling.
- Added project and issue links to Settings.

## 1.0.2

- Fixed Steam Web API keys being rejected by the wrong API host.
- Added a visible Settings button to the library header.
- Made mouse-wheel library scrolling smooth and row-based.

## 1.0.1

- Added a distinct Omakade launcher icon and matching in-app brand mark.
- Made the library scrollbar larger and easier to drag with a mouse.
- Added a persistent accent outline to the selected game card.
- Improved the new-collection layout and clarified Twitch setup for IGDB.
- Hid unavailable game-insight metrics and reflowed the remaining cards.
- Reserved scroll gutters so tiled layouts never place content under a scrollbar.

## 1.0.0

- Added persistent launch activity across Steam, Lutris, and Heroic.
- Made Recently Played sort by exact activity time instead of a yes or no flag.
- Added optional IGDB critic aggregates and game-length estimates with an
  offline cache and Secret Service credential storage.
- Added custom collections, tags, completion states, card badges, and smart
  organization filters.
- Added runtime source controls with persisted scan times, detected locations,
  and per-source errors.
- Added stale-install checks, Flatpak launcher verification, actionable launch
  errors, and ProtonDB and PCGamingWiki links.
- Added an opt-in close-after-launch setting and visible version diagnostics.

## 0.5.0

- Added Steam, Lutris, and Heroic libraries in one view.
- Added explicit, reversible linking for duplicate installations.
- Added a source selector so every linked installation remains launchable.
- Added user-selected cover artwork with reset support.
- Made window transparency follow the active Omarchy shell theme.
- Changed game cards to open with one click.

## 0.4.0

- Added Heroic support for installed Epic, GOG, and Amazon games.
- Added local Heroic artwork and native or Flatpak launch delegation.

## 0.3.0

- Added Lutris installed-game import and launch delegation.

## 0.2.0

- Added local and connected Steam achievements.

## 0.1.0

- Added the first Steam library preview.
