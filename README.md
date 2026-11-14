# CRO-U — Chaos Rings Omega, unbound

CRO-U is a from-scratch native port of *Chaos Rings Ω (Omega)* (Square Enix / Media.Vision, 2011)
for **Windows, macOS, Linux and Android**. It was rebuilt by reverse-engineering the iPad release
and plays the game from **your own copy**, read at run time. It grew out of
**[CR1:U](https://github.com/DuIslingr/CR1-U)**, the port of the first game: Omega runs on the same
engine.

Both scenarios play from the opening to their endings.

> **No game data is included.** There is no art, audio, text, scripts or code of the game in this
> repository or in its downloads, and nothing generated from them. You need your own copy. (The
> engine's source does contain facts read out of the original, such as file names, flag numbers,
> screen positions and formula constants, where it needs them.)

**Download:** [the latest release](https://github.com/DuIslingr/CRO-U/releases/latest) has builds
for Windows, macOS, Linux (AppImage or tar.gz) and Android.

- [What you need](#what-you-need)
- [Install and first start](#install-and-first-start)
- [Controls](#controls)
- [Settings worth knowing](#settings-worth-knowing)
- [Saves and Vita save import](#saves-and-vita-save-import)
- [HD textures and HD models](#hd-textures-and-hd-models)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Credits, licence and third-party code](#credits-licence-and-third-party-code)

## What you need

A copy of the game from any one of these releases. One is enough; more than one adds to it.

| Release | What to bring | What it gives you |
|---|---|---|
| **iPad, *Chaos Rings Omega HD*** 1.0.0 or 3.1.0 | the `.ipa`, or its unpacked `ChaosRingsOmegaHDeng.app` | The whole game. 3.1.0 adds Japanese voices and eight languages. |
| **Android** 1.1.6 | the APK and its `main.110.com.square_enix.chaosringsomega.googleplay.obb` | The whole game on its own (3.1.0's voiced data, Ogg Vorbis music). Next to an iPad copy it adds sharper textures and art only it has. |
| **PlayStation Vita** (PCSG00499, or in the Prequel Trilogy PCSG00457) | Vita3K's install of it, the game's `PCSG00499` folder, or a NoNpDrm dump (a folder or a `.rar` / `.7z` / `.zip` / `.vpk`) | The whole game on its own, in the Vita's 960 × 544 layout. Next to an iPad copy it adds its voices, music, eight languages and textures. |

With several copies, **the iPad copy is the game and the others add to it**. Each add-on can be
switched off in Options, Game: *Vita Assets* / *Android Assets*. Without an iPad copy, the Vita copy
is the game; without either, the Android release is.

## Install and first start

1. Download the file for your system from the releases page and unzip it.
2. Start `chaosrings`.
3. If it doesn't find your copy, it opens a setup window: pick your `.ipa`, your Android APK and
   OBB, or your Vita folder or dump. The files it needs are **copied into the game folder**, so the
   place you picked them from can be deleted afterwards. A Vita dump is decrypted as Vita3K's
   install does it (only this game's parts; the Prequel Trilogy's add-on is merged in).
4. Play.

First-start notes per system:

- **Windows:** SmartScreen may warn about an unsigned program: choose *More info*, *Run anyway*.
- **macOS:** right-click `chaosrings.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK. The setup can pick files on the phone (an `.ipa`, the APK and OBB,
  a Vita dump archive). Folders have to be copied over USB into
  `Android/data/local.preservation.chaosringsomega/files/`, in an `ios/`, `android/` or `vita/`
  folder. From v0.5.0 on, each release installs over the one before it; coming from an older build,
  uninstall it first, one last time (that deletes the copied game files and saves, so keep a copy).

### Putting your copy in by hand

You can also skip the setup and place the files yourself. The **game folder** is:

| Platform | Game folder |
|---|---|
| Windows, Linux | the folder holding `chaosrings.exe` / `chaosrings` |
| Linux AppImage | the folder holding the `.AppImage` |
| macOS | the folder holding `chaosrings.app` (its `Contents/Resources/` works too) |
| Android | `Android/data/local.preservation.chaosringsomega/files/` on the shared storage |

Inside it, one folder per release you have (the downloads come with these folders made, each with a
`README.txt`):

```
<game folder>/
├── chaosrings                  (or chaosrings.exe, chaosrings.app, the .AppImage)
├── ios/ChaosRingsOmegaHDeng.app/          the iPad .app (1.0.0 or 3.1.0), or Payload/ChaosRingsOmegaHDeng.app/, or the .ipa
├── android/                               the .obb and the 1.1.6 APK (or its classes.dex), or the .xapk / .apks
├── vita/PCSG00499/                        the installed title (CRO/resources/cro.psarc, CRO/sound/*.psarc)
├── fsr4/                                  optional: AMD's FSR 4 files (see Settings)
└── textures/  models/                     optional: your HD textures and models
```

- A copy may sit one or two folders deeper (as it was unpacked); the folder names below `ios/`,
  `android/` and `vita/` don't matter.
- Files put in as they come are unpacked when the game starts, with a progress window, and that
  same start goes on with the game: an `.ipa` in `ios/` (its `.app` unpacked beside it), an `.xapk`
  or `.apks` in `android/` (its OBB and APKs unpacked beside it; an `.apks` needs the OBB put in
  too), and a NoNpDrm dump in `vita/` still encrypted (its `app/`, plus `addcont/` for the Prequel
  Trilogy: decrypted into `vita/PCSG00499/`) - on a phone too. Each needs room for the unpacked
  copy; the file or dump you put in can be deleted afterwards.
- Older places still work: `dataHD/`, `data/`, `ChaosRingsOmegaHDeng.app/` or `Payload/` straight in
  the game folder, or the working directory.
- A copy somewhere else: `--data <iOS data>` (or `CR_DATA`), `--vita <PCSG00499 folder>`
  (`CR_VITA`), `--android <folder>` (`CR_ANDROID`), or pick the folders in Options, Game (kept in
  `settings.ini`). `--vita none` / `--android none` run without them.

If nothing is found, the program tells you where it looked; put the files in and choose *Look
again* - no restart needed. All four ports (CR1:U, CRO-U, CR2-U,
CR3-U) find their copies the same way.

## Controls

A controller plays as the Vita does. The face buttons go by position, so on an Xbox pad ○ is B,
× is A, △ is Y and □ is X. ○ confirms and × cancels, as in the Japanese release; Options, Game,
"Swap ○ and ×" turns that round. Tested with a Steam Controller (through Steam Input).

| Button | On the field | In menus and dialogue | In puzzle rooms |
|---|---|---|---|
| Left stick / D-pad | walk (the stick pushed past half way runs; the D-pad runs, as on the Vita) | move the cursor (held, a list repeats) | walk |
| ○ | examine / talk | confirm, turn the page | examine |
| × | - | cancel, back | - |
| △ | open the Camp (Vita layout: the corner bar - D-pad picks, ○ presses, × shuts) | close the Camp (not in the Vita layout: × does) | open the Camp |
| □ | - | skip a scene's text (until a choice); hold for a fast staff roll | press "Skip" once it shows, else use the pair's ability |
| L / R | - | previous / next page; the Camp's categories, member and Status views, the shop's member and count | - |
| START | Options | close Options | restart the puzzle |
| SELECT | the corner bar (field menu) | - | the puzzle's help |
| Right stick / R3 | - | - | a touch pointer: the stick moves it, R3 held is the finger (lift and swipe blocks, tap anything) |

| Keyboard / mouse | |
|---|---|
| W A S D (Shift runs) | walk; in menus, the cursor as the arrow keys |
| E / Enter / Space | examine, confirm, turn the page |
| Esc / Backspace | cancel, back, answer No (on the field itself Esc quits) |
| K | the Camp |
| R | the corner bar |
| X | skip a scene's text (hold) |
| V | Options |
| A | the achievements list (on the title screen) |
| PgUp / PgDn (Q / E in Options) | pages |
| T, F5, F6 | puzzle rooms: the ability straight ahead, retry, help |
| F3 | toggle Control Hints (also in Options, Game; saved between runs) |
| Mouse | everything the original takes by touch: plates, buttons, the dialogue, the corner button; drag to push a block in puzzle rooms |

**Touch** (or the mouse) plays the field as the original does: put a finger down anywhere and drag.
A pad appears where it went down and the leader walks the way the finger points, running when it's
pulled further out. With something in reach, the "!" button in the bottom right corner (or a tap on
the "!" over it) examines or talks.

## Settings worth knowing

Everything is in **Options** (START, or V). Settings and saves live in the profile folder
(see [Saves](#saves-and-vita-save-import)), not the game folder.

**Display**
- **Layout:** Auto (follows your data), iPad (4:3), iPhone (3:2), Vita or Android (16:9). Also
  `--layout vita`.
- **Painted Rooms:** *Original* shows the painted backgrounds as the game does; *Fill* zooms them
  until they cover a wide screen, still following the player and the scripted camera pans
  (`--fill-paintings`).
- **Frame Rate:** 30 / 60 / 120 / 144 / unlimited. The game logic runs at its own 20 frames a
  second; characters, cameras and battle actors are drawn moving smoothly between those frames.
- *Original Presentation* puts every display and graphics option back to the original's look.

**Detail** (most need the Vulkan renderer)
- **Render Scale**, **Anti-aliasing**, **Upscaler** and **Upscale Mode**. Upscalers: FSR 3.1
  (built in), FSR 4, DLSS (own builds only) and MetalFX (macOS 13+, untested).
- **FSR 4** (Linux and Windows, AMD RX 6000 series or later): AMD only ships it as Windows DLLs that
  can't be bundled here. Put these into `fsr4/` in the game folder (its `README.txt` says where
  each comes from):
  `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` from AMD's
  [FSR SDK 2.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0)
  (`Kits/FidelityFX/signedbin/`), `amdvkffx64.dll` from the AMD Software: Adrenalin Edition 26.10.1+
  Windows driver package, and on Linux `libffx_vk_linux.so` from
  [ffx-vk-linux](https://github.com/DuIslingr/ffx-vk-linux) (MIT), which runs AMD's DLLs natively, no
  Wine. Without the driver DLL only FSR 3.1 is offered, and the game says so. `CR_FSR4_DIR` points
  elsewhere; `--upscaler fsr4` picks it.
- **DLSS** isn't in the downloads: NVIDIA's library is proprietary and can't ship with GPL code. You
  can build it in for your own use (see [Building](#building-from-source)).
- **HD Textures**, **HD Models**, **Smooth Models**, **Anisotropic Filtering**, **Texture Filtering**.

**Game**
- **Field Speed** / **Battle Speed:** Off or 2x-5x (scenes, talk and menus keep their pace).
  `--field-speed n` / `--battle-speed n` for one run.
- **Swap ○ and ×**, **Control Hints**, **Vita Assets** / **Android Assets**, **Import Vita Saves**.

**Language:** chosen in Options. The 3.1.0, Android and Vita data carry eight languages; the 1.0.0
data has Japanese and English.

**Achievements:** the original's 32 achievements are earned as in 3.1.0 and announced with the
Android release's banner. The list opens from the title screen's achievements button (or A).

## Saves and Vita save import

Settings and saves go to the `ChaosRingsOmegaHD` profile folder, separate from CR1:U's:

| Platform | Profile folder |
|---|---|
| Linux | `~/.local/share/preservation/ChaosRingsOmegaHD/` |
| Windows | `%APPDATA%\preservation\ChaosRingsOmegaHD\` |
| macOS | `~/Library/Application Support/preservation/ChaosRingsOmegaHD/` |

Both scenarios share one save, `data.dat`, as in the original; `save1.dat` / `save2.dat` from older
versions are folded into it once. The "resume from last saved point" offer on the title uses what
the end of a scene or an encounter saved, as the original does.

**Vita saves:** on the title screen open Options, then **Game → Import Vita Saves**, choose your
decrypted, exported save folder and confirm. Existing saves are backed up first, and no Python is
needed. Details and the optional command-line converter: [docs/VITA_SAVES.md](docs/VITA_SAVES.md).

## HD textures and HD models

You can make sharper textures and better models from your own copy. They are for personal use: they
are made from the game's art, so keep them out of anything you share.

**HD textures**
1. `chaosrings --export-textures` writes every picture the game draws to `textures/export/` in the
   game folder as `<hash>.png` (`--export-textures <folder>` for somewhere else).
2. `python3 tools/hd_textures.py <game folder>` upscales them into `textures/hd/`, with waifu2x
   (`waifu2x-ncnn-vulkan`) and Real-ESRGAN (`--esrgan <folder>`) for 16-bit pictures. It needs
   Python 3 with NumPy and Pillow; `--help` lists the options.
3. Start the game: with `textures/hd/` present, HD Textures switches on by itself.

Any upscaler or image editor works: save the result under the same name in `textures/hd/`, keep its
proportions (at least the original size, up to 8192 a side) and keep it the right way up. A picture
that doesn't fit is skipped and the log says why. Exports made with Vita Assets on leave `.cralias`
files so one upscale serves the game with or without the Vita's art. `CR_HD_TEXTURES=<folder>`
reads them from elsewhere; `--no-hd-textures` runs without.

**HD models**
1. `chaosrings --export-models` writes every model to `models/export/` as glTF 2.0 (`.glb`, with
   its skeleton, materials, pictures and animations). Blender opens them as they are.
2. Edit it, keeping the armature (bones are named `node_<n>...`) and the material names
   (`cr_mat_<n>...`). A material can take a picture of your own (PNG or JPEG, packed in the `.glb`).
3. Export as glTF 2.0 to `models/hd/<package>.glb` (`j009a.glb` for `j009a.bin`) and switch on HD
   Models in Options, Detail.

The game plays its own animations; up to four weights a vertex and 255 joints a mesh are read.
`crtool gltfcheck` checks a round trip, and `CR_HD_MODELS=<folder>` reads models from elsewhere.

## Troubleshooting

- **"It can't find my game":** the program lists where it looked. Check that the files are in the
  game folder under `ios/`, `android/` or `vita/` (see
  [Putting your copy in by hand](#putting-your-copy-in-by-hand)), or start it with `--data`.
- **Upscaler shows "n/a" or is greyed out:** upscalers, Render Scale and Anti-aliasing need the
  Vulkan renderer; the option's hint says why it's off. FSR 4 also needs the files in `fsr4/`.
- **Android, an update won't install:** builds before v0.5.0 were each signed with a different key.
  Uninstall the old one first (back up your game files and saves).
- **Tested on:** Linux (played) and Windows (under Wine, Vulkan and OpenGL). The macOS and Android
  builds are made by CI but haven't been played on real hardware yet. Reports are welcome in
  [Issues](https://github.com/DuIslingr/CRO-U/issues).

## Building from source

The build is CR1:U's: C++17, SDL3, Vulkan or OpenGL.

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/chaosrings
```

Put your copy in `engine/build/` (or the folder you start it from) as described above.

| Platform | How to build | Renderers and upscalers |
|---|---|---|
| Linux | as above | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (with the SDK) |
| Windows 10+ (x64) | MSVC and Ninja from a Developer prompt, plus the [Vulkan SDK](https://vulkan.lunarg.com/) for Vulkan; or from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): `-DCMAKE_TOOLCHAIN_FILE=engine/cmake/llvm-mingw-x86_64.cmake`. CMake builds SDL3 and zlib | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (MSVC, with the SDK) |
| macOS 11+ (Apple silicon and Intel) | Xcode's command-line tools, Ninja and the [Vulkan SDK](https://vulkan.lunarg.com/) (MoltenVK is copied into the `.app`); add `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` for both | Vulkan (MoltenVK), OpenGL 3.3; FSR 3.1, MetalFX (macOS 13+) |
| Android 7+ (arm64, x86-64) | the Android SDK and NDK: `git clone -b release-3.2.0 https://github.com/libsdl-org/SDL android/SDL`, then `gradle assembleRelease` in `android/` | OpenGL ES 3.0 |

- `-DCR_GLES=ON` builds the desktop game on OpenGL ES 3.0, as Android runs it (Mesa has it), to test
  that path without a device.
- **DLSS:** NVIDIA's [DLSS SDK](https://github.com/NVIDIA/DLSS) via `-DCR_DLSS_SDK=<its checkout>`
  (or in `engine/third_party/dlss/`), its runtime copied next to the game. CI builds it on Linux and
  Windows to prove it works but never uploads those builds: a DLSS build is for your own use.
- Every push is built for all four platforms by `.github/workflows/build.yml`; a `v*` tag publishes
  a release.
- `chaosrings --selftest` checks the game logic against your data (iPad 1.0.0 and 3.1.0, iPad +
  Vita, Vita alone); `--explore n [scenario]` plays a scenario by itself to its ending.

### Documentation

| Path | What's in it |
|---|---|
| [`docs/STATUS.md`](docs/STATUS.md) | each part of the port and the original's functions it follows |
| [`docs/ENGINE.md`](docs/ENGINE.md) | the engine shared with CR1:U: building, tools, the viewer, systems such as achievements |
| [`docs/FORMATS.md`](docs/FORMATS.md) | the game's file formats |
| [`docs/ANDROID.md`](docs/ANDROID.md) | the Android release: what its APK holds, its screen layout |
| [`docs/VITA_SAVES.md`](docs/VITA_SAVES.md) | importing Vita saves |
| [`docs/REFERENCE_VIDEO_CHECKS.md`](docs/REFERENCE_VIDEO_CHECKS.md) | what was checked against recordings of the original |
| `engine/` | the C++17 / SDL3 engine and `crtool` |
| `tools/` | analysis and extraction tools; they work on your own copy |

**Reverse-engineering setup:** three builds are the reference, all from your own copies (nothing of
them is committed): the iPad 1.0.0 binary (with its own symbols), the voiced iPad 3.1.0 binary and
the Vita's `eboot.bin`. `tools/re_setup.sh <1.0.0 .app> <3.1.0 .app> <PCSG00499>` builds a Ghidra
project for each into `re/` (git-ignored), carrying function names between builds. Never commit or
share `re/` or `extracted/`.

## Credits, licence and third-party code

- **Contributors:** thanks to [metastigma](https://github.com/metastigma) for fixes, reports and
  testing.
- **Licence:** GPL-2.0-or-later (`LICENSE`). The macOS `.app` is GPL-3.0, because the Vulkan loader
  and MoltenVK it carries are Apache-2.0.
- **Third-party code:** the bundled libraries (FAAD2, LibAtrac9, vgmstream's HE-VAG table, AMD's FSR
  SDK, stb, cgltf, Vita3K's psvpfsparser, and SDL3, zlib and libarchive built from source) keep
  their own licences: see [`THIRD_PARTY.md`](THIRD_PARTY.md).
- **Not affiliated:** a fan preservation project, not affiliated with or endorsed by Square Enix or
  Media.Vision. *Chaos Rings Omega* and its assets are their property; the port loads them at run
  time from a copy you own.
