#pragma once
// Omega's ten character records (_prty + 0x4a4, 0x4c bytes each): 0..2 the scenario being played
// (Vieg, Olgar, Dante - __prty_chg copies them in from / back to its own block), 3 the guest, 4..6
// scenario 0's block and 7..9 scenario 1's. Their levels, experience and equipment, and the stats
// the menus show (pcparam_max_*): base (_pcparam) + level x 50 (HP) or x 5 (the others) + the
// equipment's bonuses (param_equip_calc) + the record's per-stat bonus: what the levels from 20 on
// add (pcparam_change_lv: leaving level L >= 20, 10 HP and 1 to each of the others), the level-up
// skills' boosts and the seeds, at most 99999 / 9999. A new game's records (prty_init /
// _prty_init_equip): each its level, weapon, armor, jewel and gene plate. The level needs
// _exp[level] experience in total (GetNextExp), to level 255.
//
// The seeds (items 3..6, `use_item`) add 10 each to Attack / Defense / Speed / Luck, into the
// same bonus (at most 9999): one more while it's below 9999 (`MuCom_GetUsedItemLimitNum`:
// (9999 - bonus) / 10 + 1; the record's +0x1c..+0x28).

#include <array>

namespace cr {

class ItemTable;

constexpr int kCharacters = 10; // the records above
constexpr int kMaxLevel = 255;
enum Stat { kHp, kMp, kAttack, kDefense, kSpeed, kLuck, kStats };
enum EquipSlot { kWeapon, kArmor, kJewel, kEquipSlots };

struct PartyMember {
    int level = 1;
    int exp = 0;
    int hp = -1, mp = -1; // current; -1 = full
    int equip[kEquipSlots] = {0, 0, 0}; // item numbers, 0 = none
    int seeds[4] = {0, 0, 0, 0};        // seed points: Attack, Defense, Speed, Luck
    int genes[4] = {0, 0, 0, 0};        // gene plates worn (game/genes.hpp): Human Genes, then 3
    int boosts[6] = {0, 0, 0, 0, 0, 0}; // level-up skills' points (164..169): HP, MP, Attack.. Luck
};
constexpr int kMaxSeedPoints = 9999;
constexpr int kSeedPoints = 10; // what one seed adds
constexpr int kMaxHpBonus = 99999, kMaxStatBonus = 9999;
constexpr int kScenarios = 2; // Select Scenario's two: their own pair, leader, bag and Oz

using Party = std::array<PartyMember, kCharacters>;

Party newParty();
// prty_init_new(scenario): that scenario's block (records 4 + 3s .. 6 + 3s) as a new game's
void newScenarioRecords(Party& p, int scenario);
// _prty_init_equip's two extra items (genes / skills) for the bag, per record (0 none)
int startItem(int record, int k);
int expForLevel(int level); // total experience level `level` needs (level 1 = 0)
// Stat `stat` with the member's equipment, seeds and level-up boosts; `swap` (if > 0) as if that
// item were worn in its slot. memberStat (game/genes.hpp) adds the skills' (the Gates).
int maxStat(const PartyMember& m, int character, Stat stat, const ItemTable& items, int swap = 0);
// _pcparam (0xd2cd0): the character's level-0 stat
int baseStat(int character, Stat stat);
// What levels 20..level added to `stat` (pcparam_change_lv)
int levelGrowth(int level, Stat stat);
// The per-stat bonus: growth, boosts and (Attack..Luck) seeds, capped
int statBonus(const PartyMember& m, Stat stat);
// How many more seeds of `stat` (kAttack..kLuck) the member can take (MuCom_GetUsedItemLimitNum)
int seedRoom(const PartyMember& m, Stat stat);
// The slot an item goes in (kind 6 / 7 / 8), -1 if it isn't equipment
int equipSlot(int kind);

} // namespace cr
