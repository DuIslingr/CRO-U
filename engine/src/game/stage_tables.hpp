#pragma once
// Field-loading tables from Chaos Rings Omega HD 1.0.0's binary (generated: tools/gen_tables.py -
// do not edit; the counts are the tables' own sizes there).

namespace cr {

constexpr int kNumAreas = 11;
constexpr int kNumMaps = 215;
constexpr int kNumNpcModels = 379; // 178 field NPCs and props, then (read on) the battle models and stages
constexpr int kNumTex2dImages = 429;
constexpr int kNumActionFiles = 250;
constexpr int kNumBattleStages = 35;
constexpr int kNumBattleModels = 166;
constexpr int kNumFormationCodes = 48;
constexpr int kNumFormations = 16;
constexpr int kNumMenuPictures = 165;
constexpr int kNumOutReadPictures = 35;
constexpr int kNumPlateWords = 40;
constexpr int kNumTreasureRoutes = 4;
constexpr int kNumFootstepSets = 5;

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
extern const short kFootstepSound[5][3];

} // namespace cr
