# CRO-U v0.4.5

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux and Android.

## What's new

- **FSR 3.1.6:** the built-in FSR is updated from 3.1.4 to AMD's FSR SDK 2.4 passes.
- **FSR 4 (Linux and Windows, AMD GPUs):** a new Upscaler choice. AMD ships FSR 4 for Vulkan only as
  its own Windows DLLs, so they aren't included: put AMD's two SDK DLLs, the driver's
  `amdvkffx64.dll` and (on Linux) ffx-vk-linux's `libffx_vk_linux.so` into `fsr4/` in the game
  folder - its `README.txt` says where each comes from. On Linux the DLLs run natively through
  ffx-vk-linux, no Wine. Without the files FSR 3.1 stays as it is.
- **Vita music:** Omega's own tracks - the title theme among them - play from a Vita copy again.
  The Vita names them `cr2_m41.at9`..`cr2_m49.at9`, which the game didn't look for, so with a
  Vita copy alone the title was silent.
- **Cutscenes closer to the original** (the opening checked opcode by opcode against the iOS code):
  message windows fade in over 4 frames before their text and out over 5 after it, with the
  original's open / close sounds, and a script's `Open` / `Close` / fade waits / swap-ins take the
  frames the original spends on them (about 350 ms per window - Vieg's bow and "Now, now, Ayuta..."
  no longer come early); line breaks and page turns type at the original's pace; the "next" mark no
  longer blinks and the page counter is gone (the original has neither); `Anim2` starts its action
  over from the frame the script names (the sailor's salute and its ending); portraits sit flush to
  the screen's edge, the "Battleship Napur" strip is centred and the movement tutorial's pad art is
  the original's pieces; a hidden-field `Screen` is no longer mistaken for a 255-frame fade; a
  script's warp within its area no longer runs the room's entry script a second time; a script
  waiting for a tap with `Rept` isn't cut off after 45 s. On the deck and in the first dungeon:
  `FrmAdjc2` turns an actor to its record's facing (at once, or over the move) as the original
  does; a `FrmPlayEff` effect faces the camera on the field too; a camera slide, a move, a scroll
  and a quake take their first step on the frame the script starts them; the original's one-pixel
  tremors show (the view's corner is rounded after the shake is added); `CameraPlayer` leaves a
  room without a painting on its last shot, as the original's camera does.
- **Battles started by a scene:** Escape is refused while a scene runs (the original dims it),
  the battle's music is the one the scene names - random battles play Omega's battle theme
  (track 31) instead of a track the iPad data doesn't have -, the music after a battle follows
  the scene's var 60 (silence after the deck's and the first dungeon's fights, as the original),
  the deck's first-battle tutorial picture (`battle_start`) shows, the scene's own "skip off"
  (`Chg_MsgSkip`) now hides and disables the skip corner, the fade-in is the original's 20
  frames, a tutorial's dimming fades over 100 ms, and a lost scene battle shows `gameover` and
  waits for a tap before the title.
- **Resuming:** leaving the game during a cutscene no longer makes the title's "resume from last
  saved point?" put you on that cutscene's painting with nothing running. The resume point is only
  what a scene's end (or an encounter) saved, as the original's is - quitting itself saves nothing.
- **Story battles you're meant to lose** (650, 654, 674 ...) now go on with the story instead of
  ending in a game over (reported by [metastigma](https://github.com/metastigma)).
- **Skip:** the skip button skips while you hold it and stops when you let go, as in the original.
- **Animation:** every change of motion blends over two frames, as the original does. The warp
  crystals turn on their own again, and a few models (c151, j009, m009, m122 and others) get
  motions that were missing.
- **Battles:** attacks use the original's animation choice, so monsters like m225 no longer stand
  idle through their attack; attackers only step in when their animation moves them; empty
  animation slots pass to the next one.
- **Fixes contributed by [metastigma](https://github.com/metastigma):** the player stops walking when a scene starts; an item window
  no longer answers the question before it; characters riding another model stay attached;
  turning HD Textures on in settings.ini updates the menus.
- **Windows** (hotfix, same version): the first-start setup can copy and unpack your game from a
  folder whose name uses another alphabet - a Cyrillic user name on a Russian Windows, for example.

Thanks to [metastigma](https://github.com/metastigma) for the report, those fixes and testing. All of it was checked against the original iPad code.

## Downloads

| File | Platform |
|---|---|
| `CRO-U-windows-x64.zip` | Windows 10+ x64 |
| `CRO-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CRO-U-x86_64.AppImage` / `CRO-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CRO-U-android.apk` | Android 7+ |

## Getting started

1. Download the file for your system and unzip it.
2. Start `chaosrings`. If it can't find your copy of the game, it asks you for one and sets it up.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `chaosrings.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK, then copy your game files over USB into
  `Android/data/local.preservation.chaosringsomega/files/`, in an `ios/`, `android/` or `vita/` folder.

The README's "Your copy" section lists exactly which files each release needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it. Every
  earlier version was signed with a different key, so coming from one, uninstall it first - one
  last time. That deletes the game files you copied in and your saves: keep a copy of the game
  files to copy back.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
