# CRO-U v0.5.0

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux and Android.

This is a big one: over a hundred changes since v0.4.5. The game was re-read against the newest
iPad release (3.1.0), and much of what's on screen was checked against a full recording of the
original.

## What's new

### Cutscenes and story scenes

- **The opening's "Hmm!?"** shows the original's static instead of flickering with the space
  painting tiled across the screen.
- **The sea crossing:** the ship's wake follows the ship again instead of running ahead of and
  above it. Effects in field scenes keep their own angles; only battle effects turn to the camera.
- **The ship's side:** the view reaches the painting's edges on phone and Vita screens, and the
  player is no longer hidden behind the waves.
- **Timing as the original:** message windows fade in and out with their sounds, scripted pauses,
  camera moves and quakes take the original's frames, and lines arrive when they should.
- **Messages typed as in 3.1.0:** a tap brings out the text up to the next pause instead of the
  whole page, lines stay up when the text goes on below them, and 75 voiced lines per language got
  their voice back.
- **Scripts across every area** checked against the original: characters turn and animate as
  they should (Olgar's axe no longer stays raised in the Ark arrival), the first dungeon's fire
  walls burn, story actors wait hidden until a scene brings them on, and the Ark's welcome-back
  scene plays after every return.
- **The message window** on phone layouts: the parchment reaches the screen's edges, with the
  original's "next" arrow and skip button.

### Battles

- **The opening as the original's:** a moment of white, the formation camera's turn under the fade,
  the commands arriving on the original's frame; ambushes and first strikes are announced again.
- **Presentation:** soft floor shadows under every fighter, gliding and tilting cameras, attackers
  stepping to their spots, the original's hit labels ("Miss!", "Block!", "Counter"), HP and MP
  counting down, slanted bars, the Break badge and its effect, damage numbers placed and stacked
  as the original does.
- **Menus:** pages slide in and out, Escape asks "Run from battle?", and the HELP box, gene costs
  and results screen look as in 3.1.0. Analyze tells everything the original does.
- **Rules** checked formula by formula: the pair attack's power, the Defense Gates, the level cap,
  counters, the Ward, Endure, Steal Item and more. As in 3.1.0, the Counter skill always answers,
  and a pair attack whose partner has fallen becomes the survivor's own.
- **Battle voices:** the story battles' own victory lines, a line for every action, reactions by
  the damage taken, and the lines the port never said.
- **Scene battles:** the scene's own music, the original's fades, no Escape while a scene runs, and
  a lost story battle shows its game-over screen.

### Field and menus

- **Painted Rooms: Original or Fill.** *Fill* zooms the painted backgrounds to cover a wide screen
  while still following the player and the scripted pans. (The old Whole and Fill Width options are
  gone; they stopped the pans.)
- **Achievements:** the original's 32, earned as in 3.1.0 and announced with the Android release's
  banner. The list opens from the title screen.
- **Prompts seen once stay seen:** "About Option", the Progress Tracker and the "New fun stuff"
  boards no longer come back at every launch.
- **Options:** the original's gauges (filled blue) and a Language row with 3.1.0's languages; the
  Back bar's arrows turn the page; Return to Title asks first.
- **The Camp:** gene plates open their entries before you equip them, the Status page turns to the
  equipment and skills, the Emergency Exit works, and the windows slide as the original's.
- **Also redrawn as 3.1.0 draws them:** the shop, World Select, the save screen, the field map,
  Scenarios (cards slide), the Instruction Manual (in the message window), the title's resume offer.
- **Walking:** the original's collision, so ramps climb smoothly and walls push as they should;
  field shadows and colours as the original's; the field HUD as 3.1.0's.
- **Puzzle rooms:** the pyrokinesis switches pass their light along as in the original, and touches,
  swipes and the block arrows work as in 3.1.0.

### Controller support

- **The Vita layout's field bar on a controller:** △ opens it, the D-pad moves round its entries,
  ○ presses, × shuts it.
- **Puzzle rooms by controller:** the right stick moves a touch pointer and R3 held is the finger,
  so blocks can be lifted, swiped and tapped without touch.
- **Skip:** holding X skips a battle's talk too.

### Saves

- **Opened treasure boxes are kept once:** a box opened again no longer grows the save every time,
  and saves that already repeat them shrink back.
- **One save for both scenarios** (`data.dat`), as the original's. Your old `save1.dat` /
  `save2.dat` are folded into it once.
- **Resuming** after quitting mid-cutscene no longer drops you on an empty painting.

### Graphics, sound and performance

- **FSR 3.1.6** built in, and **FSR 4** as a new Upscaler on AMD GPUs (Linux and Windows) with AMD's
  files in `fsr4/`: see the README.
- **Phones:** the scene fills the screen under a camera hole and into the corners, while menus keep
  to the safe area. The Android layout puts pieces on the phone's edges as Android 1.1.6 does.
- **Sound:** the original's mix (voices no longer drowned out by music), Omega's own Vita music
  tracks (the title theme was silent with a Vita copy alone).

### Setup and Android

- **The first-start setup on Android:** pick your files on the phone; a Vita dump copied into
  `vita/` is decrypted on the next start (on a PC too).
- **Android updates install over each other** from this release on (see Good to know).

### Fixes

- A new game from clear data reaches the opening again.
- Duplicate textures in a model use the original's choice (the "Hmm!?" fix also touches a few
  effects and models).
- No green focus frame round the game on Android after using a controller.
- Many more small layout, timing and sound fixes, found by checking the port against a full
  recording of the original (Parts 1-15); the details are in `docs/REFERENCE_VIDEO_CHECKS.md`.

### Thanks to metastigma

Thanks to [metastigma](https://github.com/metastigma) for reports and testing that led straight to
fixes in this release:

- [issue #1](https://github.com/DuIslingr/CRO-U/issues/1), the broken cutscenes report, which the
  cutscene fixes above answer;
- their fork's work exposed opened treasure boxes growing the save, the controller-driven field bar
  on the Vita layout, the right-stick touch in puzzle rooms, and the X key not skipping a battle's
  talk.

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
- **Android:** install the APK. The setup can pick your files on the phone; folders go over USB into
  `Android/data/local.preservation.chaosringsomega/files/`, in an `ios/`, `android/` or `vita/` folder.

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it. Every
  earlier version was signed with a different key, so coming from one, uninstall it first, one
  last time. That deletes the game files you copied in and your saves: keep a copy of the game
  files to copy back.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
