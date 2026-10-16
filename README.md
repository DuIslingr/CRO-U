# CRO-U — a Chaos Rings Omega reimplementation

A from-scratch native port of *Chaos Rings Ω (Omega)* (Square Enix / Media.Vision, 2011), built by
reverse-engineering the iPad's *Chaos Rings Omega HD* v1.0.0, with the PlayStation Vita release
(PCSG00499) as an add-on for its voices, music, languages and art, or on its own. It grew from
**[CR1:U](https://github.com/DuIslingr/CR1-U)**, the reimplementation of the first game: Omega runs
on the same engine, and its data uses the same formats. It loads the game from **your own copy**
at runtime.

**No game data is in this repository or distributed with it** - no art, audio, text, scripts or
code of the game, and nothing generated from them. You need your own copy of the game. The
engine's source does hold facts read out of the originals (file names, flag numbers, screen
positions, formula constants) where it needs them.

Licence: GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`).

## Quick start

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/chaosrings --data "<path to>/ChaosRingsOmegaHDeng.app/data"
build/chaosrings --data "<path to>/ChaosRingsOmegaHDeng.app/data" --vita "<path to>/PCSG00499"
build/chaosrings --data "<path to>/PCSG00499"
```

`--android <folder>` adds the Android release's expansion file (a folder holding its
`main.*.obb` and APK): its sharper textures and the art only it has (`docs/ANDROID.md`).

Either iPad release works as the game: 1.0.0, or the voiced 3.1.0 (Japanese voices, eight
languages) with the same layout and art.

The build is CR1:U's (C++17, SDL3, Vulkan or OpenGL; `docs/ENGINE.md`, "Building"). Without
`--data` it looks for `CR_DATA`, then `dataHD/` beside the executable, then `dataHD/`, `data/`,
`ChaosRingsOmegaHDeng.app/data/` or `Payload/ChaosRingsOmegaHDeng.app/data/` under the current
directory. Settings and saves go to the `ChaosRingsOmegaHD` profile folder
(`~/.local/share/preservation/ChaosRingsOmegaHD/` on Linux), apart from CR1:U's.

The Vita copy alone runs as the Vita does: the iPhone's Retina layout on its 960 × 544 screen
(Options, Display, Layout: Vita - also `--layout vita` over the iPad's data).

## Controls

A controller plays as the Vita does. The face buttons are by position, so on an Xbox pad ○ is B,
× is A, △ is Y and □ is X. ○ confirms and × cancels, as in the Japanese release; Options, Game,
"Swap ○ and ×" turns that round. Tested with a
Steam Controller (through Steam Input).

| Button | On the field | In menus and dialogue | In puzzle rooms |
|---|---|---|---|
| Left stick / D-pad | walk (the stick pushed far runs) | move the cursor (held, it repeats) | walk |
| ○ | examine / talk | confirm, turn the page | examine |
| × | - | cancel, back | - |
| △ | open the Camp | close the Camp | open the Camp |
| □ | - | skip a scene's text (until a choice) | use the pair's ability |
| L / R | - | previous / next page | - |
| START | Options | close Options | restart the puzzle |
| SELECT | the corner bar (field menu) | - | the puzzle's help |

| Keyboard / mouse | |
|---|---|
| W A S D (Shift runs) | walk; in menus, the cursor as the arrow keys |
| E / Enter / Space | examine, confirm, turn the page |
| Esc / Backspace | cancel, back, answer No (on the field itself Esc quits) |
| K | the Camp |
| R | the corner bar |
| X | skip a scene's text |
| V | Options |
| PgUp / PgDn (Q / E in Options) | pages |
| T, F5, F6 | puzzle rooms: the ability straight ahead, retry, help |
| F3 | the status line and key help (hidden in Release builds) |
| Mouse | everything the original takes by touch: plates, buttons, the dialogue, the corner button; drag to push a block in puzzle rooms |

## Status

Both scenarios play from the opening to their endings. Checked against Omega's own code so far
(`docs/STATUS.md` has each item and the functions it comes from):

- **Data**: Omega's tables from its binary (11 areas, 215 maps, its models, images, actions, battle
  stages and monsters); its `_ipad` / `_hd` art, menus at the iPad's doubled iPhone layout, text at
  its own sizes, its menu sounds
- **Story**: the title, Select Scenario (both pages), the opening as the reference shows it, the
  party as Omega keeps it (ten records, two scenarios with their own pair, bag and Oz), the painted
  rooms' paintings, overlays, camera pans and scene effects, the puzzle rooms (Omega's 16 stages),
  gold boxes, the natives the scripts call
- **Battles**: Omega's monsters, line-ups, genes (plates 474..504), actions and calcs, stat
  formula, level-ups and seeds, the bosses' staged actions and set moves, the story battles' talks
  and HP floors, battle voices' speakers; its damage, resistances, dodges and counters, the
  barrier's reflection, skills 187..190, turn order, the AI, and the results screen
- **Menus**: the Camp (its plates, eight item categories, Status, seven equipment slots), the shop,
  the World Select with its bands, the Scenarios bingo cards and the Collection, save / load, the
  field menu and the map screen (D01..D04 and the maze's three pages), Options and the
  Instruction Manual (the manual items held), the staff roll; the message codes
- **The Vita**: as an add-on or as the game - its scripts and eight languages, voices, music, all
  of its textures, its boot logos and its 960 × 544 screen layout
- **The collection**: all 80 presents can be found - the gold boxes (keys from the shop), the
  maze's trials and its post-game extras (open as 3.1.0 and the Vita have them); the first four
  worlds' boxes drawn again on every World Select entry, as Omega's TreasureBoxManager does
- **Testing**: `--selftest` runs clean on Omega's data (iPad 1.0.0 and 3.1.0, iPad + Vita, Vita
  alone); `--explore n [scenario]` plays either scenario by itself to its ending and staff roll -
  the second finding all 80 presents on the way

What isn't listed is CR1:U's behaviour still, to be checked against Omega's code.

## Reverse-engineering groundwork

Three builds are the reference, all from your own copies (nothing of them is committed):

| Build | Binary | Functions | Names |
|---|---|---|---|
| iPad HD 1.0.0 (2011) | `ChaosRingsOmegaHDeng`, armv7, unencrypted | 3083 | its own symbols (12965) |
| iPad HD 3.1.0 (2013): voiced, 8 languages | the same, stripped | 2930 | 1928 carried over from 1.0.0 |
| Vita PCSG00499 | `eboot.bin` (SELF) -> ELF, Thumb-2 (SNC) | 4259 | 905 from both iPad builds, 341 imports by NID |

`tools/re_setup.sh <1.0.0 .app> <3.1.0 .app> <PCSG00499>` builds all of it into `re/` (git-ignored):
the 1.0.0 disassembly, then a Ghidra project per build (`re/ios`, `re/ios310`, `re/vita`, each with
every function's C), names carried between builds by `tools/ghidra/match_functions.py`.

## Documentation and layout

| Path | Purpose |
|---|---|
| `docs/STATUS.md` | each part of the port and the original's functions it follows |
| `docs/ENGINE.md` | the engine's notes inherited from CR1:U: building, tools, the viewer, CR1's status |
| `docs/FORMATS.md` | the file formats (CR1's; Omega's differences added as they're found) |
| `docs/ANDROID.md` | the Android release's APK: what it holds, its screen layout (`tools/re_setup_android.sh`) |
| `engine/` | the C++17 / SDL3 engine and `crtool` |
| `tools/` | analysis / extraction tools; they work on your own copy |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |
