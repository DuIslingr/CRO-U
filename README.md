# CRO-U — a Chaos Rings Omega reimplementation

A from-scratch native port of *Chaos Rings Ω (Omega)* (Square Enix / Media.Vision, 2011): the
iPad's *Chaos Rings Omega HD* v1.0.0, reverse-engineered from the original, with the PlayStation
Vita release (PCSG00499) as an add-on for its voices, music and art, or on its own. It begins
from **[CR1:U](https://github.com/DuIslingr/CR1-U)**, the reimplementation of the first game:
Omega runs on the same engine, and its data uses the same formats (`.mct` textures, `.bin`
models, `.mca` animations, `.cmp` collision, `.evt` scripts, `sys.evt`, `eff.bin`, `sndeff.bin`).

**No game data is in this repository or distributed with it** - no art, audio, text, scripts or
code of the game, and nothing generated from them. You need your own copy of the game; the port
loads it at runtime. The engine's source does hold facts read out of the originals (file names,
flag numbers, screen positions, formula constants) where it needs them.

Licence: GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`).

## Status

Starting point: CR1:U's engine, renamed for Omega (its own profile folder, data and Vita
lookups). With Omega's data it already boots through the boot script to the title menu, loads
944 textures and 373 models, and reads the Vita copy (2124 files, music, 2557 event and 1732
battle voice clips). Everything past the title is CR1's behaviour and still to be checked
against Omega's code: the binary (`ChaosRingsOmegaHDeng`, armv7, unencrypted, 12965 symbols)
is the reference, as 1.0.1's was for CR1.

Done so far:

- [x] Omega's tables from its own binary (`tools/gen_tables.py`, by symbol): 11 areas, 215 maps,
      379 NPC models, 429 images, 250 action files, 35 battle stages, 166 battle models, the menu
      pictures (groups 0 and 1), plate icons, treasure, footsteps
- [x] images load as `chk_hd_name` does: `<base>_ipad.mct` on the iPad, else `<base>_hd.mct`
      (the UI sheet is `window_ipad`)
- [x] the title screen as `__menuu_TitlePut` draws it (logo, Please Touch, copyright, the two
      pictures down the left, the plates)
- [x] a new game reaches the opening; with Omega's data the self-test loads all 373 models and
      spawns and walks the player on all 125 fields

Next (the CR1 self-test's checks that fail on Omega point at most of these):

- [ ] Select Scenario (Util 4, `__menuu_StartSet` / `Put` / `Prc`): Omega's own animated screen
- [ ] the other menus against Omega's `*Put` / `*Prc` (save / load, Camp, shop, World Select, ...)
- [ ] `sys.evt`'s data (items, monsters, actions) and the battle rules where Omega changed them
- [ ] puzzle rooms (Omega's `PuzzleManager`), field cameras, the story's scripts
- [ ] `c207b.bin` (a model the loader rejects)

## Reverse-engineering groundwork

Three builds are the reference, all from your own copies (nothing of them is committed):

| Build | Binary | Functions | Names |
|---|---|---|---|
| iPad HD 1.0.0 (2011) | `ChaosRingsOmegaHDeng`, armv7, unencrypted | 3083 | its own symbols (12965) |
| iPad HD 3.1.0 (2013): voiced (`voiceevnt.bin`, `voicebtl.bin`), 8 languages | the same, stripped | 2930 | 1928 carried over from 1.0.0 (74% of its bodies; 98% of call edges between matches agree) |
| Vita PCSG00499 | `eboot.bin` (SELF) -> ELF, Thumb-2 (SNC) | 4259 | 905 from both iPad builds (676 agreed by both), 341 imports by NID |

`tools/re_setup.sh <1.0.0 .app> <3.1.0 .app> <PCSG00499>` builds all of it into `re/` (git-ignored):
the 1.0.0 disassembly with import stubs named (`re/disasm.txt`, `re/symbols.txt`, `re/stubs.txt`,
`tools/dis.sh <function>`), then Ghidra (headless) per build - `re/ios`, `re/ios310`, `re/vita` each
hold `decomp.c` (every function's C), `functions.tsv` (entry, size, name, calls) and the Ghidra
project. Names move between builds with `tools/ghidra/match_functions.py` (call-graph and code
features; `--cross` for the Vita's other compiler) and `combine_names.py`, applied by
`ApplyNames.java` with a comment saying where each came from.

## Layout

| Path | Purpose |
|---|---|
| `engine/` | The C++17 / SDL3 engine, Vulkan or OpenGL |
| `docs/ENGINE.md` | The engine's notes inherited from CR1:U: building, tools, controls, CR1's status |
| `docs/FORMATS.md` | Format notes (CR1's; Omega's differences get added as they're found) |
| `tools/` | Analysis / extraction tools; they work on your own copy |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |

## Building and running

As CR1:U (`docs/ENGINE.md`, "Building"):

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/chaosrings --data "<path to>/ChaosRingsOmegaHDeng.app/data"
build/chaosrings --data "<path to>/ChaosRingsOmegaHDeng.app/data" --vita "<path to>/PCSG00499"
```

Without `--data` it looks for `CR_DATA`, then `dataHD/` beside the executable, then `dataHD/`,
`data/`, `ChaosRingsOmegaHDeng.app/data/` or `Payload/ChaosRingsOmegaHDeng.app/data/` under the
current directory. Settings, saves and HD textures go to the `ChaosRingsOmegaHD` profile folder
(`~/.local/share/preservation/ChaosRingsOmegaHD/` on Linux), apart from CR1:U's.
