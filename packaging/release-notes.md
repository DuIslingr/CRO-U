# CRO-U v0.7.4

A fix and feature release for [v0.7.3](https://github.com/DuIslingr/CRO-U/releases/tag/v0.7.3): the port's menus in nine languages, fan translations as a language of their own, and fixes for
three of metastigma's reports. Everything in the v0.7.x notes still applies.

## New

- **Menu Language** ([#10](https://github.com/DuIslingr/CRO-U/issues/10)): the port's own text
  (its Options, the first start's setup and the Vita save import) now comes in Japanese, French,
  German, Spanish, Chinese (simplified and traditional), Korean and Russian, as well as English.
  *Options > Game > Menu Language*; Auto, the default, follows the game's text language. The
  translations are gettext catalogs in `engine/lang/`; a `.po` dropped into a `lang/` folder in the
  game or profile folder is read in place of the built-in one, without building anything.
- **Fan translations** ([#11](https://github.com/DuIslingr/CRO-U/issues/11)): a translation of the
  game's text, put into `translation/` in the game folder, appears as a language of its own after the
  game's eight, e.g. "Русский (fan translation)" in *Options > Game > Language*. Nothing is
  replaced, and a message the translation lacks shows in English. It can be the translated `sys.evt`
  and area `.evt` files (and any redrawn `.gxt`) from the Vita's `cro.psarc` in `translation/ru/`, a
  rePatch folder with the whole translated `cro.psarc`, or the `.7z` / `.rar` / `.zip` either comes
  in, dropped into `translation/` and unpacked on the next start. The README has the details.
- **Camera Cutout: Black Bars.** A third choice besides Avoid and Ignore: the whole picture boxed
  inside the screen's safe area, the rest black.

## Fixed

- **Vieg is seen on the paddle wheel** ([#9](https://github.com/DuIslingr/CRO-U/issues/9)): on the
  ship, after the fade-in he climbs the wheel and stands on it for "Made it onboard somehow." and
  again after the camera's pan. The ship's waves were drawn over him; the figures are now drawn in
  one queue by draw order, as the original draws them.
- **Vieg's leg no longer disappears into the bed** ([#12](https://github.com/DuIslingr/CRO-U/issues/12)):
  running into furniture in a painted room, the forward leg of his stride went behind the piece's
  front and was cut off, so he looked as if he stepped onto or into the bed. A figure is now hidden
  by furniture in front of its body, not by furniture only its limbs reach behind. The original
  has the same glitch; this is a deliberate departure from it.
- **Android: the game no longer sits under the camera notch** with Camera Cutout on Avoid. SDL
  reports the safe area only from Android 11, and on 11 - 14 can lose it as the window goes
  fullscreen; the port now also asks Android for the cutout itself.
- **Russian letters are no longer spaced out.** The Japanese fonts the game uses draw Cyrillic a
  full character cell wide; Russian now comes from the system's proportional font (Noto Sans,
  Segoe UI, Arial, Roboto ...) when the game's font draws it that way.
- **Puzzle rooms:** the touch pad holds taps on the blocks off only while the pad is shown, as the
  original does.
- **Linux, AMD:** FSR 4 about 0.3 ms faster at 4K on RADV.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported #9 - #12 with the shots and the Vita3K
recordings that pinned them down, asked for the menus in every language, and wrote the Russian
Options' wording (CRO-U-metastigma PR #51). The other menu translations are machine translations
not yet reviewed by native speakers; corrections are welcome.

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
