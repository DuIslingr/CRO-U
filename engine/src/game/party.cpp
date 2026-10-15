#include "game/party.hpp"

#include <algorithm>

#include "game/shop.hpp"

namespace cr {

namespace {

// _pcparam: HP, MP, Attack, Defense, Speed, Luck
constexpr int kBase[kCharacters][kStats] = {
    {700, 24, 133, 177, 153, 134}, {900, 154, 195, 167, 123, 77}, {615, 233, 92, 80, 249, 120},
    {710, 110, 140, 110, 200, 90}, {700, 24, 133, 177, 153, 134}, {900, 154, 195, 167, 123, 77},
    {615, 233, 92, 80, 249, 120},  {700, 24, 133, 127, 153, 84},  {900, 154, 195, 167, 123, 77},
    {615, 233, 92, 80, 249, 120}};

// _prty_init_equip, records 3..9: level, weapon, armor, jewel, gene plate, two items for the bag
constexpr short kStart[7][7] = {{3, 0, 0, 0, 0, 0, 0},           {1, 201, 285, 0, 474, 85, 135},
                                {5, 257, 341, 0, 476, 119, 0},   {32, 229, 313, 0, 475, 86, 136},
                                {1, 201, 285, 0, 477, 85, 135},  {1, 257, 341, 0, 479, 114, 187},
                                {1, 229, 313, 0, 478, 86, 136}};

// __exp: total experience for levels 1..255
constexpr int kExp[255] = {
    0, 4, 13, 29, 54, 90, 139, 203, 284, 384, 505, 649, 818, 1014, 1239, 1495, 1784, 2108, 2469, 2869,
    3310, 3794, 4323, 4899, 5524, 6200, 6929, 7713, 8554, 9454, 10415, 11439, 12528, 13684, 14909, 16205,
    17574, 19018, 20539, 22139, 23820, 25584, 27433, 29369, 31394, 33510, 35719, 38023, 40424, 42924,
    45525, 48229, 51038, 53954, 56979, 60115, 63364, 66728, 70209, 73809, 77530, 81374, 85343, 89412,
    93637, 97993, 102482, 107106, 111867, 116767, 121808, 126992, 132321, 137797, 143422, 149198, 155127,
    161211, 167452, 173852, 180413, 187137, 194026, 201082, 208307, 215703, 223272, 231016, 238937, 247037,
    255318, 263782, 272431, 281267, 290292, 299508, 308917, 318521, 328322, 338320, 348515, 358907, 369496,
    380282, 391265, 402445, 413822, 425396, 437167, 449135, 461300, 473662, 486221, 498977, 511930, 525080,
    538427, 551971, 565712, 579650, 593785, 608117, 622646, 637372, 652295, 667415, 682732, 698246, 713957, 729865, 745970, 762272, 778771,
    795467, 812360, 829450, 846737, 864221, 881902, 899780, 917855, 936127, 954596, 973262, 992125, 1011185,
    1030442, 1049896, 1069547, 1089395, 1109440, 1129682, 1150121, 1170757, 1191590, 1212620, 1233847, 1255271, 1276892,
    1298710, 1320725, 1342937, 1365346, 1387952, 1410755, 1433755, 1456952, 1480346, 1503937, 1527725, 1551710, 1575892,
    1600271, 1624847, 1649620, 1674590, 1699757, 1725121, 1750682, 1776440, 1802395, 1828547, 1854896, 1881442, 1908185,
    1935125, 1962262, 1989596, 2017127, 2044855, 2072780, 2100902, 2129221, 2157737, 2186450, 2215360, 2244467, 2273771,
    2303272, 2332970, 2362865, 2392957, 2423246, 2453732, 2484415, 2515295, 2546372, 2577646, 2609117, 2640785, 2672650,
    2704712, 2736971, 2769427, 2802080, 2834930, 2867977, 2901221, 2934662, 2968300, 3002135, 3036167, 3070396, 3104822,
    3139445, 3174265, 3209282, 3244496, 3279907, 3315515, 3351320, 3387322, 3423521, 3459917, 3496510, 3533300, 3570287,
    3607471, 3644852, 3682430, 3720205, 3758177, 3796346, 3834712, 3873275, 3912035, 3950992, 3990146, 4029497, 4069045,
    4108790, 4148732, 4188871, 4229207, 4269740};

PartyMember startRecord(int record) {
    PartyMember m;
    if (record < 3 || record >= kCharacters) return m;
    const short* e = kStart[record - 3];
    m.level = e[0];
    m.exp = kExp[std::clamp(m.level, 1, kMaxLevel) - 1];
    for (int s = 0; s < kEquipSlots; ++s) m.equip[s] = e[1 + s];
    m.genes[0] = e[4];
    return m;
}

} // namespace

Party newParty() {
    Party p;
    for (int c = 3; c < kCharacters; ++c) p[size_t(c)] = startRecord(c);
    for (int k = 0; k < 3; ++k) p[size_t(k)] = p[size_t(4 + k)]; // scenario 0's block in play
    return p;
}

void newScenarioRecords(Party& p, int scenario) {
    scenario = std::clamp(scenario, 0, kScenarios - 1);
    for (int k = 0; k < 3; ++k) p[size_t(4 + 3 * scenario + k)] = startRecord(4 + 3 * scenario + k);
}

int startItem(int record, int k) { return record >= 3 && record < kCharacters && k >= 0 && k < 2 ? kStart[record - 3][5 + k] : 0; }

int expForLevel(int level) { return kExp[std::clamp(level, 1, kMaxLevel) - 1]; }

int baseStat(int character, Stat stat) { return kBase[std::clamp(character, 0, kCharacters - 1)][stat]; }

int levelGrowth(int level, Stat stat) {
    const int n = std::max(0, std::clamp(level, 1, kMaxLevel) - 20); // the level-ups from 20
    return stat == kHp ? 10 * n : n;                                // 10 HP, 1 to each of the others
}

int statBonus(const PartyMember& m, Stat stat) {
    int v = levelGrowth(m.level, stat) + m.boosts[stat];
    if (stat >= kAttack) v += m.seeds[stat - kAttack];
    return std::min(v, stat == kHp ? kMaxHpBonus : kMaxStatBonus);
}

int seedRoom(const PartyMember& m, Stat stat) {
    if (stat < kAttack) return 0;
    const int v = statBonus(m, stat);
    return v < kMaxStatBonus ? (kMaxStatBonus - v) / kSeedPoints + 1 : 0;
}

int equipSlot(int kind) {
    return kind == kKindWeapon ? kWeapon : kind == kKindArmor ? kArmor : kind == kKindJewel ? kJewel : -1;
}

int maxStat(const PartyMember& m, int character, Stat stat, const ItemTable& items, int swap) {
    character = std::clamp(character, 0, kCharacters - 1);
    int equip[kEquipSlots] = {m.equip[0], m.equip[1], m.equip[2]};
    if (const ItemParam* p = items.item(swap); p && equipSlot(p->kind) >= 0) equip[equipSlot(p->kind)] = swap;
    int v = kBase[character][stat] + std::clamp(m.level, 1, kMaxLevel) * (stat == kHp ? 50 : 5);
    for (int e : equip)
        if (const ItemParam* p = items.item(e)) v += p->stat[stat];
    return v + statBonus(m, stat);
}

} // namespace cr
