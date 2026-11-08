#pragma once
// Achievements (the original's: iOS 3.1.0 reports them to Game Center, Android 1.1.6 shows its own
// banner and list; docs/ENGINE.md "Achievements").
//
// 32 of them, each a "system config" flag 1..32 (SetSystemConfFlag / ChkSystemConfFlag: a bit array
// of the system data - Android's system.dat at gac +0x87604, iOS's at +0x87210 -, global, not per
// save; resetAchievements clears 0..0x40). Flags 0x21..0x40 hold what the battles saw for the
// conditions that can't be read from the save later (btlcalc_chk_dead): 0x2f / 0x30 / 0x31 a blow of
// 5,000.. / 10,000.. / 30,000.. on a monster, 0x35 the 500th monster down (var 0x17), 0x3e a gene
// plate dropped; and 0x2b a purchase (the shop's buy, __menuu_ShopPrc 7).
//
// fld_checkAchievements (3.1.0 0x8fb94, Android 0x235a8 - the same chain): the first achievement in
// the order below whose condition holds and which isn't earned yet, one at a time. Its callers in
// 3.1.0: __EvtReturn / the event end at 0x23cd4 (a script's end, unless a tutorial is kicked), the
// shop's buy (after setting 0x2b); FUN_0008fa80 (3.1.0's btl_checkAchievements: Android's is empty)
// from btlcalc_chk_dead checks the battle's five. Android also runs it again when a banner closes
// (achievement_prc), which shows the next one.

#include "game/event_vm.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace cr::achievements {

constexpr int kCount = 32;
// System config flags the conditions read (set by the battles and the shop)
constexpr int kBought = 0x2b, kHit5000 = 0x2f, kHit10000 = 0x30, kHit30000 = 0x31, kKills500 = 0x35, kPlateFound = 0x3e;
constexpr int kKillVar = 0x17; // monsters defeated (btlcalc_chk_dead: +1 to 9999)

// The system config flags (0..127; 1..32 the achievements)
struct Flags {
    std::array<uint8_t, 16> bits{};
    bool has(int n) const { return n >= 0 && n < 128 && (bits[size_t(n >> 3)] >> (n & 7) & 1); }
    void set(int n) { if (n >= 0 && n < 128) bits[size_t(n >> 3)] |= uint8_t(1u << (n & 7)); }
    void clear(int n) { if (n >= 0 && n < 128) bits[size_t(n >> 3)] &= uint8_t(~(1u << (n & 7))); }
    void reset() { for (int n = 0; n <= 0x40; ++n) clear(n); } // resetAchievements
    int earned() const { int k = 0; for (int n = 1; n <= kCount; ++n) k += has(n); return k; }
    // settings.ini's achievements=: 32 hex digits, byte 0 first
    std::string hex() const;
    bool fromHex(const std::string& s);
};

// fld_checkAchievements: the achievement to earn now (1..32), 0 none
int fieldCheck(const GameState& gs, const Flags& f);
// 3.1.0's battle check (FUN_0008fa80): 15, 16, 17, 21 or 30 by the battle's flags, 0 none
int battleCheck(const Flags& f);
// btlcalc_chk_dead's blow on a monster (target 2..4, a hit not of the kill type): the flag it sets, 0 none
int blowFlag(int damage);

// The texts (system messages, font block 10 - 3.1.0's data and Android's; 1.0.0's data has none):
// checkAchievements' header 0x101 and name (n + 0x7f) * 2; the Android list's lines
inline int headerId() { return 61257; }
inline int nameId(int n) { return 61256 + 2 * (n - 1); }
inline int hintId(int n) { return 61320 + n - 1; }        // not earned yet (the list)
inline int descriptionId(int n) { return 61352 + n - 1; } // earned
// Android's list hides these until earned (InitStoreMenu's table at 0xb9194: the story's ten and eight)
bool secret(int n);
// English names when the data has none (1.0.0's): the 3.1.0 data's own
const char* fallbackName(int n);

// achievement_prc (Android 0x23a68), one 20 Hz frame at a time: a level 0..0x1000 in steps of
// 0x400 - in over four frames (SE 0xf7 on the first), held until a tap, out over four. Drawn with
// alpha ((level >> 4) * 0xc0 / 0xff) / 255; the two lines only while held
struct Banner {
    bool on = false;
    int id = 0, state = 0, level = 0;
    void start(int n) { on = true, id = n, state = 0, level = 0; }
    enum Event { None, Sound, Closed };
    Event step(bool tap) {
        if (!on) return None;
        Event e = None;
        if (state == 1) {
            if (tap) state = 2;
        } else if (state == 2) {
            level -= 0x400;
        } else {
            if (level == 0) e = Sound;
            level += 0x400;
            if (level > 0xfff) state = 1;
        }
        if (level < 1) { on = false; return Closed; }
        return e;
    }
    int alphaByte() const { return (level >> 4) * 0xc0 / 0xff; }
    bool lines() const { return on && state == 1; }
};

} // namespace cr::achievements
