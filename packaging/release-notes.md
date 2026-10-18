# CRO-U v0.1.0

A from-scratch reimplementation of *Chaos Rings Omega* (iOS / Vita / Android), first release:
builds for Windows, macOS, Linux and Android.

| File | Platform |
|---|---|
| `CRO-U-windows-x64.zip` | Windows 10+ x64 (Vulkan or OpenGL; FSR 3.1) |
| `CRO-U-macos-universal.zip` | macOS 11+, Apple silicon and Intel (Vulkan through MoltenVK, OpenGL; FSR 3.1, MetalFX on macOS 13+) |
| `CRO-U-x86_64.AppImage`, `CRO-U-linux-x86_64.tar.gz` | Linux x86-64 (Vulkan or OpenGL; FSR 3.1) |
| `CRO-U-android.apk` | Android 7+, arm64 / x86-64 (OpenGL ES 3.0) |

**The game's data** - any one of: the iPad release's `data` folder (from
`ChaosRingsOmegaHDeng.app`, 1.0.0 or the voiced 3.1.0), the Vita's `PCSG00499` folder, or the Android
1.1.6 release's `main.110.com.square_enix.chaosringsomega.googleplay.obb` with its APK. Put it beside
the program as `dataHD` (beside the `.app` on macOS, beside the AppImage on Linux), in
`Android/data/local.preservation.chaosringsomega/files/dataHD/` on Android, or pass `--data <folder>`.

**In this release:** the game playable with its own menus, Options pages for display, graphics
(render scale, MSAA, FSR 3.1 / DLSS / MetalFX), and the new Detail page (HD textures, smoothed
models, anisotropic filtering, texture filtering); HD textures you make yourself with
`tools/hd_textures.py` (waifu2x / Real-ESRGAN).

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
