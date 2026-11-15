# CRO-U v0.7.0

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux, Android and now
iPhone and iPad.

Ninety changes since v0.5.0. The headline is the **iOS build**, with phones and foldables treated
as first-class screens; underneath, another full pass over the battles, the menus and the controls
against the newest iPad release (3.1.0) and a full recording of the original.

## What's new

### iPhone and iPad (new)

- **An iOS build** (`CRO-U-ios-unsigned.ipa`, iOS 15+): Vulkan through MoltenVK with **MetalFX**
  upscaling (iOS 16+) and FSR 3.1, OpenGL ES as the fallback. It is unsigned: sideload it with
  AltStore, Sideloadly or the like, which sign it with your Apple ID.
- **Your copy, as on Android:** the first start asks for it with the system's file picker (an `.ipa`,
  the APK and OBB, a Vita folder or a dump's archive) and shows its progress. Or put it into the app's
  folder in the Files app (*On My iPhone / iPad > Chaos Rings Omega*) yourself.
- **Built for today's screens:** 120 Hz on ProMotion displays, the iPhone layout on a small screen and
  the iPad layout on a large one - on the **iPhone Duo** it changes as you fold or open it. Built with
  the iOS 27.1 SDK, so the game fills the Duo's screens instead of sitting in a box.
- Text in iOS's own fonts (Hiragino for Japanese, as 3.1.0; PingFang and Apple SD Gothic Neo for
  Chinese and Korean), the app's own icon, and nothing drawn while the app is in the background, so
  the picture never freezes when you come back.

### Phones and tablets

- **The game fills the screen**, whatever its shape. Only the menus and buttons keep clear of a
  camera's notch or punch hole; the picture runs to the edges. *Options > Display > Camera Cutout*
  turns that off.
- **No resolution, window or aspect options on phones:** the game uses the screen's own resolution
  and shape.
- Your `ios/`, `android/`, `vita/` and `font/` folders are made on every start, so they always show
  over USB and in the Files app.

### Your copy, put in by hand

- **Drop an archive in and play:** an `.ipa` in `ios/`, an `.xapk` / `.apks` in `android/`, a Vita
  dump's `.zip` / `.rar` / `.7z` / `.vpk` in `vita/` - each is unpacked (and decrypted) when the game
  starts, with a progress window, and the game goes straight on. No setup, no second start.
- **"Could not find your copy"** now has *Look again*, so you can put the files in and carry on.
- **A Vita dump's .rar that wouldn't unpack** (the Prequel Trilogy's) now does: libarchive 3.8.11.
- **The Vita decrypter** is the project's own clean-room code, and decrypts correctly on macOS and
  Apple devices (an AES fault there was fixed).

### Cheats (new)

- **Options > Cheats:** Infinite HP, Infinite MP, Max Oz, an EXP Multiplier (up to 10x), Always First
  Strike, No Random Battles, One-Hit Kills and a Full Break Gauge. Saved with your settings.

### Battles

- **Rules as 3.1.0's:** drain shared among its users, scenario 2's boss moves, enemy pairs, counters
  after a multi-hit move, the escape chance, speed ties, story battles' HP rules (Olgar can't be
  healed or fall in his duel), a target that fell before its turn re-picked.
- **Presentation as the videos show it:** physical genes play as attacks, Risk Breaker and Venom
  Kiss, bounces, an attack's length and the next action's timing as the original's, in-place
  attacks, partner-timed pair hits, the HUD, results, escape and defeat. Shadows fade out softly at
  their edge.
- A lost battle plays the Ark's after-defeat scene again.

### Field, menus and scenes

- **The Camp's Equip page** slides its windows step by step as 3.1.0's, a worn slot's list opens on
  what is worn, and gear the partner wears is listed (a jewel with no free copy greyed out).
- **Held controls light, and act as you lift** your finger, as in 3.1.0, everywhere from the Camp to
  the World Select; counts step as the original's.
- Scene actors keep their motion while hidden, actors a scene brings in cast their shadow, the hall's
  news marks, model colours that left a body see-through, the Options sounds, puzzle taps on the Vita
  layout, the save question's taps, the title's pieces.
- **The Vita layout** as the Vita release draws it: the screen's edges, the messages, the title, the
  pictures, its fonts, no battery icon.
- **Your own font:** a `.ttf`, `.otf` or `.ttc` in `font/` replaces the system's.

### Controllers and keyboard

- **As the Vita's:** L / R, ×, the title, the D-pad running, kept battle cursors, lists that repeat,
  ○ to go on, × answering No everywhere.
- **Hot-plug:** another connected controller takes over when one is unplugged, and a wireless pad
  that wakes behind its receiver is picked up again.
- **F11 or Alt+Enter** toggles windowed / fullscreen.

### Under the hood

- SDL 3.4.20, the Vulkan SDK 1.4.363.1 and MoltenVK 1.4.2 (macOS and iOS).
- A log file, `chaosrings.log`, in the profile folder (on iOS, the app's Files folder).

### Thanks to metastigma

Thanks again to [metastigma](https://github.com/metastigma), whose fork
([CRO-U-metastigma](https://github.com/metastigma/CRO-U-metastigma)) found or led to a good share of
this release:

- **Found and worked out by metastigma:** the model colours that left a body see-through
  ([metastigma/CRO-U-metastigma#24](https://github.com/metastigma/CRO-U-metastigma/pull/24);
  [4e623f5](https://github.com/DuIslingr/CRO-U/commit/4e623f5)).
- **Found in their fork:** the puzzle taps on the Vita layout
  ([008c54a](https://github.com/DuIslingr/CRO-U/commit/008c54a)), the save question's taps
  ([43af962](https://github.com/DuIslingr/CRO-U/commit/43af962)), the title's pieces
  ([fe6ad42](https://github.com/DuIslingr/CRO-U/commit/fe6ad42)), the voiced data's action records
  ([9950d98](https://github.com/DuIslingr/CRO-U/commit/9950d98)), physical genes playing as attacks
  ([a187062](https://github.com/DuIslingr/CRO-U/commit/a187062)), a gene's cast loop
  ([de01a67](https://github.com/DuIslingr/CRO-U/commit/de01a67)) and the soft shadow edges
  ([7495c1c](https://github.com/DuIslingr/CRO-U/commit/7495c1c)).
- **From reviewing their fork:** the controller hot-plug, after their Windows XInput work
  ([0571f0f](https://github.com/DuIslingr/CRO-U/commit/0571f0f)); F11 / Alt+Enter, which their fork had
  first ([81a71a1](https://github.com/DuIslingr/CRO-U/commit/81a71a1)); script-read actors' shadows and
  the Camp's worn-slot lists ([13bdb62](https://github.com/DuIslingr/CRO-U/commit/13bdb62)); and the
  Equip page's slides ([945d981](https://github.com/DuIslingr/CRO-U/commit/945d981)).
- [Issue #2](https://github.com/DuIslingr/CRO-U/issues/2), their Options bars report, was answered by
  v0.5.0's gauges.

## Downloads

| File | Platform |
|---|---|
| `CRO-U-windows-x64.zip` | Windows 10+ x64 |
| `CRO-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CRO-U-x86_64.AppImage` / `CRO-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CRO-U-android.apk` | Android 7+ |
| `CRO-U-ios-unsigned.ipa` | iOS / iPadOS 15+ (sideload it) |

## Getting started

1. Download the file for your system and unzip it.
2. Start `chaosrings`. If it can't find your copy of the game, it asks you for one and sets it up -
   or drop your `.ipa`, `.xapk` or Vita dump into the matching folder and start it.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `chaosrings.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK. The setup can pick your files on the phone; folders go over USB into
  `Android/data/local.preservation.chaosringsomega/files/`, in an `ios/`, `android/` or `vita/` folder.
- **iPhone / iPad:** sideload the `.ipa` (AltStore, Sideloadly). The first start's picker finds your
  files, or copy them into the app's folder in the Files app.

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine); the iOS build's screen handling was checked on
  an iPhone Duo with CR1-U, which shares it. The macOS and Android builds are made by CI. Reports are
  welcome.
- **Android, updating:** releases install over each other since v0.5.0.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
