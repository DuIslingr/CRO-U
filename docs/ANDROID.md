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

The game's data is **not** in the APK: it comes as a Google Play expansion file (`main.<version
code>.<package>.obb`, MIME `application/vnd.android.obb`), a zip the native code reads with miniz
(`mz_zip_reader_*`). Its file names are the iOS data's, plus the Android art below.

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
