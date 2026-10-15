"""Generate engine/src/game/stage_tables.{hpp,cpp} from Chaos Rings Omega HD 1.0.0's binary.

Every table is found by its symbol (the 1.0.0 binary keeps them) and sized by the gap to the next
symbol, so nothing here hard-codes an address:
  - area -> event file             _evtname     (fldread_prc)
  - area -> first map number       _stgtbl      (npc_get_stgmapctl; s16, one per area)
  - map number -> collision file   _stgname
  - NPC model index -> model file  _objname     (npc_setup) - read on past its end into
                                   _btlcplname and _btlflrname, as the game's own index does:
                                   the story scenes' boss and party models sit there
  - Tex2d / read_img_set images    _mctname     (base names: chk_hd_name picks _ipad / _hd)
  - LoadAct action files           _actfilename
  - battle stages / models         _btlflrname, _btlcplname
  - battle formations / positions  _btl_init_index, _btlinitpos
  - menu pictures                  _gstWindowMct, _gstOutReadMct (MuTex_GetTexInfo groups 0 / 1)
  - plate words' icons             _kanIconNoTbl
  - treasure item pools            _TBOX_TABLE_A/B/C, _TBOX_PAIR_TABLE_A/B/C
  - footsteps                      PlayFootstepSE's FOOTSTEP_FRAME / FOOTSTEP_SE, _FOOTSTEP_FRAME.*
Only file names and numbers are copied.

usage: gen_tables.py <ChaosRingsOmegaHDeng (1.0.0) binary> <output dir: engine/src/game>
"""
import struct
import sys


def main():
    data = open(sys.argv[1], "rb").read()
    out_dir = sys.argv[2].rstrip("/")
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off, segs, symtab = 28, [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 1:  # LC_SEGMENT
            vm, vsz, fo, fsz = struct.unpack_from("<IIII", data, off + 24)
            segs.append((vm, fo, fsz))
        elif cmd == 2:  # LC_SYMTAB
            symtab = struct.unpack_from("<IIII", data, off + 8)
        off += size

    def rd(addr, n):
        for vm, fo, fsz in segs:
            if vm <= addr and addr + n <= vm + fsz:
                return data[fo + addr - vm:fo + addr - vm + n]
        raise ValueError(hex(addr))

    def cstr(a):
        out = bytearray()
        while True:
            c = rd(a + len(out), 1)
            if c == b"\0":
                return out.decode("ascii")
            out += c

    # the symbols (nlist: strx, type, sect, desc, value), for sizes from the next one
    symoff, nsyms, stroff, _ = symtab
    syms = {}
    for i in range(nsyms):
        strx, ntype, sect, desc, value = struct.unpack_from("<IBBhI", data, symoff + 12 * i)
        if ntype & 0xe0 or not sect or not value:  # stabs, undefined
            continue
        name = data[stroff + strx:data.index(b"\0", stroff + strx)].decode()
        syms.setdefault(name, value)
    starts = sorted(set(syms.values()))

    def find(name, prefix=False):
        if prefix:
            hits = [n for n in syms if n.startswith(name)]
            if len(hits) != 1:
                raise SystemExit(f"{name}*: {hits}")
            name = hits[0]
        return syms[name]

    def size(addr):
        return next(s for s in starts if s > addr) - addr

    u32 = lambda a: struct.unpack("<I", rd(a, 4))[0]
    s16 = lambda a: struct.unpack("<h", rd(a, 2))[0]
    s32 = lambda a: struct.unpack("<i", rd(a, 4))[0]

    def names(sym, count=None):
        a = find(sym)
        return [cstr(u32(a + 4 * i)) for i in range(count if count is not None else size(a) // 4)]

    events = names("_evtname")
    areas = len(events)
    bases = [s16(find("_stgtbl") + 2 * i) for i in range(areas)]
    maps = names("_stgname")
    objects = names("_objname")
    btl_models = names("_btlcplname")
    btl_stages = names("_btlflrname")
    npc_models = names("_objname", len(objects) + len(btl_models) + len(btl_stages))  # read on, as npc_setup
    images = names("_mctname")
    act_files = names("_actfilename")
    formation = find("_btl_init_index")
    formation_codes = [s16(formation + 2 * i) for i in range(size(formation) // 2)]
    posa = find("_btlinitpos")
    formations = size(posa) // (12 * 4)
    positions = [[[s16(posa + (f * 12 + k) * 4 + c * 2) for c in range(2)] for k in range(12)] for f in range(formations)]
    wm = find("_gstWindowMct")
    menu_pictures = [[s16(wm + 8 * i + 2 * k) for k in range(4)] for i in range(size(wm) // 8)]
    kan = find("_kanIconNoTbl")
    word_icons = [s32(kan + 4 * i) for i in range(size(kan) // 4)]
    orm = find("_gstOutReadMct")
    out_pictures = [[s16(orm + 8 * i + 2 * k) for k in range(4)] for i in range(size(orm) // 8)]
    tbox = []
    for t in ("_TBOX_TABLE_A", "_TBOX_TABLE_B", "_TBOX_TABLE_C"):
        a = find(t)
        assert size(a) >= 10 * 13 * 4, t
        tbox.append([[s32(a + (row * 13 + i) * 4) for i in range(13)] for row in range(10)])
    pair = []
    for t in ("_TBOX_PAIR_TABLE_A", "_TBOX_PAIR_TABLE_B", "_TBOX_PAIR_TABLE_C"):
        a = find(t)
        routes = size(a) // (10 * 8)
        pair.append([[[s32(a + lvl * routes * 8 + r * 8 + k * 4) for k in range(2)] for r in range(routes)] for lvl in range(10)])
    run_a = find("__ZZN12_GLOBAL__N_114PlayFootstepSEEfN3Cmp5ColorEE14FOOTSTEP_FRAME")
    walk_a = find("_FOOTSTEP_FRAME.", prefix=True)
    se_a = find("__ZZN12_GLOBAL__N_114PlayFootstepSEEfN3Cmp5ColorEE11FOOTSTEP_SE")
    sets = size(run_a) // 8
    assert size(walk_a) // 8 == sets
    step_run = [[s32(run_a + (r * 2 + k) * 4) for k in range(2)] for r in range(sets)]
    step_walk = [[s32(walk_a + (r * 2 + k) * 4) for k in range(2)] for r in range(sets)]
    step_se = [[s32(se_a + (t * 3 + k) * 4) for k in range(3)] for t in range(size(se_a) // 12)]
    routes = len(pair[0][0])

    q = lambda s: '"' + s + '"'

    def strings(name, count, items, per):
        out = [f"const char* const {name}[{count}] = {{"]
        for i in range(0, len(items), per):
            out.append("    " + ", ".join(q(m) for m in items[i:i + per]) + ",")
        return out + ["};", ""]

    cpp = [
        "// Generated by tools/gen_tables.py from Chaos Rings Omega HD 1.0.0's binary — do not edit.",
        "// Field-loading tables: see docs/FORMATS.md (\"Field maps\", \"Event files\").",
        '#include "game/stage_tables.hpp"',
        "",
        "namespace cr {",
        "",
        f"const char* const kAreaEventFile[kNumAreas] = {{{', '.join(q(e) for e in events)}}};",
        "",
        f"const short kAreaFirstMap[kNumAreas] = {{{', '.join(str(b) for b in bases)}}};",
        "",
    ]
    cpp += strings("kMapCollisionFile", "kNumMaps", maps, 6)
    cpp += strings("kNpcModelFile", "kNumNpcModels", npc_models, 6)
    cpp += strings("kTex2dImage", "kNumTex2dImages", images, 5)
    cpp += strings("kActionFile", "kNumActionFiles", act_files, 6)
    cpp += strings("kBattleStageFile", "kNumBattleStages", btl_stages, 6)
    cpp += strings("kBattleModelFile", "kNumBattleModels", btl_models, 6)
    cpp += ["const short kBattleFormationCode[kNumFormationCodes] = {" + ", ".join(map(str, formation_codes)) + "};", "",
            "const short kBattlePosition[kNumFormations][12][2] = {"]
    for f in positions:
        cpp.append("    {" + ", ".join("{%d, %d}" % tuple(e) for e in f) + "},")
    cpp += ["};", "", "const short kMenuPicture[kNumMenuPictures][4] = {"]
    for i in range(0, len(menu_pictures), 6):
        cpp.append("    " + ", ".join("{%d, %d, %d, %d}" % tuple(r) for r in menu_pictures[i:i + 6]) + ",")
    cpp += ["};", "", "const short kOutReadPicture[kNumOutReadPictures][4] = {"]
    for i in range(0, len(out_pictures), 6):
        cpp.append("    " + ", ".join("{%d, %d, %d, %d}" % tuple(r) for r in out_pictures[i:i + 6]) + ",")
    cpp += ["};", "", "const short kWordIcon[kNumPlateWords] = {" + ", ".join(map(str, word_icons)) + "};", "",
            "const short kTreasureItems[3][10][13] = {"]
    for t in tbox:
        cpp.append("    {" + ", ".join("{" + ", ".join(map(str, row)) + "}" for row in t) + "},")
    cpp += ["};", "", "const short kTreasurePairs[3][10][kNumTreasureRoutes][2] = {"]
    for t in pair:
        cpp.append("    {" + ", ".join("{" + ", ".join("{%d, %d}" % tuple(r) for r in lvl) + "}" for lvl in t) + "},")
    cpp += ["};", "",
            "const short kFootstepFrameRun[kNumFootstepSets][2] = {" + ", ".join("{%d, %d}" % tuple(r) for r in step_run) + "};",
            "const short kFootstepFrameWalk[kNumFootstepSets][2] = {" + ", ".join("{%d, %d}" % tuple(r) for r in step_walk) + "};",
            f"const short kFootstepSound[{len(step_se)}][3] = {{" + ", ".join("{%d, %d, %d}" % tuple(t) for t in step_se) + "};",
            "", "} // namespace cr", ""]

    hpp = f"""#pragma once
// Field-loading tables from Chaos Rings Omega HD 1.0.0's binary (generated: tools/gen_tables.py -
// do not edit; the counts are the tables' own sizes there).

namespace cr {{

constexpr int kNumAreas = {areas};
constexpr int kNumMaps = {len(maps)};
constexpr int kNumNpcModels = {len(npc_models)}; // {len(objects)} field NPCs and props, then (read on) the battle models and stages
constexpr int kNumTex2dImages = {len(images)};
constexpr int kNumActionFiles = {len(act_files)};
constexpr int kNumBattleStages = {len(btl_stages)};
constexpr int kNumBattleModels = {len(btl_models)};
constexpr int kNumFormationCodes = {len(formation_codes)};
constexpr int kNumFormations = {formations};
constexpr int kNumMenuPictures = {len(menu_pictures)};
constexpr int kNumOutReadPictures = {len(out_pictures)};
constexpr int kNumPlateWords = {len(word_icons)};
constexpr int kNumTreasureRoutes = {routes};
constexpr int kNumFootstepSets = {sets};

extern const char* const kAreaEventFile[kNumAreas];  // area -> event file (.evt)
extern const short kAreaFirstMap[kNumAreas];         // area -> first map number in its event file
extern const char* const kMapCollisionFile[kNumMaps]; // map number -> collision file (.cmp)
extern const char* const kNpcModelFile[kNumNpcModels]; // NPC table model index -> model package
// Tex2d / read_img_set image number -> texture base name: chk_hd_name loads <base>_ipad.mct on an
// iPad when there is one, else <base>_hd.mct, else the name itself
extern const char* const kTex2dImage[kNumTex2dImages];
extern const char* const kActionFile[kNumActionFiles];  // LoadAct action number -> .mca file
// Battles: encounter stage number -> stage model; monster model number -> battle model package
extern const char* const kBattleStageFile[kNumBattleStages];
extern const char* const kBattleModelFile[kNumBattleModels];
// read_btlcpl: the enemies' size classes as decimal digits (first enemy = ones) -> index / 3 is
// the formation; positions [formation][slot][x, z]: 0-2 enemies, 3-5 where attackers stand to
// hit them, 6-11 the party
extern const short kBattleFormationCode[kNumFormationCodes];
extern const short kBattlePosition[kNumFormations][12][2];
// Menu pictures (MuTex_GetTexInfo sheet 0): x, y, w, h in window_hd.mct at half scale (the
// menus' 512x384 space; double both for the HD screen and texture)
extern const short kMenuPicture[kNumMenuPictures][4];
// MuTex_GetTexInfo group 1 (_gstOutReadMct): the pictures of the textures the menus read for
// themselves (the title's title1 / chaos in slots 0x11 / 0x12, the World Select's maps, ...)
extern const short kOutReadPicture[kNumOutReadPictures][4];
// kanIconNoTbl: a plate word's icon + 1 (0 / -1 none) - ComUty_ChoicePlateDraw draws word w with icon
// kWordIcon[w] - 1
extern const short kWordIcon[kNumPlateWords];
// TreasureBoxManager::randomSelect item pools: [table A/B/C][level 0..9][13 items] and the two
// extra items per [table][level][route]
extern const short kTreasureItems[3][10][13];
extern const short kTreasurePairs[3][10][kNumTreasureRoutes][2];
// PlayFootstepSE: step frames of the run / walk animation per set, and three sounds per floor
// surface
extern const short kFootstepFrameRun[kNumFootstepSets][2];
extern const short kFootstepFrameWalk[kNumFootstepSets][2];
extern const short kFootstepSound[{len(step_se)}][3];

}} // namespace cr
"""
    open(out_dir + "/stage_tables.cpp", "w").write("\n".join(cpp))
    open(out_dir + "/stage_tables.hpp", "w").write(hpp)
    print(f"wrote {out_dir}/stage_tables.{{hpp,cpp}}: {areas} areas, {len(maps)} maps, {len(npc_models)} NPC models "
          f"({len(objects)} own), {len(images)} images, {len(act_files)} action files, {len(btl_stages)} battle stages, "
          f"{len(btl_models)} battle models, {len(menu_pictures)} + {len(out_pictures)} menu pictures, {formations} formations, "
          f"{sets} footstep sets, {routes} treasure routes")


if __name__ == "__main__":
    main()
