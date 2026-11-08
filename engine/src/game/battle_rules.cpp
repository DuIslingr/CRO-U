#include "game/battle_rules.hpp"

#include <algorithm>
#include <cstring>

#include "assets/battle_data.hpp"
#include "core/file.hpp"
#include "game/event_vm.hpp"
#include "game/genes.hpp"
#include "game/party.hpp"
#include "game/shop.hpp"

namespace cr {

namespace {

int s16(const uint8_t* p) { return int16_t(uint16_t(p[0] | p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

constexpr int kFirstBattleItem = 21, kBattleItems = 26, kItemAction = 100; // item 21 -> action 100
constexpr int kDefendAction = 26;

int monsterField(const Fighter& f, int offset) { return f.def ? s16(&f.def->raw[size_t(offset)]) : 0; }

} // namespace

std::vector<int> analyzeElements(const Fighter& f, bool weak) {
    std::vector<int> out;
    for (int k = 0; k < 5; ++k)
        if (const int v = monsterField(f, 0x10 + 2 * k); weak ? v == 3 || v == 4 : v == 1 || v == 2) out.push_back(k);
    return out;
}

bool BattleActionTable::load(const std::string& dataDir) {
    actions_.clear();
    std::vector<uint8_t> f;
    if (!readFile(dataDir + "sys.evt", f) || f.size() < 0x80) return false;
    const uint32_t at = u32(&f[0x2c]);
    if (!at || at >= f.size()) return false;
    uint32_t end = uint32_t(f.size());
    for (size_t h = 0; h < 0x80; h += 4) {
        const uint32_t o = u32(&f[h]);
        if (o > at && o < end) end = o;
    }
    // 38-byte records (Omega's, every release: gac + 0x912f0, -0x26 + 0x26 n): six voice situations
    // at +10, CR1 1.0.1's fields from there on 12 bytes later, 2 more at the end
    const bool voiced = true;
    const uint32_t stride = voiced ? 38 : 24;
    for (uint32_t p = at; p + stride <= end; p += stride) {
        const uint8_t* base = &f[p];
        BattleActionDef a;
        a.id = int(actions_.size()) + 1;
        a.power = s16(base + 2);
        a.mp = s16(base + 4);
        a.speed = s16(base + 6);
        a.calc = s16(base + 8);
        if (voiced)
            for (int k = 0; k < 6; ++k) a.voice[k] = s16(base + 10 + 2 * k);
        const uint8_t* r = voiced ? base + 12 : base; // r[10..23]: the 1.0.1 layout
        a.icon = r[10];
        a.cls = int8_t(r[11]);
        a.scope = r[12];
        a.side = r[13];
        a.pick = r[14];
        a.alwaysHits = r[15] == 1;
        a.variance = int8_t(r[16]);
        a.motion = r[17];
        a.element = int8_t(r[18]);
        a.sureGene = r[20] == 1;
        a.affinity = r[22] == 1;
        a.hostile = r[23] == 0;
        a.dodgeable = voiced && r[24] == 0;
        actions_.push_back(a);
    }
    return !actions_.empty();
}

const BattleActionDef* BattleActionTable::get(int id) const {
    return id >= 1 && id <= int(actions_.size()) ? &actions_[size_t(id - 1)] : nullptr;
}

int BattleRules::random(int n) { return (shared_ ? *shared_ : own_).get(n); }

void BattleRules::setupParty(const GameState& gs, const ItemTable& items, int) {
    known_.clear();
    bag_.assign(48, 0);
    purse = gs.oz;
    for (const auto& [item, count] : gs.items) {
        if (count > 0 && ((item >= kFirstAbility && item <= kLastSkill) || (item >= 474 && item <= 500))) known_.push_back(item);
        if (item >= 21 && item <= 47) bag_[size_t(item)] = std::max(count, 0);
    }
    // btl_prty_init: the scenario's pair (records 0..2), the second away while flag 0xfed + s holds
    const int s = std::clamp(gs.scenario, 0, kScenarios - 1);
    for (int k = 0; k < 2; ++k) {
        const int c = k == 1 && gs.flag(0xfed + s) ? -1 : gs.pairs[size_t(s)][size_t(k)];
        member(gs, items, k, c);
    }
}

// btlpcparam_prty_init: record c's level and stats in slot k, HP full (-1: nobody)
void BattleRules::member(const GameState& gs, const ItemTable& items, int k, int c) {
    Fighter& x = f[size_t(k)];
    x = Fighter();
    if (c < 0 || c >= kCharacters) return;
    x.present = true;
    x.character = c;
    const PartyMember& m = gs.party[size_t(c)];
    x.level = m.level;
    x.hpMax = memberStat(gs, c, kHp, items);
    x.mpMax = memberStat(gs, c, kMp, items);
    x.hp = x.hpMax; // every battle starts with full HP
    x.mp = m.mp < 0 ? x.mpMax : std::min(m.mp, x.mpMax);
    x.atk = memberStat(gs, c, kAttack, items);
    x.grd = memberStat(gs, c, kDefense, items);
    x.spd = memberStat(gs, c, kSpeed, items);
    x.lck = memberStat(gs, c, kLuck, items);
    for (int sk = kFirstSkill; sk <= kLastSkill; ++sk) x.skills[size_t(sk - kFirstSkill)] = hasSkill(gs, c, sk);
}

void BattleRules::setupSpecial(const GameState& gs, const ItemTable& items, int group, int) {
    // btl_prty_init's story line-ups: scenario 0's groups 651, 654, 655, 656, 688 Vieg alone,
    // 656 / 695 Olgar alone (695 with the guest beside, at the leader's level + 5: base + level x
    // 50 / x 5 and Speed and Luck from the base Defense - btlpcparam_prty_init_special), scenario
    // 1's 675 its leader alone; group 656 starts at 70% HP
    const int s = std::clamp(gs.scenario, 0, kScenarios - 1);
    if (s == 0) {
        for (int g : {651, 688, 654, 655})
            if (group == g) member(gs, items, 0, 0), f[1] = Fighter();
        if (group == 656 || group == 695) member(gs, items, 0, 1), f[1] = Fighter();
        if (group == 656) f[0].hp = f[0].hpMax * 70 / 100;
        if (group == 695) {
            Fighter& x = f[1];
            x = Fighter();
            x.present = x.guest = true;
            x.character = 3;
            x.level = std::min(kMaxLevel, f[0].level + 5);
            auto at = [&](Stat st, int per) { return baseStat(3, st) + x.level * per; };
            x.hp = x.hpMax = at(kHp, 50);
            x.mp = x.mpMax = at(kMp, 5);
            x.atk = at(kAttack, 5);
            x.grd = x.spd = x.lck = at(kDefense, 5);
        }
    } else if (group == 675)
        member(gs, items, 0, 0), f[1] = Fighter();
}

int BattleRules::leaderLevel() const {
    // btl_enemy_init_lv: the pair's average when both fight, else the leader's (it runs before
    // btl_prty_init brings group 695's guest in)
    if (f[0].present && f[1].present && !f[1].guest) return (f[0].level + f[1].level) / 2;
    return f[0].level;
}

void BattleRules::setupEnemies(const BattleData& data, const std::array<int, 3>& ids, int band) {
    const int L = leaderLevel();
    const int hi = L > 198 ? 200 : L + 2, lo = L < 6 ? 1 : L - 5;
    band = std::clamp(band, 0, 19);
    for (int k = 0; k < 3; ++k) {
        Fighter& x = f[size_t(2 + k)];
        x = Fighter();
        const MonsterDef* m = data.monster(ids[size_t(k)]);
        if (!m || m->model < 0) continue;
        x.present = x.enemy = true;
        x.monster = m->id;
        x.def = m;
        int lv;
        if (!m->strongGrowth) {
            if (m->level >= 1) lv = m->level;
            else { // the band's ten levels, kept near the pair's
                const int top = band * 10 + 10, bottom = band * 10 + 1;
                if (lo >= top) lv = top;
                else if (bottom >= hi) lv = bottom;
                else {
                    const int a = std::max(lo, bottom), b = std::min(hi, top);
                    lv = a + random(b - a + 1);
                }
            }
        } else
            lv = m->level > 0 ? m->level : L;
        x.level = std::clamp(lv, 1, kMaxLevel);
        x.hp = x.hpMax = int(m->hpAt(x.level));
        x.atk = m->atkAt(x.level);
        x.grd = m->grdAt(x.level);
        x.spd = m->spdAt(x.level);
        x.lck = m->lckAt(x.level);
        x.exp = int(m->expAt(x.level));
        x.oz = int(m->genePointsAt(x.level));
        if (x.level >= 20) // chk_skill_btl: a monster's skills (+0xae, five) count from level 20
            for (int k = 0; k < 5; ++k)
                if (const int s = monsterField(x, 0xae + 2 * k); isSkill(s)) x.skills[size_t(s - kFirstSkill)] = true;
    }
}

// btlpcparam_atk / grd / spd / lck (Omega): the value, less a fifth while Down (+0x96), its own again
// with the Ward (+0x97); the up status adds a quarter (a fifth while Down, the whole with the Ward)
static int statWith(const Fighter& x, int v, int up) {
    int out = v, add = v / 4;
    if (x.status[kStatusDown]) add = v / 5, out = v - add;
    if (x.status[kStatusWard]) out += v, add = v;
    if (x.status[up]) out += add;
    return out;
}
int BattleRules::atkOf(int i) const { return statWith(f[size_t(i)], f[size_t(i)].atk, kStatusAttackUp); }
int BattleRules::grdOf(int i) const {
    const Fighter& x = f[size_t(i)];
    int v = statWith(x, x.grd, kStatusDefenseUp);
    if (i < 2 && x.guarding) v *= 2;
    return v;
}
int BattleRules::spdOf(int i) const { return statWith(f[size_t(i)], f[size_t(i)].spd, kStatusSpeedUp); }
int BattleRules::lckOf(int i) const { return statWith(f[size_t(i)], f[size_t(i)].lck, kStatusLuckUp); }

int BattleRules::attackAction(int fighter) const {
    // __menub_mainPrc: the pair's attack is action 0x24 (0x25 when member 1 is record 1); a member's
    // own its record + 0x1b (27..30, power 150); a monster's its species + 10 (btlque_make_que)
    if (fighter < 2 && pair) return f[1].character == 1 ? 37 : 36;
    if (fighter < 2) return f[size_t(fighter)].character + 27;
    return f[size_t(fighter)].character + 10;
}

bool BattleRules::has(int i, int skill) const {
    if (i < 0 || i > 4 || !isSkill(skill)) return false;
    if (f[size_t(i)].skills[size_t(skill - kFirstSkill)]) return true;
    return i < 2 && pair && f[size_t(1 - i)].skills[size_t(skill - kFirstSkill)];
}

int BattleRules::geneCost(const BattleActionDef& a, int user) const {
    return user < 2 && has(user, 132) ? a.mp / 2 : a.mp;
}

// btlcalc_zoku_0204: the skills' scaling of a hit
int BattleRules::zoku(int amount, const BattleActionDef& a, int user, int target, bool magic) const {
    int pct = has(target, magic ? 147 : 146) ? 75 : 100;
    if (has(user, magic ? 140 : 139)) pct += 25;
    const int e = f[size_t(user)].element;
    if (a.affinity)
        for (int k = 1; k <= 3; ++k)
            if (e == k && has(target, 147 + k)) pct -= 25; // Blaze / Aqua / Gale Resistance
    if (has(target, 151) && monsterField(f[size_t(user)], 0x38)) pct -= 25; // a herbivore's attack
    if (has(target, 152) && monsterField(f[size_t(user)], 0x36)) pct -= 25; // a carnivore's
    if (a.affinity)
        for (int k = 1; k <= 3; ++k)
            if (e == k && has(user, 140 + k)) pct += 25; // Blaze / Aqua / Gale Attack
    if (has(user, 144) && monsterField(f[size_t(target)], 0x38)) pct += 25;
    if (has(user, 145) && monsterField(f[size_t(target)], 0x36)) pct += 25;
    return pct < 0 ? 0 : pct * amount / 100;
}

std::vector<int> BattleRules::targets(const BattleActionDef& a, int user) const {
    std::vector<int> out;
    const bool party = user < 2;
    auto side = [&](bool own) {
        const bool partySide = own == party;
        for (int i = partySide ? 0 : 2; i < (partySide ? 2 : 5); ++i) out.push_back(i);
    };
    switch (a.scope) {
    case 1: out.push_back(party && pair ? 0 : user); break;
    case 2: case 3: side(true); break;
    case 4: case 5: side(false); break;
    case 6: case 7: for (int i = 0; i < 5; ++i) out.push_back(i); break;
    default: side(false); break;
    }
    const bool revive = a.id == 0x6c || a.id == 0xe6;
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](int i) { return !f[size_t(i)].present || f[size_t(i)].gone || (!revive && f[size_t(i)].hp <= 0); }),
              out.end());
    return out;
}

void BattleRules::breakEvent(int who, int event, int target) {
    const bool party = who < 2;
    if (!breakStarted) { // the battle's first event: four for whoever caused it
        breakStarted = true;
        breakTarget = party ? 4 : -4;
        if (event == 2) breakFlags |= 0x200;
        return;
    }
    int d = 0;
    switch (event) {
    case 2: // damage dealt: once an action; the holder was hit
        if (target < 2 ? breakGauge > 0 : breakGauge < 0) breakFlags &= ~0x8000u;
        d = (breakFlags & 0x200) ? 0 : 1;
        breakFlags |= 0x200;
        break;
    case 4: d = 1; break;
    case 8: { // a critical: once an action per side
        const unsigned bit = party ? 0x1000 : 0x2000;
        d = (breakFlags & bit) ? 0 : 1;
        breakFlags |= bit;
        break;
    }
    case 0x10: d = 2; break;
    case 0x20: case 0x40: d = 1; break;
    default: break;
    }
    const bool keeps = event == 0x10 || event == 0x40;
    if (party) {
        if (breakGauge > 0) d = keeps ? 1 : 0;
    } else if (breakGauge < 0)
        d = keeps ? -1 : 0;
    else
        d = -d;
    breakTarget = std::clamp(breakTarget + d, -8, 8);
    if (breakGauge < 0 && breakTarget >= 0) { // the enemies lost it
        breakTarget = 0;
        breakFlags |= 0x400 | 0x800;
    } else if (breakGauge > 0 && breakTarget <= 0) { // the pair lost it
        breakTarget = 0;
        breakFlags = (breakFlags | 0x400) & ~0x800u;
    }
    breakFlags |= unsigned(event);
}

bool BattleRules::fighterKind(int i, bool sorcerer) const {
    // is_btl_warrior / is_btl_sorcerer on the fighter's +0xa2: a member's is its record + 8 (record 0
    // 11 once Vieg and Olgar are both known): 8, 9, 11 warriors, 10 the sorcerer; 12 on (records 4..9,
    // never in play) neither
    if (i < 0 || i >= 2 || !f[size_t(i)].alive()) return false;
    const int kind = f[size_t(i)].character + 8;
    return sorcerer ? kind == 10 : kind == 8 || kind == 9 || kind == 11;
}

int BattleRules::bonusFor(const int actors[2], bool magic) const {
    const int first = actors[0];
    if (first < 2) {
        int b = breakGauge > 0 ? 75 : 0;
        if (pair) b += 25;
        else if (fighterKind(first, magic)) b += 50; // a warrior's attack, a sorcerer's gene
        return b;
    }
    return breakGauge < 0 ? 125 : 50;
}

int BattleRules::elementScale(const BattleActionDef& a, int user, int target, int& type) const {
    if (!a.affinity) return 100;
    const int ea = f[size_t(user)].element, et = f[size_t(target)].element;
    if (ea == 0 || et == 0 || ea == et) return 100;
    if ((ea == 1 && et == 3) || (ea == 2 && et == 1) || (ea == 3 && et == 2)) {
        type = kHitWeak;
        return 150;
    }
    type = kHitResist;
    return 50;
}

// btlcalc_zoku_0203: a monster's resistances - +0x10 physical / +0x12 genes, then +0x14 / 16 / 18
// by the user's element (none without one): 1 immune (0, the hit's type as it was), 2 half, 3
// x1.5, 4 instant death (`type` kHitKill, the amount its HP). Omega: then skill 187 or 188 on an
// attacker makes a weak hit (the triangle's) a kill 5% of the time, unless the row's +0x1c is set
int BattleRules::resist(const BattleActionDef&, const int actors[2], int target, int amount, bool magic, int& type) {
    if (target < 2) return amount;
    const Fighter& t = f[size_t(target)];
    const int e = f[size_t(actors[0])].element;
    for (int off : {magic ? 0x12 : 0x10, e >= 1 && e <= 3 ? 0x12 + 2 * e : -1}) {
        if (off < 0) break;
        switch (monsterField(t, off)) {
        case 1: return 0;
        case 2: amount /= 2; break;
        case 3: amount = amount * 150 / 100; break;
        case 4: type = kHitKill; return t.hp;
        default: break;
        }
    }
    if (!monsterField(t, 0x1c))
        for (int k = 0; k < 2; ++k)
            if (actors[k] >= 0 && type == kHitWeak && (has(actors[k], 187) || has(actors[k], 188)) && random(100) < 5) {
                type = kHitKill;
                return t.hp;
            }
    return amount;
}

BattleHit BattleRules::physical(const BattleActionDef& a, const int actors[2], int target) {
    BattleHit h;
    h.target = target;
    int atkSum = 0, spdSum = 0, levels = 0, n = 0;
    for (int k = 0; k < 2; ++k)
        if (actors[k] >= 0) {
            atkSum += atkOf(actors[k]);
            spdSum += spdOf(actors[k]);
            levels += f[size_t(actors[k])].level;
            ++n;
        }
    int bonus = bonusFor(actors, false);
    // skill 135: an attack (Omega: actions 10..99) is critical the first time, when every attacker has it
    bool firstCrit = a.id >= 10 && a.id < 100;
    for (int k = 0; k < 2; ++k) // (chk_skill_btl: in pair mode either member's)
        if (actors[k] >= 0) firstCrit &= has(actors[k], 135) && f[size_t(actors[k])].firstAttack;
    // btlcalc_critical: the attackers' average level against the target's
    // (against the action's first target's level, whichever target this is)
    const int dl = (n ? levels / n : 0) - f[size_t(firstTarget_ >= 0 ? firstTarget_ : target)].level;
    const int critChance = dl < -49 ? 10 : dl < 0 ? 15 : dl < 50 ? 20 : 50;
    const bool crit = random(256) < critChance || a.id == 0xdb || firstCrit;
    if (crit) {
        bonus += 50;
        h.type = kHitCritical;
        h.critical = true;
    }
    const int grd = crit && has(actors[0], 137) ? 0 : grdOf(target); // 137: a critical pierces
    int dmg = (bonus + a.power) * atkSum / 100 - grd;
    if (a.id == 236) // (0xec) the attackers' lost HP on top
        for (int k = 0; k < 2; ++k)
            if (actors[k] >= 0) dmg += f[size_t(actors[k])].hpMax - f[size_t(actors[k])].hp;
    if (crit && has(actors[0], 134)) dmg *= 2;
    if (!crit && dmg > 0) {
        const int d = spdSum - lckOf(target);
        int hit = d < -100 ? 50 : d < -49 ? 60 : d < 0 ? 70 : d < 50 ? 80 : d < 101 ? 90 : 99;
        if (a.alwaysHits) hit = 100;
        if (random(100) >= hit) {
            dmg = 0;
            h.type = kHitGuard;
        }
    }
    if (scenario == 0 && group == 693 && (a.id == 27 || a.id == 28)) dmg = 0, h.type = 0, nullHit = true; // (0x2b5: the story's, +0xa78)
    dmg = std::max(dmg, 0);
    if (h.type != kHitGuard) dmg = dmg * elementScale(a, actors[0], target, h.type) / 100;
    if (dmg >= 1) dmg = resist(a, actors, target, dmg, false, h.type);
    if (dmg >= 1 && h.type != kHitKill) dmg = zoku(dmg, a, actors[0], target, false);
    if (dodgeHit_) dmg = 0, h.type = kHitMiss, note_ = 57061; // "Neutralized physical damage!"
    h.amount = dmg;
    return h;
}

BattleHit BattleRules::magical(const BattleActionDef& a, const int actors[2], int target) {
    BattleHit h;
    h.target = target;
    int atkSum = 0;
    for (int k = 0; k < 2; ++k)
        if (actors[k] >= 0) atkSum += atkOf(actors[k]);
    int dmg = (bonusFor(actors, true) + a.power) * atkSum / 100 - grdOf(target);
    if (has(actors[0], 156)) dmg += dmg / 5; // Boost Magic Power
    if (a.id == 122 || a.id == 224) { // poison (row +0x2e: immune)
        if (!monsterField(f[size_t(target)], 0x2e)) {
            f[size_t(target)].status[kStatusPoison] = 5;
            statusTaken = true;
            notice(target, 6);
        } else
            note_ = 57074;
    }
    if (a.element > 0) // the gene's element becomes the caster's
        for (int k = 0; k < 2; ++k)
            if (actors[k] >= 0) f[size_t(actors[k])].element = a.element, elementTaken = true;
    dmg = std::max(dmg, 0);
    dmg = dmg * elementScale(a, actors[0], target, h.type) / 100;
    dmg = resist(a, actors, target, dmg, true, h.type); // (0406: even at 0)
    if (dmg >= 1 && h.type != kHitKill) dmg = zoku(dmg, a, actors[0], target, true);
    if (a.id == 241) dmg = 99999, h.type = 0; // (0xf1, 435's quick form) all of it, unblockable
    else if (dodgeGene_) dmg = 0, h.type = kHitMiss, note_ = 57062; // "... magical damage!"
    h.amount = dmg;
    return h;
}

BattleHit BattleRules::heal(const BattleActionDef& a, const int actors[2], int target) {
    BattleHit h;
    h.target = target;
    int lckSum = 0;
    for (int k = 0; k < 2; ++k)
        if (actors[k] >= 0) lckSum += lckOf(actors[k]);
    int bonus;
    if (target < 2) {
        bonus = breakGauge > 0 ? 100 : 0;
        if (fighterKind(actors[0], true) || fighterKind(actors[1], true)) bonus += 50; // (either a sorcerer)
    } else
        bonus = breakGauge < 0 ? 150 : 50;
    h.amount = -((bonus + a.power) * lckSum / 100);
    if (has(actors[0], 155)) h.amount *= 2; // Boost Recovery
    return h;
}

void BattleRules::enemyChoice(int enemy, bool asPair, Entry& e, const BattleActionTable& actions, int exclude) {
    // btlai_prc / btlai_make: the row's lists (5 actions each, +0x4a + 10 x list; the enemy
    // pair's 50 on): 0 always (weight 100), 1 when the enemies hold the Break / 2 otherwise (50),
    // 3 at a quarter of the HP or less (100), 4 every fifth round (200); "do nothing" actions
    // 413..429, 459..467 and 478..485 weigh 10 wherever they are; silenced, only attacks
    // (1..126, 261..401) and those
    const Fighter& x = f[size_t(enemy)];
    struct Choice { int action, weight; };
    std::vector<Choice> list;
    const int base = 0x4a + (asPair ? 50 : 0);
    auto add = [&](int listNo, int weight) {
        for (int k = 0; k < 5; ++k) {
            const int a = monsterField(x, base + listNo * 10 + 2 * k);
            if (a <= 0 || a == exclude) continue;
            const bool idle = a - 413u < 17u;
            const bool silenced = x.status[kStatusSilence] || (asPair && f[3].status[kStatusSilence]); // (either of the pair)
            if (silenced && (a - 261u > 140u) && (a - 1u > 125u) && !idle) continue;
            list.push_back({a, idle || a - 459u < 9u || a - 478u < 8u ? 10 : weight});
        }
    };
    add(0, 100);
    add(breakGauge < 0 ? 1 : 2, 50);
    if (x.hp <= x.hpMax / 4) add(3, 100);
    if (round % 5 == 0) add(4, 200);
    int total = 0;
    for (const Choice& c : list) total += c.weight;
    e.action = 0;
    int r = random(total);
    for (const Choice& c : list) {
        if (r < c.weight) {
            e.action = c.action;
            break;
        }
        r -= c.weight;
    }
    if (const int sp = specialAction(enemy)) e.action = sp; // (btlai_prc)
    if (forceAction > 0 && enemy == 2) e.action = forceAction;
    const BattleActionDef* a = actions.get(e.action);
    if (!a) {
        e.kind = kCmdNone;
        return;
    }
    const int c = a->cls + 1; // the kind: attacks 8-10 (and -1), genes 11 / 13 / 14, items 12
    e.kind = c < 16 && ((1 << c) & 0xe01) ? kCmdAttack : c < 16 && ((1 << c) & 0xd000) ? kCmdGene
           : c == 13                     ? kCmdItem
                                         : kCmdGene;
    e.target = aiTarget(*a, enemy);
    if (!exclude) e.repeats = std::max(1, int(x.def ? x.def->raw[0x3e] : 1));
}

int BattleRules::specialAction(int enemy) const {
    const int m = enemy >= 2 && enemy < 5 ? f[size_t(enemy)].monster : 0;
    if (scenario == 0)
        switch (group) {
        case 650: return round == 2 ? 430 : 0;                 // (0x28a) Particle Cannon
        case 655: return round == 4 ? 437 : 0;
        case 656: return round == 4 || round == 6 ? 437 : 0;
        case 657: return round == 2 ? 437 : 0;
        default: return 0;
        }
    switch (group) {
    case 663: case 665: // (0x297 / 0x299)
        if (round == 1) return m == 21 ? 243 : 0;
        if (round == 2) return m == 19 ? 242 : 0;
        return 0;
    case 664: // (0x298) monster 21: 486, 487, 488, then 241 on
        if (m != 21 || round < 1) return 0;
        return round <= 3 ? 485 + round : 241;
    case 668: // (0x29c)
        if (round == 1) return m == 22 ? 243 : 0;
        if (round == 2) return m == 20 ? 242 : 0;
        if (round == 3) return m == 22 ? 240 : 0;
        return 0;
    case 674: // (0x2a2)
        if (round >= 1 && round <= 3) return 106;
        if (round == 4) return m == 22 ? 243 : 0;
        if (round == 5) return m == 20 ? 242 : 0;
        if (round == 6) return m == 22 ? 240 : 0;
        return 0;
    case 676: return round == 1 ? 509 : round == 2 ? 508 : 0; // (0x2a4)
    case 677: return round == 1 ? 511 : round == 2 ? 510 : 0; // (0x2a5)
    default: return 0;
    }
}

int BattleRules::aiTarget(const BattleActionDef& a, int user) {
    auto alive = [&](int i) { return f[size_t(i)].alive(); };
    std::vector<int> pool;
    auto pickFrom = [&](int from, int to) {
        pool.clear();
        for (int i = from; i < to; ++i)
            if (alive(i)) pool.push_back(i);
        return pool.empty() ? -1 : pool[size_t(random(int(pool.size())))];
    };
    auto byHp = [&](int from, int to, bool lowest) {
        int best = -1;
        for (int i = from; i < to; ++i)
            if (alive(i) && (best < 0 || (lowest ? f[size_t(i)].hp < f[size_t(best)].hp : f[size_t(i)].hp > f[size_t(best)].hp)))
                best = i;
        return best;
    };
    // (a pick btlai_prc doesn't handle leaves the target unset, and btlque_make_que then takes the
    // actor itself for an own-side action, a random living opponent for the rest)
    auto opponent = [&]() { return user < 2 ? pickFrom(2, 5) : pickFrom(0, 2); };
    switch (a.side) {
    case 1: return user;
    case 2: // its own side (the enemies)
        if (a.pick == 4) return alive(2) ? 2 : user;
        if (a.pick == 2) return byHp(2, 5, true);
        return pickFrom(2, 5);
    case 3: // the pair
        switch (a.pick) {
        case 3: return byHp(0, 2, false);
        case 4: return alive(0) ? 0 : pickFrom(0, 2);
        case 5: return alive(1) ? 1 : pickFrom(0, 2);
        default: return pickFrom(0, 2);
        }
    default: return a.pick == 1 ? pickFrom(0, 5) : opponent();
    }
}

void BattleRules::beginRound(const BattleCommand cmd[2], const BattleActionTable& actions) {
    ++round;
    queue_.clear();
    next_ = 0;
    escaped_ = false;
    // state 500: what the Break must survive, defend takes hold now
    breakFlags = 0;
    if (breakGauge != 0) {
        breakFlags |= 0x8000;
        if (breakGauge > 0) breakFlags |= 0x4000;
    }
    const bool partySkips = round == 1 && strike == 2, enemiesSkip = round == 1 && strike == 1;
    int order = 0;
    if (!partySkips)
        for (int k = 0; k < (pair ? 1 : 2); ++k) {
            const BattleCommand& c = cmd[k];
            if (!pair && !f[size_t(k)].alive()) continue;
            if (k == 1 && allyAuto) continue; // the guest chooses for itself below
            Entry e;
            e.kind = c.kind;
            e.order = order++;
            e.actors[0] = pair ? 0 : k;
            e.actors[1] = pair ? 1 : -1;
            e.target = c.target;
            switch (c.kind) {
            case kCmdAttack: e.action = attackAction(k); break;
            case kCmdGene: e.action = c.action; break;
            case kCmdItem: e.item = c.item; e.action = c.item - kFirstBattleItem + kItemAction; break;
            case kCmdDefend:
                for (int a : e.actors)
                    if (a >= 0) f[size_t(a)].guarding = true;
                continue; // done (btl_main 500)
            case kCmdEscape: // __menub_mainPrc: both members' entries become the escape - one action of theirs
                e.action = 0;
                e.actors[0] = 0, e.actors[1] = f[1].alive() ? 1 : -1;
                if (!f[0].alive()) std::swap(e.actors[0], e.actors[1]);
                k = 2;
                break;
            default: continue;
            }
            queue_.push_back(e);
        }
    // btlord_enemy_rand: scenario 0's group 0x2b6 - member 1's entry is made for it every round:
    // a plain attack (action 16) on slot 2 or 3 at random (a fallen one: make_que retargets)
    if (allyAuto && f[1].alive() && !partySkips) {
        Entry e;
        e.order = order++;
        e.actors[0] = 1;
        e.action = 16;
        e.kind = kCmdAttack;
        e.target = random(2) + 2;
        queue_.push_back(e);
    }
    // btlord_enemy_rand: in formation 2 (two size-1 monsters) with both standing, not group
    // 0x297's, a third of the rounds they act together
    enemyPair = false;
    if (!enemiesSkip) {
        if (formation == 2 && f[2].alive() && f[3].alive() && group != 663 && random(3) == 1) {
            enemyPair = true;
            Entry e;
            e.order = order++;
            e.actors[0] = 2;
            e.actors[1] = 3;
            enemyChoice(2, true, e, actions);
            if (e.kind != kCmdNone) queue_.push_back(e);
        } else
            for (int i = 2; i < 5; ++i) {
                if (!f[size_t(i)].alive()) continue;
                Entry e;
                e.order = order++;
                e.actors[0] = i;
                enemyChoice(i, false, e, actions);
                if (e.kind != kCmdNone) queue_.push_back(e);
            }
    }
    // btlord_enemy_rand: skill 153 - standing at a quarter of the HP or less, 20% holding the
    // Break (else 10) of guarding this round (in pair mode the pair both), as well as when hit
    for (int i = 0; i < 5; ++i) {
        Fighter& x = f[size_t(i)];
        if (!x.alive() || x.hp > x.hpMax / 4 || !has(i, 153)) continue;
        if (random(100) < (holdsBreak(i) ? 20 : 10)) {
            x.guarding = true;
            if (i < 2 && pair) f[size_t(1 - i)].guarding = true;
            if (enemyPair && (i == 2 || i == 3)) f[size_t(5 - i)].guarding = true;
        }
    }
    // btlord_calc_spd: the actors' average speed and the action's (Omega's has no +19998 for the
    // invincible, CR1's did)
    for (Entry& e : queue_) {
        int sum = 0, n = 0;
        for (int a : e.actors)
            if (a >= 0) sum += spdOf(a), ++n;
        e.speed = (n ? sum / n : 0);
        if (const BattleActionDef* d = actions.get(e.action)) e.actionSpeed = d->speed;
        e.speed += e.actionSpeed;
    }
    for (Entry& e : queue_) { // Asura Lotus strikes 2..5 times (2..3 in pair mode); 234 / 239 twice, 243 six times, whoever acts
        if (e.action == 207) e.repeats = random(e.actors[0] < 2 ? (pair ? 2 : 4) : (enemyPair ? 2 : 4)) + 2;
        if (e.action == 234 || e.action == 239) e.repeats = 2;
        if (e.action == 243) e.repeats = 6;
        if (e.kind == kCmdEscape) e.speed = 1 << 20; // btlque_make_que: an escape goes first, whatever the speeds
    }
    for (const Entry& e : queue_) // btlque_make_que +0xa4a: the party member queued last
        if (e.actors[0] >= 0 && e.actors[0] < 2) soloMember_ = e.actors[0];
    std::stable_sort(queue_.begin(), queue_.end(), [](const Entry& a, const Entry& b) { return a.speed > b.speed; });
}

std::vector<int> BattleRules::resolveTargets(const BattleActionDef& a, const Entry& e) {
    const int user = e.actors[0];
    std::vector<int> pool = targets(a, user);
    if (pool.empty()) return pool;
    std::vector<int> out;
    if (isGroupScope(a) || a.scope == 0) out = pool;
    else {
        int t = e.target;
        if (std::find(pool.begin(), pool.end(), t) == pool.end()) // fell meanwhile: someone else there
            t = pool[size_t(random(int(pool.size())))];
        out.push_back(t);
    }
    if (e.target2 >= 0 && !isGroupScope(a) && a.scope != 0 && f[size_t(e.target2)].alive() &&
        std::find(out.begin(), out.end(), e.target2) == out.end())
        out.push_back(e.target2); // (btlque_add_counter: all the blow's actors)
    // btlque_make_que: in pair mode a single target on the pair's side is both
    if (pair && out.size() == 1 && out[0] < 2 && a.scope != 1) {
        out.clear();
        for (int i = 0; i < 2; ++i)
            if (f[size_t(i)].alive()) out.push_back(i);
    }
    return out;
}

size_t BattleRules::pickNext() {
    // btlque_make_que: btlord_calc first (every entry's speed from its actors as they are now - a
    // Speed Up, Down or Ward taken this round moves the rest of it), then a counter, then an escape,
    // then the fastest - the first of equals (the entries stand in fighter order)
    size_t best = queue_.size();
    int bestKey = 0;
    for (size_t i = next_; i < queue_.size(); ++i) {
        Entry& e = queue_[i];
        if (e.done) continue;
        int sum = 0, n = 0;
        for (int a : e.actors)
            if (a >= 0 && f[size_t(a)].alive()) sum += spdOf(a), ++n;
        if (e.kind != kCmdEscape) e.speed = (n ? sum / n : 0) + (e.actionSpeed);
        const int key = e.counter ? (1 << 22) : e.kind == kCmdEscape ? (1 << 21) : e.speed;
        if (best == queue_.size() || key > bestKey) best = i, bestKey = key;
    }
    return best;
}

bool BattleRules::nextTurn(const BattleActionTable& actions, BattleTurn& out) {
    while (next_ < queue_.size()) {
        if (outcome() != 0) return false;
        {
            const size_t i = pickNext();
            if (i == queue_.size()) return false;
            if (i != next_) std::rotate(queue_.begin() + std::ptrdiff_t(next_), queue_.begin() + std::ptrdiff_t(i), queue_.begin() + std::ptrdiff_t(i) + 1);
        }
        Entry e = queue_[next_++];
        if (e.rechoose && e.actors[0] >= 2) { // btlque_make_que: a twice-acting monster's second go is chosen as it comes
            const int was = e.action;
            Entry fresh = e;
            fresh.rechoose = false;
            enemyChoice(e.actors[0], e.actors[1] >= 0, fresh, actions, was);
            fresh.repeats = 1;
            if (fresh.kind == kCmdNone) continue;
            e = fresh;
        }
        if (e.repeats > 1 && outcome() == 0) { // btlque_make_que: the entry stays for another go
            Entry again = e;
            --again.repeats;
            again.paid = true;
            // a monster whose row (+0x3e) has it act twice chooses afresh then (not Asura Lotus); the
            // 234 / 239 / 243 repeats are the same action over
            if (e.actors[0] >= 2 && e.action != 207 && monsterField(f[size_t(e.actors[0])], 0x3e) > 1) again.rechoose = true;
            queue_.insert(queue_.begin() + std::ptrdiff_t(next_), again);
        }
        for (int& a : e.actors) // the fallen don't act
            if (a >= 0 && !f[size_t(a)].alive()) a = -1;
        if (e.actors[0] < 0) std::swap(e.actors[0], e.actors[1]);
        if (e.actors[0] < 0 || e.done) continue;
        // btlque_make_que (3.1.0): a pair attack with one actor left becomes that one's own - 36 / 37
        // the member's record + 27, the first game's 18..21 its species + 10 - and is announced anew
        if (e.actors[1] < 0 && e.actors[0] < 2 && e.kind == kCmdAttack && ((e.action >= 18 && e.action <= 21) || e.action == 36 || e.action == 37)) {
            e.action = e.action >= 36 ? f[size_t(e.actors[0])].character + 27 : f[size_t(e.actors[0])].character + 10;
            e.paid = false; // (its name comes up again)
        }
        out = BattleTurn();
        notices_.clear();
        note_ = 0, noteValue_ = 0;
        out.kind = e.kind;
        out.action = e.action;
        out.actors[0] = e.actors[0];
        out.actors[1] = e.actors[1];
        out.counter = e.counter;
        out.repeat = e.paid;
        breakFlags &= ~0x3200u; // btl_main 600: per action
        if (e.kind == kCmdEscape) {
            // btlact_escape_init: certain while the pair holds the Break, else by levels
            int chance = 100;
            if (group != 664 && breakGauge < 1 && !has(0, 160) && !has(1, 160)) { // 160: Beast Wavelength; group 0x298 always
                int pl = 0, pn = 0, el = 0, en = 0;
                for (int i = 0; i < 2; ++i)
                    if (f[size_t(i)].alive()) pl += f[size_t(i)].level, ++pn;
                for (int i = 2; i < 5; ++i)
                    if (f[size_t(i)].alive()) el += f[size_t(i)].level, ++en;
                const int d = (pn ? pl / pn : 0) - (en ? el / en : 0);
                chance = d < -49 ? 40 : d < 0 ? 50 : d < 50 ? 60 : 70;
            }
            out.escapeChance = chance;
            out.escaped = random(100) < chance;
            escaped_ = out.escaped;
            return true;
        }
        const BattleActionDef* a = actions.get(e.action);
        if (!a) continue;
        for (int k : e.actors)
            if (k >= 0) f[size_t(k)].lastAction = e.action;
        if (e.kind == kCmdItem) out.item = e.item;
        // btlque_make_que: a gene with one of its actors silenced since it was chosen (+0x8f) - "can't
        // use genes" (57065) in place of the action; the party's last of an item gone - 57080; the
        // pair's gene without the MP any more - 57081
        const unsigned ac = unsigned(e.action);
        bool silenced = false;
        for (int k : e.actors)
            if (k >= 0 && f[size_t(k)].alive() && f[size_t(k)].status[kStatusSilence]) silenced = true;
        if (silenced && ac - 261u > 140u && ac - 1u > 125u && ac - 413u > 16u) {
            out.cancelled = true, out.message = 57065;
            return true;
        }
        if (e.kind == kCmdItem && e.actors[0] < 2 && e.item >= 21 && e.item <= 47 && !bag_.empty()) {
            if (bag_[size_t(e.item)] <= 0) {
                out.cancelled = true, out.message = 57080;
                return true;
            }
            --bag_[size_t(e.item)];
        }
        if (e.kind == kCmdGene && e.actors[0] < 2 && !e.paid) { // the gene's MP: each member pays it in pair mode
            const int cost = geneCost(*a, e.actors[0]);
            bool enough = true;
            for (int k : e.actors)
                if (k >= 0) enough &= f[size_t(k)].mp >= cost;
            if (!enough) {
                out.cancelled = true, out.message = 57081;
                return true;
            }
            for (int k : e.actors)
                if (k >= 0) f[size_t(k)].mp -= cost;
        }
        int counterBy = -1;
        std::vector<int> hitList = resolveTargets(*a, e);
        firstTarget_ = hitList.empty() ? -1 : hitList.front(); // btlcalc_critical reads G+0x7a
        // btlque_make_que: each target's 131 / 136 (20% holding the Break, else 10) and Omega's 189
        // (5%, a dodgeable action, and the dodger answers it) - one success fends off the whole
        // action ("Miss!" on every target); +20 rules the gene dodge out
        dodgeHit_ = dodgeGene_ = false;
        for (int t : hitList) {
            if (has(t, 131) && !dodgeHit_ && random(100) < (holdsBreak(t) ? 20 : 10)) dodgeHit_ = true;
            if (has(t, 136) && !dodgeGene_ && random(100) < (holdsBreak(t) ? 20 : 10)) dodgeGene_ = true;
            if (has(t, 189) && a->dodgeable && random(100) < 5) {
                dodgeHit_ = dodgeGene_ = true;
                out.counterSkill = true; // (que +0xc5)
                if (counterBy < 0 && !e.counter && e.actors[0] >= 0 && (t < 2) != (e.actors[0] < 2)) counterBy = t;
            }
        }
        if (a->sureGene) dodgeGene_ = false;
        // btlact_gene / btlact_item: a hostile gene or item meeting a barrier on any target takes
        // every target's barrier and comes back (0x1004 / 0x100e) - on its users, or with scope 5
        // on all of their side standing - its users' power as before; the Break counts it for the
        // barrier's side, and what it fells leaves no item or gene (+0xaf)
        int breakBy = e.actors[0];
        bool reflected = false;
        if (a->hostile && (e.kind == kCmdGene || e.kind == kCmdItem) && e.actors[0] >= 0) {
            for (int t : hitList)
                if (f[size_t(t)].status[kStatusBarrier]) f[size_t(t)].status[kStatusBarrier] = 0, reflected = true;
            if (reflected) {
                breakBy = hitList.front();
                out.barriers = hitList;
                hitList.clear();
                const bool party = e.actors[0] < 2;
                if (a->scope == 5) {
                    for (int i = party ? 0 : 2; i < (party ? 2 : 5); ++i)
                        if (f[size_t(i)].alive()) hitList.push_back(i);
                } else
                    for (int k : e.actors)
                        if (k >= 0 && f[size_t(k)].alive()) hitList.push_back(k);
                dodgeGene_ = false;
                out.reflected = true;
            }
        }
        int drained = 0;
        auto atkSumOf = [&]() { int s = 0; for (int k : e.actors) if (k >= 0) s += atkOf(k); return s; };
        auto magicBoost = [&](int v) { return has(e.actors[0], 156) ? v + v / 5 : v; };
        auto recovery = [&](int v) { return has(e.actors[0], 155) ? v * 2 : v; };
        for (int t : hitList) {
            BattleHit h;
            h.target = t;
            int calc = a->calc;
            Fighter& tx = f[size_t(t)];
            switch (calc) {
            case 2: h.amount = a->power * 100 / std::max<int>(1, int(hitList.size())); break; // btlcalc_0407: shared
            case 3: h.amount = a->power * 100; break;                                           // 0408: fixed
            case 4: case 6: { // 0409 / 0411: MP lost (power x 10 / power% of it)
                h.mp = true;
                h.amount = calc == 4 ? a->power * 10 : a->power * tx.mp / 100;
                if (tx.status[kStatusInvincible]) h.amount = 0;
                tx.mp = std::clamp(tx.mp - h.amount, 0, tx.mpMax);
                out.hits.push_back(h);
                continue;
            }
            case 5: case 7: case 8: { // 0410 power% of its HP / 0412 of the users' HP / 0413 of their attack
                int base = tx.hp;
                if (calc == 7) { base = 0; for (int k : e.actors) if (k >= 0) base += std::max(0, f[size_t(k)].hp - 1); }
                if (calc == 8) base = atkSumOf();
                h.amount = magicBoost(zoku(a->power * base / 100, *a, e.actors[0], t, true));
                if (dodgeGene_) h.amount = 0, h.type = kHitMiss, note_ = 57062;
                if (calc == 8) drained += h.amount;
                break;
            }
            case 9: h.amount = std::max(0, tx.hp - 1); break; // 0414: down to 1
            case 10: { // 0415: the purse loses power% (at least 1)
                int take = purse > 0 ? std::max(1, a->power * purse / 100) : 0;
                take = std::min(take, purse);
                purse -= take;
                ozStolen += take;
                f[size_t(e.actors[0])].stolen += take;
                out.message = take > 0 ? 57039 : 57038;
                out.messageValue = take;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            }
            case 11: { // 0416: the monster runs (sure while the enemies hold the Break)
                int chance = 100;
                if (breakGauge >= 0) {
                    int pl = 0, pn = 0;
                    for (int i = 0; i < 2; ++i) if (f[size_t(i)].alive()) pl += f[size_t(i)].level, ++pn;
                    const int d = tx.level - (pn ? pl / pn : 0);
                    chance = d < -49 ? 50 : d < 0 ? 70 : d < 50 ? 85 : 100;
                }
                if (random(100) < chance) tx.gone = out.gone = true;
                out.message = out.gone ? 57000 : 57001;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            }
            case 13: { // 0418: MP back by the luck formula
                int lck = 0;
                for (int k : e.actors) if (k >= 0) lck += lckOf(k);
                int bonus = t < 2 ? (breakGauge > 0 ? 20 : 0) + (e.actors[0] == 1 || e.actors[1] == 1 ? 5 : 0)
                                  : (breakGauge < 0 ? 25 : 5);
                int v = recovery((bonus + a->power) * lck / 100);
                if (a->variance > 0) v = (100 - a->variance + random(2 * a->variance + 1)) * v / 100;
                h.mp = true;
                h.amount = -v;
                tx.mp = std::clamp(tx.mp + v, 0, tx.mpMax);
                out.hits.push_back(h);
                continue;
            }
            case 14: // 0419: everything back
                tx.mp = tx.mpMax;
                h.amount = -tx.hpMax;
                break;
            case 15: h.amount = -recovery(tx.hpMax * a->power / 100); break; // 0420: power% of the most HP
            case 16: { // 0421: power% of the most MP
                const int v = recovery(tx.mpMax * a->power / 100);
                h.mp = true;
                h.amount = -v;
                tx.mp = std::clamp(tx.mp + v, 0, tx.mpMax);
                out.hits.push_back(h);
                continue;
            }
            case 19: // 0424: back on its feet with half (all, holding the Break) its HP
                if (tx.hp > 0) { h.type = kHitQuiet; out.hits.push_back(h); continue; }
                h.amount = -std::min(tx.hpMax, recovery(tx.hpMax * (holdsBreak(t) ? 100 : 50) / 100));
                h.revived = true;
                break;
            case 22: case 23: // 042700 poison cured / 042701 every status but the Ward (and the element) gone
                if (calc == 22) tx.status[kStatusPoison] = 0;
                else {
                    for (int k = 0; k < kStatusCount; ++k)
                        if (k != kStatusWard) tx.status[k] = 0;
                    tx.element = 0;
                }
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 24: { // Analyze (0428 itself does nothing): the scene's lines tell what the target is
                h.type = kHitQuiet;      // (btlact_gene 0xc1c..: analyzeElements)
                out.hits.push_back(h);
                continue;
            }
            case 25: // zokusei_chr1: the target takes the action's element
                if (a->element > 0) tx.element = a->element, elementTaken = true, notice(t, 0x32 + a->element);
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 34: tx.counterStance = true; h.type = kHitQuiet; out.hits.push_back(h); continue; // 030218
            case 35: case 36: // 0601 / 0602: this battle's experience / Oz doubled
                (calc == 35 ? doubleExp : doubleOz) = true;
                out.message = calc == 35 ? 57023 : 57024;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 0: case 1: case 12: {
                h = calc == 0 ? physical(*a, e.actors, t) : calc == 1 ? magical(*a, e.actors, t) : heal(*a, e.actors, t);
                if (calc == 0 && a->id == 203 && h.amount > 0) { // btlcalc_0404: Risk Breaker
                    h.amount = h.amount * (holdsBreak(e.actors[0]) ? 200 : 125) / 100;
                    if (t == hitList.front()) // the users give a quarter of their HP first
                        for (int k : e.actors) {
                            if (k < 0 || !f[size_t(k)].alive()) continue;
                            Fighter& u = f[size_t(k)];
                            BattleHit self;
                            self.target = k;
                            self.silent = true; // (chk_dead kind 99)
                            self.amount = u.hp / 4;
                            self.amount = floored(k, self.amount);
                            if (self.amount > 0) breakEvent(e.actors[0], 2, t);
                            u.hp = std::max(0, u.hp - self.amount);
                            if (u.hp == 0 && u.status[kStatusEndure]) u.status[kStatusEndure] = 0, u.hp = 1;
                            if (u.hp == 0) { // btlcalc_chk_dead
                                self.fell = true;
                                for (int& st : u.status) st = 0;
                                u.guarding = false;
                                if (k < 2) pair = false;
                            }
                            out.hits.push_back(self);
                        }
                }
                if (a->variance > 0 && h.amount != 0) h.amount = (100 - a->variance + random(2 * a->variance + 1)) * h.amount / 100;
                break;
            }
            case 17: h.amount = -a->power * (has(e.actors[0], 155) ? 20 : 10); break; // btlcalc_0422: HP
            case 18: { // btlcalc_0423: MP
                Fighter& x = f[size_t(t)];
                h.mp = true;
                h.amount = -a->power * (has(e.actors[0], 155) ? 20 : 10);
                x.mp = std::clamp(x.mp - h.amount, 0, x.mpMax);
                out.hits.push_back(h);
                continue;
            }
            case 26: case 27: case 28: case 29: case 30: case 31: case 33: // btlcalc_0302xx: statuses
                h.type = kHitQuiet;
                if (calc == 31 && monsterField(tx, 0x30)) { // 030210: row +0x30 can't be silenced
                    note_ = 57075;
                    out.hits.push_back(h);
                    continue;
                }
                if (calc >= 27 && calc <= 30 && tx.status[kStatusWard]) { // is_param_berserk: no up under the Ward - "No effect" (57047)
                    note_ = 57047;
                    out.hits.push_back(h);
                    continue;
                }
                h.status = calc == 26 ? kStatusBarrier : calc == 31 ? kStatusSilence : calc == 33 ? kStatusInvincible
                                                                                   : kStatusAttackUp + (calc - 27);
                f[size_t(t)].status[h.status] = calc == 33 ? 2 : 5;
                statusTaken = true;
                notice(t, calc == 26 ? 0x1d : calc == 31 ? 4 : calc == 33 ? 0x24 : 0x19 + (calc - 27));
                out.hits.push_back(h);
                continue;
            case 32:
                h.status = kStatusEndure;
                f[size_t(t)].status[kStatusEndure] = 1;
                statusTaken = true;
                notice(t, 0x1e);
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 38: { // btlcalc_0430 (237 / 238): a random element (1..3), and the first two of
                // _kiss_tbl - shuffled, and kept so, between kisses - of poison, silence and half the HP
                if (a->id == 237 && monsterField(tx, 0x2e)) { h.type = kHitQuiet; out.hits.push_back(h); continue; }
                tx.element = 1 + random(3);
                for (int k = 0; k < random(64); ++k) { // (the bound drawn again each time round)
                    const int j = random(2);
                    std::swap(kiss_[size_t(j)], kiss_[size_t(j + 1)]);
                }
                h.amount = 0;
                for (int k = 0; k < 2; ++k) {
                    if (kiss_[size_t(k)] == 10) tx.status[kStatusSilence] = 5;
                    else if (kiss_[size_t(k)] == 21) h.amount = tx.hp / 2;
                    else if (kiss_[size_t(k)] == 4) tx.status[kStatusPoison] = 5;
                }
                statusTaken = true;
                if (h.amount <= 0) { h.type = kHitQuiet; out.hits.push_back(h); continue; }
                break;
            }
            case 39: // btlcalc_0431 (242): the target's element at random (0..3); 57100 on the first target
                tx.element = random(4);
                if (t == firstTarget_) note_ = 57100;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 40: // btlcalc_0432 (437): all its HP - or 1000 once flag 0x584 is set (57102 the first time, 0x594; 57105 after)
                if (story && story->flag(0x584)) {
                    if (!story->flag(0x594)) story->setFlag(0x594, true), note_ = 57102;
                    else note_ = 57105;
                    h.amount = 1000;
                } else h.amount = tx.hp;
                break;
            case 42: // btlcalc_0434 (441): unwarded, the ups gone and Down for 5 rounds (57107; 57047 under the Ward)
                if (!tx.status[kStatusWard]) {
                    for (int k = kStatusAttackUp; k <= kStatusLuckUp; ++k) tx.status[k] = 0;
                    tx.status[kStatusDown] = 5;
                    statusTaken = true;
                    note_ = 57107;
                } else note_ = 57047;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 43: // btlcalc_0435 (126): the Ward, the ups gone (57108)
                tx.status[kStatusWard] = 1;
                for (int k = kStatusAttackUp; k <= kStatusLuckUp; ++k) tx.status[k] = 0;
                statusTaken = true;
                note_ = 57108;
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            case 44: // btlcalc_0436 (439 / 509 / 511): power x 10 MP, power x 100 HP
                tx.mp = std::max(0, tx.mp - a->power * 10);
                h.amount = a->power * 100;
                break;
            case -1: case 20: case 21: h.type = kHitQuiet; out.hits.push_back(h); continue;
            default: {
                const std::string why = "calc " + std::to_string(calc);
                if (std::find(unsupported.begin(), unsupported.end(), why) == unsupported.end()) unsupported.push_back(why);
                h.type = kHitQuiet;
                out.hits.push_back(h);
                continue;
            }
            }
            Fighter& tf = f[size_t(t)];
            const int before = tf.hp;
            if (h.type == kHitKill) h.amount = tf.hp;
            if (h.amount > 0 && invincible(t)) { // Is_invincible (a pair's either member): the damage 0, the label as it was ("0")
                h.amount = 0;
                if (h.type == kHitKill) h.type = kHitNormal;
            }
            if (scenario == 0 && group == 693 && e.actors[0] < 2 && e.actors[1] < 0 && h.amount > 0) // btlcalc_dmg_hp: 0x2b5's
                h.amount = 0, h.type = 0, nullHit = true;                                         // solo blows don't land (+0xa78)
            h.amount = floored(t, h.amount); // chk_karaok_dmg_hp
            if (calc == 0 && round > 3 && h.amount >= 1 && a->id == 18) lateAttack = true; // btlcalc_0404: +0xa50
            if (h.amount > 0) breakEvent(breakBy, 2, t);
            tf.hp = std::clamp(tf.hp - h.amount, 0, tf.hpMax);
            if (tf.hp == 0 && before > 0 && tf.status[kStatusEndure]) {
                tf.status[kStatusEndure] = 0;
                tf.hp = 1;
            }
            if (h.type == kHitCritical || h.critical) breakEvent(breakBy, 8, t); // (que +0xab: a critical the triangle relabelled, or that didn't land)
            if (before > 0 && tf.hp == 0) { // btlcalc_chk_dead
                h.fell = true;
                for (int& s : tf.status) s = 0;
                if (t < 2) tf.element = 0; // (a member's element goes with it)
                tf.guarding = false;
                if (tf.enemy && !tf.rewarded) {
                    tf.rewarded = true;
                    // chk_dead: pair mode either member's skill, else the member +0xa4a names
                    auto member = [&](int skill) { return pair ? has(0, skill) || has(1, skill) : has(soloMember_, skill); };
                    const bool expUp = member(161), ozUp = member(162);
                    expGained += tf.exp + (expUp ? tf.exp / 5 : 0);
                    ozGained += tf.oz + (ozUp ? tf.oz / 5 : 0) + tf.stolen; // what it stole comes back
                    tf.stolen = 0;
                    fallRewards(t, e.actors, !reflected);
                }
                if (t < 2) pair = false; // a fallen member ends pair mode
                breakEvent(breakBy, 0x10, t);
            }
            if (h.amount > 0 && tf.hp > 0 && (t < 2) != (e.actors[0] < 2)) {
                // 153: at a quarter of the HP or less the target may start guarding
                if (tf.hp <= tf.hpMax / 4 && !tf.guarding && has(t, 153) && random(100) < (holdsBreak(t) ? 20 : 10)) {
                    tf.guarding = true;
                    if (t < 2 && pair) f[size_t(1 - t)].guarding = true;
                }
                // 154 (or calc 34's stance): a physical hit may be answered with the target's attack
                if (calc == 0 && counterBy < 0 && !e.counter &&
                    (tf.counterStance || (has(t, 154) && (random(100), true)))) // (3.1.0: the draw is made but not compared - 1.0.0 took 10 / 20 %)
                    counterBy = t;
            }
            if (calc == 0 && a->id == 201 && h.amount >= 1) { // btlcalc_0404: Trompe-L'oeil
                const bool held = holdsBreak(e.actors[0]);
                if (random(8) == 0 && held && !cancelTried_) {
                    cancelTried_ = true;
                    if (!monsterField(tx, 0x32)) // the targets' commands this round are cancelled
                        for (int c : hitList) {
                            for (size_t q = next_; q < queue_.size(); ++q)
                                if (queue_[q].actors[0] == c || queue_[q].actors[1] == c) queue_[q].done = true;
                            notice(c, 3);
                        }
                }
                if (!held && !cancelBreak_) cancelBreak_ = true, breakEvent(e.actors[0], 0x20, t);
            }
            out.hits.push_back(h);
        }
        if (a->id >= 10 && a->id < 100) // (btlcalc_prc_0404: +0xd1 set)
            for (int k : e.actors)
                if (k >= 0) f[size_t(k)].firstAttack = false;
        if (a->id == 223) { // btlcalc_0504: Steal Item, when the blow landed
            int dealt = 0, mine = 0, n = 0, theirs = 0, m = 0;
            bool anyLeft = false;
            for (const BattleHit& h : out.hits) dealt += std::max(0, h.amount);
            for (int k : e.actors) if (k >= 0) mine += lckOf(k), ++n;
            for (int t : hitList) { theirs += lckOf(t), ++m; anyLeft |= !f[size_t(t)].robbed; }
            const int d = (n ? mine / n : 0) + 20 - (m ? theirs / m : 0) + a->power;
            const int chance = d < -100 ? 80 : d < -49 ? 85 : d < 0 ? 90 : 100; // Omega's btlcalc_0504
            const bool roll = random(100) < chance;
            if (!anyLeft) out.message = 57037; // nothing to steal
            else if (dealt > 0 && roll) {
                out.message = 57038;
                for (int t : hitList) {
                    Fighter& x = f[size_t(t)];
                    if (x.robbed || !x.def) continue;
                    const int item = monsterField(x, 0xd2 + 2 * levelBand(x.level)); // btlcalc_0504: five by level
                    if (item <= 0) continue;
                    x.robbed = true;
                    stolen.push_back(item);
                    out.message = 0; // the catch is queued instead, one message an item
                    notice(-item, 5);
                }
            } else
                out.message = 57038; // failed
        }
        if (a->calc == 7) // 0412: the users give up their HP to 1
            for (int k : e.actors)
                if (k >= 0 && f[size_t(k)].alive()) {
                    BattleHit self;
                    self.target = k;
                    self.silent = true; // (chk_dead kind 99)
                    self.amount = f[size_t(k)].hp - 1;
                    f[size_t(k)].hp = 1;
                    out.hits.push_back(self);
                }
        if (a->calc == 8 && drained > 0 && !hitList.empty()) // 0413: what was drained heals the users
            for (int k : e.actors)
                if (k >= 0 && f[size_t(k)].alive()) {
                    BattleHit back;
                    back.target = k;
                    back.amount = -recovery(drained) / int(hitList.size());
                    f[size_t(k)].hp = std::clamp(f[size_t(k)].hp - back.amount, 0, f[size_t(k)].hpMax);
                    out.hits.push_back(back);
                }
        if (counterBy >= 0 && e.actors[0] >= 0) { // btlque_add_counter: next in the queue
            // one answer by all the blow's targets (que +0x7a) on all its actors (+0x70), with the
            // firing fighter's row's counter action (+0xb8; a member's row is the character's)
            Entry c;
            c.counter = true;
            c.kind = kCmdAttack;
            c.actors[0] = counterBy;
            for (int t : hitList)
                if (t != counterBy && f[size_t(t)].alive() && (t < 2) == (counterBy < 2) && c.actors[1] < 0) c.actors[1] = t;
            c.target = e.actors[0];
            c.target2 = e.actors[1];
            c.action = monsterField(f[size_t(counterBy)], 0xb8);
            if (!actions.get(c.action)) c.action = attackAction(counterBy);
            queue_.insert(queue_.begin() + std::ptrdiff_t(next_), c);
        }
        catchUpBreak();
        firstTarget_ = -1;
        out.notices = notices_;
        if (!out.message) out.message = note_, out.messageValue = noteValue_;
        return true;
    }
    return false;
}

void BattleRules::notice(int fighter, int entry) {
    for (const BattleNotice& n : notices_)
        if (n.fighter == fighter) return;
    if (notices_.size() < 5) notices_.push_back({fighter, entry}); // G+0xa14: five places
}

void BattleRules::endRound() {
    // btlord_chk: the holder wasn't hit all round
    if (outcome() == 0 && !(breakFlags & 0x400) && (breakFlags & 0x8000)) breakEvent(breakGauge < 1 ? 2 : 0, 0x40, -1);
    catchUpBreak();
    // btlcalc_status_prc, each fighter standing: its statuses' rounds counted down (not Endure's or
    // the Ward's), then one change of HP - skill 190 a quarter back (5%; the pair's one roll in pair
    // mode), 133 a twentieth back, poison a tenth off, stage 19 a twentieth off the pair (Omega), a
    // story boss whole again - through btlcalc_dmg_hp (its floors; the Break left alone, +0xa76);
    // skill 138 a twentieth of MP back
    const bool pairRegen = pair && has(0, 190) && random(100) <= 4;
    for (int i = 0; i < 5; ++i) {
        Fighter& x = f[size_t(i)];
        if (!x.alive()) continue;
        x.counterStance = false;
        x.guarding = false;
        int d = 0;
        if (has(i, 190)) {
            const int roll = random(100);
            if (i < 2 && pair ? pairRegen : roll <= 4) d = -(x.hpMax / 4);
        }
        if (has(i, 133)) d -= x.hpMax / 20;
        if (has(i, 138)) x.mp = std::min(x.mpMax, x.mp + x.mpMax / 20);
        if (x.status[kStatusPoison] > 0) d += x.hpMax / 10;
        for (int k = 0; k < kStatusCount; ++k)
            if (k != kStatusEndure && k != kStatusWard && x.status[k] > 0) --x.status[k];
        if (i < 2 && stage == 0x13) d += x.hpMax / 20;
        if (regenFull[size_t(i)]) d = x.hp - x.hpMax; // (group 0x293's, until flag 0x58c)
        if (d > 0) d = floored(i, d);
        x.hp = std::clamp(x.hp - d, 0, x.hpMax);
        if (x.hp == 0 && d > 0 && x.status[kStatusEndure]) x.hp = 1, x.status[kStatusEndure] = 0; // (btlcalc_dmg_hp: Endure holds here too)
    }
}

bool BattleRules::invincible(int t) const {
    if (t < 0 || t > 4) return false;
    if (t < 2 && pair) return f[0].status[kStatusInvincible] || f[1].status[kStatusInvincible];
    if (t >= 2 && enemyPair && (t == 2 || t == 3)) return f[2].status[kStatusInvincible] || f[3].status[kStatusInvincible];
    return f[size_t(t)].status[kStatusInvincible] != 0;
}

// btlcalc_0504 / 0505: which of a row's five items - from level 25 a third of the time the one
// for its band (25 / 50 / 100 / 150), else the first
int BattleRules::levelBand(int lv) {
    if (lv < 25 || random(3) != 1) return 0;
    return lv < 50 ? 1 : lv < 100 ? 2 : lv < 150 ? 3 : 4;
}

bool BattleRules::knows(int item) const {
    return std::find(known_.begin(), known_.end(), item) != known_.end() ||
           std::find(learned.begin(), learned.end(), item) != learned.end() ||
           std::find(drops.begin(), drops.end(), item) != drops.end();
}

// btlcalc_0505 / 0506 / chk_dead: what a fallen monster leaves
void BattleRules::fallRewards(int enemy, const int actors[2], bool itemsAndGenes) {
    const Fighter& x = f[size_t(enemy)];
    if (!x.def) return;
    int sum = 0, n = 0;
    for (int k = 0; k < 2; ++k)
        if (actors[k] >= 0) sum += f[size_t(actors[k])].level, ++n;
    const int avg = n ? sum / n : 1, lv = x.level;
    auto row = [&](int off) { return monsterField(x, off); };
    if (itemsAndGenes && row(0xc8) + row(0xca) + row(0xcc) + row(0xce) + row(0xd0) != 0) { // btlcalc_0505: an item - the roll, then which
        int d = avg + 10 - lv;
        const int chance = d < -4 ? 20 : d < 1 ? 30 : d < 6 ? 40 : 50;
        if (random(100) < chance) {
            const int item = row(0xc8 + 2 * levelBand(lv)); // five, by level
            if (item >= 81 && item <= 191) { // steal_item_add: a gene or skill is learned at once, no result line
                if (!knows(item) && std::find(learned.begin(), learned.end(), item) == learned.end()) learned.push_back(item);
            } else if (item > 0) drops.push_back(item);
        }
    }
    if (itemsAndGenes && row(0xbc) != 0) { // a gene
        bool more = false; // 158: Get More Genes
        for (int k = 0; k < 2; ++k) more |= actors[k] >= 0 && has(actors[k], 158);
        const int d = avg - (more ? 15 : 0) - lv;
        const int chance = d < -4 ? 80 : d < 1 ? 40 : d < 6 ? 30 : 10;
        if (random(100) < chance) { // btlcalc_0506: the first not known yet of up to six (+0xbc..+0xc6:
            // the later ones from levels 10 / 25 / 50 / 75 / 100)
            static const int kFrom[6] = {0, 10, 25, 50, 75, 100};
            for (int k = 0; k < 6; ++k) {
                const int g = row(0xbc + 2 * k);
                if (lv >= kFrom[k] && g > 0 && !knows(g) &&
                    std::find(learned.begin(), learned.end(), g) == learned.end()) {
                    learned.push_back(g), learnedNew = true;
                    note_ = isSkill(g) ? 57050 : 57049, noteValue_ = g; // (btlcalc_0506's line, at once)
                    break;
                }
            }
        }
    }
    const int plate = plateDrop(x.def->model); // chk_dead: 481 + the species
    if (plate > 0 && !knows(plate)) drops.push_back(plate);
}

int BattleRules::outcome() const {
    if (escaped_) return 1;
    if (!f[0].alive() && !f[1].alive()) return 3;
    bool any = false;
    for (int i = 2; i < 5; ++i) any |= f[size_t(i)].alive();
    return any ? 0 : 2;
}

void BattleRules::defeatEnemies() {
    const int actors[2] = {0, f[1].present ? 1 : -1};
    for (int t = 2; t < 5; ++t) {
        Fighter& tf = f[size_t(t)];
        if (!tf.present || tf.hp <= 0) continue;
        tf.hp = 0;
        for (int& s : tf.status) s = 0;
        tf.guarding = false;
        if (tf.enemy && !tf.rewarded) {
            tf.rewarded = true;
            expGained += tf.exp;
            ozGained += tf.oz + tf.stolen;
            tf.stolen = 0;
            fallRewards(t, actors);
        }
    }
}

std::vector<int> BattleRules::applyRewards(GameState& gs) const {
    std::vector<int> levelled;
    const bool won = outcome() == 2;
    for (int k = 0; k < 2; ++k) {
        const Fighter& x = f[size_t(k)];
        if (!x.present || x.guest) continue;
        PartyMember& m = gs.party[size_t(x.character)];
        m.mp = x.skills[163 - 131] && outcome() != 3 ? -1 : x.mp; // 163: MP back to full
        if (won) {
            const int gained = expGained * (doubleExp ? 2 : 1);
            m.exp += x.hp > 0 ? gained : gained / 2; // MbResultSet: a fallen member gets half
            const int before = m.level;
            while (m.level < levelCap(gs.scenario) && m.exp >= expForLevel(m.level + 1)) ++m.level; // (GetLvMax)
            if (m.level != before) {
                levelled.push_back(x.character);
                levelUpBoosts(gs, x.character, m.level - before);
            }
        }
    }
    if (ozStolen > 0) addOz(gs, -ozStolen);
    // steal_item_add / add2: a catch and a gene taught go into the bag the moment they happen,
    // whatever the battle's outcome; only the other drops wait for the result screen (MbResultSet)
    for (int item : stolen) gs.items[item] = std::min(kMaxItemCount, gs.items[item] + 1);
    for (int item : learned) gs.items[item] = std::max(1, gs.items[item]);
    if (won) {
        addOz(gs, ozGained * (doubleOz ? 2 : 1));
        for (int item : drops) gs.items[item] = std::min(kMaxItemCount, gs.items[item] + 1);
    }
    return levelled;
}

} // namespace cr
