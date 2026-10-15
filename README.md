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

Known so far:

- [ ] the title's emblems and other menu pieces: Omega's `window_hd.mct` sheet differs from CR1's
- [ ] `c207b.bin` doesn't load (a model format difference)
- [ ] maps, cameras, events, battles, menus: CR1's tables (map list, NPC models, items, the
      menus' layouts) are compiled in and need Omega's

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
