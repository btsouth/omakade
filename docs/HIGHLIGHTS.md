# Highlights

Highlights keep the last moments of a game ready to save. While a game runs, Omakade
keeps a short recording of the display it is on, in memory, and writes a clip only when you
ask for one or unlock a Steam achievement. Nothing is kept until then.

Turn it on in **Settings → Highlights**. It needs
[gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/about/), which Omarchy
installs for its own screen recording, and Omakade's background service
(`omakade-sessiond`), which Omakade installs and starts.

## Save a clip

- **The in-game guide.** Press the controller Home button or Super + Ctrl + G over a game
  and choose **Clip 30 s**. The clip ends where the game was when you summoned the guide,
  so the card is never in it. The tile shows **Saving…** and then **Saved**.
- **The keyboard.** Settings → Highlights → **Add Ctrl + Print** writes one line to
  `~/.config/hypr/bindings.lua` that runs `omakade --clip`. If the key is already used for
  something else, Omakade says what and adds nothing; bind another key to `omakade --clip`
  yourself.
- **A controller's capture button.** The Share button on Xbox Series controllers and the
  Capture button on Nintendo controllers save a clip. This uses the controller Home button
  service, so turn on **Controller Home button** in Settings → Controls. PlayStation's Create
  button is a game button, so on those controllers use the guide.
- **Steam achievements.** With **Steam achievement clips** on, an achievement unlocked
  while you play saves a clip that ends a few seconds after the unlock, named after the
  achievement. Unlocks a few seconds apart share one clip. An unlock while the game is out
  of sight, such as one for time played while you use another app, saves nothing. Omakade reads the unlock from the
  Steam client's own stats files; an unlock Steam only writes later, such as after a sync,
  is never clipped, because the clip would show the wrong moment.

`omakade --clip` prints why when a clip cannot be saved, and a notification says the same.

## Where clips go

Clips are MP4 (H.264 and AAC) files in your Videos folder, in `Omakade/<game title>/`,
named after the game and the time. Each game's page lists its clips with a still, newest
first: **Play** opens the clip in your video player, and **Trash** moves it to the trash
after a second press. Clips you move or delete outside Omakade simply stop being listed.
A recording the service could not finish filing, such as when it stopped mid-save, is kept
in `Omakade/Recovered/` the next time it starts.

The clip length is 15, 30, 60, 90 or 120 seconds. The recording in memory is a little
longer than a clip, at least two minutes.

## What is recorded

The display the game is on, at 60 frames per second without the pointer, and the sound
your computer plays. Anything shown over the game on that display is in the recording, and
so is anything you switch to on it while the game keeps running. Recording is done on the
graphics card's video encoder, so a game loses very little to it.

The recording starts when Omakade sees the game on screen, follows it to another display,
and keeps running while the game does, even when you switch to something else on that
display. It stops when the game ends or highlights are turned off. If the recorder fails, Omakade
tries twice more and then says why in a notification, and the guide does not offer Clip.
