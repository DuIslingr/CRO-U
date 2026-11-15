# CRO-U v0.7.1

A fix release for [v0.7.0](https://github.com/DuIslingr/CRO-U/releases/tag/v0.7.0): two story battles
that ended the game, a controller Windows stopped finding, and missing text on some Android phones.
Everything in v0.7.0's notes still applies.

## Fixed

- **The opening battle with the Agent, and its twin in the second scenario, no longer end in a game
  over** ([#3](https://github.com/DuIslingr/CRO-U/issues/3)). These are battles you're meant to lose:
  in the original, the pair falls in the first round and the story carries on, as the reference
  recording shows. CRO-U only let the story go on from the second round, so the first-round defeat
  sent you back to the title; only Infinite HP got past it.
- **Flydigi controllers on Windows** ([#4](https://github.com/DuIslingr/CRO-U/issues/4)): a Vader 4
  Pro, wired or through its receiver, wasn't found since v0.7.0's SDL update. SDL 3.4 added its own
  Flydigi driver, which takes the controller and then gets no input from it; CRO-U now leaves that
  driver off on Windows, so the controller is read through XInput as before. Setting the environment
  variable `SDL_JOYSTICK_HIDAPI_FLYDIGI=1` turns SDL's driver back on. Every controller SDL finds is
  now written to `chaosrings.log`, so if one still isn't picked up, the log shows why.
- **No text on some Android phones:** phones whose maker ships its own fonts, without Android's usual
  Noto fonts where CRO-U looked, showed the game's Japanese, Chinese and Korean text missing (and the
  rest in a blocky fallback). CRO-U now reads the phone's own font list and uses the fonts it names
  for those languages, then the phone's default font.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported the Flydigi problem and investigated it in their
fork ([CRO-U-metastigma#42](https://github.com/metastigma/CRO-U-metastigma/pull/42),
[#43](https://github.com/metastigma/CRO-U-metastigma/pull/43)); CRO-U's controller events already work
as their #43 does, and this release takes the narrower fix above instead of their development build of
SDL.

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
