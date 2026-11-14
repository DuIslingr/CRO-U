#pragma once
// Genes (chk_gene, MuCom_IsSkillEquip, GetSkillAddrFromPlate, btlcalc_0506, btlcalc_chk_dead).
//
// Omega: gene abilities are items 81..130 (owning one = knowing it; its battle action is the
// item + 120, its MP that action's), skills (passive) 131..198, gene plates 474..504 (_geneplate:
// 8 s16 each - three abilities, three skills); a record wears four plates (+0x30..+0x36) and an
// ability / skill counts when it is held and on one of them (chk_gene). What follows is CR1's: A plate (0xd3890, 8 s16 each) lists three abilities and three skills; plates 479 /
// 480 are the men's and the women's Human Genes (each character's own ability 81 + c and skill
// 131 + c). A character wears four plates (the save's +0x350): slot 0 the Human Genes, 1..3 gene
// plates. An ability can be used, a skill counts, when it's known and on a plate the character
// wears.
//
// Learning: a fallen monster may teach the first not known yet of the six abilities / skills it
// carries (row +0xbc any level, +0xbe from level 10, +0xc0 25, +0xc2 50, +0xc4 75, +0xc6 100),
// 80 / 40 / 30 / 10% for the attackers' average level (less 15 with skill 158) less its level
// < -4 / < 1 / < 6 / more (btlcalc_0506); and battle models 33..138 leave their gene plate
// (481 + _btlanmtbl's column 15) when the bag hasn't one (chk_dead).
// A new game (prty_init_new, 0xcb3f0): each character wears the Human Genes and knows its own
// ability and skill; the pair starts with 3 Emergency Exits and 2 Chocolates.

#include <array>
#include <vector>

namespace cr {

struct GameState;
class ItemTable;

constexpr int kGeneSlots = 4;
constexpr int kFirstPlate = 474, kPlates = 31; // Omega: 474..480 the characters' own, 481..504
constexpr int kFirstAbility = 81, kLastAbility = 130, kFirstSkill = 131, kLastSkill = 198;

inline bool isPlate(int item) { return item >= kFirstPlate && item < kFirstPlate + kPlates; }
inline bool isGeneAbility(int item) { return item >= kFirstAbility && item <= kLastAbility; }
inline bool isSkill(int item) { return item >= kFirstSkill && item <= kLastSkill; }
inline int geneAction(int ability) { return ability + 120; }

// The plate's six entries (abilities then skills, 0 = none); empty for a non-plate
const std::array<int, 8>& plateContents(int plate);
// How many of the plate's entries are known
int plateLearned(const GameState& gs, int plate);
// The abilities character c can use (known, on a worn plate; the Human Genes' only c's own)
std::vector<int> usableAbilities(const GameState& gs, int character);
bool hasSkill(const GameState& gs, int character, int skill);
// chk_gene2: gear that resists an element counts as its skill (148 Blaze / 149 Aqua / 150 Gale
// Resistance): offsets 21..26 of each 28-item set (201..368) - 0 none
int armorSkill(int armor);
// pcparam_max_*: the stat with equipment, seeds, boosts and the Gates' (175..177 Attack, 178..180
// Defense, 181..183 Speed, 184..186 Luck: each known stage worn adds its item's own stat; HP and
// MP take none), clamped as the original's (1..99999 HP, 1..9999 the rest)
int memberStat(const GameState& gs, int character, int stat, const ItemTable& items, int swap = 0);
// pcparam_change_lv's level-ups with skills 164..169 worn: +30 HP / +3 MP / +5 Attack, Defense, Speed, Luck a level
void levelUpBoosts(GameState& gs, int character, int levels);
// The gene plate a monster of battle model `model` leaves (0 none)
int plateDrop(int model);
// __prty_chg(s): the records in play back to their scenario's block, then scenario s's in; and the
// bag and Oz swapped with the other scenario's (its own kinds: everything but genes, skills and
// kinds 9 / 10)
void changeScenario(GameState& gs, const ItemTable& items, int scenario);
bool sharedItem(const ItemTable& items, int item); // kinds 4, 5, 9, 10: one count for both scenarios
// prty_init_new's genes and items; ensureStartingGenes only adds what an older save lacks
void giveStartingItems(GameState& gs, const ItemTable& items);
// _GetItmCnt: the other scenario's Human Genes plates (scenario 0: 477..480, 1: 474..476) count none
bool otherScenarioPlate(const GameState& gs, int item);
// An old save's one shared set of entries: each scenario keeps it, learned apart from here on
void splitSharedGenes(GameState& gs, const ItemTable& items);
// add_item while scenario `scenario` is set: its own bag, or the shared one
void addScenarioItem(GameState& gs, const ItemTable& items, int scenario, int item, int count);
void ensureStartingGenes(GameState& gs);
// Omega keeps worn equipment in the bag (prty_init's add_item2 of each record's weapon, armor,
// jewel; __ChangeEquipItem only sets the slot): copies the pair wears (and scenario 1's third
// record), which can't be sold (MuScrList_GetSellNum), and a save from before the port did the same
// brought in line
int wornCount(const GameState& gs, int item);
// Add_Omu(0xdb): Ohm's growth (items 521..540 owned, var 0x28) - item 219's Attack over its 50
int ohmGrowth(const GameState& gs);
void putWornInBag(GameState& gs, const ItemTable& items);

} // namespace cr
