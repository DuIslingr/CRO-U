# CRO-U v0.4.1

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux and Android.

## What's new

- **Android-data fixes:** corrected field walking animations, dialogue boxes, and missing cutscene backgrounds. Shared rendering fixes also cover iOS and Vita data.
- **Control Hints setting:** hide or show the on-screen keyboard and controller help from Options → Game, with the choice saved between sessions.
- **Vita save importing:** import decrypted Vita saves from the native title-screen Options → Game → Import Vita Saves menu, with automatic backups. Imported abilities retain their ownership in each scenario. The optional command-line converter and instructions are included too.

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

- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
