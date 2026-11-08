#include "game/achievements.hpp"

#include "game/genes.hpp"

namespace cr::achievements {

namespace {

int owned(const GameState& gs, int item) {
    auto it = gs.items.find(item);
    return it == gs.items.end() ? 0 : it->second;
}

// GetItmCnt over first..last: how many of them are owned
int ownedKinds(const GameState& gs, int first, int last) {
    int n = 0;
    for (int i = first; i <= last; ++i) n += owned(gs, i) != 0;
    return n;
}

// Add_Omu(0xdb): Ohm's childcare items 521..540 owned, each its step (3.1.0's table at 0xd1370,
// Android's at 0xb07fc: the same), and var 0x28
int ohmGrowth(const GameState& gs) {
    static const int kStep[20] = {5, 5, 5, 5, 5, 15, 15, 15, 15, 15, 30, 30, 30, 30, 30, 40, 40, 40, 40, 40};
    int v = 0;
    for (int i = 0; i < 20; ++i)
        if (owned(gs, 0x209 + i) != 0) v += kStep[i];
    auto it = gs.vars.find(0x28);
    return v + (it == gs.vars.end() ? 0 : it->second);
}

// GetSkillNumFromPlate(7..30): the entries known on the monsters' 24 plates (481..504; 144 in all)
int platesLearned(const GameState& gs) {
    int n = 0;
    for (int p = 7; p < 0x1f; ++p) n += plateLearned(gs, kFirstPlate + p);
    return n;
}

} // namespace

std::string Flags::hex() const {
    static const char* const kDigits = "0123456789abcdef";
    std::string s;
    for (uint8_t b : bits) s += kDigits[b >> 4], s += kDigits[b & 15];
    return s;
}

bool Flags::fromHex(const std::string& s) {
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    if (s.size() < bits.size() * 2) return false;
    std::array<uint8_t, 16> b{};
    for (size_t i = 0; i < b.size(); ++i) {
        const int hi = digit(s[2 * i]), lo = digit(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        b[i] = uint8_t(hi << 4 | lo);
    }
    bits = b;
    return true;
}

int fieldCheck(const GameState& gs, const Flags& f) {
    // The story's ten: the Progenitor's parts, Yorath, Dante and Rachel
    static const int kStory[10] = {0x48a, 0x48b, 0x4b2, 0x4b3, 0x49e, 0x49f, 0x4c6, 0x4c7, 0x4f9, 0x4f4};
    for (int i = 0; i < 10; ++i)
        if (gs.flag(kStory[i]) && !f.has(i + 1)) return i + 1;
    if (f.has(kBought) && !f.has(11)) return 11;
    if (ownedKinds(gs, 0xc9, 0x11b) >= 50 && !f.has(12)) return 12;  // weapons
    if (ownedKinds(gs, 0x11d, 0x16f) >= 50 && !f.has(13)) return 13; // armour
    if (ownedKinds(gs, 0x171, 0x1a1) >= 15 && !f.has(14)) return 14; // gems
    if (f.has(kHit5000) && !f.has(15)) return 15;
    if (f.has(kHit10000) && !f.has(16)) return 16;
    if (f.has(kHit30000) && !f.has(17)) return 17;
    const unsigned oz = unsigned(gs.oz); // (the scenario in play's: _prty + 0xc + 4 s)
    if (oz > 99999) {
        if (!f.has(18)) return 18;
        if (oz > 999999) {
            if (!f.has(19)) return 19;
            if (oz > 7769999 && !f.has(20)) return 20;
        }
    }
    if (f.has(kKills500) && !f.has(21)) return 21;
    // The Ultimate Agent, The Almighty (Striped), Garrick, Raging and Extreme Piu-Piu; Ohm's first item
    static const int kLater[6] = {0x69d, 0x69f, 0x6a0, 0x6a1, 0x6a2, 0x65f};
    for (int i = 0; i < 6; ++i)
        if (gs.flag(kLater[i]) && !f.has(22 + i)) return 22 + i;
    const int ohm = ohmGrowth(gs);
    if (ohm > 199) {
        if (!f.has(28)) return 28;
        if (ohm > 899 && !f.has(29)) return 29;
    }
    if (f.has(kPlateFound) && !f.has(30)) return 30;
    const int learned = platesLearned(gs);
    if (learned > 0x47) {
        if (!f.has(31)) return 31;
        if (learned > 0x8f && !f.has(32)) return 32;
    }
    return 0;
}

int battleCheck(const Flags& f) {
    if (f.has(kHit5000) && !f.has(15)) return 15;
    if (f.has(kHit10000) && !f.has(16)) return 16;
    if (f.has(kHit30000) && !f.has(17)) return 17;
    if (f.has(kKills500) && !f.has(21)) return 21;
    if (f.has(kPlateFound) && !f.has(30)) return 30;
    return 0;
}

int blowFlag(int damage) {
    return damage >= 30000 ? kHit30000 : damage >= 10000 ? kHit10000 : damage >= 5000 ? kHit5000 : 0;
}

bool secret(int n) { return (n >= 1 && n <= 10) || (n >= 22 && n <= 29); }

const char* fallbackName(int n) {
    static const char* const kNames[kCount] = {
        "Cavity Buster", "Jawbreaker", "Pelvic Pummeler", "Viceral Victor", "Tongue Lasher", "Spine Tingler",
        "Eye for an Eye", "Skull Reaver", "Child Combatants", "Parental Praise", "First Purchase", "Weapon Collector",
        "Armor Collector", "Gem Collector", "Royal Talon", "Overkiller", "Omnismasher", "Moneybags", "Bounty Hunter",
        "Money Miser", "Unstoppable Duo", "Gold is Good", "Smashing Stripes", "Garrick in Perpetuum",
        "Push Comes to Shove", "Beyond Berserk", "First-Time Parent", "Father Figure", "Dad of the Year",
        "Gene Therapist", "Gene Engineer", "Gene Splicer"};
    return n >= 1 && n <= kCount ? kNames[n - 1] : "";
}

} // namespace cr::achievements
