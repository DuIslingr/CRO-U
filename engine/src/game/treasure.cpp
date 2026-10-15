#include "game/treasure.hpp"

#include <algorithm>

#include "game/stage_tables.hpp"

namespace cr {

namespace {

uint32_t next(uint32_t& rng) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 16;
}

// PickUpNpcTbl(list, from, to, count)
void pickUp(const EventFile& file, int from, int to, size_t count, uint32_t& rng, std::vector<int>& out) {
    std::vector<int> found;
    for (int id = from; id < to; id += 2) {
        NpcRow r;
        if (file.npcRow(id, r) && (r.box & 1)) found.push_back(id);
    }
    while (found.size() > count) found.erase(found.begin() + long(next(rng) % found.size()));
    out.insert(out.end(), found.begin(), found.end());
}

} // namespace

std::vector<TreasureBox> selectTreasureBoxes(const EventFile& file, uint32_t seed, int level, int route) {
    uint32_t rng = seed * 2654435761u + 1;
    std::vector<int> ids;
    pickUp(file, 100, 140, 7, rng, ids);
    pickUp(file, 140, 180, 8, rng, ids);

    level = std::clamp(level, 0, 9);
    route = std::clamp(route, 0, 3);
    const int table = int(next(rng) % 3);
    std::vector<int> pool(kTreasureItems[table][level], kTreasureItems[table][level] + 13);
    pool.push_back(kTreasurePairs[table][level][route][0]);
    pool.push_back(kTreasurePairs[table][level][route][1]);
    for (size_t i = pool.size(); i > 1; --i) std::swap(pool[i - 1], pool[next(rng) % i]);

    std::vector<TreasureBox> out;
    for (size_t i = 0; i < ids.size(); ++i) out.push_back({ids[i], i < pool.size() ? pool[i] : 1});
    return out;
}

std::vector<TreasureBox> selectTreasureBoxesOmega(const EventFile& file, const std::function<int(int)>& random, int band,
                                                  int scenario) {
    auto pick = [&](int from, int to, size_t count) { // PickUpNpcTbl
        std::vector<int> found;
        for (int id = from; id < to; id += 2) {
            NpcRow r;
            if (file.npcRow(id, r) && (r.box & 1)) found.push_back(id);
        }
        while (found.size() > count) found.erase(found.begin() + random(int(found.size())));
        return found;
    };
    std::vector<int> ids = pick(100, 140, 7);
    const std::vector<int> more = pick(140, 180, 8);
    ids.insert(ids.end(), more.begin(), more.end());
    const int table = random(3);
    const int level = std::clamp(band, 0, 9);
    const int pair = std::clamp(scenario, 0, kNumTreasureRoutes - 1);
    struct Item { int slot, item; };
    std::vector<Item> items;
    for (int k = 0; k < 13; ++k) items.push_back({k, kTreasureItems[table][level][k]});
    items.push_back({13, kTreasurePairs[table][level][pair][0]});
    items.push_back({14, kTreasurePairs[table][level][pair][1]});
    const int n = int(items.size());
    for (int d = 0; d < n; ++d) { // taken out, then hooked in before a random one of the rest
        const Item x = items[size_t(d)];
        items.erase(items.begin() + d);
        items.insert(items.begin() + random(n - 1), x);
    }
    int last = 0; // GetLastEvtNo
    for (NpcRow r; file.npcRow(last + 1, r);) ++last;
    std::vector<TreasureBox> out;
    for (size_t i = 0; i < ids.size() && i < items.size(); ++i) { // AddInfoList
        int id = ids[i];
        if (std::clamp(band, 0, 19) > 1 && items[i].slot >= 9) {
            const int gold = id < 140 ? id + 700 : id - 80;
            NpcRow g;
            if (gold <= last && file.npcRow(gold, g) && (g.box & 2)) id = gold;
        }
        out.push_back({id, items[i].item});
    }
    return out;
}

int treasureItem(const std::vector<TreasureBox>& boxes, int id) {
    for (const TreasureBox& b : boxes)
        if (b.id == id) return b.item == -1 ? 1 : b.item; // (getItemNo: 1 when taken or not found)
    return 1;
}

void applyTreasureFlags(const EventFile& file, const std::vector<FieldNpc>& npcs, const std::vector<TreasureBox>& selected,
                        GameState& state, bool byPick) {
    const std::vector<int>& opened = state.openedBoxes;
    const int areaKey = file.area() * 1000;
    for (int f = 3100; f < 3260; ++f) state.setFlag(f, false);
    for (const FieldNpc& n : npcs) {
        if (!(n.treasure & 3) || (n.id >= 180 && n.id < 800)) continue;
        const auto it = std::find_if(selected.begin(), selected.end(), [&](const TreasureBox& b) { return b.id == n.id; });
        if (it == selected.end()) continue;
        state.setFlag(n.flagShow, true);
        if (byPick) { // (Omega's setup: taken when its pick says so)
            NpcRow lid;
            if (it->item == -1 && file.npcRow(n.id + 1, lid)) state.setFlag(lid.flagHide, true);
            continue;
        }
        // The saved entry is {box id, item}; item 0xFFFF = taken, which hides the box's lid
        NpcRow lid;
        if (std::find(opened.begin(), opened.end(), areaKey + n.id) != opened.end() && file.npcRow(n.id + 1, lid))
            state.setFlag(lid.flagHide, true);
    }
}

} // namespace cr
