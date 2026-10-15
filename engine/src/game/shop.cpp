#include "game/shop.hpp"

#include <algorithm>
#include <cstring>

#include "core/file.hpp"

namespace cr {

namespace {

int s16(const uint8_t* p) { return int16_t(uint16_t(p[0] | p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// _kashUseItemTbl: consumables by level, {item, from level}
constexpr int kConsumables[][2] = {{1, 2},  {21, 2}, {28, 2}, {2, 3},  {22, 3}, {29, 3}, {32, 3}, {33, 4},
                                   {34, 4}, {35, 4}, {36, 5}, {37, 5}, {38, 5}, {30, 6}, {31, 6}};
// _eBuyWpnTop / _eBuyArmTop: the records' first weapon and armor; _eBuySkillTop: scenario 1's genes
constexpr int kWeaponTop[3] = {201, 257, 229}, kArmorTop[3] = {285, 341, 313};
constexpr int kSkillStock[] = {87, 83, 84, 81, 116, 88, 117, 137, 131, 190, 132, 189, 133, 134, 138};
// __GetExtraSpItemState's extras (flag 0x655, scenario 1): per record, weapon / armor; and the jewel
constexpr int kExtraWeapon[3] = {0xda, 0x112, 0xf6}, kExtraArmor[3] = {0x12e, 0x166, 0x14a}, kExtraJewel = 0x17e;

// The sell lists' item ranges per category (_ItemNoRangeTbl)
constexpr int kSellRange[kShopCategories][2] = {{1, 60}, {201, 368}, {201, 368}, {369, 418}};

int owned(const GameState& gs, int item) {
    auto it = gs.items.find(item);
    return it == gs.items.end() ? 0 : it->second;
}

} // namespace

bool ItemTable::load(const std::string& dataDir) {
    items_.clear();
    std::vector<uint8_t> f;
    if (!readFile(dataDir + "sys.evt", f) || f.size() < 0x80) return false;
    const uint32_t at = u32(&f[0x38]);
    if (!at || at >= f.size()) return false;
    uint32_t end = uint32_t(f.size()); // up to the next section
    for (size_t h = 0; h < 0x80; h += 4) {
        const uint32_t o = u32(&f[h]);
        if (o > at && o < end) end = o;
    }
    for (uint32_t p = at; p + 20 <= end; p += 20) {
        const uint8_t* r = &f[p];
        ItemParam it;
        it.price = int(u32(r));
        for (int k = 0; k < 6; ++k) it.stat[k] = s16(r + 4 + 2 * k);
        it.icon = int8_t(r[16]);
        it.kind = r[17];
        it.owners = r[19];
        items_.push_back(it);
    }
    return !items_.empty();
}

const ItemParam* ItemTable::item(int n) const {
    return n >= 1 && n <= int(items_.size()) ? &items_[size_t(n - 1)] : nullptr;
}

// MuCom_GetCollectionItemNum: how many of the collection's items (551..630) are held
int collectionCount(const GameState& gs) {
    int n = 0;
    for (int i = 551; i <= 630; ++i) n += owned(gs, i) > 0;
    return n;
}

int shopLevel(const GameState& gs, int) {
    // MuCom_GetShopBuyLevel: scenario 0 by its story flags, scenario 1 by the collection; 3..7
    if (gs.scenario == 0) {
        if (gs.flag(0x4dc) || gs.flag(0x433)) return 7;
        if (gs.flag(0x432)) return 6;
        if (gs.flag(0x431)) return 5;
        return gs.flag(0x430) ? 4 : 3;
    }
    const int n = collectionCount(gs);
    return n >= 40 ? 7 : n >= 30 ? 6 : n >= 20 ? 5 : n >= 10 ? 4 : 3;
}

std::vector<int> shopStock(const GameState& gs, const ItemTable& items, int character, int category, int level) {
    // __GetShopItemTbl: consumables to the level (and the seeds: scenario 0 once flag 0x4dc is set,
    // scenario 1 with 60 of the collection; scenario 1's genes too); level - 1 of the record's
    // weapons / armor / the jewels from 369; the extras with flag 0x655 in scenario 1
    std::vector<int> out;
    const int c = std::clamp(character, 0, 2);
    const bool extra = gs.flag(0x655) && gs.scenario != 0;
    switch (category) {
    case 0:
        for (const auto& u : kConsumables) {
            if (u[1] > level) break;
            out.push_back(u[0]);
        }
        if (gs.scenario == 0 ? gs.flag(0x4dc) : collectionCount(gs) >= 60)
            for (int n = 3; n <= 6; ++n) out.push_back(n);
        if (gs.scenario == 1) out.insert(out.end(), std::begin(kSkillStock), std::end(kSkillStock));
        break;
    case 1:
        for (int k = 0; k < level - 1; ++k) out.push_back(kWeaponTop[c] + k);
        if (extra) out.push_back(kExtraWeapon[c]);
        break;
    case 2:
        for (int k = 0; k < level - 1; ++k) out.push_back(kArmorTop[c] + k);
        if (extra) out.push_back(kExtraArmor[c]);
        break;
    case 3:
        for (int k = 0; k < level - 1; ++k) out.push_back(369 + k);
        if (extra) out.push_back(kExtraJewel);
        break;
    default: break;
    }
    out.erase(std::remove_if(out.begin(), out.end(), [&](int n) { return !items.item(n); }), out.end());
    return out;
}

std::vector<int> sellableItems(const GameState& gs, const ItemTable& items, int character, int category) {
    std::vector<int> out;
    if (category < 0 || category >= kShopCategories) return out;
    // MuCom_ShopMakeSellItemData: items and jewels whoever's; weapons / armor of their kind and
    // the record's (+0x13: owner bits, < 0 nobody's)
    const int c = std::clamp(character, 0, 7);
    for (int n = kSellRange[category][0]; n <= kSellRange[category][1]; ++n) {
        const ItemParam* p = items.item(n);
        if (!p || owned(gs, n) == 0) continue;
        if (category == 1 && p->kind != kKindWeapon) continue;
        if (category == 2 && p->kind != kKindArmor) continue;
        const int8_t own = int8_t(p->owners);
        if (category != 0 && category != 3 && !(own >= 0 && (own >> c & 1))) continue;
        out.push_back(n);
    }
    return out;
}

int maxPurchase(int price, int oz, int have) {
    if (price <= 0) return 0;
    return std::clamp(std::min(oz / price, kMaxItemCount - have), 0, kMaxItemCount);
}

void addOz(GameState& gs, int delta) {
    gs.oz = int(std::clamp<int64_t>(int64_t(gs.oz) + delta, 0, kMaxOz));
}

} // namespace cr
