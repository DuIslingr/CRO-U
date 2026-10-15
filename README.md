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
- [x] the menus' coordinates: Omega's iPad build is the iPhone layout doubled and centred
      (`tex2d_add`: screen = 2 x + (32, 64)); menu sounds are CR1's + 0x91
- [x] Select Scenario (`__menuu_Start*`: the scenario's art and banner, one plate, Back) and the
      title's save / load screen over it
- [x] the opening as the reference video plays it: painted rooms' `FrmPanCam` moves from
      `GetScroll`'s unclamped value, camera-wise (up = + y); the leader's model as `Set_Prtyno`
      (f009a / f009b, f010, f011)
- [x] rooms' paintings and overlays as their stage records name them (+60 / +62 of the record)
- [x] scene effects as Omega's `ScnEffectRead` / `Proc`: eleven sets, effects 2..30 (weather, the
      elevator shaft, the space backdrop, the battleship's sky behind its painting) and effect 1
      (syuutyuusen's concentration lines, every other frame); d03's fireflies and d06's smoke
      blend additively
- [x] a new game reaches the opening; with Omega's data the self-test loads all 373 models and
      spawns and walks the player on all 125 fields
- [x] the party as Omega keeps it (`_prty`): ten records (the scenario's three in play, a guest,
      each scenario's block), two scenarios with their own pair, leader, bag and Oz;
      `prty_init` / `prty_init_new`, `__prty_chg`, Omega's `_pcparam`, starting gear and 255-level
      `__exp`
- [x] battles on Omega's data: its 220-byte monster rows (growth, model scale, AI lists, skills,
      counters, drops, steals, genes, plates by species from `_btlanmtbl`), the pair and story
      line-ups of `btl_prty_init`, the party's models and names, cameras on the actors' node 1003;
      the opening's deck battle (group 737) frames as the reference does
- [x] the Camp: main screen (five plates, the Scenarios / Collection buttons, the scenario's
      boards), Omega's eight item categories, the Status page (portrait and state board), the
      equipment's seven slots (the Human Genes slot takes the characters' own plates); the shop
      (Omega's levels, stock and sell lists); the World Select (each scenario's worlds, bands -
      which set the scenario's level byte for enemies and treasure -, the skip question)
- [x] Omega's genes (plates 474..504, skills to 198, `_geneplate`), its 38-byte battle actions,
      the story battles' talks and HP floors (`chk_karaok_*`), the defeat warp, the encounter
      level, battle voices' speakers (3.1.0 / Vita), the talk window's parchment (`window_cutin`)
- [x] native calls 2036..2047 (SetPrtyNo, SetPrtyLv_Add, TBox, SaveEvent, Chk / Chg_BtlFld,
      Chg_MsgSkip, Chg_char, Add_OMU_ITEM, NpcShadow_onoff, puzzle_teleport_cnt, EquipAcce) and
      2015 FireWall
- [x] puzzle rooms' stages (16, maps 153..168) from Omega's `PUZZLE_ROOM_DATA`, played through in
      the self-test on Omega's own rooms (teleport, psychokinesis, toughness, pyrokinesis: each to
      its crystal or its gem; the banner on flag 3090); `--puzzle-dump` lists every stage's objects
- [x] the story plays by itself (`--explore`): the first scenario from the opening through all six
      worlds (100 rooms, 22 battles, the puzzle rooms taken as solved) to its ending and staff roll;
      the second (`--explore n 1`) through its seven worlds to its ending (flag 32) - the Scenarios
      cards it fills from the Camp give the points the last world needs (var 0x16, the presents a
      full collection would hold taken as found once it's out of ideas)
- [x] the Scenarios screen as Omega's bingo cards (`__menuu_Sccnarios*`): the faces and both
      Achievement rates, the cards and their pictures, cells opened from the Camp with presents;
      the Collection (`__menuu_Collection*`: weapons, armor and jewels seen, the rate's rank)
- [x] Select Scenario's second page (flag 31): pulled down by its cord, falling in with its bounce,
      the music changing with the page
- [x] worn equipment stays in the bag, as Omega's `prty_init` / `__ChangeEquipItem` have it
- [x] the bosses' staged actions (`btlact_main`'s kinds 10..22: 430..443, 509) with Omega's
      effects, sounds and camera chains, and the story battles' set moves (`_btlque_special`);
      groups 0x291 / 0x22c's extra model (`read_btlcpl`: m127 at (500, 0, 465), its slot 14 in 440)
- [x] Omega's own battle calcs (38..44: the kiss, Down and the Ward, 437's blow, MP / HP x power),
      its stat formula (`btlpcparam_*` with Down / the Ward) and warrior / sorcerer by character
- [x] level-ups as `pcparam_change_lv` has them (from 20: 10 HP and 1 to each other stat a level;
      the level-up genes 30 HP / 3 MP / 5), seeds 10 a seed to 9999 (`use_item`,
      `MuCom_GetUsedItemLimitNum`); group 695's guest takes orders and leaves the enemies' levels to
      Olgar's; the save screen's load flags (31 / 32) and clear data; gold boxes (`Uty_ChkItem`, key 2)
- [x] Omega's text at its own size (PutMsg's lines doubled with the rest of its tex2d; names at
      about three quarters), the item band (`Uty_GetItem_*`), SD full-screen cards (the second
      scenario's notice), Select Scenario's card (flag 0x643)
- [x] the Vita, as an add-on (`--vita`) or alone (`--data <PCSG00499>`): its scripts and eight
      languages, voices (bank order: `_no_silent` then the clips by name) and music, all of its
      textures (P8 with an ARGB palette for the effects), its boot logos; alone it runs as the Vita
      does - the iPhone's Retina path (`_is_iPadDisplay` 0, `_is_RetinaDisplay` 1, the `_hd` sheets)
      on its 960 x 544 screen (Layout "Vita": the 2D at 0.85, the paintings, script pictures and
      portraits at full size, the battle over the whole screen)
- [x] the self-test runs clean on Omega's data (its checks moved onto Omega's rooms, groups,
      scripts and tables)

Next:


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
