# CRO-U v0.7.2

A fix release for [v0.7.1](https://github.com/DuIslingr/CRO-U/releases/tag/v0.7.1): three things that
looked wrong, all reported by metastigma. Everything in v0.7.0's and v0.7.1's notes still applies.

## Fixed

- **Battle models no longer turn speckled and see-through on some graphics cards**
  ([#5](https://github.com/DuIslingr/CRO-U/issues/5)). A battle model draws parts of itself twice, and
  the second copy only keeps pixels that are fully opaque. CRO-U checked that with an exact
  comparison, while the original allows for rounding; on GPUs that land a hair short, scattered pixels
  were dropped and Vieg's skin, arms and sword showed the sky through them. The check now matches the
  original's, in both the Vulkan and the OpenGL renderer.
- **The opening's "CHAOS RINGS" title sits inside the eclipse's ring**
  ([#7](https://github.com/DuIslingr/CRO-U/issues/7)), as the original has it, instead of up and to
  the left of it with its black box showing over the glow.
- **The snowfield's frosty frame borders the whole screen**
  ([#8](https://github.com/DuIslingr/CRO-U/issues/8)) on the iPhone, Android and Vita layouts and in
  widescreen, instead of sitting off-centre with a second copy at the edge. The same goes for the
  canvas and frames of a later area.
- **Infinite HP no longer traps you in a battle the story has you lose** (already in v0.7.1's
  re-uploaded downloads): it holds while you can still bring a foe down, and lapses once none can
  fall, so the defeat and the story come.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported all three, with screenshots that made each one
easy to find.

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
