# CRO-U v0.7.3

A fix release for [v0.7.2](https://github.com/DuIslingr/CRO-U/releases/tag/v0.7.2): Flydigi controllers
again, a battle on the ship, and DDS texture replacements. Everything in the earlier v0.7.x notes still
applies.

## Fixed

- **Flydigi controllers, take three** ([#4](https://github.com/DuIslingr/CRO-U/issues/4)): v0.7.2's SDL
  update found the Vader 4 Pro, but the controller shows up as two "Xbox 360 Controller" devices and its
  buttons and sticks come through the second one, which CRO-U never opened. Now every controller is
  opened, and the one you press or move takes over.
- **Vieg no longer walks into the turret's platform when he attacks it**
  ([#6](https://github.com/DuIslingr/CRO-U/issues/6)). The original places that battle's stage, the
  ship's deck, 10 units further back than CRO-U did. Vieg's blow now ends on the deck in front of the
  platform, as on the Vita and Android releases, instead of inside its wall with his legs hidden.
  v0.7.2 wrongly said this matched the original.
- **Vieg walks back from the area edges the game turns you away from**, instead of sliding back in
  his standing pose.
- **Battle stages are lit as the original lights them.** The original draws a stage unlit, with a few
  rules for its transparent parts. Some stages were up to 20% too bright, and the reflective floor of
  the Agent's last stage mirrored the sky instead of showing its grey sheen.

## New

- **DDS texture replacements:** a `<hash>.dds` in `textures/hd` works like a `<hash>.png`. It can hold
  BC1-BC5 (DXT1/3/5, ATI1/2), BC7 or uncompressed 32-bit RGBA/BGRA; the first mip is used. Cube
  maps, volumes and arrays aren't supported. The blocks are decoded by
  [bcdec](https://github.com/iOrange/bcdec) (MIT / Unlicense).

### Thanks to metastigma

[metastigma](https://github.com/metastigma) sent the log that showed the controller as two devices, the
Vita3K screenshots that showed I had #6 wrong, and the report of the sliding Vieg. Their fork's
texture replacements ([CRO-U-metastigma#10](https://github.com/metastigma/CRO-U-metastigma/pull/10))
led to the DDS support.

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
