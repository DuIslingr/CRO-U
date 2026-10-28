#include <map>
#include "game/genes.hpp"

#include <algorithm>
#include <unordered_map>

#include "game/event_vm.hpp"
#include "game/party.hpp"
#include "game/shop.hpp"
#include "game/stage_tables.hpp"

namespace cr {

namespace {

// _geneplate: the plates' contents
constexpr std::array<int, 8> kPlate[kPlates] = {
    {85, 114, 116, 0, 135, 187, 189, 0},    {86, 82, 117, 0, 136, 188, 190, 0},   {119, 0, 0, 0, 0, 0, 0, 0},
    {85, 87, 83, 0, 135, 137, 131, 0},      {86, 82, 84, 0, 136, 190, 132, 0},    {114, 81, 116, 0, 187, 189, 133, 0},
    {82, 88, 117, 0, 188, 134, 138, 0},     {108, 94, 113, 0, 160, 164, 156, 0},  {89, 92, 100, 0, 170, 175, 182, 0},
    {105, 90, 93, 0, 146, 176, 166, 0},     {93, 108, 110, 0, 165, 155, 147, 0},  {89, 106, 92, 0, 184, 141, 148, 0},
    {113, 103, 111, 0, 173, 157, 186, 0},   {94, 91, 100, 0, 167, 143, 150, 0},   {105, 91, 104, 0, 160, 153, 169, 0},
    {90, 93, 113, 0, 178, 142, 149, 0},     {91, 90, 89, 0, 170, 147, 158, 0},    {105, 99, 100, 0, 181, 171, 180, 0},
    {99, 107, 112, 0, 139, 179, 161, 0},    {92, 95, 107, 0, 141, 148, 154, 0},   {93, 96, 99, 0, 168, 142, 149, 0},
    {94, 97, 104, 0, 146, 143, 150, 0},     {103, 111, 109, 0, 151, 140, 172, 0}, {106, 100, 98, 0, 145, 185, 139, 0},
    {99, 106, 107, 0, 144, 183, 177, 0},    {98, 108, 109, 0, 159, 162, 174, 0},  {102, 110, 101, 0, 152, 163, 140, 0},
    {92, 97, 112, 0, 145, 180, 186, 0},     {108, 96, 110, 0, 171, 149, 172, 0},  {102, 109, 103, 0, 191, 146, 183, 0},
    {100, 95, 99, 0, 191, 141, 177, 0}};
constexpr std::array<int, 8> kNone{};

int owned(const GameState& gs, int item) {
    auto it = gs.items.find(item);
    return it == gs.items.end() ? 0 : it->second;
}

// (CR1's Human Genes kept each character's own entries; Omega's plates have no such rule)
bool ownEntry(int, int) { return true; }

} // namespace

const std::array<int, 8>& plateContents(int plate) { return isPlate(plate) ? kPlate[plate - kFirstPlate] : kNone; }

int plateLearned(const GameState& gs, int plate) {
    int n = 0;
    for (int e : plateContents(plate)) n += e > 0 && owned(gs, e) > 0;
    return n;
}

std::vector<int> usableAbilities(const GameState& gs, int character) {
    std::vector<int> out;
    character = std::clamp(character, 0, kCharacters - 1);
    for (int plate : gs.party[size_t(character)].genes)
        for (int e : plateContents(plate))
            if (isGeneAbility(e) && owned(gs, e) > 0 && ownEntry(e, character) &&
                std::find(out.begin(), out.end(), e) == out.end())
                out.push_back(e);
    return out;
}

int armorSkill(int armor) {
    const int d = armor - 201; // six sets of 28 (three weapons', three armors')
    if (d < 0 || d >= 6 * 28 || d % 28 < 21 || d % 28 > 26) return 0;
    return 148 + (d % 28 - 21) % 3;
}

bool hasSkill(const GameState& gs, int character, int skill) {
    if (skill >= 148 && skill <= 150 && armorSkill(gs.party[size_t(std::clamp(character, 0, kCharacters - 1))].equip[kArmor]) == skill)
        return true; // chk_gene_btl: the resisting armors
    if (!isSkill(skill) || owned(gs, skill) <= 0 || !ownEntry(skill, character)) return false;
    for (int plate : gs.party[size_t(std::clamp(character, 0, kCharacters - 1))].genes) {
        const auto& p = plateContents(plate);
        if (std::find(p.begin(), p.end(), skill) != p.end()) return true;
    }
    return false;
}

int memberStat(const GameState& gs, int character, int stat, const ItemTable& items, int swap) {
    character = std::clamp(character, 0, kCharacters - 1);
    int v = maxStat(gs.party[size_t(character)], character, Stat(std::clamp(stat, 0, 5)), items, swap);
    for (int skill = 175; skill <= 186; ++skill)
        if (hasSkill(gs, character, skill))
            if (const ItemParam* p = items.item(skill)) v += p->stat[std::clamp(stat, 0, 5)];
    return v;
}

void levelUpBoosts(GameState& gs, int character, int levels) {
    if (levels <= 0) return;
    for (int k = 0; k < 6; ++k)
        if (hasSkill(gs, character, 164 + k)) // pcparam_change_lv: 30 HP, 3 MP, 5 to the others a level
            gs.party[size_t(character)].boosts[k] += (k == 0 ? 30 : k == 1 ? 3 : 5) * levels;
}

bool sharedItem(const ItemTable& items, int item) {
    const ItemParam* p = items.item(item);
    return p && (p->kind == 4 || p->kind == 5 || p->kind == 9 || p->kind == 10);
}

// Vita imports retain the original scenario-local learned entries. Older native saves
// used one shared set; keep their interpretation until explicitly imported.
bool localScenarioItem(const GameState& gs, const ItemTable& items, int item) {
    if (gs.scenarioGenes)
        for (int p = 0; p < 7; ++p)
            for (int entry : kPlate[p])
                if (item > 0 && entry == item) return true;
    return !sharedItem(items, item);
}

void changeScenario(GameState& gs, const ItemTable& items, int scenario) {
    scenario = std::clamp(scenario, 0, kScenarios - 1);
    for (int k = 0; k < 3; ++k) gs.party[size_t(4 + 3 * gs.scenario + k)] = gs.party[size_t(k)];
    if (scenario != gs.scenario) {
        std::unordered_map<int, int> mine, theirs = std::move(gs.otherBag);
        for (auto it = gs.items.begin(); it != gs.items.end();)
            if (localScenarioItem(gs, items, it->first)) mine[it->first] = it->second, it = gs.items.erase(it);
            else ++it;
        for (auto& [item, count] : theirs) gs.items[item] = count;
        gs.otherBag = std::move(mine);
        std::swap(gs.oz, gs.otherOz);
        gs.scenario = scenario;
    }
    for (int k = 0; k < 3; ++k) gs.party[size_t(k)] = gs.party[size_t(4 + 3 * gs.scenario + k)];
}

// btlcalc_chk_dead: models 33..138 leave the plate of their species (_btlanmtbl [15])
int plateDrop(int model) { return model >= 33 && model <= 138 ? 481 + kBattleAnimTable[model][15] : 0; }

void addScenarioItem(GameState& gs, const ItemTable& items, int scenario, int item, int count) {
    if (item <= 0) return;
    auto& bag = !localScenarioItem(gs, items, item) || scenario == gs.scenario ? gs.items : gs.otherBag;
    bag[item] = std::min(99, bag[item] + count);
}

void giveStartingItems(GameState& gs, const ItemTable& items) {
    // prty_init: records 3..6 fill scenario 0's bag, 7..9 scenario 1's - their plates, two extras
    // and what they wear (add_item2: worn equipment stays in the bag) -, scenario 1 also gets 480, 82 and
    // 188, scenario 0 _prty_init_item_all; then plate 475 (record 6's) and 369 go again
    static const short kItemAll[] = {419, 420, 421, 422, 423, 428, 429, 430, 431, 432, 433,
                                     434, 435, 437, 438, 443, 444, 445, 459, 460};
    for (int c = 3; c < kCharacters; ++c) {
        const int s = (c - 3) / 4;
        addScenarioItem(gs, items, s, gs.party[size_t(c)].genes[0], 1);
        for (int k = 0; k < 2; ++k) addScenarioItem(gs, items, s, startItem(c, k), 1);
        for (int e : gs.party[size_t(c)].equip) addScenarioItem(gs, items, s, e, 1);
    }
    for (int i : {480, 82, 188}) addScenarioItem(gs, items, 1, i, 1);
    for (int i : kItemAll) addScenarioItem(gs, items, 0, i, 1);
    for (int i : {475, 369}) {
        auto& bag = sharedItem(items, i) || gs.scenario == 0 ? gs.items : gs.otherBag;
        if (bag.count(i) && --bag[i] <= 0) bag.erase(i);
    }
}

void ensureStartingGenes(GameState&) {} // (CR1's Human Genes; Omega's records carry their plates)

int wornCount(const GameState& gs, int item) {
    int n = 0;
    if (item <= 0) return 0;
    for (int r = 0; r < 3; ++r)
        for (int e : gs.party[size_t(r)].equip) n += e == item;
    return n;
}

void putWornInBag(GameState& gs, const ItemTable& items) {
    // Records 3.. hold each scenario's block (4..6 scenario 0, 7..9 scenario 1; 3 the guest, with
    // the one in play); 0..2 are copies of the block in play
    std::map<std::pair<bool, int>, int> need; // (the other scenario's bag?, item) -> copies worn
    for (int r = 3; r < kCharacters; ++r) {
        const int s = r == 3 ? gs.scenario : (r - 4) / 3;
        for (int e : gs.party[size_t(r)].equip)
            if (e > 0) ++need[{!sharedItem(items, e) && s != gs.scenario, e}];
    }
    for (const auto& [key, n] : need) {
        auto& bag = key.first ? gs.otherBag : gs.items;
        bag[key.second] = std::min(99, std::max(bag[key.second], n));
    }
    gs.wornInBag = true;
}

} // namespace cr
