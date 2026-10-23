# CRO-U v0.3.0

A from-scratch reimplementation of *Chaos Rings Omega* (iOS / Vita / Android): builds for Windows,
macOS, Linux and Android.

**New in this release: much closer to the original**, from an audit against the game's code and a
comparison with a full playthrough on video.

Battles
- A boss's end as the game plays it: the camera on the boss, its down motion played through, the
  burst and fade (or the white-out for the story's bosses), then "Victory!" with the camera on the
  pair, held until a tap. The story battles' set pieces between rounds, group 0x294's rumbling
  rounds, and the scene brought on by blows that don't land.
- The pair stands side by side in pair mode, as in the game.
- Omega's own battle sounds: each swing by species, each hit by its kind, genes and items as they
  start, the bosses' own; the victory tune only when it isn't the battle's own music.
- An action that can't happen any more says so; the last battle item listed.
- Effects at their monster's size, and five effects (a boss's burst, flares, pillars of light) that
  had drawn collapsed; fading models fade instead of vanishing at once.
- The item and gene lists with the game's plates, icons and HELP board.

The field, scripts and menus
- Walking by touch: the pad and the "!" button.
- Scripts: an NPC's fade, the camera turning onto someone, scaling, straight walks; sounds fading
  as the scripts ask (the shop's room sounds too).
- The menus' darkening as the game's: a slanted mask that fades in and out; Back's arrows only
  where pages turn, drawn as the game draws them; the battery beside the clock (and on the
  Piu-Piu Card).
- A message that ends by itself stays up until the scene closes it (Extra Mode's warning); the map
  screen dims only the game's own screen in widescreen.

**From v0.2.0:** your copy found by itself in `ios/`, `android/` or `vita/` beside the program, and HD
textures in `textures/hd/` turned on by themselves (below, and the README's "Your copy").

| File | Platform |
|---|---|
| `CRO-U-windows-x64.zip` | Windows 10+ x64 (Vulkan or OpenGL; FSR 3.1) |
| `CRO-U-macos-universal.zip` | macOS 11+, Apple silicon and Intel (Vulkan through MoltenVK, OpenGL; FSR 3.1, MetalFX on macOS 13+) |
| `CRO-U-x86_64.AppImage`, `CRO-U-linux-x86_64.tar.gz` | Linux x86-64 (Vulkan or OpenGL; FSR 3.1) |
| `CRO-U-android.apk` | Android 7+, arm64 / x86-64 (OpenGL ES 3.0) |

**The game's data** - nothing to set: put your copy in the folder holding the program (the folder
holding the `.app` on macOS, the AppImage's on Linux; on Android
`Android/data/local.preservation.chaosringsomega/files/` on the shared storage), in a folder named after its release:
`ios/` - the iPad's `ChaosRingsOmegaHDeng.app` (1.0.0 or the voiced 3.1.0; or the unpacked `.ipa`); `android/` - the 1.1.6 release's `main.110.com.square_enix.chaosringsomega.googleplay.obb` and its APK; `vita/` - the installed `PCSG00499` folder. Any one is enough, and each
may hold it as it was unpacked or installed (one or two folders down). The iOS copy is the game and the others add their voices, music, languages and art to it (Options, Game); alone, the Vita or the Android release is the game. If none is found it
says where it looked; the README's "Your copy" has the details.

**Also in it (from v0.1.0):** the game playable with its own menus, Options pages for display, graphics
(render scale, MSAA, FSR 3.1 / DLSS / MetalFX), and the new Detail page (HD textures, smoothed
models, anisotropic filtering, texture filtering); HD textures you make yourself (`chaosrings --export-textures` into `textures/export/`, then
`tools/hd_textures.py <the game folder>` into `textures/hd/`, read by itself: the README's "HD textures").

**Not included:** any of the game. You need your own copy; the port reads it at run time. DLSS is
not in these builds either (NVIDIA's NGX library is proprietary and the port is GPL): build it
yourself with the SDK for your own use (README, Platforms).

**First run**
- **Windows:** unzip and run `chaosrings.exe`. SmartScreen may warn about an unsigned program: *More info*, *Run anyway*.
- **macOS 11+:** unzip, then right-click `chaosrings.app` and choose *Open* (it is not signed or notarized), or run `xattr -dr com.apple.quarantine chaosrings.app`.
- **Linux:** `chmod +x CRO-U-x86_64.AppImage` and run it, or unpack the tar.gz.
- **Android 7+:** allow installing from your browser or file manager, install the APK, then copy the data over USB as below.

**Status of this build:** the Linux build is the one played and tested; the Windows build is checked
under Wine (Vulkan and OpenGL, the same frames as Linux's). The macOS and Android builds are built
by CI but have not yet been run on a real Mac or device - MetalFX, especially, is untested. Reports
welcome.

Licence: GPL-2.0-or-later; the macOS `.app` carries MoltenVK and the Vulkan loader (Apache-2.0), so
it is distributed under GPL-3.0 (THIRD_PARTY.md). Personal, non-commercial preservation project, not
affiliated with Square Enix or Media.Vision.
