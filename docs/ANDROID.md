# The Android release (Google Play 1.1.6)

Notes from the Android APK (`com.square_enix.chaosringsomega.googleplay`, 1.1.6), read with
`tools/re_setup_android.sh <unpacked APK>` (Ghidra; output in `re/android/`, never committed).
Nothing of the APK is in this repository.

## What the APK holds

| File | What it is |
|---|---|
| `lib/armeabi-v7a/libchaosringsomega_native.so` | the game: the iOS code base, Thumb-2 (GCC), **1857 functions exported by name** - the iOS build's names plus ~470 it keeps static or lacks |
| `lib/armeabi-v7a/libmc_eruption_for_android_jni.so` | MascotCapsule eruption, the 3D engine (as on iOS) |
| `lib/armeabi-v7a/lib__57d5__.so` | a 12 KB packed stub (no section headers; licence / anti-tamper, not game code) |
| `classes.dex` | the Java shell: the activity, Google Play licensing (LVL) and the APK Expansion downloader |
| `assets/00000`, `00001` | two OpenType (CFF) fonts |
| `assets/loading.mct` | the loading picture |

The game's data is **not** in the APK: it comes as a Google Play expansion file
(`main.110.com.square_enix.chaosringsomega.googleplay.obb`, 1,076,352,109 bytes): the game's 2125
files one after another, **with no table of its own**. The table is in `classes.dex`: a Java
class's constructor fills a `String[2125]` with the names, one const-string at a time, and two
`int[2125]` arrays (fill-array-data payloads) hold each file's size and CRC-16
(`GetSizeAndOffsetFromOBB` -> `NDKGetSizeAndOffsetFromOBB`); a file's offset is the sizes before
it added up, and they add up to the OBB exactly. `cr::ObbArchive` (`engine/src/assets/obb.cpp`)
reads the table out of your APK at run time; `crtool obb <obb> <apk> [name -o out]` lists, checks
or extracts. (The game's library links miniz, `mz_zip_reader_*`, but the OBB isn't a zip.)

The files: the multi-language 3.1.0 data (`MAPtitle_*` in eight languages, the voiced scripts,
`voicebtl.bin` / `voiceevnt.bin`) plus

- 341 `_15` pictures (the 1.5x screens' art) and `window_cutin_480 / 800 / 854 / 960 / 1024 /
  1280 / 1366`, `achievement*`;
- every texture as RGB / RGBA or an 8-bit palette - no PVRTC (Android's GPUs lack it), so the
  `_hd` art is sharper than the iPad's PVRTC (the formats: RGB888 12, RGBA8888 9, PAL8_RGB8 202,
  PAL8_RGBA8 551, PAL8_RGB5A1 7, PAL8_RGBA4 522, PAL4_RGB8 3, PAL4_RGBA8 17);
- the music as `j001m01.mca` .. `j001m30.mca` and `j001m41` .. `49` (38: no 07): Ogg Vorbis after a 1 KB header (`setbgm_caf` builds
  the names, `SLBgmPlay` opens the file at +0x400) - the iOS release's `bgm*.caf`; the other `.mca`
  files are animations, as on iOS.

`--android <folder | main.*.obb>` (or `CR_ANDROID`, or Options, Game, Android Assets; saved as
`android=`) adds it as a source: a picture the data lacks, or one of the same size, comes from
it, and a model package's textures when it holds as many of the same sizes (the meshes stay the
data's); the music plays from its Ogg Vorbis tracks (stb_vorbis) before the iPad's AAC (the
Vita's ATRAC9 still first), looping at the Android build's own points - `setbgm_caf`'s table at
0xaed80, the sample `SndBgmPlayerCallback` seeks back to (-1: bgm30 plays once) - and it fills
bgm10, bgm11 and bgm29, which the iPad's data lacks. The folder must hold the OBB and the APK (or the APK's `classes.dex`); the one whose table adds up to the OBB is the
one used.

## The screen

- `is_iPadDisplay` is always 0: Android takes the iPhone code path. `is_RetinaDisplay`: the screen
  is at least 960 x 640; `is_720x480Display`: at least 720 x 480.
- `gameapp_init_first(w, h)`: the 2D scale is 2.0 (Retina; `_hd` art), 1.5 (720 x 480 and up;
  `_15` art: `window_15`, `window_char_15`, `chara1..3_15`, `title1/2_15`) or 1.0 (`window`).
  The iPhone's 480 x 320 tex2d screen is drawn at that scale and **centred**: offset
  ((w - 480 s) / 2, (h - 320 s) / 2) (gac +0x1e2688 / +0x1e268c); touches are mapped back the
  same way (`gameapp_touch`). So on a 1280 x 720 phone the game's screen is 960 x 640 at (160, 40).
- The message window's cut-in strip is drawn per screen width (`tex2d_setup_tex`): `window_cutin_480`,
  `_800`, `_854`, `_960`, `_1024`, `_1280`, `_1366` - the parts that span the whole width.
- Full-screen pictures (`MuComPictTexNoDrawFull15`) fit an 800 x 480 design to the screen,
  min(w / 800, h / 480).
- gac +0x1e26bc = w / 480 (w / 512 above 480), at most 2.

The port's Layout: Android (Options, Display, or `--layout android`) draws the iPhone layout as a
720-tall Android phone at 2x would: the 480 x 320 screen centred on a 16:9 screen (the window's
aspect in widescreen), and, with `--android`, the message window as `draw_win` draws it when it
has a strip (`read_window_flag`): `window_cutin_<w>` (the first width at least the screen's, for
a 720-tall screen of its aspect; none above 1366, where the iPhone's parchment stays) 1:1 from
the screen's left edge, its (0, 0, w, h - y) at y 2 y + 20 (`FUN_00094a70`), and over it the
parchment's name streak, `window`'s (512, 120, 196, 40) at (x, y + 16) (`FUN_00094b50`). The
pictures `MuComPictTexNoDrawFull2` fits to the screen (min(w / 480, h / 320) / 2) keep the
iPhone's place.

## Other differences

- `gameapp_prc` sets flags 0x65..0x6a (the present packs, as iOS 3.1.0 and the Vita do) and
  0xfa6 every frame.
- Battle voices (`btl_voice_*`, `voicebtl.bin` / `voiceevnt.bin`), languages
  (`NDKGetLanguage`, `IsLanguageSettingMultibyte`) - the voiced release.
- Stereoscopic 3D for the phones that had it (`getStereoDevice`, `setStereoMode`,
  `updateStereo`; `FieldScene::drawBack2` draws the painting twice).
- GREE ads (`GreeAdSetup`, `GreeMoodDialog`), achievements (`achievement_prc`), the back key
  (`gameapp_setbackkeyflag`), pause / resume.
- Names the iOS 1.0.0 symbols lack and Android exports, among them `btlcalc_attack_hit`,
  `btlcalc_zoku_dmg`, `btlcalc_rand_dmg`, `ComUty_ComPlate_GameCenter`, `chk_daibensharoom`,
  every module's `initialize_*` and `Clr*Menu`.
