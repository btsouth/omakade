# Support

Start with [Troubleshooting](docs/TROUBLESHOOTING.md). It covers missing games,
filters, sources that are not found, where each launcher is read from, ROM folders,
RetroArch cores, Battle.net prefixes, covers, Repair Library, and how to collect
logs.

If that does not fix it, open a
[GitHub issue](https://github.com/btsouth/omakade/issues/new/choose) with these
details:

- Omakade version
- Omarchy version and active theme
- The launcher or emulator version and whether it is native or Flatpak
- For Battle.net, whether it runs under Wine, Proton, or Bottles
- For Xodus Nexus games, the Nexus launcher and backend versions from its Settings
- The source shown for the affected game
- `umu-launcher` version for a direct Windows GOG game
- Steps that reproduce the problem

`Ctrl+D` opens Settings. Under **Sources**, each source's **Details** shows its
status, the folders it found and any warnings; include those. **About & help** shows
the version and database path. Do not post a Steam or RetroAchievements Web API key,
full configuration directory, or private account data.

Omakade can ask Steam to begin installing an owned game. Steam remains
responsible for installation, updates, moves, and removal. Problems with those
actions belong to the launcher that owns the game.

## Manual entries and restores

For a manual game, include whether it was added as an executable or desktop
entry and the error shown. Review paths and arguments before sharing them.
Windows runner setup belongs to the launcher that owns the game.

If a restore says to stop the play-session recorder, close Omakade, run
`systemctl --user stop omakade-sessiond`, open Omakade to apply the restore, then
start the recorder again with `systemctl --user start omakade-sessiond` (on Omarchy,
opening Omakade starts it). Closing Omakade or turning recording off does not stop
the recorder. See [Back up your library](docs/GUIDE.md#back-up-your-library).

For other restore failures, include the Omakade version, merge or replace choice,
and the error shown. Keep the recovery folder while diagnosing the problem.
Do not attach the archive, database, settings file, or recovery folder to a
public report; they contain personal library data and may contain private paths
or account identifiers. The startup recovery screen offers retry or undo before
the library opens. A completed restore can be reversed by previewing its saved
recovery archive as a new restore, which also previews changes made since then.
