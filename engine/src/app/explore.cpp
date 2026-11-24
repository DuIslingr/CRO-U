#include "app/app.hpp"

namespace crapp {

// --selftest: renders every model through every animation and reports GL errors.
// Battles: every (stage, group) the encounter tables can start: stage, pair and monsters load,
// names resolve, a few frames draw; and one map's counter starts one by walking. Returns problems.
int battleSelfTest(App& app, int& frames, int& glErrors) {
    {
        std::set<std::pair<int, int>> starts;
        for (int area = 0; area < cr::kNumAreas; ++area) {
            cr::EventFile ef;
            if (!ef.load(app.dataDir, area)) continue;
            for (int map = 0; map < cr::kNumMaps; ++map) {
                cr::EncounterTable t;
                if (cr::encounterTable(ef, map, t))
                    for (const cr::EncounterGroup& g : t.groups) starts.insert({t.stage, g.group});
            }
        }
        { // the last world (area 7): monsters with no level of their own take the World Select's band,
            // which is -1 there after a Back out of a band list (btl_enemy_init_lv: level 0) - any?
            int unfixed = 0, groups = 0;
            cr::EventFile ef;
            if (ef.load(app.dataDir, 7))
                for (int map = 0; map < cr::kNumMaps; ++map) {
                    cr::EncounterTable t;
                    if (!cr::encounterTable(ef, map, t)) continue;
                    for (const cr::EncounterGroup& g : t.groups) {
                        ++groups;
                        for (int id : app.battleData.group(g.group))
                            if (const cr::MonsterDef* m = app.battleData.monster(id); id > 0 && m && m->level <= 0) ++unfixed;
                    }
                }
            std::printf("last world: %d encounter groups, %d monsters taking the band's level\n", groups, unfixed);
        }
        if (!app.events.loaded() || app.mapNumber < 0) showMap(app, 0, 0, 2);
        int started = 0, actors = 0, unnamed = 0, noModel = 0, noAction = 0, errs = 0;
        for (const auto& [stage, group] : starts) {
            if (SDL_getenv("CR_BATTLE_TRACE")) std::printf("  battle %d on stage %d\n", group, stage), std::fflush(stdout);
            startBattle(app, {group, stage, -1, 0}, 0);
            if (!app.battle.active) continue;
            ++started;
            const std::array<int, 3> ids = app.battleData.group(group);
            const size_t want = size_t(app.battle.rules.f[0].present) + size_t(app.battle.rules.f[1].present) + // who fights
                                size_t(std::count_if(ids.begin(), ids.end(), [](int id) { return id > 0; })) +
                                size_t(group == 0x291 || group == 0x22c); // (their extra model)
            if (app.battle.actors.size() != want) ++noModel;
            noModel += app.battle.stage.meshes.empty();
            for (const App::Battle::Actor& a : app.battle.actors) {
                ++actors;
                unnamed += a.name.empty() && a.fighter != kPropFighter;
                noModel += app.battle.models[a.model].meshes.empty();
                noAction += a.action < 0;
            }
            const bool trace = SDL_getenv("CR_BATTLE_TRACE") != nullptr;
            if (trace) std::printf("    started, %zu actors\n", app.battle.actors.size()), std::fflush(stdout);
            for (int f = 0; f < 3; ++f) {
                app.lastTicks = 0;
                drawFrame(app, false);
                stepBattle(app);
                ++frames;
            }
            if (trace) std::printf("    drawn\n"), std::fflush(stdout);
            errs += app.renderer.takeErrors();
            endBattle(app, true);
            if (trace) std::printf("    ended\n"), std::fflush(stdout);
            app.runner.stop();
        }
        // The first dungeon's first room: walk a unit a frame until a battle starts
        showMap(app, 0, 0, 2);
        app.rng.seed(1);
        int walkedFrames = 0;
        cr::BattleStart bs;
        auto random = [&](int n) { return app.rng.get(n); };
        bool met = false;
        for (; walkedFrames < 20000 && !met; ++walkedFrames) {
            app.encounters.walked(1.0f);
            met = app.encounters.check(random, 1, false, false, bs);
        }
        std::printf("battles: %d of %zu stage/group pairs started, %d actors (%d unnamed, %d without a model, %d without "
                    "an action), %d render API errors; walking map 0 met group %d after %d units\n",
                    started, starts.size(), actors, unnamed, noModel, noAction, errs, met ? bs.group : -1, walkedFrames);
        glErrors += errs;
        int problems = int(starts.size()) - started + unnamed + noModel + noAction + errs + !met;

        // The rules (game/battle_rules.hpp) against numbers worked out by hand
        {
            int bad = 0;
            cr::BattleRules r;
            cr::GameState gs;
            r.setupParty(gs, app.itemTable, 0);
            // the battle's stats are the menus' (btlpcparam_prty_init: pcparam_max_*)
            bad += r.f[0].hpMax != cr::memberStat(gs, 0, cr::kHp, app.itemTable) || r.f[1].hpMax != cr::memberStat(gs, 1, cr::kHp, app.itemTable) ||
                   r.f[0].mp != cr::memberStat(gs, 0, cr::kMp, app.itemTable) || r.f[1].mp != cr::memberStat(gs, 1, cr::kMp, app.itemTable);
            // physical: (50 solo bonus + 150) x 200 / 100 - 100 = 300, a critical (+50) 400
            for (cr::Fighter& x : r.f) x = cr::Fighter();
            r.f[0].present = r.f[2].present = true;
            r.f[0].character = 0; // (record 0: a warrior, the +50 solo bonus)
            r.f[0].atk = 200, r.f[0].spd = 100, r.f[0].level = r.f[2].level = 1, r.f[0].hp = r.f[0].hpMax = 100;
            r.f[2].grd = 100, r.f[2].lck = 0, r.f[2].hp = r.f[2].hpMax = 10000, r.f[2].enemy = true;
            cr::BattleActionDef atk;
            atk.id = 10, atk.power = 150, atk.calc = 0, atk.alwaysHits = true, atk.scope = 4, atk.side = 3;
            const int who[2] = {0, -1};
            int normal = 0, crits = 0, other = 0;
            for (uint32_t seed = 1; seed <= 400; ++seed) {
                r.seed(seed);
                const cr::BattleHit h = r.physical(atk, who, 2);
                if (h.type == cr::kHitNormal && h.amount == 300) ++normal;
                else if (h.type == cr::kHitCritical && h.amount == 400) ++crits;
                else ++other;
            }
            bad += other != 0 || normal == 0 || crits == 0 || crits > 60; // 20 in 256
            r.f[0].status[cr::kStatusAttackUp] = 1; // x1.25: (200) x 250 / 100 - 100 = 400
            r.seed(7);
            for (int k = 0; k < 20; ++k) {
                const cr::BattleHit h = r.physical(atk, who, 2);
                bad += h.amount != (h.type == cr::kHitCritical ? 525 : 400);
            }
            r.f[0].status[cr::kStatusAttackUp] = 0;
            r.f[2].guarding = true; // guarding only counts for the pair
            bad += r.grdOf(2) != 100;
            r.f[0].guarding = true, r.f[0].grd = 70;
            bad += r.grdOf(0) != 140;
            // healing: -(0 + 150) x luck 80 / 100 = -120 for the pair's actor 0 casting
            cr::BattleActionDef cure;
            cure.id = 228, cure.power = 150, cure.calc = 12;
            r.f[0].lck = 80;
            bad += r.heal(cure, who, 0).amount != -120;
            // the Break: the first event puts it at 4; the enemies' damage takes 1 an action
            r.breakEvent(0, 2, 2);
            r.catchUpBreak();
            bad += r.breakGauge != 4;
            r.breakFlags &= ~0x3200u;
            r.breakEvent(2, 2, 0);
            r.catchUpBreak();
            bad += r.breakGauge != 3;
            r.breakEvent(2, 8, 0); // its critical too
            r.catchUpBreak();
            bad += r.breakGauge != 2;
            r.breakEvent(0, 2, 2); // the holder's damage doesn't count
            r.catchUpBreak();
            bad += r.breakGauge != 2;
            r.breakEvent(0, 0x10, 2); // its kill does
            r.catchUpBreak();
            bad += r.breakGauge != 3;
            r.breakEvent(2, 0x10, 0), r.breakEvent(2, 0x10, 1); // two kills by the enemies: gone, even
            r.catchUpBreak();
            bad += r.breakGauge != 0 || !(r.breakFlags & 0x400);
            // enemies: the first group whose lead takes the band's levels, at band 0 with the pair at
            // level 1: levels 1..3, the row's growth (read_btlcpl)
            cr::BattleRules e;
            e.setupParty(gs, app.itemTable, 0);
            int banded = 1;
            for (int gi = 1; gi < 400; ++gi) {
                const cr::MonsterDef* m = app.battleData.monster(app.battleData.group(gi)[0]);
                if (m && m->model >= 0 && !m->strongGrowth && m->level <= 0) { banded = gi; break; }
            }
            int lo = 99, hi = 0;
            for (uint32_t seed = 1; seed <= 50; ++seed) {
                e.seed(seed);
                e.setupEnemies(app.battleData, app.battleData.group(banded), 0);
                lo = std::min(lo, e.f[2].level), hi = std::max(hi, e.f[2].level);
                const cr::MonsterDef* m = e.f[2].def;
                bad += !m || e.f[2].hpMax != int(m->hpAt(e.f[2].level)) || e.f[2].exp != int(m->expAt(e.f[2].level));
            }
            const int lead = e.leaderLevel(); // the new pair's 1 and 5: 3
            bad += lo != (lead < 6 ? 1 : lead - 5) || hi != std::min(10, lead + 2);
            // items: 21 (Chocolate) is action 100, 1000 HP
            const cr::BattleActionDef* choc = app.battleActions.get(21 + 79);
            bad += !choc || choc->calc != 17 || choc->power != 100;
            // escaping: certain while the pair holds the Break
            e.breakGauge = e.breakTarget = 2;
            cr::BattleCommand esc[2];
            esc[0].kind = cr::kCmdEscape;
            e.pair = true;
            e.beginRound(esc, app.battleActions);
            cr::BattleTurn t;
            bool escaped = false;
            while (e.nextTurn(app.battleActions, t))
                if (t.kind == cr::kCmdEscape) escaped = t.escaped;
            bad += !escaped || e.outcome() != 1;
            // genes: the new game's own, a plate's known ones, plate drops, learning, pair MP
            {
                cr::GameState g;
                cr::giveStartingItems(g, app.itemTable);
                const std::vector<int> own = cr::usableAbilities(g, 0);
                // plate 474 in scenario 0: 85 and 135 known (114 is scenario 1's own - _chk_geneplateno's flags)
                bad += own != std::vector<int>{85} || !cr::hasSkill(g, 0, 135) || cr::hasSkill(g, 0, 131);
                g.items[481] = 1, g.items[108] = 1;
                g.party[0].genes[1] = 481;
                const std::vector<int> more = cr::usableAbilities(g, 0);
                bad += more.size() != own.size() + 1 || cr::plateLearned(g, 481) != 1; // 108 of plate 481
                bad += cr::plateDrop(32) != 0 || cr::plateDrop(33) != 481 + cr::kBattleAnimTable[33][15] || cr::plateDrop(139) != 0;
                int plates = 0, taught = 0;
                for (uint32_t seed = 1; seed <= 40; ++seed) { // monster 200 (model 13) falls to a level-1 attack
                    cr::GameState fresh;
                    cr::giveStartingItems(fresh, app.itemTable);
                    cr::BattleRules k;
                    k.seed(seed);
                    k.setupParty(fresh, app.itemTable, 0);
                    std::array<int, 3> one{200, 0, 0};
                    k.setupEnemies(app.battleData, one, 0);
                    k.f[2].hp = 1;
                    cr::BattleCommand cmd[2];
                    cmd[0].kind = cr::kCmdAttack, cmd[0].target = 2;
                    cmd[1].kind = cr::kCmdDefend;
                    k.f[2].lck = -1000; // always hit
                    k.beginRound(cmd, app.battleActions);
                    cr::BattleTurn tt;
                    while (k.nextTurn(app.battleActions, tt)) {}
                    plates += std::count(k.drops.begin(), k.drops.end(), 481) > 0;
                    taught += !k.learned.empty();
                    if (k.outcome() == 2) {
                        k.applyRewards(fresh);
                        bad += fresh.items[481] != 1;
                    }
                }
                bad += plates != 40 || taught == 0 || taught == 40; // the plate every time, a gene 40% of the time
                cr::BattleRules pr; // a pair gene: both members pay its MP
                pr.setupParty(g, app.itemTable, 0);
                pr.setupEnemies(app.battleData, app.battleData.group(1), 0);
                pr.pair = true;
                cr::BattleCommand gc[2];
                gc[0].kind = cr::kCmdGene, gc[0].action = cr::geneAction(94), gc[0].target = 2;
                const int mp0 = pr.f[0].mp, mp1 = pr.f[1].mp;
                const int cost = pr.geneCost(*app.battleActions.get(cr::geneAction(94)), 0); // Musiea's MP 1/2 halves it
                pr.beginRound(gc, app.battleActions);
                cr::BattleTurn gt;
                bool cast = false;
                while (pr.nextTurn(app.battleActions, gt)) cast |= gt.kind == cr::kCmdGene && gt.actors[0] == 0;
                bad += !cast || pr.f[0].mp != mp0 - cost || pr.f[1].mp != mp1 - cost;
                { // Options' Cheats in the rules: the pair at 1 HP casting a pair gene round after round - no MP paid,
                  // nobody falls, the monsters felled (One-Hit Kills); the EXP multiplied (applyRewards)
                    int fell = 0, paid = 0, cleared = 0, brk = 0;
                    for (uint32_t seed = 1; seed <= 20; ++seed) {
                        cr::BattleRules ch;
                        ch.seed(seed);
                        ch.setupParty(g, app.itemTable, 0);
                        ch.setupEnemies(app.battleData, app.battleData.group(1), 0);
                        ch.cheatHp = ch.cheatMp = ch.cheatOneHit = ch.cheatBreak = true;
                        ch.breakStarted = true, ch.breakTarget = ch.breakGauge = 8;
                        ch.pair = true;
                        ch.f[0].hp = ch.f[1].hp = 1;
                        const int m0 = ch.f[0].mp, m1 = ch.f[1].mp;
                        for (int round = 0; round < 8 && ch.outcome() == 0; ++round) {
                            cr::BattleCommand c[2];
                            int tgt = 2;
                            for (int t = 4; t >= 2; --t)
                                if (ch.f[size_t(t)].alive()) tgt = t;
                            c[0].kind = cr::kCmdGene, c[0].action = cr::geneAction(94), c[0].target = tgt;
                            ch.beginRound(c, app.battleActions);
                            cr::BattleTurn tt;
                            while (ch.nextTurn(app.battleActions, tt)) {}
                            ch.endRound();
                        }
                        paid += ch.f[0].mp < m0 || ch.f[1].mp < m1;
                        brk += ch.breakGauge != 8;
                        fell += !ch.f[0].alive() || !ch.f[1].alive();
                        cleared += ch.outcome() == 2;
                    }
                    cr::BattleRules ex;
                    ex.setupParty(g, app.itemTable, 0);
                    ex.setupEnemies(app.battleData, app.battleData.group(1), 0);
                    ex.defeatEnemies();
                    cr::GameState g1 = g, g5 = g;
                    ex.applyRewards(g1);
                    ex.expScale = 5;
                    ex.applyRewards(g5);
                    const int c0 = ex.f[0].character, plain = g1.party[size_t(c0)].exp - g.party[size_t(c0)].exp,
                              five = g5.party[size_t(c0)].exp - g.party[size_t(c0)].exp;
                    bad += paid != 0 || fell != 0 || cleared < 18 || brk != 0 || plain <= 0 || five != 5 * plain;
                    std::printf("cheats: a pair gene at 1 HP, 20 battles - MP paid in %d, a member fell in %d, cleared %d, the Break off 8 in %d; "
                                "EXP %d -> %d at 5x\n", paid, fell, cleared, brk, plain, five);
                }
                std::printf("genes: new game knows %zu, a plate adds %zu, plates dropped %d/40, genes taught %d/40\n", own.size(),
                            more.size() - own.size(), plates, taught);
                { // each scenario's own entries apart (_chk_geneplateno's flags), the other's Human Genes plates
                    // none (_GetItmCnt): scenario 0 knows 85 / 135 of 474, not 114 / 187; scenario 1 the reverse
                    cr::GameState sg;
                    cr::giveStartingItems(sg, app.itemTable);
                    auto have = [&](int it) { return !cr::otherScenarioPlate(sg, it) && sg.items.count(it) && sg.items.at(it) > 0; };
                    const bool s0 = have(85) && have(135) && !have(114) && !have(187) && have(474) && !have(477) && !have(480);
                    cr::changeScenario(sg, app.itemTable, 1);
                    const bool s1 = have(114) && have(187) && !have(474) && have(477);
                    cr::GameState old; // an old save's shared set: kept in both
                    old.items[114] = 1;
                    cr::splitSharedGenes(old, app.itemTable);
                    const bool split = old.scenarioGenes && old.items.count(114) && old.otherBag.count(114);
                    bad += !s0 || !s1 || !split;
                    std::printf("genes per scenario: scenario 0's own %s, scenario 1's own %s, an old save split %s\n", s0 ? "yes" : "NO",
                                s1 ? "yes" : "NO", split ? "yes" : "NO");
                }
                { // _btlque_special by the enemy's model: scenario 2's group 663 - model 21 opens with 243, model 19
                    // follows with 242
                    cr::BattleRules sp;
                    sp.scenario = 1, sp.group = 663;
                    sp.setupEnemies(app.battleData, app.battleData.group(663), 0);
                    sp.round = 1;
                    const int r1 = sp.specialAction(2);
                    sp.round = 2;
                    const int r2 = sp.specialAction(3);
                    const bool ok = r1 == 243 && r2 == 242;
                    bad += !ok;
                    std::printf("boss moves by model: group 663 round 1 %d, round 2 %d %s\n", r1, r2, ok ? "ok" : "WRONG");
                }
                { // _btlcalc_dmg_hp's story rules: 656 - Olgar alone, no heals on him, never below 1; 658 - no
                    // heals on the party while the enemies hold the Break
                    cr::GameState sg;
                    cr::giveStartingItems(sg, app.itemTable);
                    cr::BattleRules st;
                    st.scenario = 0, st.group = 656;
                    st.setupSpecial(sg, app.itemTable, 656, 0);
                    const bool olgar = st.f[0].character == 1 && st.storyNoHeal(0, -100) && !st.storyNoHeal(0, 100) && st.storyKeepsUp(0);
                    cr::BattleRules s8;
                    s8.scenario = 0, s8.group = 658;
                    s8.breakGauge = -2;
                    const bool held = s8.storyNoHeal(0, -50);
                    s8.breakGauge = 2;
                    const bool free = !s8.storyNoHeal(0, -50) && !s8.storyKeepsUp(0);
                    const bool ok = olgar && held && free;
                    bad += !ok;
                    std::printf("story heal rules: 656's Olgar %s, 658 under the enemies' Break %s, without it %s\n", olgar ? "ok" : "WRONG",
                                held ? "ok" : "WRONG", free ? "ok" : "WRONG");
                }
                { // _btlque_add_counter returns while the attacker's entry has goes left (+0x3c9): Asura Lotus
                    // (2..5 goes) on a target in counter stance is answered once, after its last go
                    int good = 0;
                    for (uint32_t seed = 1; seed <= 12; ++seed) {
                        cr::BattleRules ct;
                        ct.seed(seed);
                        ct.setupParty(g, app.itemTable, 0);
                        std::array<int, 3> one{200, 0, 0};
                        ct.setupEnemies(app.battleData, one, 0);
                        ct.f[2].hp = ct.f[2].hpMax = 99999;
                        ct.f[2].lck = -1000;
                        ct.f[0].mp = ct.f[0].mpMax = 999;
                        cr::BattleCommand cc[2];
                        cc[0].kind = cr::kCmdGene, cc[0].action = 207, cc[0].target = 2;
                        cc[1].kind = cr::kCmdDefend;
                        ct.beginRound(cc, app.battleActions);
                        ct.f[2].counterStance = true;
                        cr::BattleTurn tt;
                        int goes = 0, counters = 0, goesAfter = 0;
                        while (ct.nextTurn(app.battleActions, tt)) {
                            if (tt.counter) ++counters;
                            else if (tt.action == 207 && tt.actors[0] == 0) goes += 1, goesAfter += counters;
                        }
                        good += goes >= 2 && counters == 1 && goesAfter == 0;
                    }
                    bad += good != 12;
                    std::printf("counter after a multi-go move's last go: %d/12 %s\n", good, good == 12 ? "ok" : "WRONG");
                }
                { // btl_enemy_init_lv: a monster of no level of its own, the band 0 against a level-60 pair -
                    // its top, 10; the band -1 (the World Select's Back, then the last world) - level 0
                    int id = 0;
                    for (int k = 1; k <= app.battleData.monsterCount() && !id; ++k)
                        if (const cr::MonsterDef* md = app.battleData.monster(k); md && md->model >= 0 && md->level <= 0 && !md->strongGrowth) id = k;
                    cr::GameState lg;
                    cr::giveStartingItems(lg, app.itemTable);
                    lg.party[0].level = lg.party[1].level = 60;
                    cr::BattleRules lr;
                    lr.seed(1);
                    lr.setupParty(lg, app.itemTable, 0);
                    std::array<int, 3> one{id, 0, 0};
                    lr.setupEnemies(app.battleData, one, 0);
                    const int at0 = lr.f[2].level;
                    lr.setupEnemies(app.battleData, one, -1);
                    const int atBack = lr.f[2].level;
                    const bool ok = id > 0 && at0 == 10 && atBack == 0;
                    bad += !ok;
                    std::printf("band levels (monster %d, pair 60): band 0 %d, band -1 %d %s\n", id, at0, atBack, ok ? "ok" : "WRONG");
                }
                { // _add_item takes no item whose icon byte (+0x10) is 0 (225-228, 310-312, 395-418, ...):
                    // does a monster's drop or steal row (+0xc8..+0xda) name one? (none: no guard needed)
                    int named = 0;
                    std::string which;
                    for (int id = 1; id <= app.battleData.monsterCount(); ++id)
                        if (const cr::MonsterDef* m = app.battleData.monster(id))
                            for (int off = 0xc8; off <= 0xda; off += 2) {
                                const int it = int16_t(m->raw[size_t(off)] | m->raw[size_t(off + 1)] << 8);
                                if (it <= 0 || (it >= 81 && it <= 191)) continue; // (genes / skills: learned, not bagged)
                                const cr::ItemParam* ip = app.itemTable.item(it);
                                if (!ip || ip->icon < 0) ++named, which += " " + std::to_string(id) + ":" + std::to_string(it);
                            }
                    bad += named != 0;
                    std::printf("drop rows naming an icon-less item: %d%s %s\n", named, which.c_str(), named ? "WRONG" : "ok");
                }
                { // MuScrList_GetSellNum: the pair's worn copies (gear and plates) kept back, and scenario 1's
                    // third record's gear; scenario 0's third member's can be sold
                    cr::GameState wg;
                    wg.scenario = 0;
                    wg.party[0].equip[0] = 200, wg.party[1].genes[1] = 481, wg.party[2].equip[1] = 300;
                    const bool s0 = cr::wornCount(wg, 200) == 1 && cr::wornCount(wg, 481) == 1 && cr::wornCount(wg, 300) == 0;
                    wg.scenario = 1; // (pair 0 and 2: record 1 the third)
                    wg.party[1].equip[2] = 390;
                    const bool s1 = cr::wornCount(wg, 300) == 1 && cr::wornCount(wg, 390) == 1 && cr::wornCount(wg, 481) == 0;
                    bad += !s0 || !s1;
                    std::printf("worn copies kept from selling: scenario 0 %s, scenario 1 %s\n", s0 ? "ok" : "WRONG", s1 ? "ok" : "WRONG");
                }
                { // _GetItmPara(0xdb): Ohm's Attack is Add_Omu + 50 - items 521.. owned (5 each at first) and var 0x28
                    cr::GameState og;
                    cr::giveStartingItems(og, app.itemTable);
                    og.party[0].equip[cr::kWeapon] = 219;
                    const int bare = cr::memberStat(og, 0, cr::kAttack, app.itemTable);
                    og.items[521] = 1, og.items[522] = 1, og.vars[0x28] = 100;
                    const int grown = cr::memberStat(og, 0, cr::kAttack, app.itemTable);
                    const bool ok = grown - bare == 110;
                    bad += !ok;
                    std::printf("Ohm's Attack follows its growth: +%d %s\n", grown - bare, ok ? "ok" : "WRONG");
                }
            }
            { // decompile checks: levels from 20 add 10 HP and 1 to the others (Omega's pcparam_change_lv),
              // seeds add 10 into that bonus, to 9999 (use_item, MuCom_GetUsedItemLimitNum), guests' Speed / Luck are their Defense, a monster
              // teaches the first of its six genes not known (btlcalc_0506)
                int gbad = 0;
                gbad += cr::levelGrowth(20, cr::kHp) != 0 || cr::levelGrowth(21, cr::kHp) != 10 ||
                        cr::levelGrowth(22, cr::kHp) != 20 || cr::levelGrowth(40, cr::kHp) != 200 ||
                        cr::levelGrowth(255, cr::kHp) != 2350 || cr::levelGrowth(21, cr::kAttack) != 1 ||
                        cr::levelGrowth(99, cr::kLuck) != 79 || cr::levelGrowth(21, cr::kMp) != 1;
                cr::PartyMember pm;
                pm.level = 99;
                gbad += cr::seedRoom(pm, cr::kAttack) != (9999 - 79) / 10 + 1;
                pm.seeds[0] = 9915; // 9994: one more seed, to 9999
                gbad += cr::seedRoom(pm, cr::kAttack) != 1;
                pm.seeds[0] = 9925;
                gbad += cr::statBonus(pm, cr::kAttack) != 9999 || cr::seedRoom(pm, cr::kAttack) != 0;
                cr::GameState fresh;
                cr::giveStartingItems(fresh, app.itemTable);
                cr::BattleRules gr;
                gr.setupSpecial(fresh, app.itemTable, 695, 0); // Olgar's guest (record 3)
                const bool quirk = gr.f[1].guest && gr.f[1].spd == gr.f[1].grd && gr.f[1].lck == gr.f[1].grd &&
                                   gr.f[1].hpMax == cr::baseStat(gr.f[1].character, cr::kHp) + 50 * gr.f[1].level &&
                                   gr.f[1].character == 3 && gr.f[1].level == gr.f[0].level + 5;
                gbad += !quirk;
                int firstOk = 0, tries = 0; // monster 203 (row +0xb4.. 92 170 89 175 100 182) at level 30
                for (uint32_t seed = 1; seed <= 60 && firstOk == tries; ++seed) {
                    cr::GameState g2;
                    cr::giveStartingItems(g2, app.itemTable);
                    g2.items[92] = 1; // the first already known: the second comes next
                    cr::BattleRules k;
                    k.seed(seed);
                    k.setupParty(g2, app.itemTable, 0);
                    std::array<int, 3> one{203, 0, 0};
                    k.setupEnemies(app.battleData, one, 0);
                    k.f[2].level = 30, k.f[2].hp = 1, k.f[2].lck = -1000;
                    cr::BattleCommand cmd[2];
                    cmd[0].kind = cr::kCmdAttack, cmd[0].target = 2;
                    cmd[1].kind = cr::kCmdDefend;
                    k.beginRound(cmd, app.battleActions);
                    cr::BattleTurn tt;
                    while (k.nextTurn(app.battleActions, tt)) {}
                    if (!k.learned.empty()) ++tries, firstOk += k.learned.size() == 1 && k.learned[0] == 170;
                }
                gbad += tries == 0 || firstOk != tries;
                const bool armor = cr::armorSkill(222) == 148 && cr::armorSkill(226) == 149 && cr::armorSkill(227) == 150 &&
                                   cr::armorSkill(228) == 0 && cr::armorSkill(365) == 148 && cr::armorSkill(368) == 0 &&
                                   cr::armorSkill(421) == 0 && cr::armorSkill(215) == 0; // chk_gene_btl's lists
                gbad += !armor;
                // _get_random: MT19937 (seed 5489 when unseeded) and the original's bounded draw
                cr::GameRandom gr0, gr1;
                std::mt19937 ref;
                bool mt = true;
                for (int k = 0; k < 1000; ++k) mt &= gr0.next() == uint32_t(ref());
                const int first = gr1.below(10000); // 3499211612 >> 1, less 10000 << 17, below 10000 << 16
                int hist[6] = {};
                cr::GameRandom gr2(77);
                for (int k = 0; k < 60000; ++k) ++hist[gr2.get(6)];
                bool flat = true;
                for (int h : hist) flat &= h > 9500 && h < 10500;
                gbad += !mt || first != 6696 || !flat || gr2.get(0) != 0;
                std::printf("random: MT19937 %s, first draw below 10000 %d (6696), get_random(6) even %s\n", mt ? "yes" : "NO",
                            first, flat ? "yes" : "NO");
                std::printf("level growth: +%d HP / +%d each by level 99, seeds 10 into the 9999, guests' Speed / Luck their "
                            "Defense %s, the first unknown gene taught %d/%d, resisting armors %s\n", cr::levelGrowth(99, cr::kHp),
                            cr::levelGrowth(99, cr::kAttack), quirk ? "yes" : "NO", firstOk, tries, armor ? "yes" : "NO");
                bad += gbad;
            }
            { // btlcalc_0404's Human Genes: Risk Breaker (203) and Trompe-L'oeil (201)
                int rbBad = 0, cancels = 0, rbLanded = 0, rbPaid = 0;
                bool once = true;
                for (uint32_t seed = 1; seed <= 200; ++seed) {
                    cr::GameState fresh;
                    cr::giveStartingItems(fresh, app.itemTable);
                    for (int pass = 0; pass < 2; ++pass) {
                        cr::BattleRules k;
                        k.seed(seed);
                        k.setupParty(fresh, app.itemTable, 0);
                        k.setupEnemies(app.battleData, app.battleData.group(1), 0);
                        for (int i = 0; i < 5; ++i)
                            if (k.f[size_t(i)].present) k.f[size_t(i)].mp = k.f[size_t(i)].mpMax = 999;
                        k.f[0].spd = 9999;            // acts first
                        k.f[2].lck = -1000;           // always hit
                        k.f[2].hp = k.f[2].hpMax = 99999;
                        k.breakStarted = true;
                        k.breakGauge = k.breakTarget = 4; // the pair holds the Break
                        cr::BattleCommand cmd[2];
                        cmd[0].kind = cr::kCmdGene, cmd[0].action = pass ? 201 : 203, cmd[0].target = 2;
                        cmd[1].kind = cr::kCmdDefend;
                        const int hp0 = k.f[0].hp;
                        k.beginRound(cmd, app.battleActions);
                        cr::BattleTurn tt;
                        bool enemyActed = false, cancelled = false;
                        while (k.nextTurn(app.battleActions, tt)) {
                            if (tt.actors[0] == 2) enemyActed = true;
                            if (tt.actors[0] != 0) continue;
                            int self = -1, dealt = 0;
                            for (const cr::BattleHit& h : tt.hits)
                                if (h.target == 0) self = h.amount;
                                else dealt = std::max(dealt, h.amount);
                            if (pass == 0 && dealt > 0) ++rbLanded, rbPaid += self == hp0 / 4 && k.f[0].hp == hp0 - hp0 / 4;
                            if (pass == 0 && dealt <= 0) rbBad += self != -1; // nothing landed: nothing paid
                            for (const cr::BattleNotice& n : tt.notices) cancelled |= n.entry == 3 && n.fighter == 2;
                        }
                        if (pass == 1 && cancelled) {
                            ++cancels;
                            once &= !enemyActed;
                            for (int r = 0; r < 20; ++r) { // never again this battle
                                k.f[2].hp = k.f[2].hpMax;
                                k.beginRound(cmd, app.battleActions);
                                while (k.nextTurn(app.battleActions, tt))
                                    for (const cr::BattleNotice& n : tt.notices) once &= n.entry != 3;
                            }
                        }
                    }
                }
                rbBad += rbLanded == 0 || rbPaid != rbLanded || cancels == 0 || cancels > 60 || !once;
                std::printf("human genes: Risk Breaker cost a quarter of the HP %d/%d times, Trompe-L'oeil cancelled %d/200 "
                            "(once a battle %s)\n", rbPaid, rbLanded, cancels, once ? "yes" : "NO");
                bad += rbBad;
            }
            // skills: the damage scaling, criticals, MP, healing, regen, the Gates, level-up boosts
            {
                int sbad = 0;
                cr::BattleRules q;
                for (cr::Fighter& x : q.f) x = cr::Fighter();
                q.f[0].present = q.f[2].present = true;
                q.f[0].character = 0; // (record 0: a warrior, the +50 solo bonus)
                q.f[0].atk = 200, q.f[0].spd = 100, q.f[0].level = q.f[2].level = 1, q.f[0].hp = q.f[0].hpMax = 100;
                q.f[2].grd = 100, q.f[2].lck = -1000, q.f[2].hp = q.f[2].hpMax = 10000, q.f[2].enemy = true;
                cr::BattleActionDef hit;
                hit.id = 10, hit.power = 150, hit.calc = 0, hit.alwaysHits = true;
                const int who[2] = {0, -1};
                auto plain = [&](int want, bool critOnly) { // every non-critical (or critical) hit equals `want`
                    for (uint32_t seed = 1; seed <= 300; ++seed) {
                        q.seed(seed);
                        const cr::BattleHit h = q.physical(hit, who, 2);
                        if ((h.type == cr::kHitCritical) == critOnly && h.amount != want) return false;
                    }
                    return true;
                };
                q.f[2].skills[146 - 131] = true; // Physical Resistance: 300 x 75%
                sbad += !plain(225, false);
                q.f[0].skills[139 - 131] = true; // and Physical Attack: back to 100%
                sbad += !plain(300, false);
                q.f[2].skills[146 - 131] = q.f[0].skills[139 - 131] = false;
                q.f[0].skills[137 - 131] = true; // Critical Pierce: (250) x 2 - 0
                sbad += !plain(500, true);
                q.f[0].skills[134 - 131] = true; // and Boost Critical: x2
                sbad += !plain(1000, true);
                q.f[0].skills[137 - 131] = q.f[0].skills[134 - 131] = false;
                q.f[0].skills[135 - 131] = true; // Critical First Attack: once
                q.seed(9);
                sbad += q.physical(hit, who, 2).type != cr::kHitCritical;
                q.f[0].firstAttack = false;
                q.f[0].skills[135 - 131] = false;
                cr::BattleActionDef gene;
                gene.mp = 20;
                q.f[0].skills[132 - 131] = true;
                sbad += q.geneCost(gene, 0) != 10;
                cr::BattleActionDef cure;
                cure.power = 150, cure.calc = 12;
                q.f[0].lck = 80, q.f[0].skills[155 - 131] = true;
                sbad += q.heal(cure, who, 0).amount != -240;
                q.f[0].hp = 50, q.f[0].skills[133 - 131] = true; // regen: +5 of 100
                q.endRound();
                sbad += q.f[0].hp != 55;
                // the Gates: Dragon Gate Stage 1 (175, on plate 482) adds its item's Attack
                cr::GameState g;
                cr::giveStartingItems(g, app.itemTable);
                const int base = cr::memberStat(g, 0, cr::kAttack, app.itemTable);
                g.items[482] = 1, g.items[175] = 1;
                g.party[0].genes[1] = 482;
                const cr::ItemParam* gate = app.itemTable.item(175);
                sbad += !gate || cr::memberStat(g, 0, cr::kAttack, app.itemTable) != base + gate->stat[cr::kAttack];
                g.items[164] = 1; // HP Boost isn't on a worn plate: nothing
                cr::levelUpBoosts(g, 0, 3);
                sbad += g.party[0].boosts[0] != 0;
                g.items[166] = 1, g.items[483] = 1, g.party[0].genes[2] = 483; // Attack Boost (plate 483): +5 a level
                cr::levelUpBoosts(g, 0, 3);
                sbad += g.party[0].boosts[2] != 15;
                // the other calcs, each through a round: member 0 uses the action on its target
                auto runAs = [&](int action, int target, cr::BattleRules& rr) {
                    for (cr::Fighter& x : rr.f) x = cr::Fighter();
                    for (int i : {0, 1, 2, 3}) {
                        cr::Fighter& x = rr.f[size_t(i)];
                        x.present = true, x.enemy = i >= 2, x.level = 1, x.hp = x.hpMax = 5000, x.mp = x.mpMax = 400;
                        x.atk = 100, x.grd = 0, x.spd = i == 0 ? 10000 : 1, x.lck = 0, x.character = i < 2 ? i : -1;
                    }
                    rr.purse = 1000;
                    cr::BattleCommand cc[2];
                    cc[0].kind = cr::kCmdGene, cc[0].action = action, cc[0].target = target;
                    cc[1].kind = cr::kCmdDefend;
                    rr.beginRound(cc, app.battleActions);
                    cr::BattleTurn tt, first;
                    bool got = false;
                    while (rr.nextTurn(app.battleActions, tt))
                        if (!got && tt.actors[0] == 0) first = tt, got = true;
                    return first;
                };
                int cbad = 0;
                { // Omega's calcs 42..44 and the stats under Down / the Ward (btlpcparam_atk)
                    int obad = 0;
                    cr::BattleRules r44; runAs(439, 2, r44); // power 2: 200 HP (with its variance), 20 MP
                    obad += r44.f[2].mp != 380 || r44.f[2].hp >= 5000 || r44.f[2].hp < 4700;
                    cr::BattleRules rw; runAs(402, 3, rw); // (126 is the enemies' own: the Ward set here)
                    rw.f[2].status[cr::kStatusWard] = 1;
                    obad += rw.atkOf(2) != 200;
                    rw.f[2].status[cr::kStatusAttackUp] = 3;
                    obad += rw.atkOf(2) != 300; // the up: its whole value with the Ward
                    cr::BattleRules rd; runAs(441, 2, rd);
                    obad += rd.f[2].status[cr::kStatusDown] != 5 || rd.atkOf(2) != 80;
                    rd.f[2].status[cr::kStatusAttackUp] = 3;
                    obad += rd.atkOf(2) != 100; // 80 + a fifth
                    std::printf("omega calcs: 44 (HP / MP), 42 (Down), the stats with the Ward / Down %s\n", obad ? "WRONG" : "ok");
                    if (obad)
                        std::printf("  44: hp %d mp %d; ward %d %d atk %d; down %d %d atk %d\n", r44.f[2].hp, r44.f[2].mp,
                                    rw.f[2].status[cr::kStatusWard], rw.f[0].status[cr::kStatusWard], rw.atkOf(2),
                                    rd.f[2].status[cr::kStatusDown], rd.f[0].status[cr::kStatusDown], rd.atkOf(2));
                    cbad += obad;
                }
                { cr::BattleRules rr; runAs(402, 2, rr); if (rr.f[2].hp != 4000) std::printf("  fixed: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(433, 2, rr); if (rr.f[2].hp != 3000 || rr.f[3].hp != 5000) std::printf("  shared: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(432, 2, rr); if (rr.f[2].hp != 1) std::printf("  to1: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(404, 2, rr); if (rr.f[2].hp != 3500) std::printf("  pct: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(403, 2, rr); if (rr.f[2].mp != 300) std::printf("  mp: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(409, 2, rr); if (rr.ozStolen != 100 || rr.purse != 900) std::printf("  oz: hp %d %d mp %d oz %d\n", rr.f[2].hp, rr.f[3].hp, rr.f[2].mp, rr.ozStolen), ++cbad; }
                { cr::BattleRules rr; runAs(109, 0, rr); cbad += !rr.doubleExp; }
                { // a barrier sends the next hostile gene back on its caster, then is gone
                    cr::BattleRules rr;
                    runAs(212, 2, rr);
                    const int plain = 5000 - rr.f[2].hp;
                    cr::BattleRules rc;
                    for (cr::Fighter& x : rc.f) x = cr::Fighter();
                    for (int i : {0, 1, 2, 3}) {
                        cr::Fighter& x = rc.f[size_t(i)];
                        x.present = true, x.enemy = i >= 2, x.level = 1, x.hp = x.hpMax = 5000, x.mp = x.mpMax = 400;
                        x.atk = 100, x.spd = i == 0 ? 10000 : 1, x.character = i < 2 ? i : -1;
                    }
                    rc.f[2].status[cr::kStatusBarrier] = 5;
                    cr::BattleCommand cc[2];
                    cc[0].kind = cr::kCmdGene, cc[0].action = 212, cc[0].target = 2;
                    cc[1].kind = cr::kCmdDefend;
                    rc.beginRound(cc, app.battleActions);
                    cr::BattleTurn tt;
                    int hp2 = -1, hp0 = -1;
                    bool back = false;
                    while (rc.nextTurn(app.battleActions, tt))
                        if (tt.actors[0] == 0) hp2 = rc.f[2].hp, hp0 = rc.f[0].hp, back = tt.reflected;
                    if (plain <= 0 || hp2 != 5000 || hp0 >= 5000 || !back || rc.f[2].status[cr::kStatusBarrier] != 0)
                        std::printf("  barrier: plain %d, with %d, caster %d, left %d\n", plain, hp2, hp0, rc.f[2].status[cr::kStatusBarrier]), ++cbad;
                }
                { // revive: a fallen member back with half its HP
                    cr::BattleRules rr;
                    for (cr::Fighter& x : rr.f) x = cr::Fighter();
                    for (int i : {0, 1, 2}) {
                        cr::Fighter& x = rr.f[size_t(i)];
                        x.present = true, x.enemy = i >= 2, x.level = 1, x.hp = x.hpMax = 1000, x.mp = x.mpMax = 400;
                        x.atk = 100, x.spd = i == 0 ? 10000 : 1, x.character = i < 2 ? i : -1;
                    }
                    rr.f[1].hp = 0;
                    cr::BattleCommand cc[2];
                    cc[0].kind = cr::kCmdGene, cc[0].action = 230, cc[0].target = 1;
                    rr.beginRound(cc, app.battleActions);
                    cr::BattleTurn tt;
                    while (rr.nextTurn(app.battleActions, tt)) {}
                    cbad += rr.f[1].hp != 500;
                }
                { // poison: 10% a round; invincible: no damage
                    cr::BattleRules rr;
                    for (cr::Fighter& x : rr.f) x = cr::Fighter();
                    rr.f[2].present = true, rr.f[2].hp = rr.f[2].hpMax = 1000, rr.f[2].status[cr::kStatusPoison] = 5;
                    rr.f[0].present = true, rr.f[0].hp = rr.f[0].hpMax = 1000;
                    rr.endRound();
                    cbad += rr.f[2].hp != 900;
                }
                { // story line-ups (btl_prty_init): 651 Vieg alone, 695 Olgar with the guest at his level
                  // + 5 (no rotation: it takes orders; the enemies' levels from Olgar's alone), 675 scenario
                  // 1's leader alone
                    cr::GameState g;
                    cr::giveStartingItems(g, app.itemTable);
                    cr::BattleRules a;
                    a.setupParty(g, app.itemTable, 0);
                    a.setupSpecial(g, app.itemTable, 651, 0);
                    cbad += a.f[0].character != 0 || a.f[1].present;
                    cr::BattleRules gb;
                    gb.setupParty(g, app.itemTable, 0);
                    gb.setupSpecial(g, app.itemTable, 695, 0);
                    cbad += gb.allyAuto || gb.f[0].character != 1 || gb.f[1].character != 3 ||
                            gb.f[1].level != gb.f[0].level + 5 || !gb.f[1].guest || gb.leaderLevel() != gb.f[0].level;
                    cr::BattleRules zb;
                    g.scenario = 1;
                    zb.setupParty(g, app.itemTable, 0);
                    zb.setupSpecial(g, app.itemTable, 675, 0);
                    cbad += zb.f[0].character != 0 || zb.f[1].present;
                    cr::BattleRules nb; // not a story group: the pair as it is
                    nb.setupParty(g, app.itemTable, 0);
                    nb.setupSpecial(g, app.itemTable, 651, 0);
                    cbad += !nb.f[1].present;
                }
                { // a monster with row +0x3e 2 acts twice; Asura Lotus strikes 2..5 times
                    cr::BattleRules rr;
                    cr::GameState g;
                    rr.setupParty(g, app.itemTable, 0);
                    std::array<int, 3> one{81, 0, 0};
                    rr.setupEnemies(app.battleData, one, 0);
                    rr.f[0].hp = rr.f[0].hpMax = rr.f[1].hp = rr.f[1].hpMax = 99999;
                    rr.f[2].hp = rr.f[2].hpMax = 99999;
                    cr::BattleCommand cc[2];
                    cc[0].kind = cc[1].kind = cr::kCmdDefend;
                    rr.beginRound(cc, app.battleActions);
                    cr::BattleTurn tt;
                    int turns = 0;
                    while (rr.nextTurn(app.battleActions, tt)) turns += tt.actors[0] == 2;
                    if (turns != 2) std::printf("  twice-acting monster acted %d times\n", turns), ++cbad;
                    cr::BattleRules lo;
                    int fewest = 99, most = 0;
                    for (uint32_t seed = 1; seed <= 30; ++seed) {
                        lo.seed(seed);
                        int n = 0;
                        runAs(207, 2, lo);
                        cr::BattleRules q2;
                        q2.seed(seed);
                        for (cr::Fighter& x : q2.f) x = cr::Fighter();
                        for (int i : {0, 1, 2}) {
                            cr::Fighter& x = q2.f[size_t(i)];
                            x.present = true, x.enemy = i >= 2, x.level = 1, x.hp = x.hpMax = 99999, x.mp = x.mpMax = 400;
                            x.atk = 10, x.spd = i == 0 ? 10000 : 1, x.character = i < 2 ? i : -1;
                        }
                        cr::BattleCommand c2[2];
                        c2[0].kind = cr::kCmdGene, c2[0].action = 207, c2[0].target = 2;
                        c2[1].kind = cr::kCmdDefend;
                        q2.beginRound(c2, app.battleActions);
                        while (q2.nextTurn(app.battleActions, tt)) n += tt.actors[0] == 0;
                        fewest = std::min(fewest, n), most = std::max(most, n);
                    }
                    if (fewest != 2 || most != 5) std::printf("  Asura Lotus %d..%d times\n", fewest, most), ++cbad;
                }
                { // Steal Item: the first monster with something to steal (+0xd2, the first of five by level:
                  // the only one below level 25), once
                    int got = 0, twice = 0, thief = 1, loot = 0;
                    for (int id = 1; id < 600 && !loot; ++id) {
                        const cr::MonsterDef* m = app.battleData.monster(id);
                        if (!m || m->model < 0) continue;
                        if (int16_t(m->raw[0xd2] | m->raw[0xd3] << 8) > 0) thief = id, loot = int16_t(m->raw[0xd2] | m->raw[0xd3] << 8);
                    }
                    for (uint32_t seed = 1; seed <= 30; ++seed) {
                        cr::BattleRules rr;
                        rr.seed(seed);
                        cr::GameState g;
                        rr.setupParty(g, app.itemTable, 0);
                        std::array<int, 3> one{thief, 0, 0};
                        rr.setupEnemies(app.battleData, one, 0);
                        rr.f[2].hp = rr.f[2].hpMax = 99999, rr.f[2].lck = 0, rr.f[2].level = 1;
                        rr.f[0].lck = 500, rr.f[0].atk = 3000; // (no damage, no catch)
                        for (int r = 0; r < 2; ++r) {
                            cr::BattleCommand cc[2];
                            cc[0].kind = cr::kCmdGene, cc[0].action = 223, cc[0].target = 2;
                            cc[1].kind = cr::kCmdDefend;
                            rr.beginRound(cc, app.battleActions);
                            cr::BattleTurn tt;
                            while (rr.nextTurn(app.battleActions, tt)) {}
                            rr.endRound();
                        }
                        got += !rr.stolen.empty() && rr.stolen[0] == loot;
                        twice += rr.stolen.size() > 1;
                    }
                    if (got == 0 || twice || !loot) std::printf("  steal (monster %d): %d got, %d twice\n", thief, got, twice), ++cbad;
                }
                std::printf("calcs: fixed, split by targets, to 1, %%HP, MP, Oz stolen, Apples, barrier, revive, poison: %d problems\n", cbad);
                sbad += cbad;
                { // btlcalc_zoku_0203: resistance 1 is immune, no element no element resistance; Omega's
                  // 187 / 188 kill on a weak hit (5%, not a +0x1c monster); status_prc: 190 a quarter
                  // back (5%), the round's changes added up (133 + poison at full HP: 95)
                    int zbad = 0;
                    cr::MonsterDef md;
                    cr::BattleRules q;
                    const auto reset = [&] {
                        for (cr::Fighter& x : q.f) x = cr::Fighter();
                        q.f[0].present = q.f[2].present = true;
                        q.f[0].atk = 200, q.f[0].spd = 100, q.f[0].level = q.f[2].level = 1, q.f[0].hp = q.f[0].hpMax = 100;
                        q.f[2].grd = 0, q.f[2].hp = q.f[2].hpMax = 10000, q.f[2].enemy = true, q.f[2].def = &md;
                    };
                    cr::BattleActionDef hit;
                    hit.id = 10, hit.power = 150, hit.calc = 0, hit.alwaysHits = true, hit.scope = 4, hit.side = 3, hit.affinity = true;
                    const int who[2] = {0, -1};
                    md.raw[0x10] = 1; // immune to blows
                    reset(), q.seed(3);
                    zbad += q.physical(hit, who, 2).amount != 0;
                    md.raw[0x10] = 0, md.raw[0x14] = 1; // immune to element 1, the attacker without one
                    reset(), q.seed(3);
                    zbad += q.physical(hit, who, 2).amount == 0;
                    md.raw[0x14] = 0;
                    int kills = 0, guarded = 0;
                    for (uint32_t seed = 1; seed <= 1000; ++seed) {
                        for (int row1c : {0, 1}) {
                            md.raw[0x1c] = uint8_t(row1c);
                            reset();
                            q.f[0].element = 1, q.f[2].element = 3, q.f[0].skills[187 - 131] = true;
                            q.seed(seed);
                            const bool kill = q.physical(hit, who, 2).type == cr::kHitKill;
                            (row1c ? guarded : kills) += kill;
                        }
                    }
                    md.raw[0x1c] = 0;
                    zbad += kills < 25 || kills > 80 || guarded != 0;
                    int healed = 0;
                    for (uint32_t seed = 1; seed <= 1000; ++seed) {
                        reset();
                        q.f[0].hp = 100, q.f[0].hpMax = 400, q.f[0].skills[190 - 131] = true;
                        q.seed(seed);
                        q.endRound();
                        healed += q.f[0].hp == 200;
                    }
                    zbad += healed < 25 || healed > 80;
                    reset();
                    q.f[0].skills[133 - 131] = true, q.f[0].status[cr::kStatusPoison] = 3;
                    q.endRound();
                    zbad += q.f[0].hp != 95 || q.f[0].status[cr::kStatusPoison] != 2;
                    std::printf("resistances and Omega's skills 187..190: %d kills in 1000 weak hits, %d heals in 1000 rounds; %d problems\n",
                                kills, healed, zbad);
                    sbad += zbad;
                }
                std::printf("skills: resistance, attack, pierce, boost critical, first attack, MP 1/2, recovery, regen, "
                            "Gate +%d, boosts: %d problems\n", gate ? gate->stat[cr::kAttack] : 0, sbad);
                bad += sbad;
            }
            std::printf("battle rules: %d physical hits (%d critical) as worked out, healing, Break, enemy levels %d..%d, "
                        "items, escape: %d problems\n", normal + crits, crits, lo, hi, bad);
            problems += bad;
        }

        // Every effect (eff.bin's 98): loaded, played on an enemy, drawn without render API errors
        {
            int loaded = 0, played = 0, ferrs = 0;
            startBattle(app, {1, 0, -1, 0}, 0);
            if (app.battle.active) {
                for (int id = 0; id < 100; ++id) {
                    if (!effectModel(app, id)) continue;
                    ++loaded;
                    spawnEffect(app, id, 2);
                    played += !app.battle.effects.empty();
                    for (int f = 0; f < 6; ++f) stepBattle(app);
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    ferrs += app.renderer.takeErrors();
                    app.battle.effects.clear();
                }
                endBattle(app, false);
                app.runner.stop();
            }
            std::printf("battle effects: %d of eff.bin's entries loaded, %d played, %d render API errors\n", loaded, played, ferrs);
            problems += (loaded < 98) + (played < loaded) + ferrs;
            glErrors += ferrs;
        }

        // The bosses' staged actions (btlact_main's kinds 10..22): each plays its own slot (the boss
        // models' 14th / 15th, read past the usual 13), steps through its cameras, flashes and
        // shakes where it has them, hits at its frame and ends after its wait (the later ones: as
        // their motion ends, plus theirs); hits shake the screen; and the effects' textures scroll
        // with their materials' actions
        {
            // hit -1: the slot's loop + 45, -2: the motion's end; wait -1: the motion's end + the
            // action's own; cams -1: chained, as many as there are; quake: one before the hit;
            // after: the strongest from the hit on (3 = the calc's own, when a hit lands; -1 not checked)
            struct Want { int action, group, hit, wait, cams; bool flash, quake; float after; };
            const Want wants[] = {{430, 340, 140, 40, 3, true, true, 3},     {432, 348, -1, 40, 5, false, false, 0},
                                  {434, 352, 130, 65, 2, true, true, 3},     {241, 664, 63, 142, 0, true, false, 3}, /* btlact_435 from 100 */
                                  /* (436: only CR1's boss rows still list it - model 0, no slot 13) */   {503, 666, 140, 40, 3, true, true, 3},
                                  {505, 716, -1, 40, 5, false, false, 0},    {507, 667, 130, 65, 2, true, true, 3},
                                  {437, 655, 0xd7, -1, -1, true, false, 0},    {439, 659, 0x62, -1, -1, true, true, -1},
                                  {440, 657, 0x97, -1, -1, true, true, -1},  {441, 660, 0x52, -1, -1, false, true, -1},
                                  {442, 660, 0x9b, -1, -1, true, true, -1},  {511, 677, -2, 40, -1, false, false, -1}};
            constexpr int kWants = int(sizeof wants / sizeof wants[0]);
            int sbad = 0, rerrs = 0, ran = 0, hitQuakes = 0, otherSlot = -1;
            const cr::GameState saved = app.game;
            for (const Want& w : wants) {
                app.game = saved;
                startBattle(app, {w.group, 0, -1, 0}, 0);
                App::Battle& b = app.battle;
                const cr::BattleActionDef* def = app.battleActions.get(w.action);
                if (!b.active || !def) { ++sbad; continue; }
                b.rules.forceAction = w.action;
                for (int k = 0; k < 2; ++k) // (the pair outlasts the later groups' blows)
                    if (b.rules.f[size_t(k)].present) b.rules.f[size_t(k)].hp = b.rules.f[size_t(k)].hpMax = 9999999;
                int hitAt = -1, endAt = -1, cams = 0, slot = -1, stageEnd = 0, motionEnd = 0;
                float flash = 0, quake = 0, after = 0;
                bool started = false, landed = false;
                for (int f = 0; f < 8000 && b.active && endAt < 0; ++f) {
                    if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) { // the pair defends
                        battleChooseTop(app, 0);
                        battleChooseCommand(app, 3);
                        if (b.menu == App::Battle::Commands) battleChooseCommand(app, 3);
                    }
                    const bool was = b.turnOn && b.turn.action == w.action;
                    stepBattle(app);
                    const bool on = b.turnOn && b.turn.action == w.action;
                    if (on && w.action == 440 && b.turnFrame == 10)
                        if (const App::Battle::Actor* o = battleActor(app, kPropFighter)) otherSlot = o->slot;
                    if (on && !started) {
                        started = true;
                        if (const App::Battle::Actor* u = battleActor(app, b.turn.actors[0])) slot = u->slot;
                        stageEnd = b.stageEnd;
                        motionEnd = b.hitFrame;
                    }
                    if (on) {
                        cams = std::max(cams, b.stageCams);
                        flash = std::max(flash, b.flash.on ? b.flash.alpha : 0.0f);
                        if (!b.hitsShown) quake = std::max({quake, std::fabs(b.quake.off[0]), std::fabs(b.quake.off[1])});
                        else if (b.quake.mode) after = std::max(after, b.quake.amp);
                        if (b.hitsShown && hitAt < 0) {
                            hitAt = b.turnFrame;
                            for (const cr::BattleHit& h : b.turn.hits) // btlcalc_0404 / 0406 shake for a hit that lands
                                landed |= h.type != cr::kHitGuard && h.type != cr::kHitMiss && (def->calc == 1 ? h.amount >= 0 : h.amount > 0);
                        }
                        if (b.turnFrame == 20 || b.turnFrame == hitAt) {
                            app.lastTicks = 0;
                            drawFrame(app, false);
                            rerrs += app.renderer.takeErrors();
                        }
                    }
                    if (was && !b.turnOn) endAt = b.turnFrame;
                }
                const float wantAfter = w.after > 3 ? w.after : w.after > 0 && landed ? 3.0f : 0.0f;
                const int wantHit = w.hit >= 0 ? w.hit : w.hit == -2 ? motionEnd
                                  : app.battleData.anim(battleActor(app, 2)->modelId, def->cls).loopStart + 45;
                const int wantEnd = w.wait >= 0 ? wantHit + w.wait : std::max(stageEnd, wantHit + 1);
                const int bad = !started + (slot != def->cls) + (hitAt != wantHit) + (endAt != wantEnd) + (w.wait < 0 && stageEnd <= 0) +
                                (w.cams >= 0 && cams != w.cams) + (w.flash != (flash >= 254)) + (w.quake != (quake > 0.5f)) +
                                (w.after >= 0 && after != wantAfter);
                if (bad)
                    std::printf("  staged %d: slot %d (cls %d), hit at %d (want %d), ended at %d, %d cameras, flash %.0f, "
                                "quake %.1f, after the hit %.0f\n", w.action, slot, def->cls, hitAt, wantHit, endAt, cams, flash,
                                quake, after);
                sbad += bad;
                ran += started;
                endBattle(app, false);
                app.runner.stop();
            }
            // a plain hit (btlcalc_0404) shakes the screen for 5 frames
            app.game = saved;
            startBattle(app, {1, 0, -1, 0}, 0);
            if (app.battle.active) {
                App::Battle& b = app.battle;
                for (int f = 0; f < 3000 && b.active && !hitQuakes; ++f) {
                    if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) {
                        battleChooseTop(app, 1);
                        battleChooseCommand(app, 0);
                        if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
                    }
                    stepBattle(app);
                    if (b.turnOn && b.hitsShown && b.turn.actors[0] < 2 && !b.turn.hits.empty() && b.turn.hits[0].amount > 0)
                        hitQuakes = b.quake.mode == 1 && b.quake.frames == 5;
                }
                endBattle(app, false);
                app.runner.stop();
            }
            // the first effect with scrolling materials (CR1: 48's light shafts): their offset moves
            // between frames 5 and 15
            int scrolling = 0, moved = 0, scrollFx = -1;
            startBattle(app, {1, 0, -1, 0}, 0);
            for (int id = 0; id < effects::kEffects && scrollFx < 0 && app.battle.active; ++id)
                if (const LoadedModel* fx = effectModel(app, id))
                    for (const cr::Material& mt : fx->cpu.materials) {
                        float s0[2] = {0, 0}, k0[2] = {1, 1};
                        if (effectUvMotion(*fx, &mt, 5, s0, k0)) { scrollFx = id; break; }
                    }
            if (const LoadedModel* fx = app.battle.active && scrollFx >= 0 ? effectModel(app, scrollFx) : nullptr)
                for (const cr::Material& mt : fx->cpu.materials) {
                    float s0[2] = {0, 0}, s1[2] = {0, 0}, k0[2] = {1, 1}, k1[2] = {1, 1};
                    if (!effectUvMotion(*fx, &mt, 5, s0, k0)) continue;
                    ++scrolling;
                    effectUvMotion(*fx, &mt, 15, s1, k1);
                    moved += s0[0] != s1[0] || s0[1] != s1[1] || k0[0] != k1[0] || k0[1] != k1[1];
                }
            if (app.battle.active) endBattle(app, false);
            app.runner.stop();
            app.game = saved;
            const int boss15 = app.battleData.anim(9, 14).loopEnd > 0 || app.battleData.anim(9, 14).hit > 0;
            sbad += otherSlot != 14; // btlact_440_init: group 0x291's extra model on to slot 14
            std::printf("battle staging: %d of %d boss actions staged, hit quake %s, effect %d: %d of %d scrolling materials move, "
                        "boss slots past 13 %s, 440's extra model in slot %d; %d problems, %d render API errors\n", ran, kWants, hitQuakes ? "yes" : "no", scrollFx, moved,
                        scrolling, boss15 ? "read" : "missing", otherSlot, sbad, rerrs);
            problems += sbad + rerrs + (ran != kWants) + !hitQuakes + (scrolling == 0 || moved == 0);
            glErrors += rerrs;
        }

        // The weather goes on in battles with its own copies (set 1's snow: slots 0x3d / 0x3f, stepped
        // by the battle's frames; still with Environment Effects off); the field-only backdrops
        // stand still there; on the field the space backdrop (28) scrolls ever down and the
        // elevator's walls (13, 14) slide and stop; all of it drawn without errors
        {
            int fbad = 0, ferrs = 0;
            loadSceneEffectSet(app, 1);
            setSceneEffect(app, true, 5);
            startBattle(app, {1, 0, -1, 0}, 0);
            if (app.battle.active) {
                fbad += !app.fx.tex[0x3d - 0x30].id || !app.fx.tex[0x3f - 0x30].id || !app.fx.tex[0x45 - 0x30].id;
                const int t0 = app.fx.timerA[5];
                for (int f = 0; f < 10; ++f) stepBattle(app);
                fbad += app.fx.timerA[5] != t0 + 10;
                app.environmentEffects = false; // Options: Environment Effects off - the weather stands still, undrawn
                for (int f = 0; f < 5; ++f) stepBattle(app);
                fbad += app.fx.timerA[5] != t0 + 10;
                app.environmentEffects = true;
                app.lastTicks = 0;
                drawFrame(app, false);
                ferrs += app.renderer.takeErrors();
                endBattle(app, false);
                app.runner.stop();
            } else ++fbad;
            endSceneEffects(app);
            loadSceneEffectSet(app, 10);
            fbad += !app.fx.tex[0x40 - 0x30].id;
            setSceneEffect(app, true, 28);
            const int ct0 = app.fx.frameCt;
            for (int f = 0; f < 100; ++f) stepSceneEffects(app);
            const int scrolled = ct0 - app.fx.frameCt;
            endSceneEffects(app);
            loadSceneEffectSet(app, 7);
            fbad += !app.fx.tex[0x50 - 0x30].id || !app.fx.tex[0x51 - 0x30].id || !app.fx.tex[0x52 - 0x30].id;
            setSceneEffect(app, true, 13);
            setSceneEffect(app, true, 14);
            for (int f = 0; f < 150; ++f) stepSceneEffects(app);
            const int slid13 = app.fx.timerA[13], slid14 = app.fx.timerA[14];
            fbad += scrolled != 100 || slid13 != 100 || slid14 != 40;
            startBattle(app, {1, 0, -1, 0}, 0);
            if (app.battle.active) {
                app.lastTicks = 0;
                drawFrame(app, false);
                ferrs += app.renderer.takeErrors();
                endBattle(app, false);
                app.runner.stop();
            }
            endSceneEffects(app);
            std::printf("battle weather: snow over a battle with its own copies, the space backdrop scrolled %d, the elevator's "
                        "walls slid %d / %d; %d problems, %d render API errors\n", scrolled, slid13, slid14, fbad, ferrs);
            problems += fbad + ferrs;
            glErrors += ferrs;
        }

        // Battle voices (voiced data only): the 2.2.0 action records line up (Particle Cannon's class
        // 13, Attack's motion), the speakers come from the name ids, and a fought battle says its
        // lines - commands, attacks, hits, the victory - from the table's clips
        if (!app.battleVoiceTable.empty() && app.sound.voiceClips(true) == 0) startSound(app), vitaAudio(app); // (tests run before it)
        if (btlvoice::on(app)) {
            int vbad = 0;
            const cr::BattleActionDef* cannon = app.battleActions.get(430);
            vbad += !cannon || cannon->cls != 13 || cannon->calc != 0 || app.battleActions.count() < 450;
            startBattle(app, {1, 0, -1, 0}, 0);
            App::Battle& b = app.battle;
            if (b.active) {
                vbad += btlvoice::speaker(app, 0) != b.rules.f[0].character; // the party's own
                { // FUN_000928b0's lines by the damage and the HP left, and its gates; chooseSpeaker(99)'s coin; the win's wait
                    auto bad = [&](bool failed, const char* what) { if (failed) { ++vbad; std::printf("battle voices FAILED: %s\n", what); } };
                    auto said = [&](int situation) { // the log's last clip is member 0's `situation` (none in the table: nothing to say)
                        const int c = btlvoice::clipFor(app, btlvoice::speaker(app, 0), situation);
                        return c <= 0 || (!app.sound.battleLineLog.empty() && app.sound.battleLineLog.back() == c);
                    };
                    auto lineOf = [&](int from) { return said(from) || said(from + 1) || said(from + 2); };
                    cr::Fighter& m = b.rules.f[0];
                    const int hp = m.hp;
                    for (bool& sp : b.voiceSpeaks) sp = true;
                    b.turn = cr::BattleTurn();
                    btlvoice::hpChange(app, 0, 0);
                    bad(!lineOf(0x22), "no damage says 0x22..0x24");
                    m.hp = m.hpMax;
                    btlvoice::hpChange(app, 0, 1);
                    bad(!lineOf(0x16), "a hit above a quarter HP says 0x16..0x18");
                    m.hp = std::max(1, m.hpMax / 5);
                    btlvoice::hpChange(app, 0, 1);
                    bad(!lineOf(0x19), "a hit at a quarter HP or less says 0x19..0x1b");
                    m.hp = 0;
                    btlvoice::hpChange(app, 0, 1);
                    bad(!lineOf(0x1c), "a fall says 0x1c..0x1e");
                    b.turn.counterSkill = true;
                    btlvoice::hpChange(app, 0, 1);
                    bad(!said(0xa8), "the Counter skill's answer says 0xa8");
                    b.turn.counterSkill = false;
                    b.turn.action = 437;
                    const size_t n = app.sound.battleLineLog.size();
                    m.hp = m.hpMax;
                    btlvoice::hpChange(app, 0, m.hpMax - 1); // 437: only while the HP left equals the damage
                    bad(app.sound.battleLineLog.size() != n, "437 with the HP left unequal to the damage says nothing");
                    btlvoice::hpChange(app, 0, m.hpMax);
                    bad(!lineOf(0x16) || app.sound.battleLineLog.size() != n + 1, "437 with the HP left equal to the damage reacts");
                    b.turn = cr::BattleTurn();
                    m.hp = hp;
                    if (b.rules.f[1].present) { // chooseSpeaker(99): member 1 may be picked though down
                        const int hp1 = b.rules.f[1].hp;
                        b.rules.f[1].hp = 0;
                        bool one = false, zero = false;
                        for (int i = 0; i < 40; ++i) (btlvoice::chooseSpeaker(app, 99) ? one : zero) = true;
                        bad(!one || !zero, "chooseSpeaker(99) flips a coin with member 1 down");
                        b.rules.f[1].hp = hp1;
                    }
                    const int group = b.start.group;
                    bad(btlvoice::winWaits(app), "no win wait outside the listed groups");
                    b.start.group = 673;
                    btlvoice::play(app, 0, 1, 1, 1);
                    if (app.sound.battleLinePlaying()) bad(!btlvoice::winWaits(app), "group 673's win waits for a line playing");
                    app.sound.stopBattleLines();
                    bad(btlvoice::winWaits(app), "no win wait once the lines are stopped");
                    b.start.group = group;
                }
                app.sound.battleLineLog.clear();
                for (int f = 0; f < 6000 && b.active && b.phase != App::Battle::Results && b.phase != App::Battle::Lost; ++f) {
                    if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) {
                        battleChooseTop(app, 0);
                        for (int k = 0; k < 2 && b.phase == App::Battle::Command && b.menu == App::Battle::Commands; ++k) {
                            battleChooseCommand(app, 0);
                            if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
                        }
                    }
                    stepBattle(app);
                }
                const std::vector<int> log = app.sound.battleLineLog;
                const bool won = b.phase == App::Battle::Results;
                int inRange = 0;
                for (int c : log) inRange += c > 0 && c < app.sound.voiceClips(true);
                vbad += log.size() < 3 || inRange != int(log.size());
                if (won && !log.empty()) { // the last line is the victory's: 500..505, 506..511 with a strong-growth monster in it
                    const int base = btlvoice::strongInBattle(app) ? 506 : 500;
                    bool victoryLine = false;
                    for (size_t i = 0; i < app.battleVoiceTable.size(); ++i)
                        if (app.battleVoiceTable[i] == log.back() && int(i % 1000) + 1 >= base && int(i % 1000) + 1 < base + 6) victoryLine = true;
                    if (!victoryLine) { ++vbad; std::printf("battle voices FAILED: the won battle's last line is a victory line (%d..)\n", base); }
                }
                if (SDL_getenv("CR_BATTLE_DEBUG"))
                    for (int c : log)
                        for (size_t i = 0; i < app.battleVoiceTable.size(); ++i)
                            if (app.battleVoiceTable[i] == c) { std::printf("  line: speaker %zu situation %zu (clip %d)\n", i / 1000, i % 1000 + 1, c); break; }
                std::printf("battle voices: %zu lines in a %s battle (%d clips in range), action 430 class %d; %d problems\n",
                            log.size(), won ? "won" : "lost", inRange, cannon ? cannon->cls : -1, vbad);
                endBattle(app, won);
                app.runner.stop();
            } else ++vbad;
            problems += vbad;
        } else
            std::printf("battle voices: none in this data (%zu situations, %d clips)\n", app.battleVoiceTable.size(),
                        app.sound.voiceClips(true));

        // Whole battles through the scene and its menus: pair or solo attacks, defending, an item,
        // escaping - every one ends (won, lost or fled) and draws without render API errors
        {
            app.rng.seed(5);
            int won = 0, lost = 0, fled = 0, stuck = 0, rerrs = 0, n = 0, levelled = 0, resultsBad = 0, purseClimbed = 0, purseHeld = 0;
            const cr::GameState saved = app.game;
            std::vector<std::pair<int, int>> list(starts.begin(), starts.end());
            for (size_t i = 0; i < list.size() && n < 24; i += std::max<size_t>(1, list.size() / 24), ++n) {
                app.game = saved;
                app.game.items[21] = 2;
                startBattle(app, {list[i].second, list[i].first, -1, int(i % 3 == 2 ? 1 : 0)}, 0);
                App::Battle& b = app.battle;
                if (!b.active) continue;
                const int style = n % 4; // 0 pair attacks, 1 solo attacks, 2 defend then attack + an item, 3 escape
                int frame = 0, ended = 0;
                for (; frame < 30000 && b.active; ++frame) {
                    if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) {
                        if (style == 3 && b.rules.round >= 1) battleChooseTop(app, 2), battleEscapeAnswer(app, true, false);
                        else {
                            const bool both = b.rules.f[0].alive() && b.rules.f[1].alive();
                            battleChooseTop(app, style == 0 && both ? 1 : 0);
                            for (int k = 0; k < 2 && b.phase == App::Battle::Command; ++k) {
                                if (style == 2 && b.rules.round == 0 && k == 0) { battleChooseCommand(app, 3); continue; }
                                if (style == 2 && b.rules.round == 1 && k == 0 && app.game.items[21] > 0) {
                                    battleChooseCommand(app, 2);
                                    if (b.menu == App::Battle::Items) battleChooseItem(app, b.picks[0]);
                                    if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
                                    continue;
                                }
                                battleChooseCommand(app, 0);
                                if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks.back());
                            }
                        }
                    }
                    if (b.phase == App::Battle::Results) { // MbResultPrc: a tap ends the counting, then a line a tap, then away
                        ++won;
                        levelled += int(b.levelled.size());
                        const size_t lines = b.result.ups.size() + b.result.items.size();
                        // the purse: below the OZ earned it climbs; at or above it, it holds until the count settles
                        {
                            const App::Battle::Result keep = b.result;
                            const float from = b.result.shownPurse;
                            for (int g = 0; g < 10 && b.result.count > 0; ++g) stepResults(app);
                            const bool climbs = from < float(keep.ozTotal) && keep.ozTotal >= 60;
                            (climbs ? purseClimbed : purseHeld) += keep.count > 10;
                            if (b.result.frames >= 10 && keep.count > 10 && (climbs ? b.result.shownPurse <= from : b.result.shownPurse != from))
                                std::printf("  results: the purse went %.0f -> %.0f (earned %d)\n", from, b.result.shownPurse, keep.ozTotal), ++resultsBad;
                            b.result = keep;
                        }
                        int taps = 0;
                        for (int g = 0; g < 3000 && b.exit != 4; ++g) {
                            stepBattle(app);
                            if (b.result.frames >= 30 && b.result.stage != 3) resultsTap(app), ++taps, b.result.frames = 4;
                        }
                        const bool through = b.exit == 4 && b.result.shownExp == float(b.result.expTotal) && size_t(taps) >= lines + 1;
                        if (!through) std::printf("  results: exit %d, %d taps for %zu lines, exp %.0f of %d\n", b.exit, taps, lines,
                                                  b.result.shownExp, b.result.expTotal), ++resultsBad;
                        b.exit = 0;
                        ended = 1;
                        endBattle(app, true);
                        break;
                    }
                    if (b.phase == App::Battle::Lost && b.phaseFrames > 5) { ++lost; ended = 1; endBattle(app, false); break; }
                    if (b.exit == 1) {
                        ++fled;
                        ended = 1;
                        if (SDL_getenv("CR_BATTLE_TRACE")) std::printf("  fled: style %d round %d\n", style, b.rules.round);
                        endBattle(app, false);
                        break;
                    }
                    stepBattle(app);
                    if (b.exit) continue; // counted above, before drawFrame would take it
                    if (frame % 40 == 0) {
                        app.lastTicks = 0;
                        drawFrame(app, false);
                        rerrs += app.renderer.takeErrors();
                    }
                }
                if (!ended) {
                    ++stuck;
                    std::printf("  battle %d on stage %d didn't end (phase %d, menu %d, round %d)\n", list[i].second,
                                list[i].first, int(b.phase), int(b.menu), b.rules.round);
                    if (b.active) endBattle(app, false);
                }
                app.runner.stop();
            }
            app.game = saved;
            std::printf("battle scenes: %d played - %d won (%d level-ups; the purse climbed %d, held %d), %d lost, %d fled, %d didn't end, "
                        "%d results stuck; %d render API errors\n", n, won, levelled, purseClimbed, purseHeld, lost, fled, stuck, resultsBad, rerrs);
            problems += stuck + rerrs + resultsBad + (won == 0) + (fled == 0);
            glErrors += rerrs;
        }
        return problems;
    }
}

// A read-back frame to a .bmp, opaque. The frame's alpha is no picture: the window shows the colour alone
// (the swapchain composites opaque), and what the scene leaves there is whatever its draws wrote - an
// opaque mesh writes its texture's alpha (d0553bt's star sky, a PAL8_RGBA8 picture with alpha ~0, leaves 0
// behind the arches), where the original never writes it (read_mdl_hd clears property bit 1, the alpha
// write mask). Kept, a viewer showed those parts as see-through - white - though the screen shows them black
bool saveFrameBmp(const std::vector<uint8_t>& px, int w, int h, const char* path) {
    SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = static_cast<uint8_t*>(surf->pixels) + size_t(y) * surf->pitch;
        std::memcpy(row, &px[size_t(y) * w * 4], size_t(w) * 4);
        for (int x = 0; x < w; ++x) row[x * 4 + 3] = 255;
    }
    const bool ok = SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    return ok;
}

// The last drawn frame to a .bmp (self-test looks)
void saveFrame(App& app, const char* path) {
    int sw = 0, sh = 0;
    std::vector<uint8_t> px;
    if (!app.renderer.readPixels(px, sw, sh)) return;
    saveFrameBmp(px, sw, sh, path);
}

// Achievements (game/achievements.hpp): each condition earns exactly its achievement, once; the
// battle's check; nothing at the title; the banner's frames (achievement_prc); the queue shown one
// at a time on the field, the next checked as one closes; the earned set kept across a restart
// (settings.ini); the list's rows
int achievementsSelfTest(App& app) {
    namespace ach = cr::achievements;
    int problems = 0;
    const App::Achievements keepAch = app.ach;
    const cr::GameState keepGame = app.game;
    const bool keepPersist = app.persist, keepBooting = app.booting, keepTitle = app.titleOpen, keepBattle = app.battle.active;
    const Mode keepMode = app.mode;
    app.persist = false, app.booting = false, app.titleOpen = false, app.battle.active = false, app.mode = Mode::Maps;
    app.runner.stop();
    auto fresh = [&] {
        app.ach = App::Achievements();
        app.game = cr::GameState();
    };
    fresh();
    // One state a condition: the achievement it earns (fld_checkAchievements' order)
    struct Case { const char* what; int expect; std::function<void()> set; };
    const std::vector<Case> cases = {
        {"flag 0x48a", 1, [&] { app.game.setFlag(0x48a, true); }},
        {"flag 0x4f4", 10, [&] { app.game.setFlag(0x4f4, true); }},
        {"a purchase (0x2b)", 11, [&] { app.ach.flags.set(ach::kBought); }},
        {"50 weapons", 12, [&] { for (int i = 0; i < 50; ++i) app.game.items[0xc9 + i] = 1; }},
        {"49 weapons", 0, [&] { for (int i = 0; i < 49; ++i) app.game.items[0xc9 + i] = 1; }},
        {"50 armour", 13, [&] { for (int i = 0; i < 50; ++i) app.game.items[0x11d + i] = 1; }},
        {"15 gems", 14, [&] { for (int i = 0; i < 15; ++i) app.game.items[0x171 + i] = 1; }},
        {"a 10,000 blow (0x30)", 16, [&] { app.ach.flags.set(ach::kHit10000); }},
        {"100,000 Oz", 18, [&] { app.game.oz = 100000; }},
        {"99,999 Oz", 0, [&] { app.game.oz = 99999; }},
        {"7,770,000 Oz, 18 and 19 earned", 20, [&] { app.game.oz = 7770000, app.ach.flags.set(18), app.ach.flags.set(19); }},
        {"500 down (0x35)", 21, [&] { app.ach.flags.set(ach::kKills500); }},
        {"flag 0x65f", 27, [&] { app.game.setFlag(0x65f, true); }},
        {"Ohm's items to 200", 28, [&] { for (int i = 0; i < 20; ++i) app.game.items[0x209 + i] = 1; app.game.vars[0x28] = 0; }}, // (5x5 + 15x5 + 30x5 + 40x5 = 450)
        {"Ohm 900, 28 earned", 29, [&] { app.game.vars[0x28] = 900; app.ach.flags.set(28); }},
        {"a plate found (0x3e)", 30, [&] { app.ach.flags.set(ach::kPlateFound); }},
        {"every plate's genes", 31, [&] { for (int p = 7; p < 0x1f; ++p) for (int e : cr::plateContents(cr::kFirstPlate + p)) if (e > 0) app.game.items[e] = 1; }},
    };
    int conditions = 0;
    for (const Case& c : cases) {
        fresh();
        c.set();
        fieldCheckAchievements(app);
        const bool once = c.expect ? app.ach.earnedLog == std::vector<int>{c.expect} && app.ach.flags.has(c.expect) &&
                                         app.ach.queue == std::vector<int>{c.expect}
                                   : app.ach.earnedLog.empty();
        app.ach.queue.clear(); // (as the banner would have been shown: the same state checks again)
        fieldCheckAchievements(app);
        const bool again = c.expect ? app.ach.earnedLog.size() == 1 || app.ach.earnedLog[1] != c.expect : app.ach.earnedLog.empty();
        if (once && again) ++conditions;
        else std::printf("achievements: %s earned %d (want %d)\n", c.what, app.ach.earnedLog.empty() ? 0 : app.ach.earnedLog[0], c.expect), ++problems;
    }
    // the battle's check (3.1.0's FUN_0008fa80): a blow's flag earns at once, in the battle
    fresh();
    app.battle.active = true;
    battleConfFlag(app, ach::blowFlag(31000));
    const bool battle = app.ach.flags.has(ach::kHit30000) && app.ach.earnedLog == std::vector<int>{17} && ach::blowFlag(4999) == 0 &&
                        ach::blowFlag(5000) == ach::kHit5000 && ach::blowFlag(29999) == ach::kHit10000;
    problems += !battle;
    // ... and its banner waits for the field
    app.ach.clock = 0;
    stepAchievements(app, 0.06f);
    const bool waited = !app.ach.banner.on && app.ach.queue == std::vector<int>{17};
    app.battle.active = false;
    stepAchievements(app, 0.05f);
    const bool shown = app.ach.banner.on && app.ach.banner.id == 17 && app.ach.queue.empty();
    problems += !waited + !shown;
    // nothing while the title is up (a check there would earn from whatever the game holds)
    fresh();
    app.game.setFlag(0x48a, true);
    app.titleOpen = true;
    fieldCheckAchievements(app);
    const bool title = app.ach.earnedLog.empty();
    app.titleOpen = false;
    problems += !title;
    // achievement_prc's frames: in over four (SE on the first; 48, 96, 144, 192 of 255), the lines held
    // with the last, until a press; then one frame whole without them and three out; closed
    ach::Banner b;
    b.start(5);
    std::vector<int> alphas;
    std::vector<bool> lines;
    bool sound = false;
    for (int f = 0; f < 4; ++f) {
        sound |= b.step(false) == ach::Banner::Sound && f == 0;
        alphas.push_back(b.alphaByte()), lines.push_back(b.lines());
    }
    for (int f = 0; f < 200; ++f) b.step(false);
    const bool held = b.on && b.lines() && b.alphaByte() == 192;
    b.step(true);
    alphas.push_back(b.alphaByte()), lines.push_back(b.lines());
    for (int f = 0; f < 3; ++f) b.step(false), alphas.push_back(b.alphaByte()), lines.push_back(b.lines());
    const bool closed = b.step(false) == ach::Banner::Closed && !b.on;
    const bool timing = sound && held && closed && alphas == std::vector<int>{48, 96, 144, 192, 192, 144, 96, 48} &&
                        lines == std::vector<bool>{false, false, false, true, false, false, false, false};
    problems += !timing;
    // the queue on the field: one at a time, in order; the next checked as one closes (and shown)
    fresh();
    app.game.setFlag(0x48a, true), app.game.setFlag(0x48b, true);
    fieldCheckAchievements(app);
    const bool one = app.ach.earnedLog == std::vector<int>{1};
    int shownOrder[2] = {0, 0}, banners = 0;
    for (int f = 0; f < 60 && banners < 2; ++f) {
        const bool was = app.ach.banner.on;
        stepAchievements(app, 1.0f / kFramesPerSecond);
        if (!was && app.ach.banner.on) shownOrder[banners++] = app.ach.banner.id;
        if (app.ach.banner.lines()) app.ach.tap = true; // (a press as soon as it's held)
    }
    const bool chain = one && shownOrder[0] == 1 && shownOrder[1] == 2 && app.ach.earnedLog == std::vector<int>({1, 2});
    problems += !chain;
    // the field held while it's up (FieldScene::onUpdate does nothing; the HUD's corner button gone)
    const bool hold = app.ach.banner.on && !fieldHudShown(app);
    problems += !hold;
    // kept across a restart: settings.ini's achievements= (an isolated folder, as controlHintsSelfTest's)
    bool kept = false;
    {
        const char* oldEnv = SDL_getenv("CR_SAVE_DIR");
        const std::string keepEnv = oldEnv ? oldEnv : "";
        const auto testDir = std::filesystem::temp_directory_path() / ("cr-ach-test-" + std::to_string(SDL_GetTicksNS()));
        if (std::filesystem::create_directory(testDir)) {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR", testDir.u8string().c_str(), true);
            const App::Mix keepMix = app.mix;
            const int keepLanguage = app.language;
            app.persist = true;
            fresh();
            app.game.setFlag(0x4c6, true);
            fieldCheckAchievements(app); // (7: written as it is earned)
            setSystemConfFlag(app, ach::kHit5000);
            const ach::Flags before = app.ach.flags;
            app.ach = App::Achievements();
            loadMix(app);
            kept = before.has(7) && app.ach.flags.has(7) && app.ach.flags.has(ach::kHit5000) && app.ach.flags.bits == before.bits &&
                   app.ach.flags.earned() == 1;
            app.persist = false;
            app.mix = keepMix, app.language = keepLanguage;
            applyLanguage(app), applyMix(app);
            SDL_RemovePath(settingsPath().c_str());
            std::filesystem::remove(testDir);
            if (oldEnv) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR", keepEnv.c_str(), true);
            else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR");
        }
    }
    problems += !kept;
    ach::Flags hex;
    hex.set(1), hex.set(32), hex.set(0x3e);
    ach::Flags back;
    const bool hexOk = back.fromHex(hex.hex()) && back.bits == hex.bits && hex.hex().size() == 32 && !back.fromHex("zz");
    problems += !hexOk;
    // the list: earned rows with their lines, the story's hidden until earned (InitStoreMenu's table)
    fresh();
    app.ach.flags.set(1), app.ach.flags.set(12);
    app.titleOpen = true;
    openAchievementList(app);
    const bool listRows = app.ach.listRows.size() == 15 && app.ach.listRows.front() == 1 && app.ach.listRows[1] == 11 &&
                          app.ach.listRows.back() == 32;
    closeAchievementList(app);
    app.titleOpen = false;
    problems += !listRows;
    const bool names = achievementName(app, 7) == "Eye for an Eye" && achievementName(app, 32) == "Gene Splicer";
    problems += !names;
    std::printf("achievements: %d of %zu conditions earn their own once, battle check %s (banner waits %s, then %s), none at the title "
                "%s, banner frames %s, one at a time in order %s, field held %s, kept across a restart %s, flags text %s, list rows %s, "
                "names %s; %d problems\n",
                conditions, cases.size(), battle ? "yes" : "NO", waited ? "yes" : "NO", shown ? "shown" : "NOT SHOWN",
                title ? "yes" : "NO", timing ? "yes" : "NO", chain ? "yes" : "NO", hold ? "yes" : "NO", kept ? "yes" : "NO",
                hexOk ? "yes" : "NO", listRows ? "yes" : "NO", names ? "yes" : "NO", problems);
    app.ach = keepAch;
    app.game = keepGame;
    app.persist = keepPersist, app.booting = keepBooting, app.titleOpen = keepTitle, app.battle.active = keepBattle, app.mode = keepMode;
    return problems;
}

int controlHintsSelfTest(App& app) {
    const bool keepHud = app.showHud, keepPersist = app.persist;
    // settingsPath uses CR_SAVE_DIR, not the game's saveDirOverride. Give this
    // persistence test its own directory even when invoked in a real profile.
    const char* oldEnv = SDL_getenv("CR_SAVE_DIR");
    const bool hadEnv = oldEnv != nullptr;
    const std::string keepEnv = oldEnv ? oldEnv : "";
    const auto testDir = std::filesystem::temp_directory_path() /
        ("cr-settings-test-" + std::to_string(SDL_GetTicksNS()));
    if (!std::filesystem::create_directory(testDir)) return 1;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR", testDir.u8string().c_str(), true);
    app.persist = true; // an isolated settings file: exercise the same save / load as a restart
    int problems = 0;
    bool off = false, on = false, shortcut = false;
    const auto rows = optionRows(app, 0);
    const auto row = std::find_if(rows.begin(), rows.end(), [](const OptRow& r) { return r.label == "Control Hints"; });
    if (row == rows.end() || !row->act) ++problems;
    else {
        row->act(1); // Off, as a tap on its settings button
        const bool hidden = !app.showHud && !keyHints(app);
        app.showHud = true;
        loadMix(app);
        off = hidden && !app.showHud && !keyHints(app);
        problems += !off;
        row->act(0); // On
        const bool shown = app.showHud && keyHints(app);
        app.showHud = false;
        loadMix(app);
        on = shown && app.showHud && keyHints(app);
        problems += !on;
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_F3;
        key.key.down = true;
        SDL_AppEvent(&app, &key);
        const bool toggled = !app.showHud;
        app.showHud = true;
        loadMix(app);
        shortcut = toggled && !app.showHud;
        problems += !shortcut;
    }
    SDL_RemovePath(settingsPath().c_str());
    std::filesystem::remove(testDir);
    if (hadEnv) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR", keepEnv.c_str(), true);
    else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "CR_SAVE_DIR");
    app.showHud = keepHud;
    app.persist = keepPersist;
    std::printf("control hints: settings Off persists %s, On persists %s, F3 persists %s; %d problems\n",
                off ? "yes" : "NO", on ? "yes" : "NO", shortcut ? "yes" : "NO", problems);
    return problems;
}

// The field's rendering as 3.1.0 draws it: npc_shodow's shadows, the stage record's alpha model,
// the loaders' colours, the effects' depth writes, the fades, the front fade's place, the planes and
// (3.1.0's data) the Options' Language row
int fieldRenderSelfTest(App& app) {
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) std::printf("field render: %s FAILED\n", what);
        problems += !ok;
    };
    const bool keepBg = app.showBackground, keepNoEnc = app.noEncounters;
    const int keepLang = app.language;
    app.showBackground = true, app.noEncounters = true;
    app.mode = Mode::Maps;
    auto settle = [&] {
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
        app.dialogue.clear();
    };
    auto room = [&](const char* cmp, int map, int area) {
        const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cmp));
        if (at == app.maps.end()) return false;
        showMap(app, int(at - app.maps.begin()), map, area);
        settle();
        return true;
    };
    auto frame = [&] {
        app.lastTicks = 0;
        drawFrame(app, false);
    };
    // The alpha model: t0110 names t0112's, t0131 t0101's, d0301 none (all three have files of their own)
    std::string t0110, t0131, d0301 = "?";
    if (room("t0110_10.cmp", 177, 9)) t0110 = app.mapProps;
    if (room("t0131_10.cmp", 180, 9)) t0131 = app.mapProps;
    if (room("d0301_10.cmp", 31, 4)) d0301 = app.mapProps;
    check(t0110 == "t0112.bin" && t0131 == "t0101.bin" && d0301.empty(), "alpha model");
    // Shadows: the leader's (80% of its alpha) and every flagged NPC's - the first room with one
    int flagged = 0, playerA = -1, npcA = -1, npcWant = -1, fadedA = -1;
    for (size_t m = 0; m < app.maps.size() && !flagged; ++m) {
        showMap(app, int(m));
        settle();
        for (const App::NpcDraw& d : app.npcDraws) {
            const bool on = d.index >= 0 && d.index < int(app.npcs.size()) && app.npcs[size_t(d.index)].hasShadow();
            if (!on || app.npcs[size_t(d.index)].shadow[1] <= 0 || app.npcs[size_t(d.index)].shadow[2] <= 0) continue;
            if (!flagged) npcWant = std::abs(int(app.npcs[size_t(d.index)].shadow[3])) * int(std::lround(d.alpha * 255)) / 100;
            ++flagged;
        }
    }
    if (flagged && app.havePlayer) {
        int drawnBefore = app.shadowsDrawn;
        frame();
        const int drawn = app.shadowsDrawn - drawnBefore;
        if (!app.fieldShadowAlpha.empty()) playerA = app.fieldShadowAlpha[0];
        if (app.fieldShadowAlpha.size() >= 2 * 96) npcA = app.fieldShadowAlpha[96];
        check(drawn == 1 + flagged && playerA == 204 && npcA == npcWant, "shadows (the leader's 204, the NPC's by its percent)");
        ViewerHost host(app); // NpcFade 0 to half: the shadow follows the model's alpha
        host.fadeNpc(0, 128.0f / 255.0f, 128.0f / 255.0f, 1);
        frame();
        if (!app.fieldShadowAlpha.empty()) fadedA = app.fieldShadowAlpha[0];
        check(fadedA == 80 * 128 / 100 && app.playerFaded, "the leader's shadow faded with it");
        app.fieldHidden = true; // Screen's toggle: no models, the shadows all the same
        drawnBefore = app.shadowsDrawn;
        frame();
        check(app.shadowsDrawn - drawnBefore == 1 + flagged, "shadows with the field hidden");
        app.fieldHidden = false;
        // mdl_set_alpha over 0 frames: the alpha as it was, the model alpha-blended from then on
        const int id = app.npcDraws.front().id;
        const float before = app.npcDraws.front().alpha;
        host.fadeNpc(id, 0.0f, 0.0f, 0);
        const App::NpcDraw& d1 = app.npcDraws.front();
        check(d1.id == id && d1.faded && d1.alpha == before && d1.fadeLeft == 0, "NpcFade over 0 frames");
    } else check(false, "a room with a shadowed NPC");
    std::printf("field render: alpha models t0110 %s, t0131 %s, d0301 %s; shadows: %d flagged NPCs in %s, the leader's alpha "
                "%d, the NPC's %d (want %d), the leader at half %d\n", t0110.c_str(), t0131.c_str(), d0301.empty() ? "none" : d0301.c_str(),
                flagged, app.maps[size_t(app.mapIndex)].c_str(), playerA, npcA, npcWant, fadedA);
    { // __EvtNpcMdlRead points a new slot at the NPC's row, so npc_shodow finds its +30..+36 there: j001_d06's d0650
      // lists 999 alone, its scene reads 409 (row: +18 bit 15, 3 / 3 / 80) - drawn with that shadow
        int sx = -1, sz = -1, sp = -1, drawnNpc = -1;
        if (room("d0650_10.cmp", 91, 7) && !findNpc(app, 409)) {
            ViewerHost host(app);
            host.loadNpc(409);
            host.showNpc(409, true);
            if (const cr::FieldNpc* n = findNpc(app, 409)) sx = n->shadow[1], sz = n->shadow[2], sp = n->shadow[3];
            const int before = app.shadowsDrawn;
            frame();
            drawnNpc = app.shadowsDrawn - before;
        }
        check(sx == 3 && sz == 3 && sp == 80 && drawnNpc == 2, "a script-read NPC's shadow (d0650's 409: 3, 3, 80)");
        std::printf("field render: d0650's script-read 409 shadow %d / %d / %d, shadows drawn %d (the leader's and its)\n", sx, sz, sp, drawnNpc);
    }
    // The colours: read_mdl's models white and opaque, glColor alone; effects their own diffuse, no alpha test
    {
        int notWhite = 0, effMats = 0, effBad = 0, depthEffect = -1;
        for (const auto& [name, lm] : app.npcModels)
            for (const cr::Material& mt : lm.cpu.materials) notWhite += !mt.unlit || mt.diffuse[0] < 1 || mt.diffuse[3] < 1;
        for (int id = 0; id < effects::kEffects; ++id) {
            const LoadedModel* lm = effectModel(app, id);
            if (!lm) continue;
            for (const cr::Material& mt : lm->cpu.materials) {
                ++effMats, effBad += !mt.unlit || mt.alphaThreshold != 0;
                if (depthEffect < 0 && mt.depthWrite()) depthEffect = id;
            }
        }
        check(notWhite == 0 && effMats > 0 && effBad == 0, "the loaders' colours");
        // eff3d_set_with_pos clears the depth writes; _with_pos_and_ang (FrmPlayEff2 at a record) keeps them
        int plain = -1, angled = -1;
        if (depthEffect >= 0 && app.havePlayer) {
            app.fieldEffects.clear();
            startEffect(app, app.fieldEffects, depthEffect, app.walker.pos, true, 1.0f, 0.0f);
            int w = app.effectDepthWrites;
            frame();
            plain = app.effectDepthWrites - w;
            if (!app.fieldEffects.empty()) app.fieldEffects.back().keepDepthWrite = true;
            w = app.effectDepthWrites;
            frame();
            angled = app.effectDepthWrites - w;
            app.fieldEffects.clear();
        }
        check(plain == 0 && angled > 0, "the effects' depth writes");
        std::printf("field render: %d read_mdl materials not white, %d effect materials (%d not as eff3d leaves them); "
                    "effect %d writes depth in %d materials with angles, %d without\n", notWhite, effMats, effBad, depthEffect,
                    angled, plain);
    }
    // The 2D order: the front fade over a portrait and under the message's text; over the portrait alone
    if (app.windowTex.id) {
        App::Portrait& p = app.portraits[15];
        const App::Portrait keep = p;
        p = App::Portrait();
        p.tex = app.windowTex, p.alpha = 1;
        app.fadeGrey = 0, app.fadeFrom = 0.5f, app.fadeTo = 0.5f, app.fadeFrames = 10, app.fadeT = 0;
        app.dialogue = {{"Fade order"}};
        app.dialoguePage = 0;
        frame();
        const App::Order2D withText = app.order2d;
        app.dialogue.clear();
        const int keepWin = app.msgWin.state;
        app.msgWin.state = 6;
        frame();
        const App::Order2D alone = app.order2d;
        app.msgWin.state = keepWin;
        p = keep;
        app.fadeFrames = 0, app.fadeT = 0;
        check(withText.portrait > 0 && withText.frontFade > withText.portrait && withText.text > withText.frontFade &&
                  alone.portrait > 0 && alone.frontFade > alone.portrait && alone.text == 0,
              "the front fade's place");
        std::printf("field render: 2D order with a message: portrait %d, front fade %d, text %d; without: portrait %d, fade %d\n",
                    withText.portrait, withText.frontFade, withText.text, alone.portrait, alone.frontFade);
    }
    { // setCamera3D's planes: far 2000 (map 83 4000), near 1 + the map's offset
        float n0, f0, n83, f83;
        const int keepMap = app.mapNumber;
        const cr::Vec3 eye{0, 50, 100}, tgt{0, 0, 0};
        app.mapNumber = 19, fieldDepthRange(app, eye, tgt, n0, f0);
        app.mapNumber = 83, fieldDepthRange(app, eye, tgt, n83, f83);
        app.mapNumber = keepMap;
        check(n0 == 1 && f0 == 2000 && n83 == 101 && f83 == 4000, "near / far");
    }
    // Options (3.1.0's data): one Language row, its arrows through the languages the data has
    if (languageSheet(app)) {
        app.language = 1, app.fanText = false;
        std::vector<OptRow> rows = optionRows(app, 0);
        bool ok = rows.size() > 1 && rows[0].kind == OptControl::Value && rows[0].valueTex && rows[1].label != "Text Language";
        // (then a fan translation's, #11: a ninth choice, by its name)
        const bool fan = choiceUsable(app, cr::kLanguages);
        std::set<int> seen;
        int usable = fan;
        for (int l = 0; l < cr::kLanguages; ++l) usable += languageUsable(app, l);
        for (int k = 0; ok && k < usable; ++k) {
            optionRows(app, 0)[0].act(1);
            seen.insert(languageChoice(app));
        }
        ok = ok && app.language == 1 && !app.fanText && int(seen.size()) == usable;
        app.language = 0;
        optionRows(app, 0)[0].act(-1);
        const int back = languageChoice(app);
        ok = ok && (fan ? back == cr::kLanguages && app.fanText && !optionRows(app, 0)[0].valueTex
                        : languageUsable(app, 7) ? back == 7 : back != 0);
        if (app.fanText) setLanguageChoice(app, 7);
        rows = optionRows(app, 0);
        ok = ok && rows[0].valueUv[0] == 88.0f * float(app.language);
        check(ok, "the Language row (3.1.0)");
        std::printf("field render: the Language row cycles %zu languages, left from Japanese to %s\n", seen.size(),
                    languageChoiceName(app, back).c_str());
        app.fanText = false;
    } else {
        const std::vector<OptRow> rows = optionRows(app, 0);
        check(rows.size() > 1 && rows[0].kind == OptControl::Pair && rows[0].words[0] == 37 && rows[1].label == "Text Language",
              "the Language pair (1.0.0)");
        std::printf("field render: Options keep 1.0.0's Japanese / English pair\n");
    }
    app.language = keepLang;
    applyLanguage(app);
    app.showBackground = keepBg, app.noEncounters = keepNoEnc;
    problems += app.renderer.takeErrors();
    std::printf("field render: %d problems\n", problems);
    return problems;
}

int fieldPresentationSelfTest(App& app) {
    const bool keepNoEncounters = app.noEncounters;
    app.noEncounters = true;
    const auto at = std::find(app.maps.begin(), app.maps.end(), "d0101_10.cmp");
    if (at == app.maps.end()) return 1;
    app.mode = Mode::Maps;
    showMap(app, int(at - app.maps.begin()), 0, 2);
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.dialogue.clear();
    const cr::FieldWalker start = app.walker;
    int problems = 0, moving = 0;
    // A pause left in the model viewer must not stop field animation. Exercise the actual
    // player update with both touch-pad gaits, including each leader's loaded actions.
    app.playing = false;
    for (int c = 0; c < 4; ++c) {
        app.playerChar = c;
        placePlayer(app);
        for (int gait = 1; gait <= 2; ++gait) {
            app.walker = start;
            app.walkClock = 0;
            app.touchPad = App::TouchPad();
            app.touchPad.down = true;
            app.touchPad.dx = gait == 1 ? 20.0f : 40.0f;
            updatePlayer(app, 1.0f / kFramesPerSecond, 0);
            updatePlayer(app, 1.0f / kFramesPerSecond, 0); // (the gait's change: its 2-frame cross-fade)
            const auto first = app.player.world;
            updatePlayer(app, 1.0f / kFramesPerSecond, 0);
            bool changed = false;
            for (size_t i = 0; i < first.size() && i < app.player.world.size(); ++i)
                changed |= !same(first[i], app.player.world[i]);
            const bool ok = app.havePlayer && int(app.walker.gait) == gait && app.player.action == gait &&
                            gait < int(app.player.actions.size()) &&
                            app.player.actions[gait].name == "built-in " + std::to_string(gait + 1) + ".1" &&
                            app.player.time > 0 && changed;
            moving += ok;
            if (!ok) std::printf("field pose: leader %d gait %d action %d time %.2f changed %d, runner %d battle %d\n",
                                 c, gait, app.player.action, app.player.time, changed, app.runner.running(), app.battle.active);
            problems += !ok;
        }
    }
    app.touchPad = App::TouchPad();
    app.playing = true;
    app.dialogue = {{"Confirm"}};
    app.dialoguePage = 0;
    SDL_Event confirm{};
    confirm.type = SDL_EVENT_KEY_DOWN;
    confirm.key.key = SDLK_SPACE;
    confirm.key.down = true;
    SDL_AppEvent(&app, &confirm);
    const bool confirmed = app.dialogue.empty() && app.playing;
    problems += !confirmed;
    app.dialogue.clear();
    app.playerChar = 0;
    app.playing = true;
    problems += app.renderer.takeErrors();
    std::printf("field animation: %d/8 walk/run poses advance after viewer pause, Space confirms %s; %d problems\n",
                moving, confirmed ? "yes" : "NO", problems);
    // Omega already exempts stage backdrops from the parked-actor filter. Keep that behavior.
    const auto stage = std::find(app.maps.begin(), app.maps.end(), "d0550_10.cmp");
    if (stage == app.maps.end()) return problems + 1;
    showMap(app, int(stage - app.maps.begin()), 71, 6);
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    ViewerHost host(app);
    host.loadNpc(517);
    host.showNpc(517, true);
    const auto draws = host.npc(517);
    const bool backdrop = draws.size() == 1 && draws[0]->model == "t0155bt.bin";
    problems += !backdrop;
    std::printf("cutscene deck: shown at origin on Born %s; %d problems\n", backdrop ? "yes" : "NO", !backdrop);
    {
        // Render the loaded atlas's panel over magenta and check both halves in every layout.
        // This also guards the iOS / Vita art when changing Android's separate parchment path.
        const char* source = app.androidWindow ? "Android" : app.vita.isOpen() ? "Vita" : "iOS";
        const App::Video keepVideo = app.video;
        const int keepStyle = app.msgStyle, keepLines = app.msgLines;
        int panels = 0;
        app.msgStyle = 2, app.msgLines = 2;
        for (int layout = 0; layout < 6; ++layout) {
            app.video.layout = layout % 5;
            app.video.widescreen = layout == 5;
            applyVideo(app);
            // A smaller viewport exercises the wide aspect without resizing the GL surface.
            int fw = 1024, fh = layout == 5 ? 448 : 768;
            auto& r = app.renderer;
            r.beginFrame(fw, fh);
            r.setOrigin(r.uiX());
            r.fillRect(-r.uiX(), 0, r.screenW(), cr::Renderer::kLogicalH, 1, 0, 1, 1);
            drawMessageBox(app, {}, "Speaker", false, "");
            r.endFrame(false);
            std::vector<uint8_t> pixels;
            bool paper = r.readPixels(pixels, fw, fh);
            float x0, y0, x1, y1;
            r.toLogical(0, 0, x0, y0);
            r.toLogical(float(fw), float(fh), x1, y1);
            for (float x : {200.0f, 800.0f}) {
                const int px = int((x + r.uiX() - x0) * fw / (x1 - x0));
                const int py = int((620 - y0) * fh / (y1 - y0));
                if (!paper || px < 0 || px >= fw || py < 0 || py >= fh) { paper = false; break; }
                paper = pixels[(size_t(py) * fw + px) * 4 + 1] > 100; // parchment, not magenta
            }
            panels += paper;
            if (!paper) std::printf("%s dialogue: parchment missing in layout %d\n", source, layout);
        }
        const int panelProblems = 6 - panels + app.renderer.takeErrors();
        std::printf("%s dialogue: parchment covers both halves in %d/6 layouts; %d problems\n", source, panels, panelProblems);
        problems += panelProblems;
        app.msgStyle = keepStyle, app.msgLines = keepLines;
        app.video = keepVideo;
        applyVideo(app);
    }
    { // The Android layout's phone edges (androidEdgeX / Y): Android 1.1.6's centring offset over
      // its 2D scale - (80, 20) tex2d on a 1280 x 720 phone, the HUD's badge, rows and corner
      // buttons moved by it - and none in the iPad, iPhone and Vita layouts
        const App::Video keepVideo = app.video;
        int edgeProblems = 0;
        for (int layout = 1; layout <= 4; ++layout) {
            app.video.layout = layout, app.video.widescreen = false;
            applyVideo(app);
            app.renderer.beginFrame(1280, 720);
            app.renderer.endFrame(false);
            const float ex = androidEdgeX(app), ey = androidEdgeY(app), wx = layout == 4 ? 80.0f : 0.0f, wy = layout == 4 ? 20.0f : 0.0f;
            const bool ok = std::fabs(ex - wx) < 0.01f && std::fabs(ey - wy) < 0.01f;
            edgeProblems += !ok;
            if (!ok) std::printf("android edges: layout %d gives (%.2f, %.2f), wanted (%.0f, %.0f)\n", layout, ex, ey, wx, wy);
            // The Vita layout's edge groups (vitaEdgeX): the Vita's 43.352966 tex2d (its __menub_mainPut / __menuf_mainPut /
            // _PutMsg), the whole 43 of its tables (vitaEdge43, vitaLeft / vitaRight 86 layout units out), none elsewhere
            const bool vita = layout == 3;
            const float vx = vitaEdgeX(app), v43 = vitaEdge43(app), vl = wideLeft(app) - vitaLeft(app), vr = vitaRight(app) - wideRight(app);
            const bool vok = std::fabs(vx - (vita ? 43.352966f : 0.0f)) < 0.0001f && v43 == (vita ? 43.0f : 0.0f) &&
                             std::fabs(vl - 2 * v43) < 0.01f && std::fabs(vr - 2 * v43) < 0.01f;
            edgeProblems += !vok;
            if (!vok) std::printf("vita edges: layout %d gives %.4f / %.0f (left %.1f, right %.1f)\n", layout, vx, v43, vl, vr);
        }
        app.video = keepVideo;
        applyVideo(app);
        std::printf("android edges: the phone's edges (80, 20) tex2d outside the centred screen in the Android layout, 0 in the others; "
                    "the Vita layout's edge groups 43.352966 / 43 tex2d out, 0 in the others; %d problems\n",
                    edgeProblems);

        problems += edgeProblems;
    }
    app.noEncounters = keepNoEncounters;
    return problems;
}

// Exercise the SDL input path, including window/frame scaling and the phone projection.
int mobileInputSelfTest(App& app) {
    int problems = 0, gestures = 0, slots = 0;
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.titleOpen = app.optionsOpen = app.charSelectOpen = false;
    app.saveScreen.open = false;
    app.game = cr::GameState();
    app.playerChar = 0;
    for (int n = 1; n <= 630; ++n) app.game.items[n] = 10;
    app.game.setFlag(0x38, true); // (56010, the first plate's once-only message, read: 3.1.0's CampMainSet - 4d8fe6a)
    const auto keepVideo = app.video;
    auto pointer = [&](SDL_EventType type, float mx, float my) {
        float x0, y0, x1, y1;
        app.renderer.toLogical(0, 0, x0, y0);
        app.renderer.toLogical(1000, 1000, x1, y1);
        const bool phone = phoneMenus(app);
        const float lx = vitaRight(app) + 2 * (mx + (phone ? 16 : 0)); // (the Camp's lists: the Vita's 43 further right)
        const float ly = 2 * (my + (phone ? 32 : 0));
        int ww, wh;

        SDL_GetWindowSize(app.window, &ww, &wh);
        const float x = (lx - x0) * 1000 / (x1 - x0) * ww / app.renderer.frameWidth();
        const float y = (ly - y0) * 1000 / (y1 - y0) * wh / app.renderer.frameHeight();
        SDL_Event e{};
        e.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) e.motion.x = x, e.motion.y = y;
        else e.button.x = x, e.button.y = y, e.button.button = SDL_BUTTON_LEFT;
        SDL_AppEvent(&app, &e);
    };
    auto tap = [&](float x, float y) {
        pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
        pointer(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    };
    for (int layout = 1; layout <= 4; ++layout) for (bool wide : {false, true}) {
        app.video.layout = layout, app.video.widescreen = wide;
        applyVideo(app);
        app.renderer.beginFrame(1280, 720);
        app.renderer.endFrame(false);
        // Item, gene, equipment-slot and equipment-candidate lists use the same gesture.
        for (int view = 0; view < 4; ++view) {
            openCamp(app);
            auto& c = app.camp;
            campOpenPage(app, view == 0 ? 0 : view == 1 ? 2 : 1);
            c.anim = 8;
            if (view >= 2) {
                c.phase = App::Camp::Slot;
                c.member = 0, c.slot = c.slotTop = 0;
                if (view == 3) campChoose(app);
            }
            const auto phase = c.phase;
            const int n = campScrollCount(app);
            if (n <= 6) { ++problems; continue; }
            pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, campListX(app) + 60, 250);
            pointer(SDL_EVENT_MOUSE_MOTION, campListX(app) + 60, 150);
            pointer(SDL_EVENT_MOUSE_BUTTON_UP, campListX(app) + 60, 150);
            bool ok = campScrollTop(app) == 2 && c.phase == phase;
            SDL_Event wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL, wheel.wheel.y = -1;
            SDL_AppEvent(&app, &wheel);
            ok &= campScrollTop(app) == 3;
            pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 468, 140);
            pointer(SDL_EVENT_MOUSE_MOTION, 468, 284);
            pointer(SDL_EVENT_MOUSE_BUTTON_UP, 468, 284);
            ok &= campScrollTop(app) == n - 1; // (the gauge's foot: the last row on the line)
            pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, 468, 250);
            pointer(SDL_EVENT_MOUSE_MOTION, 468, 92);
            pointer(SDL_EVENT_MOUSE_BUTTON_UP, 468, 92);
            ok &= campScrollTop(app) == 0;
            tap(468, 292);
            ok &= campScrollTop(app) == 1;
            tap(468, 82);
            ok &= campScrollTop(app) == 0;
            // (a row under the line comes to it; the row on it is the one a tap chooses)
            tap(campListX(app) + 60, campui::kFocusY + campui::kRowH + 20);
            ok &= (view == 2 ? c.slot : c.listCursor) == 1 && c.phase == phase;
            if (!ok) std::printf("mobile input: layout %d wide %d view %d FAILED (top %d cursor %d slot %d phase %d)\n",
                                      layout, wide, view, c.listTop, c.listCursor, c.slot, int(c.phase));
            problems += !ok;
            ++gestures;
        }
        for (int anim : {4, 8}) for (int slot = 0; slot < campui::kEquipSlots; ++slot) {
            openCamp(app);
            auto& c = app.camp;
            c.page = App::Camp::Equip, c.phase = App::Camp::Slot, c.anim = anim;
            c.slot = c.slotTop = slot; // (the slot on the line)
            tap(campListX(app) + 60, campui::kFocusY + 20);
            // In scenario 0 the Human Genes row of Escher's own 474 is a -4 row (3.1.0's _MuScrList_GetChoiceListNo
            // kind 4: 474..476 while the scenario byte is 0 - b171882): the tap on it is refused, the Slot phase kept.
            // The row's hitbox is then checked in scenario 1, where the pair's own Human Genes (477..) open
            const bool refused = slot == 3;
            bool ok = c.slot == slot && c.phase == (refused ? App::Camp::Slot : App::Camp::List);
            if (refused) {
                app.game.scenario = 1;
                openCamp(app);
                c.page = App::Camp::Equip, c.phase = App::Camp::Slot, c.anim = anim;
                c.slot = c.slotTop = slot;
                tap(campListX(app) + 60, campui::kFocusY + 20);
                ok &= c.slot == slot && c.phase == App::Camp::List;
                app.game.scenario = 0;
            }
            problems += !ok;
            if (!ok) std::printf("mobile equipment: layout %d wide %d anim %d slot %d FAILED\n", layout, wide, anim, slot);
            ++slots;
        }
    }
    app.campOpen = false;
    app.video = keepVideo;
    applyVideo(app);
    // The settings file must restore both the lock and its chosen landscape direction.
    app.persist = true;
    const bool keepLock = app.lockOrientation, keepFlip = app.lockLandscapeFlipped;
    for (bool locked : {false, true}) for (bool flipped : {false, true}) {
        app.lockOrientation = locked, app.lockLandscapeFlipped = flipped;
        applyOrientation(app);
        saveMix(app);
        app.lockOrientation = !locked, app.lockLandscapeFlipped = !flipped;
        loadMix(app);
        applyVideo(app);
        const char* expected = !locked ? "LandscapeLeft LandscapeRight" : flipped ? "LandscapeRight" : "LandscapeLeft";
        problems += app.lockOrientation != locked || app.lockLandscapeFlipped != flipped ||
                    std::string(SDL_GetHint(SDL_HINT_ORIENTATIONS)) != expected;
    }
    app.lockOrientation = keepLock, app.lockLandscapeFlipped = keepFlip;
    app.persist = false;
    applyOrientation(app);
    startSound(app);
    vitaAudio(app);
    ViewerHost host(app);
    int clip = -1;
    for (int i = 0; i < app.sound.entries(); ++i) {
        host.playSound(0x400000u | uint32_t(i));
        if (app.sound.dialogueVoiceCount() == 1) { clip = i; break; }
    }
    if (clip >= 0) {
        for (int i = 0; i < 10; ++i) host.playSound(0x400000u | uint32_t(clip));
        problems += app.sound.dialogueVoiceCount() != 1;
        app.dialogue = {{"First page"}, {"Silent page"}};
        app.dialoguePage = 0;
        advanceDialogue(app);
        problems += app.sound.dialogueVoiceCount() != 0 || app.dialoguePage != 1;
        host.playSound(0x400000u | uint32_t(clip));
        advanceDialogue(app);
        problems += app.sound.dialogueVoiceCount() != 0 || !app.dialogue.empty();
    } else ++problems;
    // Select Scenario's slide is entry 301, beyond the inherited 256-entry limit.
    host.playSound(0x400000u | se::kSlide);
    problems += app.sound.dialogueVoiceCount() != 1;
    app.sound.stopDialogueVoices();
    if (app.sound.hasVoices()) {
        int story = -1;
        for (int i = 1; i < app.sound.voiceClips(); ++i) {
            app.sound.playLine(i);
            if (app.sound.linePlaying()) { story = i; break; }
        }
        problems += story < 0;
        if (story >= 0) {
            for (int i = 0; i < 10; ++i) app.sound.playLine(story);
            problems += app.sound.dialogueVoiceCount() != 1;
            app.dialogue = {{"Voiced page"}};
            app.dialoguePage = 0;
            advanceDialogue(app);
            problems += app.sound.dialogueVoiceCount() != 0;
        }
    }
    app.sound.stopAll();
    problems += app.renderer.takeErrors();
    std::printf("mobile input: %d list gesture/tap checks, %d equipment hitboxes; orientation persists both directions, rapid speech replacement (clip %d); %d problems\n",
                gestures, slots, clip, problems);
    return problems;
}

// Exercise teardown while native field tasks are still active. No player saves are written.
int inputCancelSelfTest(App& app) {
    int problems = 0;
    for (Uint32 type : {Uint32(SDL_EVENT_WINDOW_FOCUS_LOST), Uint32(SDL_EVENT_FINGER_CANCELED), Uint32(SDL_EVENT_WILL_ENTER_BACKGROUND)}) {
        app.touchPad.down = app.touchPad.shown = true;
        app.campPointer.down = app.optionsBarDrag = app.dragging = true;
        holdSkip(app, kSkipPointer, true), app.tapPending = true;
        SDL_Event e{};
        e.type = type;
        if (type == SDL_EVENT_FINGER_CANCELED) e.tfinger.windowID = SDL_GetWindowID(app.window);
        else e.window.windowID = SDL_GetWindowID(app.window);
        SDL_AppEvent(&app, &e);
        e.type = SDL_EVENT_USER; // main-thread checkpoint after a lifecycle callback
        SDL_AppEvent(&app, &e);
        const bool clean = !app.touchPad.down && !app.touchPad.shown && !app.campPointer.down &&
                           !app.optionsBarDrag && !app.dragging && !app.skipScene && !app.tapPending;
        problems += !clean;
        if (!clean) std::printf("input cancel FAILED: event %u retained an active gesture\n", type);
        if (type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
            e.type = SDL_EVENT_DID_ENTER_FOREGROUND;
            SDL_AppEvent(&app, &e);
        }
    }
    // Use a real psychokinesis room: cancellation keeps its lifted block and ESP;
    // a fresh drag can then drop it in place, without sending it toward the old release.
    int pkStage = -1;
    for (int i = 0; i < cr::kPuzzleStages; ++i)
        if (cr::kPuzzleStage[i].ability == cr::kAbilityPsychokinesis) { pkStage = i; break; }
    app.mode = Mode::Maps;
    app.runner.stop();
    app.game = cr::GameState();
    app.playerChar = 0, app.mapNumber = app.entryArea = -1;
    cr::WarpRequest w;
    w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + pkStage;
    const bool entered = pkStage >= 0 && performWarp(app, w, -1);
    app.runner.stop(), app.pendingEntry = App::Entry::None;
    int target = -1;
    for (size_t i = 0; i < app.puzzle.objs.size(); ++i)
        if (app.puzzle.objs[i].type == cr::kTypeNormal && app.puzzle.objs[i].shown) { target = int(i); break; }
    if (!entered || target < 0) ++problems;
    else {
        const auto& at = app.puzzle.objs[size_t(target)];
        const auto& layout = cr::kPuzzleStage[pkStage];
        setActor(app, 0, puzzle::centre(app, at.row == 0 ? layout.rows - 1 : 0,
                                       at.col == 0 ? layout.cols - 1 : 0), nullptr, false);
        puzzle::pkGrab(app, target, 200, 200);
        for (int i = 0; i < 4; ++i) stepEvents(app);
        const auto& block = app.puzzle.objs[size_t(target)];
        const int row = block.row, col = block.col, esp = app.puzzle.esp, uses = app.game.vars[13];
        problems += app.puzzle.pkState != 1 || block.pk != 2;
        SDL_Event e{};
        e.type = SDL_EVENT_WINDOW_FOCUS_LOST, e.window.windowID = SDL_GetWindowID(app.window);
        SDL_AppEvent(&app, &e);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.x = 900, e.button.y = 100;
        SDL_AppEvent(&app, &e);
        for (int i = 0; i < 3; ++i) stepEvents(app);
        const bool held = app.puzzle.pkState == 1 && app.puzzle.pkPointerCancelled && block.row == row && block.col == col;
        problems += !held;
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.x = 100, e.button.y = 200;
        SDL_AppEvent(&app, &e);
        problems += app.puzzle.pkState != 1 || app.puzzle.pkPointerCancelled || app.puzzle.pkMoved;
        e.type = SDL_EVENT_MOUSE_BUTTON_UP; // fresh press/release in place, not the old swipe
        SDL_AppEvent(&app, &e);
        for (int i = 0; i < 60; ++i) stepEvents(app);
        const bool dropped = app.puzzle.pkState == 0 && block.row == row && block.col == col &&
                             app.puzzle.esp == esp && app.game.vars[13] == uses;
        problems += !dropped;
        std::printf("puzzle input cancel: block held %s, fresh release drops in place without ESP %s\n",
                    held ? "yes" : "NO", dropped ? "yes" : "NO");
    }
    app.trackerArmed = false;
    std::printf("viewer input cancellation: %d problems\n", problems);
    return problems;
}

int fieldLifecycleSelfTest(App& app) {
    int problems = 0, titles = 0;
    app.persist = false;
    app.noEncounters = true;
    auto enter = [&](int stage) {
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
        app.game = cr::GameState();
        app.playerChar = 0;
        app.mapNumber = app.entryArea = -1;
        cr::WarpRequest w;
        w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + stage;
        const bool ok = performWarp(app, w, -1) && app.puzzle.stage == stage;
        app.pendingEntry = App::Entry::None;
        return ok;
    };
    // Every ability owns different objects/timers. A pending Retry must not rebuild a puzzle
    // from the title's event file after its old NPC list has been released.
    int ability = -1;
    for (int stage = 0; stage < cr::kPuzzleStages; ++stage) {
        if (cr::kPuzzleStage[stage].ability == ability) continue;
        ability = cr::kPuzzleStage[stage].ability;
        if (!enter(stage)) { ++problems; continue; }
        puzzle::reset(app);
        bootTitle(app);
        const bool clean = app.puzzle.stage == -1 && app.puzzle.objs.empty() && app.puzzle.grid.empty() &&
                           !app.puzzleStartPending && app.moves.empty() && app.rescales.empty();
        std::printf("field lifecycle: ability %d return to title %s (stage %d, %zu objects)\n",
                    ability, clean ? "ok" : "FAILED", app.puzzle.stage, app.puzzle.objs.size());
        problems += !clean;
        // Keep stepping past the cancelled retry's eight-frame delay.
        if (clean) {
            for (int f = 0; f < 12; ++f) stepEvents(app);
            problems += app.puzzle.stage != -1;
        }
        ++titles;
    }
    if (enter(0)) {
        ViewerHost host(app);
        auto n = std::find_if(app.npcs.begin(), app.npcs.end(), [](const cr::FieldNpc& n) { return n.id > 0; });
        if (n == app.npcs.end()) ++problems;
        else {
            const int id = n->id;
            const float scale[3] = {2, 2, 2};
            host.scaleNpc(id, scale, 100);
            const bool active = host.animBusy(id) && !app.rescales.empty();
            cr::WarpRequest w;
            w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + 1;
            const bool clean = performWarp(app, w, -1) && app.rescales.empty() && !host.animBusy(id);
            std::printf("field lifecycle: NPC scale task active %d, next room %s\n", active, clean ? "ok" : "FAILED");
            problems += !active + !clean;
        }
    } else ++problems;
    // Interrupt an actual Teleport after its fade-out, then reload the captured room.
    int tpStage = -1;
    for (int stage = 0; stage < cr::kPuzzleStages; ++stage)
        if (cr::kPuzzleStage[stage].ability == cr::kAbilityTeleport && cr::kPuzzleStage[stage].esp != 0) {
            tpStage = stage; break;
        }
    if (tpStage >= 0 && enter(tpStage)) {
        const auto saved = captureSave(app);
        int target = -1;
        for (size_t i = 0; i < app.puzzle.objs.size(); ++i)
            if (puzzle::target(app, app.puzzle.objs[i])) { target = int(i); break; }
        if (target < 0) ++problems;
        else {
            setActor(app, 0, puzzle::centre(app, 0, 0), nullptr, false);
            puzzle::teleportTo(app, target);
            for (int f = 0; f < 4; ++f) stepEvents(app);
            const bool faded = app.puzzle.tpState != 0 && app.playerAlpha < 0.01f;
            const bool loaded = applySave(app, saved);
            const bool visible = loaded && app.playerAlpha == 1 && app.playerFadeLeft == 0 && !app.puzzle.tpState;
            std::printf("field lifecycle: interrupted teleport faded %d, reload visible %s\n", faded, visible ? "ok" : "FAILED");
            problems += !faded + !visible;
        }
    } else ++problems;
    problems += app.renderer.takeErrors();
    std::printf("field lifecycle: %d abilities, %d problems\n", titles, problems);
    app.trackerArmed = false;
    return problems;
}

// Omega's native crystal returns: every puzzle room, including a solved stage 0 and save reloads.
int mobileTransitionSelfTest(App& app) {
    int problems = 0, cases = 0;
    app.noEncounters = true;
    auto settle = [&] {
        for (int quiet = 0, f = 0; f < 5000 && quiet < 10; ++f) {
            stepEvents(app);
            if (!app.dialogue.empty()) advanceDialogue(app);
            if (app.choiceFlag >= 0) answerChoice(app, true);
            if (app.battle.active) endBattle(app, true);
            app.tapPending = true;
            if (f % 64 == 0) { app.lastTicks = 0; drawFrame(app, false); }
            quiet = app.runner.running() || app.pendingEntry != App::Entry::None || app.tutoTex.on ||
                    app.puzzle.tpState || app.fadeT < app.fadeFrames ? 0 : quiet + 1;
        }
    };
    for (int stage = 0; stage < cr::kPuzzleStages; ++stage) for (bool reload : {false, true}) {
        app.runner.stop();
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.playerChar = 0;
        app.game.setFlag(3090, true);
        // The first room is solved through its actual teleport and crystal-camera code.
        app.game.setFlag(3000 + stage, stage != 0);
        app.mapNumber = app.entryArea = -1;
        cr::WarpRequest w;
        w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + stage;
        performWarp(app, w, -1);
        settle();
        if (reload) {
            const auto save = captureSave(app);
            app.fadeGrey = app.fadeFrom = app.fadeTo = 1;
            app.fadeT = app.fadeFrames = 1;
            app.crystalCam.npc = 9999;
            problems += !applySave(app, save);
            settle();
        }
        if (stage == 0) {
            for (const auto& cell : {std::array<int, 4>{0, 2, 1, 1}, std::array<int, 4>{4, 2, 2, 1}}) {
                setActor(app, 0, puzzle::centre(app, cell[0], cell[1]), nullptr, false);
                const int block = puzzle::objectAt(app, 1, cell[2], cell[3]);
                if (block >= 0) puzzle::teleportTo(app, block);
                else ++problems;
                settle();
            }
            problems += !puzzle::allSwitchesOn(app) || !app.game.flag(3300);
        }
        const int event = 1 + 2 * stage;
        cr::WarpEntry dest;
        const bool have = app.events.warpEntry(event, dest);
        problems += !have || !app.runner.start(app.events, event, app.game, app.area, app.mapNumber);
        app.runnerWhat = "mobile regression: crystal exit";
        settle();
        const bool ok = have && app.area == dest.area && app.mapNumber == dest.map && app.puzzle.stage == -1 &&
                        app.crystalCam.npc == -1 && !app.runner.running() && screenFadeAlpha(app) < 0.01f;
        std::printf("mobile transition: stage %d reload %d -> area %d map %d fade %.2f; %s\n",
                    stage, reload, app.area, app.mapNumber, screenFadeAlpha(app), ok ? "ok" : "FAILED");
        problems += !ok + app.renderer.takeErrors();
        ++cases;
        app.lastTicks = 0;
        drawFrame(app, false);
    }
    app.trackerArmed = false;
    std::printf("mobile transitions: %d exits, %d problems\n", cases, problems);
    return problems;
}

int graphicsRecoverySelfTest(App& app) {
    const auto at = std::find(app.maps.begin(), app.maps.end(), "d0101_10.cmp");
    if (at == app.maps.end()) return 1;
    app.mode = Mode::Maps;
    showMap(app, int(at - app.maps.begin()), 0, 2);
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.showBackground = true;
    app.fadeFrames = 0;
    app.optionsOpen = app.charSelectOpen = app.saveScreen.open = false;
    app.dialogue = {{"Graphics recovery"}};
    app.dialoguePage = 0;
    app.dialogueInfo.resize(1);
    app.dialogueInfo[0].closeAfter = 0; // static panel, without the wall-clock "next" blink
    app.showHud = false;
    auto frame = [&] {
        app.lastTicks = 0; // freeze the scene during pixel comparisons
        drawFrame(app, false);
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        app.renderer.readPixels(pixels, w, h);
        return pixels;
    };
    if (!SDL_GL_GetCurrentContext()) { // (Vulkan: the GL backups and their context aren't used - nothing to replace)
        std::printf("graphics recovery: needs the OpenGL renderer (--renderer gl); 1 problem\n");
        return 1;
    }
    frame(); // populate lazy UI resources
    const auto before = frame();
    if (const char* file = SDL_getenv("CR_SELFTEST_GRAPHICS_CAPTURE")) saveFrame(app, file);
    // Require visible field pixels above the dialogue, not just matching blank frames.
    size_t visible = 0;
    for (size_t i = 0; i + 3 < before.size() / 2; i += 4)
        visible += int(before[i]) + before[i + 1] + before[i + 2] > 32;
    int problems = app.renderer.takeErrors() + (visible < before.size() / 8 / 10);
    std::printf("graphics recovery: visible field pixels %zu\n", visible);
    for (int cycle = 0; cycle < 3; ++cycle) {
        SDL_GL_DestroyContext(SDL_GL_GetCurrentContext());
        if (!SDL_GL_CreateContext(app.window)) return 1;
        SDL_Event event{};
        event.type = SDL_EVENT_RENDER_DEVICE_RESET;
        event.render.windowID = SDL_GetWindowID(app.window);
        if (SDL_AppEvent(&app, &event) != SDL_APP_CONTINUE) return 1;
        event.type = SDL_EVENT_USER; // next game-thread event consumes the queued reset
        if (SDL_AppEvent(&app, &event) != SDL_APP_CONTINUE) return 1;
        const auto after = frame();
        const bool sameFrame = !before.empty() && before == after;
        problems += !sameFrame + app.renderer.takeErrors();
        std::printf("graphics recovery: cycle %d, identical field/dialogue frame %s\n", cycle + 1, sameFrame ? "yes" : "NO");
    }
    std::printf("graphics recovery: %d problems\n", problems);
    return problems;
}

int sceneAudioSelfTest(App& app) {
    app.mode = Mode::Maps;
    const auto at = std::find(app.maps.begin(), app.maps.end(), "d0101_10.cmp");
    if (at == app.maps.end() || !startSound(app)) return 1;
    vitaAudio(app);
    showMap(app, int(at - app.maps.begin()), 0, 2);
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    int problems = 0, clip = -1;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("scene audio FAILED: %s\n", what); }
    };
    if (app.sound.hasVoices()) {
        for (int i = 0; i < app.sound.voiceClips(); ++i) {
            app.sound.playLine(i);
            if (app.sound.linePlaying()) { clip = i; break; }
        }
        check(clip >= 0, "a real story clip can play");
        if (clip >= 0) { // PrcMsg 0x1e: a page's two cues, the typing held at the second while the first plays
            app.dialogue = {{"Old page"}};
            app.dialogueVoices = {{{0, clip}, {0, clip}}};
            ++app.dialogueSerial;
            app.dialoguePage = 0;
            app.dialogueTyped = 0;
            check(!stepVoiceCue(app) && app.sound.linePlaying() && app.voiceCue == 1, "a page's first cue starts its clip");
            check(stepVoiceCue(app) && app.voiceCue == 1, "its second cue holds the typing while the first clip plays");
            app.sound.stopLine(); // the first clip has finished
            int played = app.voicesPlayed;
            check(!stepVoiceCue(app) && app.sound.linePlaying() && app.voicesPlayed == played + 1 && app.voiceCue == 2,
                  "the second clip follows once the first is done");
            app.dialogueVoices = {{{0, clip}, {0, clip}}};
            ++app.dialogueSerial; // a new page with two cues: its first starts ...
            app.sound.stopLine();
            check(!stepVoiceCue(app) && app.sound.linePlaying() && app.voiceCue == 1, "a new page's cues start over");
            app.dialogue.clear(); // ... then a scene closes the window directly, without a page tap
            app.sound.stopLine(); // the first clip has finished
            played = app.voicesPlayed;
            updateVoices(app);
            check(!stepVoiceCue(app) && app.voiceCue == 0 && app.voicesPlayed == played && !app.sound.linePlaying(),
                  "a closed page cannot restart its remaining clip");
            app.sound.playLine(clip);
            app.dialogue = {{"Silent replacement"}};
            app.dialogueVoices = {{}};
            ++app.dialogueSerial;
            updateVoices(app);
            check(!app.sound.linePlaying(), "an unvoiced replacement stops the previous page's line");
        }
    }
    app.dialogue = {{"Interrupted by battle"}};
    // The Android speech-in-effects path exists in every data variant too.
    app.sound.play(0xc00000);
    check(app.sound.dialogueVoiceCount() > 0, "speech-marked looping sample is playing before battle");
    startBattle(app, {1, 0, -1, 0}, 0);
    check(app.battle.active, "real battle starts");
    app.sound.update(); // refill an old looping stream if battle entry failed to stop it
    check(app.dialogue.empty() && app.sound.dialogueVoiceCount() == 0,
          "battle entry clears the field dialogue's audio and queued clips");
    if (app.battle.active) endBattle(app, false);
    app.runner.stop();
    app.sound.stopAll();
    bootTitle(app);
    check(app.sound.dialogueVoiceCount() == 0, "title cleanup remains silent");
    problems += app.renderer.takeErrors();
    std::printf("scene audio: story clip %d, battle entry and title; %d problems\n", clip, problems);
    return problems;
}

int sessionUiSelfTest(App& app) {
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("session UI FAILED: %s\n", what); }
    };
    const auto at = std::find(app.maps.begin(), app.maps.end(), "d0101_10.cmp");
    if (at == app.maps.end()) return 1;
    auto field = [&] {
        app.mode = Mode::Maps;
        app.playerChar = 0;
        showMap(app, int(at-app.maps.begin()), 0, 2);
        app.booting = app.titleOpen = false;
        app.game.setFlag(0x43, true); // Options tutorial already read in this fixture
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
    };
    auto key = [&](SDL_Keycode code) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        SDL_AppEvent(&app, &event);
    };
    // Options' Return to Title as 3.1.0's OptionPrc plays it (a638921): the row asks 57060 with No under
    // the cursor (0xc / 0xd), Yes fades to black and the title comes twelve frames on (0x18 / 0x19)
    auto returnToTitle = [&] {
        key(SDLK_END); key(SDLK_RETURN);
        if (app.resetAsk != 3 || app.resetConfirm != 1) return;
        key(SDLK_UP); key(SDLK_RETURN);
        for (int f = 0; f < 14 && app.titleReturnIn > 0; ++f) stepEvents(app);
    };
    field();
    openCamp(app);
    campOpenPage(app, 4); // Camp -> Options, the real Return to Title path
    app.playing = false;
    returnToTitle();
    check(app.booting && !app.campOpen && !app.optionsOpen, "Camp cannot cover or capture input on the title");
    check(app.playing, "return to title resumes normal game updates");
    // This directory contains only this test's synthetic save, never player progress.
    const std::string keepDir = app.saveDirOverride;
    app.saveDirOverride = userDir()+"session-ui-test-"+std::to_string(SDL_GetTicksNS())+"/";
    SDL_CreateDirectory(app.saveDirOverride.c_str());
    field();
    cr::SaveData save = captureSave(app);
    save.game.oz = 54321;
    const std::string path = slotPath(app, 1);
    { // an opened box is kept once, however often a save lists it (SetOpenedTresureBox once a box)
        cr::SaveData boxes = save;
        boxes.game.openedBoxes = {6012, 3004, 6012, 6012, 3004};
        cr::SaveData got;
        check(cr::writeSave(path, boxes) && cr::readSave(path, got) && got.game.openedBoxes == std::vector<int>{3004, 6012},
              "opened boxes saved once each");
    }
    check(cr::writeSave(path, save), "synthetic save created");
    { // a save cut short is refused, here and in the Vita import's order (the party last, no boxes)
        std::vector<uint8_t> whole;
        cr::readFile(path, whole);
        const std::string text(whole.begin(), whole.end());
        const auto cutsRefused = [&](const std::string& file, const std::string& last) {
            const size_t end = file.find('\n', file.rfind('\n' + last) + 1) + 1;
            bool refused = file.rfind('\n' + last) != std::string::npos;
            cr::SaveData got;
            for (size_t cut = 0; cut < file.size(); ++cut) {
                if (cut >= end && file[cut - 1] == '\n') continue; // (a save that ends here is an older one)
                SDL_SaveFile(path.c_str(), file.data(), cut);
                refused = refused && !cr::readSave(path, got);
            }
            return refused;
        };
        std::string imported, partyLines;
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            if (line.rfind("boxes", 0) == 0 || line.rfind("treasure", 0) == 0) continue;
            (line.rfind("party", 0) == 0 ? partyLines : imported) += line + '\n';
        }
        imported += partyLines;
        check(cutsRefused(text, "walkexp"), "a save cut short is refused");
        check(cutsRefused(imported, "party"), "a Vita import cut short is refused");
        cr::SaveData got;
        const auto valueRefused = [&](const std::string& key, int value) { // a whole save, a place no game has
            const size_t at = text.find('\n' + key + ' ') + 1 + key.size() + 1;
            const std::string file = text.substr(0, at) + std::to_string(value) + text.substr(text.find('\n', at));
            SDL_SaveFile(path.c_str(), file.data(), file.size());
            return !cr::readSave(path, got);
        };
        check(valueRefused("area", cr::kNumAreas) && valueRefused("map", cr::kNumMaps), "a save's place out of range is refused");
        SDL_SaveFile(path.c_str(), imported.data(), imported.size());
        check(cr::readSave(path, got) && got.game.oz == 54321, "a whole Vita import reads");
        SDL_SaveFile(path.c_str(), text.data(), text.size());
        check(cr::readSave(path, got) && got.game.oz == 54321, "a whole save still reads");
    }
    std::vector<uint8_t> before, after, dataBefore, dataAfter;
    cr::readFile(path, before);
    openSaveScreen(app, false);
    // (a save goes to data.dat since 63286c5, the one save: the screen's read folds save1.dat into it)
    cr::readFile(dataPath(app), dataBefore);
    key(SDLK_V); returnToTitle();
    check(app.booting && !app.saveScreen.open && app.saveScreen.pending == 0,
          "return to title cancels a pending save confirmation");
    key(SDLK_RETURN); // a late confirmation must not overwrite the test's existing file
    cr::readFile(path, after);
    cr::readFile(dataPath(app), dataAfter);
    check(!before.empty() && after == before && !dataBefore.empty() && dataAfter == dataBefore,
          "late Enter cannot overwrite the previous save from the title");
    // Clear broader overlays and detach an asynchronous picker from the old session.
    app.campOpen = app.manualOpen = app.scc.open = app.charSelectOpen = true;
    app.itemWindowOpen = app.shop.open = app.shop.talkRunning = true;
    app.mapScreen.open = app.mapScreen.closing = app.staffRoll.open = app.lastBattle.open = true;
    app.resumePrompt = 1;
    app.menuMask.to = 160;
    app.touchPad.down = app.touchPad.shown = app.campPointer.down = true;
    app.optionsBarDrag = app.dragging = app.tapPending = true;
    holdSkip(app, kSkipKey, true);
    app.padHeld = 1;
    app.heldPage = true, app.lastPage = {"Old page"};
    app.saveImport.state = vitaimport::Session::Picking;
    app.saveImport.choice = std::make_shared<vitaimport::Session::Choice>();
    const auto oldPicker = app.saveImport.choice;
    const int fieldSpeed = app.fieldSpeed, volume = app.mix.master;
    bootTitle(app);
    check(!app.campOpen && !app.manualOpen && !app.scc.open && !app.charSelectOpen &&
          !app.shop.open && !app.shop.talkRunning && !app.mapScreen.open && !app.mapScreen.closing &&
          !app.staffRoll.open && !app.lastBattle.open && !app.itemWindowOpen && !app.resumePrompt,
          "old overlays are closed at the title boundary");
    check(!app.touchPad.down && !app.campPointer.down && !app.dragging && !app.optionsBarDrag &&
          !app.tapPending && !app.skipScene && app.padHeld == -1 && !app.heldPage && app.lastPage.empty() && app.menuMask.to == 0,
          "old gestures, pending dialogue and menu fade are cleared");
    { std::lock_guard<std::mutex> lock(oldPicker->mutex); oldPicker->path = "/no-such-late-import-after-title"; oldPicker->ready = true; }
    app.saveImport.poll();
    check(!app.saveImport.active() && !app.saveImport.choice, "late folder-picker completion cannot reopen import over the title");
    check(app.fieldSpeed == fieldSpeed && app.mix.master == volume, "user preferences survive the session boundary");
    // Loading through the actual confirmation must retain the load-complete panel.
    app.chosenPair = 1;
    app.titleOpen = true;
    openSaveScreen(app, true);
    key(SDLK_RETURN); key(SDLK_RETURN);
    check(app.game.oz == 54321, "confirmed load restores the synthetic save's progress");
    check(app.saveScreen.open && app.saveScreen.phase == 2 && app.saveScreen.load &&
          app.saveScreen.haveSave && app.saveScreen.data.game.oz == 54321,
          "load-complete panel retains its mode and loaded save after session cleanup");
    for (int i = 0; i < 40; ++i) stepSaveScreen(app);
    check(!app.saveScreen.open && !app.titleOpen, "load-complete panel dismisses to the field");
    app.playSeconds = 1234.5;
    startNewGame(app);
    check(app.playSeconds == 0, "a completely fresh game resets the previous session's playtime");
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.playSeconds = 4321.5;
    app.resetAsk = 1;
    resetAnswer(app, false);
    check(app.playSeconds == 4321.5 && SDL_GetPathInfo(path.c_str(), nullptr), "declining reset keeps playtime and saves");
    save.playSeconds = 777;
    check(cr::writeSave(path, save), "synthetic save rewritten");
    app.playSeconds = 9999;
    bootTitle(app);
    check(app.playSeconds == 777, "the title goes back to the newest save's playtime");
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.playSeconds = 4321.5;
    app.resetAsk = 1;
    resetAnswer(app, true);
    check(app.resetAsk == 2 && app.playSeconds == 4321.5 && SDL_GetPathInfo(path.c_str(), nullptr), "first reset confirmation keeps playtime and saves");
    resetAnswer(app, true);
    check(app.playSeconds == 0 && !SDL_GetPathInfo(path.c_str(), nullptr), "confirmed data reset clears playtime with saved progress");
    app.playSeconds = 55;
    bootTitle(app);
    check(app.playSeconds == 0, "the title with no save starts the playtime from zero");
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.saveImport.cancel();
    SDL_RemovePath(path.c_str());
    SDL_RemovePath(app.saveDirOverride.c_str());
    app.saveDirOverride = keepDir;
    problems += app.renderer.takeErrors();
    std::printf("session UI: Camp/Options, save confirmation, late picker and title; %d problems\n", problems);
    return problems;
}

// How field events start and the field's HUD, as 3.1.0 has them: RectWrapper::checkEventKick's lastKick
// kept across a warp (the Ark's NPC 260 kicks on arrival), npc_setup's flags read at a load only, its
// story actors killed, a battle's end undoing Born / Kill and Uty_TBox, the examine's choice and SE,
// menuf_mainPut / Prc's rules
int fieldEventsSelfTest(App& app) {
    int problems = 0;
    auto chk = [&](bool wrong, const char* what) {
        if (wrong) ++problems, std::printf("field events: not as expected: %s\n", what);
    };
    const cr::GameState keepGame = app.game;
    const int keepChar = app.playerChar;
    app.game = cr::GameState();
    cr::giveStartingItems(app.game, app.itemTable);
    app.playerChar = 0;
    auto load = [&](int map, int area) {
        const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map]));
        if (at == app.maps.end()) return false;
        showMap(app, int(at - app.maps.begin()), map, area);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        app.dialogue.clear();
        return app.mapNumber == map && app.area == area && app.havePlayer;
    };
    auto find = [&](int id) -> const cr::FieldNpc* {
        for (const cr::FieldNpc& n : app.npcs)
            if (n.id == id) return &n;
        return nullptr;
    };
    ViewerHost host(app);
    // 1. A dungeon's way back to the Ark (d01's warp key 1: area 9, map 169, (0, 7.5, 15)) lands inside
    // NPC 260's 40 x 60 box (show flag 1391): lastKick stays the warping box's id, so 260 kicks
    int kicked = -1, kickAfter = -1;
    bool kept = false;
    app.game.setFlag(1391, true);
    if (load(0, 2)) {
        app.lastKick = 1;
        cr::WarpRequest w;
        w.valid = true, w.area = 9, w.map = 169, w.pos[0] = 0, w.pos[1] = 7.5f, w.pos[2] = 15, w.angleDegrees = 0;
        performWarp(app, w, 1);
        app.pendingEntry = App::Entry::None; // (the area's script: not this test's)
        kept = app.lastKick == 1;
        checkTriggers(app);
        kicked = app.runner.running() ? app.runner.eventNo() : 0;
        app.runner.stop();
        checkTriggers(app); // standing on in it: no second kick
        kickAfter = app.runner.running() ? app.runner.eventNo() : 0;
        app.runner.stop();
    }
    chk(!kept || kicked != 260 || kickAfter != 0, "the Ark's arrival kicks NPC 260 once");
    // 2. Flags are read as the room loads: 1391 set mid-room (t01's entry 0 does) arms nothing until a reload
    int before = -1, midRoom = -1, reloaded = -1;
    app.game.setFlag(1391, false);
    if (load(169, 9)) {
        const cr::Vec3 p{0, 7.5f, 15};
        before = triggerAt(app, p);
        app.game.setFlag(1391, true);
        midRoom = triggerAt(app, p);
        load(169, 9);
        reloaded = triggerAt(app, p);
    }
    chk(before != 0 || midRoom != 0 || reloaded != 260, "flag 1391 set mid-room arms box 260 only after a reload");
    // 3. npc_setup kills the story actors (400..449) as a room loads: d04 map 53's 424 / 425
    bool storyKilled = false, bornShows = false;
    if (load(53, 5)) {
        const cr::FieldNpc* a = find(424);
        storyKilled = a && !npcActive(app, *a) && (!find(425) || !npcActive(app, *find(425)));
        host.showNpc(424, true);
        a = find(424);
        bornShows = a && npcActive(app, *a);
    }
    chk(!storyKilled || !bornShows, "d04 map 53's story actors 424 / 425 killed at load, 424 Born");
    // 4. A battle's end reads the field back: Born / Kill and Uty_TBox(0) undone (d01: a room with a box)
    int boxRoom = -1, boxId = -1;
    bool tboxKilled = false, tboxBack = false, bornUndone = false, killUndone = false;
    for (int m = 0; m < 14 && boxRoom < 0; ++m)
        if (load(m, 2))
            for (const cr::FieldNpc& n : app.npcs)
                if ((n.treasure & 3) && npcActive(app, n)) { boxRoom = m, boxId = n.id; break; }
    if (boxRoom >= 0) {
        const cr::FieldNpc* box = find(boxId);
        const cr::FieldNpc* lid = find(boxId + 1);
        app.walker.pos = {box->base(0) + box->offset[0] + box->box[0] / 2 + app.walker.radius, box->pos[1], box->base(2) + box->offset[2]};
        const bool reach = interactTarget(app) == box;
        host.treasureBoxesShown(false);
        tboxKilled = reach && !npcActive(app, *find(boxId)) && (!lid || !npcActive(app, *find(boxId + 1))) && !interactTarget(app);
        int other = -1; // a person or object shown, Killed, and a story actor's id Born
        for (const cr::FieldNpc& n : app.npcs)
            if (!(n.treasure & 3) && n.id != boxId + 1 && npcActive(app, n)) { other = n.id; break; }
        if (other >= 0) host.showNpc(other, false);
        for (const cr::FieldNpc& n : app.npcs)
            if (npcStoryActor(app, n.id)) { host.showNpc(n.id, true); break; }
        startBattle(app, {1, 0, -1, 0}, 0);
        if (app.battle.active) endBattle(app, true);
        app.runner.stop(), app.pendingEntry = App::Entry::None;
        tboxBack = npcActive(app, *find(boxId)) && (!lid || npcActive(app, *find(boxId + 1)) == npcCreateFlags(app, *find(boxId + 1)));
        killUndone = other < 0 || npcActive(app, *find(other));
        bornUndone = true;
        for (const cr::FieldNpc& n : app.npcs)
            if (npcStoryActor(app, n.id) && npcActive(app, n)) bornUndone = false;
    }
    chk(boxRoom < 0 || !tboxKilled || !tboxBack || !killUndone || !bornUndone, "Uty_TBox(0) and Kill undone by a battle's end");
    // 6. The examine: an object in reach wins over a nearer person; a model-less NPC's reach about its
    // record's position + the offset once; the window lingers 10 frames; SE 0x93 on the examine
    bool objectFirst = false, offsetOnce = false, lingers = false, gone = false, se93 = false;
    if (load(0, 2)) {
        const std::vector<cr::FieldNpc> keepNpcs = app.npcs;
        const cr::Vec3 p = app.walker.pos;
        cr::FieldNpc obj, man, ghost;
        obj.id = 900, obj.kind = 8, obj.model = 9, obj.box[0] = obj.box[1] = obj.box[2] = 4;
        obj.pos[0] = p.x + app.walker.radius + 2.5f, obj.pos[1] = p.y, obj.pos[2] = p.z; // (reach: half the box + radius + 1.5)
        man = obj, man.id = 901, man.kind = 0x18, man.pos[0] = p.x + 1;
        ghost = obj, ghost.id = 902, ghost.model = -1, ghost.offset[0] = 30, ghost.pos[0] = p.x - 30 + 40;
        app.npcs = {obj, man};
        app.npcShown[900] = app.npcShown[901] = app.npcShown[902] = true;
        app.examineLinger.clear();
        const cr::FieldNpc* t = interactTarget(app);
        objectFirst = t && t->id == 900;
        stepExamineWindows(app);
        app.walker.pos = {p.x + 60, p.y, p.z};
        stepExamineWindows(app);
        t = interactTarget(app);
        lingers = t && t->id == 900;
        for (int f = 0; f < 10; ++f) stepExamineWindows(app);
        gone = interactTarget(app) == nullptr;
        app.npcs = {ghost}; // base = pos (no offset for model -1), reach about pos + 30 = p.x + 40
        app.walker.pos = {p.x + 40, p.y, p.z};
        t = interactTarget(app);
        offsetOnce = t && t->id == 902;
        app.walker.pos = p;
        app.npcs = {obj};
        app.menuSoundLog.clear();
        interact(app);
        se93 = !app.menuSoundLog.empty() && app.menuSoundLog.back() == se::kChoose;
        app.runner.stop();
        app.dialogue.clear();
        app.npcs = keepNpcs;
    }
    chk(!objectFirst || !lingers || !gone || !offsetOnce || !se93, "the examine's target, its window's linger and SE 0x93");
    // 7. The HUD: no fade (gone the frame a script runs, back whole); the Camp's close shuts the bar at
    // once; the pad lock (Util 2046) refuses Camp / Save, not the corner; the key icons; the low battery
    bool noFade = false, restOnClose = false, padLock = false, keys = false, battery = false;
    if (load(40, 4) || load(0, 2)) {
        App::FieldMenu& fm = app.fieldMenu;
        fm = App::FieldMenu();
        stepFieldMenu(app);
        const int shown = fm.fade;
        app.itemWindowOpen = true; // (a scene holding the field: G+0x364 & 9)
        stepFieldMenu(app);
        const int during = fm.fade;
        app.itemWindowOpen = false;
        stepFieldMenu(app);
        noFade = shown == 320 && during == 0 && fm.fade == 320;
        toggleFieldMenu(app);
        for (int f = 0; f < 4; ++f) stepFieldMenu(app);
        app.campOpen = true;
        trackFieldMenus(app);
        app.campOpen = false;
        trackFieldMenus(app);
        restOnClose = !fm.open && fm.anim == 0;
        host.puzzleTeleportCount(5);
        toggleFieldMenu(app);
        const bool cornerOpens = fm.open;
        for (int f = 0; f < 4; ++f) stepFieldMenu(app);
        pressFieldButton(app, 0);
        padLock = app.padLock == 5 && cornerOpens && !app.campOpen;
        app.padLock = 0;
        fm = App::FieldMenu();
        const int keepMap = app.mapNumber;
        app.mapNumber = 40;
        app.game.setFlag(0x4b4, true);
        std::vector<KeyIcon> k = fieldKeyIcons(app);
        const bool two = k.size() == 2 && k[0].lit && !k[1].lit && k[1].x - k[0].x == 32 && k[0].u == 128 && k[1].u == 160;
        app.game.setFlag(0x4b6, true);
        const bool done = fieldKeyIcons(app).empty();
        app.game.scenario = 1; // (the flags + 400 a scenario)
        k = fieldKeyIcons(app);
        const bool other = k.size() == 2 && !k[0].lit;
        app.game.scenario = 0;
        app.mapNumber = 60;
        app.game.setFlag(0x4cb, true);
        k = fieldKeyIcons(app);
        const bool three = k.size() == 3 && !k[0].lit && k[1].lit && !k[2].lit && k[0].u == 192;
        app.mapNumber = 20;
        keys = two && done && other && three && fieldKeyIcons(app).empty();
        app.mapNumber = keepMap;
        bool low = false;
        battery = !lowBatteryStep(low, 30) && lowBatteryStep(low, 17) && lowBatteryStep(low, 20) && !lowBatteryStep(low, 23) &&
                  !lowBatteryStep(low, 20);
    }
    chk(!noFade || !restOnClose || !padLock || !keys || !battery, "the HUD's rules");
    std::printf("field events: the Ark's arrival kicks %d (lastKick kept %s); box 260 before / mid-room / reloaded %d / %d / %d; "
                "d04's 424 / 425 killed at load %s, Born %s; a battle's end brings back box %d %s, Kill %s, story actors %s; "
                "examine: the object first %s, lingers %s then gone %s, model -1's offset once %s, SE 0x93 %s; HUD: no fade %s, "
                "the Camp's close shuts the bar %s, the pad lock %s, key icons %s, low battery %s; %d problems\n",
                kicked, kept ? "yes" : "NO", before, midRoom, reloaded, storyKilled ? "yes" : "NO", bornShows ? "shows" : "NO",
                boxId, tboxKilled && tboxBack ? "yes" : "NO", killUndone ? "undone" : "NO", bornUndone ? "killed" : "NO",
                objectFirst ? "yes" : "NO", lingers ? "yes" : "NO", gone ? "yes" : "NO", offsetOnce ? "yes" : "NO", se93 ? "yes" : "NO",
                noFade ? "yes" : "NO", restOnClose ? "yes" : "NO", padLock ? "yes" : "NO", keys ? "yes" : "NO", battery ? "yes" : "NO",
                problems);
    app.runner.stop();
    app.pendingEntry = App::Entry::None;
    app.fieldMenu = App::FieldMenu();
    app.lastKick = 0;
    app.game = keepGame;
    app.playerChar = keepChar;
    return problems;
}

// Story battles lost: btl_main 0x4b0 ends groups 0x289, 0x28e - j001_d06's Vieg alone -, 0x2a2 - c101_t01's
// - and 0x295 as a won one (9000), and btlord_chk's flag 0xffe ends 0x28a - the opening, j001_t01's - and
// scenario 1's 0x2a3 - c101_t01's - from their first round, lost or not: no defeat, the script goes on from
// its Battle. Any other story battle lost is the game over (0x4b1, btl_lose: the title)
int storyLossSelfTest(App& app) {
    int problems = 0;
    const cr::GameState keepGame = app.game;
    const int keepChar = app.playerChar;
    const App::Cheats keepCheats = app.cheats;
    struct Case { int scenario, group, stage, round; bool story; };
    // (each twice: the second time with Infinite HP and One-Hit Kills on - a story loss must still come, the cheats
    // left out of it; any other battle keeps them)
    for (int cheat = 0; cheat < 2; ++cheat)
    for (const Case& c : {Case{0, 654, 19, 1, true}, Case{1, 674, 0, 1, true}, Case{0, 650, 0, 2, true}, Case{0, 650, 0, 1, true},
                          Case{1, 675, 0, 1, true}, Case{0, 651, 11, 1, false}}) {
        app.cheats = keepCheats;
        app.cheats.hp = app.cheats.oneHit = cheat == 1;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.scenario = c.scenario;
        for (int f : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(f, true); // (the tutorials seen)
        showMap(app, 0, 0, c.scenario == 0 ? 6 : 10);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {c.group, c.stage, -1, 0}, 0);
        App::Battle& b = app.battle;
        if (!b.active) {
            std::printf("story battle %d lost: didn't start\n", c.group);
            ++problems;
            continue;
        }
        b.eventBattle = true; // (as a script's Battle starts it)
        if (cheat) { // Infinite HP: on while a foe can fall; in a story loss (0x4b0's) it lapses once none can - held at its
            // floor or invincible - so the pair's fall comes; anywhere else it holds
            const bool lossGroup = c.group == 0x289 || c.group == 0x28e || c.group == 0x2a2 || c.group == 0x295;
            const bool onAtStart = b.rules.infiniteHp();
            const cr::BattleRules keep = b.rules;
            for (int i = 2; i < 5; ++i)
                if (b.rules.f[size_t(i)].alive()) b.rules.hpFloor[size_t(i)] = std::max(1, b.rules.hpFloor[size_t(i)]), b.rules.f[size_t(i)].hp = b.rules.hpFloor[size_t(i)];
            const bool onHeld = b.rules.infiniteHp();
            b.rules = keep;
            const bool ok = b.rules.cheatHp && b.rules.cheatOneHit && onAtStart && onHeld == !lossGroup;
            if (!ok) {
                std::printf("story battle %d with the cheats on: Infinite HP %s at the start, %s with the foes held - WRONG\n", c.group,
                            onAtStart ? "on" : "off", onHeld ? "on" : "off");
                ++problems;
            }
        }
        if (cheat && !c.story) { // (that one's defeat can't come with Infinite HP: checked above, not played)
            endBattle(app, false);
            app.runner.stop();
            app.pendingEntry = App::Entry::None;
            continue;
        }
        bool lostShown = false;
        int exit = 0;
        for (int f = 0; f < 6000 && b.active; ++f) {
            if (b.phase == App::Battle::Command && !battletalk::on(app)) { // the pair down as that round ends
                for (int k = 0; k < 2; ++k) b.rules.f[size_t(k)].hp = 0;
                b.rules.round = c.round;
                endRound(app);
            }
            if (f % 45 == 44 && !app.dialogue.empty()) advanceDialogue(app);
            lostShown |= b.phase == App::Battle::Lost;
            if (b.phase == App::Battle::Lost && b.phaseFrames >= 40) b.lostTap = true; // (the game over waits for a tap)
            if (b.exit) { exit = b.exit, b.exit = 0; break; }
            stepBattle(app);
        }
        const bool right = c.story ? !lostShown && exit == 1 : lostShown && exit == 2;
        problems += !right;
        std::printf("story battle %d lost in round %d%s: %s, way out %d (%s) %s\n", c.group, c.round, cheat ? " (Infinite HP on)" : "",
                    lostShown ? "the defeat shown" : "no defeat", exit,
                    exit == 1 ? "the script goes on" : exit == 2 ? "the title" : exit == 3 ? "the Ark" : "none", right ? "ok" : "WRONG");
        if (b.active) endBattle(app, false);
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
    }
    { // j001_d06's own script: from 0x6219 (vars 58 .. 60, then Battle 654 at 0x623d) the battle lost, its
      // talk (Gosub 0x62e7: Msg 1200, 1201) and on to flag 1408 (0x6247)
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.scenario = 0;
        for (int f : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(f, true);
        showMap(app, 0, 0, 7); // (area 7: j001_d06.evt)
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        const bool started = app.runner.startAt(app.events, 0x6219, 0, app.game, app.area, app.mapNumber);
        bool fought = false, wrongWay = false, on = false;
        int group = -1;
        for (int f = 0; f < 20000 && started && !on && !wrongWay; ++f) {
            if (f % 45 == 44 && !app.dialogue.empty()) advanceDialogue(app);
            if (app.battle.active) {
                App::Battle& b = app.battle;
                fought = true, group = b.start.group;
                if (b.phase == App::Battle::Command && !battletalk::on(app)) {
                    for (int k = 0; k < 2; ++k) b.rules.f[size_t(k)].hp = 0;
                    endRound(app);
                }
                if (b.exit) { // (as drawFrame takes it)
                    const int e = b.exit;
                    b.exit = 0;
                    if (e == 1) endBattle(app, false);
                    else wrongWay = true, endBattle(app, false);
                } else stepBattle(app);
                continue;
            }
            stepEvents(app);
            on = app.game.flag(1408);
        }
        const bool right = started && fought && group == 654 && !wrongWay && on;
        problems += !right;
        std::printf("j001_d06's Battle 654 lost: %s, battle %d, %s %s\n", started ? "started" : "NOT STARTED", group,
                    on ? "the script went on to flag 1408" : wrongWay ? "the game over" : "flag 1408 NOT reached", right ? "ok" : "WRONG");
        if (app.battle.active) endBattle(app, false);
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
    }
    // Played with Infinite HP and One-Hit Kills on, the pair attacking each round: 654 (Vieg alone: its foes held at their
    // 1-HP floor) and 674 (c101_t01: its foes take nothing from the pair and heal) still end, the story going on - never
    // a fight with nothing to end it. (A story loss that can be won keeps Infinite HP while the foes' HP goes down; none of
    // 0x4b0's four is: 661 - 0x295 - has no monsters in the data)
    for (const Case& c : {Case{0, 654, 19, 0, true}, Case{1, 674, 0, 0, true}}) {
        app.cheats = keepCheats;
        app.cheats.hp = app.cheats.oneHit = true;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.scenario = c.scenario;
        for (int f : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(f, true);
        showMap(app, 0, 0, c.scenario == 0 ? 6 : 10);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {c.group, c.stage, -1, 0}, 0);
        App::Battle& b = app.battle;
        if (!b.active) {
            std::printf("story battle %d played: didn't start - WRONG\n", c.group);
            ++problems;
            continue;
        }
        b.eventBattle = true;
        int exit = 0, rounds = 0;
        bool won = false, lostShown = false, pairFell = false;
        for (int f = 0; f < 20000 && b.active; ++f) {
            if (f % 45 == 44 && !app.dialogue.empty()) advanceDialogue(app);
            if (b.phase == App::Battle::Command && b.menu == App::Battle::Top && !battletalk::on(app)) {
                battleChooseTop(app, 1); // (Pair, else Solo)
                if (b.menu == App::Battle::Top) battleChooseTop(app, 0);
                if (b.menu == App::Battle::Commands) battleChooseCommand(app, 0);
                if (b.menu == App::Battle::Targets && !b.picks.empty()) battleChooseTarget(app, b.picks[0]);
                if (b.phase == App::Battle::Command && b.menu == App::Battle::Commands) battleChooseCommand(app, 0);
                if (b.menu == App::Battle::Targets && !b.picks.empty()) battleChooseTarget(app, b.picks[0]);
            }
            rounds = std::max(rounds, b.rules.round);
            won |= b.rules.outcome() == 2;
            pairFell |= !b.rules.f[0].alive() && !b.rules.f[1].alive();
            lostShown |= b.phase == App::Battle::Lost;
            if (b.phase == App::Battle::Lost && b.phaseFrames >= 40) b.lostTap = true;
            if (b.phase == App::Battle::Won || b.phase == App::Battle::Results) b.wonTap = true, resultsTap(app);
            if (b.exit) { exit = b.exit, b.exit = 0; break; }
            stepBattle(app);
        }
        const bool right = exit == 1 && !lostShown && (won || pairFell);
        problems += !right;
        std::printf("story battle %d played with Infinite HP and One-Hit Kills on: %s after %d rounds, way out %d %s\n", c.group,
                    won ? "won" : pairFell ? "the pair fell (Infinite HP lapsed, no foe could fall)" : "neither", rounds, exit, right ? "ok" : "WRONG");
        if (b.active) endBattle(app, false);
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
    }
    for (int cheat = 0; cheat < 2; ++cheat) { // j001_t01's opening (entry 202): from 0xa0d2 (vars 58 .. 60, then Battle 650 at 0xa0f6) the pair
      // defending as it plays out - the Agent's Particle Cannon (430, some 2200 a shot) puts them down in the
      // first round, as the reference video shows -, the battle over after that round without a defeat, then
      // its talk (Gosub 0xa191: Msg 1400 ..) and on to flag 1401 (0xa100) - CRO-U #3
        app.cheats = keepCheats;
        app.cheats.hp = app.cheats.oneHit = cheat == 1; // (the second time with Infinite HP on: the same end)
        startNewGame(app, 0); // (Vieg and Olgar as a new game makes them: newPairParty)
        for (int f : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(f, true);
        showMap(app, 0, 0, 9); // (area 9: j001_t01.evt)
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        // (the scene found by its bytes - vars 58..60 set, then Battle 650 - not by 1.0.0's offset 0xa0d2: 3.1.0's
        // multi-language file has it at 0xa100)
        uint32_t sceneAt = 0xa0d2;
        {
            static const uint8_t kScene[] = {0x09, 0x10, 0xe5, 0x03, 0x00, 0x00, 0x13, 0x10, 0x3a, 0x00, 0x00, 0x00, 0x09, 0x10,
                                             0xe5, 0x03, 0x00, 0x00, 0x13, 0x10, 0x3b, 0x00, 0x00, 0x00, 0x09, 0x10, 0xe5, 0x03,
                                             0x00, 0x00, 0x13, 0x10, 0x3c, 0x00, 0x00, 0x00, 0x20, 0x8a, 0x02, 0x00, 0x00};
            const std::vector<uint8_t>& ev = app.events.bytes();
            const auto it = std::search(ev.begin(), ev.end(), std::begin(kScene), std::end(kScene));
            if (it != ev.end()) sceneAt = uint32_t(it - ev.begin());
        }
        const bool started = app.runner.startAt(app.events, sceneAt, 0, app.game, app.area, app.mapNumber);
        bool fought = false, lostShown = false, wrongWay = false, on = false;
        int group = -1, rounds = 0;
        for (int f = 0; f < 30000 && started && !on && !wrongWay; ++f) {
            if (f % 45 == 44 && !app.dialogue.empty()) advanceDialogue(app);
            if (app.battle.active) {
                App::Battle& b = app.battle;
                fought = true, group = b.start.group, rounds = std::max(rounds, b.rules.round);
                if (b.phase == App::Battle::Command && b.menu == App::Battle::Top && !battletalk::on(app)) { // the pair defends
                    battleChooseTop(app, 0);
                    battleChooseCommand(app, 3);
                    if (b.phase == App::Battle::Command && b.menu == App::Battle::Commands) battleChooseCommand(app, 3);
                }
                lostShown |= b.phase == App::Battle::Lost;
                if (b.exit) {
                    const int e = b.exit;
                    b.exit = 0;
                    if (e != 1 && e != 4) wrongWay = true;
                    endBattle(app, e == 4);
                } else stepBattle(app);
                continue;
            }
            stepEvents(app);
            on = app.game.flag(1401);
        }
        const bool right = started && fought && group == 650 && !lostShown && !wrongWay && rounds == 1 && on;
        problems += !right;
        std::printf("j001_t01's Battle 650%s: %s, battle %d over after round %d, %s, %s %s\n", cheat ? " (Infinite HP on)" : "", started ? "started" : "NOT STARTED", group,
                    rounds, lostShown ? "the defeat shown" : "no defeat",
                    on ? "the script went on to flag 1401" : wrongWay ? "the game over" : "flag 1401 NOT reached", right ? "ok" : "WRONG");
        if (app.battle.active) endBattle(app, false);
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
    }
    app.cheats = keepCheats;
    std::printf("story battles lost: %d problems\n", problems);
    app.game = keepGame;
    app.playerChar = keepChar;
    return problems;
}

// New with the clear data from the title, as a player begins it (scenario 0 cleared, its load flag
// 31): the cleared save's own work - var 0, var 1 as a played game leaves them - must not take the
// boot script's over (prty_init_new leaves the work alone); its New goes on to the opening, map 69,
// as a plain one does, with var 1 = 3 and the scenario's story flags cleared
int clearDataNewGameSelfTest(App& app, const cr::SaveData& base) {
    cr::SaveData sd = base;
    sd.game.setFlag(31, true);
    sd.game.setFlag(1100, true);
    sd.game.vars[0] = 7, sd.game.vars[1] = 4;
    sd.leader = 0;
    cr::writeSave(dataPath(app), sd);
    app.game = cr::GameState();
    bootTitle(app);
    for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
    titleChoose(app, 0);
    for (int f = 0; f < 300 && !app.charSelectOpen; ++f) stepEvents(app);
    app.charSelectCursor = 0, app.charSelectPage = 0;
    charSelectKey(app, SDLK_RETURN);
    for (int f = 0; f < 300 && !app.saveScreen.open; ++f) stepEvents(app);
    const std::vector<int> plates = savePlates(app);
    const auto newPlate = std::find(plates.begin(), plates.end(), 2);
    int opening = -1, var1 = -1;
    const bool asked = app.saveScreen.open && newPlate != plates.end();
    if (asked) {
        saveScreenChoose(app, int(newPlate - plates.begin())); // New: with the clear data?
        const bool clearQuestion = app.saveScreen.kind == 3;
        saveScreenYes(app);
        var1 = clearQuestion ? app.game.vars[1] : -2;
        for (int f = 0; f < 1500 && opening < 0; ++f) {
            stepEvents(app);
            if (!app.booting && app.mapNumber >= 0) opening = app.mapNumber;
        }
    }
    app.runner.stop();
    const int problems = (var1 != 3) + (opening != 69) + app.game.flag(1100) + !app.game.flag(31);
    std::printf("new game with clear data from the title: var 1 = %d, opening map %d; %d problems\n", var1, opening, problems);
    return problems;
}

// The field's collision as FieldPCCtrl::checkHit has it: the leader walked round every map (finite,
// on a floor, gets somewhere), walked into every solid NPC from four sides (never inside its box),
// up d0207's ramp (polygons 49 / 50, normal.y 0.68: floors by the KD lists, climbed on their plane)
// and onto a j001_p01 plate (a floor NPC: RectWrapper::getFloorHeight lifts it by the box's 3).
// Returns the walk-ins that ended inside a box; problems go to `mapErrors`.
int walkCollisionSelfTest(App& app, int& spawned, int& walked, int& mapErrors) {
    setMode(app, Mode::Maps);
    // On a floor: one within a unit of y (checkHit takes the floor before the pushes move x / z, so on a
    // slope y can trail by a push's rise), or a floor NPC's top; with no floor under it at all, y must
    // stay as it was (getFloor finds none: x and z move on - past a floor's edge with no wall there)
    auto floorUnder = [&](const cr::Vec3& p, float& fy) {
        return cr::floorHeight(app.collisionData, p.x, p.y + 1000.0f, p.z, 2000.0f, fy) ||
               cr::bodyFloorHeight(walkBodies(app), p, 1000.0f, fy);
    };
    auto onFloor = [&](const cr::Vec3& p) {
        float fy;
        return (cr::floorHeight(app.collisionData, p.x, p.y + 1.0f, p.z, 0.0f, fy) && fy > p.y - 1.0f) ||
               (cr::bodyFloorHeight(walkBodies(app), p, 1000.0f, fy) && std::fabs(fy - p.y) < 0.02f);
    };
    // Walk the leader around for 6 seconds of game time (stick turning a full circle) - then stand 20
    // frames, the floor and the pushes still running
    int offEdge = 0;
    for (size_t i = 0; i < app.maps.size(); ++i) {
        showMap(app, int(i));
        if (!app.havePlayer) continue;
        ++spawned;
        const cr::Vec3 start = app.walker.pos;
        const std::vector<cr::WalkBody> bodies = walkBodies(app);
        float far = 0;
        bool ok = true, off = false;
        for (int f = 0; f < 140; ++f) {
            const float a = float(f) / 120.0f * 6.2831853f;
            const bool still = f >= 120;
            const float wasY = app.walker.pos.y;
            cr::stepWalker(app.walker, app.collisionData, bodies, still ? 0.0f : std::sin(a), still ? 0.0f : -std::cos(a),
                           f % 40 < 20, 0.0f);
            const cr::Vec3 p = app.walker.pos;
            float fy;
            const bool none = !floorUnder(p, fy);
            off = off || none;
            const bool good = std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && (none ? p.y == wasY : onFloor(p));
            if (ok && !good && SDL_getenv("CR_WALK_DEBUG"))
                std::printf("  off the floor at frame %d: (%.2f %.2f %.2f), was y %.2f\n", f, p.x, p.y, p.z, wasY);
            ok = ok && good;
            const cr::Vec3 d = p - start;
            far = std::max(far, std::sqrt(cr::dot(d, d)));
        }
        if (!ok) {
            ++mapErrors;
            std::printf("player left the floor in %s\n", app.maps[i].c_str());
        }
        offEdge += off;
        walked += far > 5.0f;
    }
    std::printf("walk: on %d maps the leader went past a floor's edge (no wall there), y held\n", offEdge);
    // Walk into every solid NPC from outside its box: the player must never end up inside it
    auto wallBetween = [&](cr::Vec3 a, cr::Vec3 b) { // a wall polygon (either side) the segment a-b crosses
        const cr::Collision& col = app.collisionData;
        for (const cr::CollisionPolygon& w : col.polygons) {
            if (!w.isWall()) continue;
            const cr::Vec3 n{w.normal[0], w.normal[1], w.normal[2]};
            auto vert = [&](int k) { const float* v = &col.vertices[size_t(w.index[k]) * 3]; return cr::Vec3{v[0], v[1], v[2]}; };
            const float da = cr::dot(a - vert(0), n), db = cr::dot(b - vert(0), n);
            if ((da > 0) == (db > 0)) continue;
            const cr::Vec3 x = a + (b - a) * (da / (da - db));
            float sgn = 0;
            bool inside = true;
            for (int k = 0; k < w.count && inside; ++k) {
                const float sk = cr::dot(n, cr::cross(vert((k + 1) % w.count) - vert(k), x - vert(k)));
                inside = sk * sgn >= 0;
                if (sk != 0) sgn = sk;
            }
            if (inside) return true;
        }
        return false;
    };
    int solidTests = 0, entered = 0;
    for (int area0 = 0; area0 < cr::kNumAreas; ++area0)
        for (int map0 = 0; map0 < cr::kNumMaps; ++map0) {
            if (cr::areaForMap(map0, area0) != area0) continue;
            const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
            if (at == app.maps.end()) continue;
            showMap(app, int(at - app.maps.begin()), map0, area0);
            if (!app.havePlayer) continue;
            const std::vector<cr::WalkBody> bodies = walkBodies(app);
            for (const cr::WalkBody& b : bodies) {
                if (!b.wall || !b.shown || b.box[0] <= 0 || b.box[2] <= 0) continue;
                // the box as RectWrapper turns it: corners (offset +- size / 2) about the position
                const float s = std::sin(b.angle), c = std::cos(b.angle);
                float px[4], pz[4], cx = 0, cz = 0, reach = 0;
                for (int q = 0; q < 4; ++q) {
                    const float lx = b.offset[0] + (q == 1 || q == 2 ? 0.5f : -0.5f) * b.box[0];
                    const float lz = b.offset[2] + (q >= 2 ? 0.5f : -0.5f) * b.box[2];
                    px[q] = b.pos.x + s * lz + c * lx, pz[q] = b.pos.z + c * lz - s * lx;
                    cx += px[q] / 4, cz += pz[q] / 4;
                }
                for (int q = 0; q < 4; ++q) reach = std::max(reach, std::hypot(px[q] - cx, pz[q] - cz));
                reach += app.walker.radius + 2; // start outside the box
                for (int dir = 0; dir < 4; ++dir) {
                    const float a = dir * 1.5707963f;
                    const cr::Vec3 from{cx + reach * std::sin(a), b.pos.y + 5, cz + reach * std::cos(a)};
                    float fy;
                    if (!cr::floorHeight(app.collisionData, from.x, from.y, from.z, 3.0f, fy)) continue;
                    // (RectWrapper's sphere sits at y + 2.5: a box above or below that passes over / under it;
                    // and a start behind a one-sided wall can be pushed through it)
                    const float y0 = b.pos.y + b.offset[1], centreY = fy + 2.5f;
                    if (centreY < y0 || centreY > y0 + b.box[1] || wallBetween({from.x, centreY + 2.5f, from.z}, {cx, centreY + 2.5f, cz}))
                        continue;
                    app.walker.pos = {from.x, fy, from.z};
                    ++solidTests;
                    bool inside = false;
                    for (int f = 0; f < 120 && !inside; ++f) {
                        const cr::Vec3 d{cx - app.walker.pos.x, 0, cz - app.walker.pos.z}; // camYaw 0: the stick is (world x, world z)
                        cr::stepWalker(app.walker, app.collisionData, bodies, d.x, d.z, false, 0.0f);
                        int in = 0; // on the inner side of all four sides
                        for (int q = 0; q < 4; ++q) {
                            const int r = (q + 1) % 4;
                            const float ex = px[r] - px[q], ez = pz[r] - pz[q];
                            in += ex * (app.walker.pos.z - pz[q]) - ez * (app.walker.pos.x - px[q]) > 0;
                        }
                        inside = in == 4;
                    }
                    if (inside && (entered < 8 || SDL_getenv("CR_WALK_DEBUG")))
                        std::printf("  inside: %s map %d box centre (%.1f %.1f) reach %.1f, from dir %d, now (%.1f %.1f) y %.1f; "
                                    "body y %.1f box %.1f %.1f %.1f offset %.1f %.1f %.1f angle %.3f r %.1f\n",
                                    cr::kAreaEventFile[area0], map0, cx, cz, reach, dir, app.walker.pos.x, app.walker.pos.z,
                                    app.walker.pos.y, b.pos.y, b.box[0], b.box[1], b.box[2], b.offset[0], b.offset[1], b.offset[2], b.angle, app.walker.radius);
                    entered += inside;
                }
            }
        }
    std::printf("solid npcs: %d walk-ins, %d ended inside a box\n", solidTests, entered);
    auto showByName = [&](const char* cmp, const char* evt) {
        for (int area0 = 0; area0 < cr::kNumAreas; ++area0)
            for (int map0 = 0; map0 < cr::kNumMaps; ++map0) {
                if (cr::areaForMap(map0, area0) != area0 || std::string(cr::kMapCollisionFile[map0]) != cmp) continue;
                if (evt && std::string(cr::kAreaEventFile[area0]) != evt) continue;
                const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
                if (at == app.maps.end()) continue;
                showMap(app, int(at - app.maps.begin()), map0, area0);
                if (app.havePlayer) return true;
            }
        return false;
    };
    // d0207's ramp: up from its foot (y 30, z -60.9) to its top (y 54.1, z -83.0), x 168..188
    int rampProblems = 0;
    float rampTop = -1e9f, rampWorst = 0;
    int rampSteps = 0;
    if (showByName("d0207_10.cmp", nullptr)) {
        const std::vector<cr::WalkBody> bodies = walkBodies(app);
        app.walker.pos = {178.0f, 30.0f, -58.0f};
        app.walker.velocity = {};
        cr::checkWalkerHit(app.walker, app.collisionData, bodies);
        for (int f = 0; f < 70; ++f) {
            cr::stepWalker(app.walker, app.collisionData, bodies, 0.0f, -1.0f, false, 0.0f); // walk -z
            const cr::Vec3 p = app.walker.pos;
            if (p.z < -61.5f && p.z > -82.5f) { // on the ramp: on its plane
                ++rampSteps;
                rampWorst = std::max(rampWorst, std::fabs(p.y - (30.0f + (-60.9f - p.z) * (24.1f / 22.1f))));
            }
            rampTop = std::max(rampTop, p.y);
        }
        rampProblems = rampSteps < 30 || rampWorst > 0.2f || rampTop < 54.0f;
    } else rampProblems = 1;
    std::printf("ramp (d0207 49 / 50): %d steps on it, %.3f off its plane at worst, up to y %.1f; %d problems\n", rampSteps,
                rampWorst, rampTop, rampProblems);
    // j001_p01's plates (NPCs 153..207, model 32, kind bit 4, box 10 x 3 x 10): the leader stands on top
    int plateProblems = 1, plateId = -1;
    float plateY = 0, plateWant = 0, besideY = 0, besideWant = 0;
    for (int area0 = 0; area0 < cr::kNumAreas && plateProblems; ++area0) {
        if (std::string(cr::kAreaEventFile[area0]) != "j001_p01.evt") continue;
        for (int map0 = 0; map0 < cr::kNumMaps && plateProblems; ++map0) {
            if (cr::areaForMap(map0, area0) != area0) continue;
            const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
            if (at == app.maps.end()) continue;
            showMap(app, int(at - app.maps.begin()), map0, area0);
            if (!app.havePlayer) continue;
            for (const cr::FieldNpc& n : app.npcs) {
                if (n.id < 153 || n.id > 207 || !(n.kind & 4) || n.box[1] <= 0) continue;
                app.npcShown[n.id] = true; // (shown whatever the room's flags say)
                const std::vector<cr::WalkBody> bodies = walkBodies(app);
                const float cx = n.pos[0] + n.offset[0], cz = n.pos[2] + n.offset[2];
                float floorY;
                const int node = cr::searchNode(app.collisionData, {cx, n.pos[1], cz}, 5.0f);
                if (cr::getFloor(app.collisionData, node, {cx, n.pos[1], cz}, 1000.0f, floorY) < 0) continue;
                plateId = n.id;
                plateWant = n.pos[1] + n.offset[1] + n.box[1];
                app.walker.pos = {cx, floorY, cz};
                app.walker.velocity = {};
                cr::checkWalkerHit(app.walker, app.collisionData, bodies);
                plateY = app.walker.pos.y;
                // off its box (just past the side): back on the floor below
                const cr::Vec3 beside{cx + n.box[0] * 0.5f + 0.5f, plateY, cz};
                const int node2 = cr::searchNode(app.collisionData, beside, 5.0f);
                if (cr::getFloor(app.collisionData, node2, beside, 1000.0f, besideWant) < 0) continue;
                app.walker.pos = beside;
                bool otherPlate = false;
                for (const cr::WalkBody& b : bodies)
                    otherPlate = otherPlate || (b.floor && b.shown && std::fabs(beside.x - b.pos.x - b.offset[0]) <= b.box[0] * 0.5f &&
                                                std::fabs(beside.z - b.pos.z - b.offset[2]) <= b.box[2] * 0.5f);
                if (otherPlate) continue;
                cr::checkWalkerHit(app.walker, app.collisionData, bodies);
                besideY = app.walker.pos.y;
                plateProblems = std::fabs(plateY - plateWant) > 0.01f || plateWant < floorY + 2.9f || std::fabs(besideY - besideWant) > 0.01f;
                break;
            }
        }
    }
    std::printf("plate (j001_p01 npc %d): stands at y %.2f (top %.2f), beside it %.2f (floor %.2f); %d problems\n", plateId,
                plateY, plateWant, besideY, besideWant, plateProblems);
    mapErrors += rampProblems + plateProblems;
    return entered;
}

int runSelfTest(App& app) {
    app.persist = false;
    app.ach.live = false; // (no achievements earned by the other checks - achievementsSelfTest turns them on for its own)
    app.seenFlags.clear(); // (the prompts seen: none - the tests expect each first showing)
    app.typeOn = false; // pages whole (the typing has its own test)
    app.video.layout = 1; // the iPad layout the tests measure (the iPhone one has its own)
    applyVideo(app);
    app.testNoTutorials = true; // the battles' tutorials only where a test asks for them
    app.renderer.setVSync(false); // as fast as it renders, not at the display's rate
    if (const char* file = SDL_getenv("CR_SELFTEST_SAVE")) {
        cr::SaveData save;
        std::string error;
        if (!cr::readSave(file, save, &error) || !applySave(app, save)) {
            std::fprintf(stderr, "imported save: %s\n", error.c_str());
            return 1;
        }
        for (int i = 0; i < 3; ++i) drawFrame(app, false);
        int problems = !app.havePlayer || app.mapNumber != save.map || app.renderer.takeErrors() != 0;
        if (app.game.scenarioGenes) {
            cr::GameState swapped = app.game;
            const int original = swapped.scenario;
            cr::changeScenario(swapped, app.itemTable, 1 - original);
            cr::changeScenario(swapped, app.itemTable, original);
            problems += swapped.items != app.game.items || swapped.otherBag != app.game.otherBag ||
                        swapped.oz != app.game.oz || swapped.otherOz != app.game.otherOz;
        }
        if (SDL_getenv("CR_SELFTEST_SAVE_DEEP")) { // the save's numbers in use: the Camp's pages, then a battle
            const auto key = [&](SDL_Keycode k) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN, e.key.key = k, e.key.down = true;
                SDL_AppEvent(&app, &e);
                drawFrame(app, false);
            };
            for (int plate = 0; plate < 4; ++plate) {
                openCamp(app);
                app.camp.cursor = plate;
                key(SDLK_RETURN);
                for (SDL_Keycode k : {SDLK_RIGHT, SDLK_DOWN, SDLK_RETURN, SDLK_DOWN, SDLK_RIGHT, SDLK_RETURN}) key(k);
                for (int i = 0; i < 5 && app.campOpen; ++i) key(SDLK_ESCAPE); // (Esc on the field quits)
            }
            startBattle(app, {1, 0, -1, 0}, 0);
            for (int f = 0; f < 400; ++f) {
                drawFrame(app, false);
                stepBattle(app);
                if (f % 25 == 0) key(SDLK_RETURN);
            }
        }
        std::printf("imported save: area %d map %d, leader %d, level %d; %d problems\n", app.area, app.mapNumber,
                    app.playerChar, app.game.party[size_t(app.playerChar)].level, problems);
        return problems ? 1 : 0;
    }
    if (SDL_getenv("CR_SELFTEST_SESSION_UI")) return sessionUiSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_STORY_LOSS")) return storyLossSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_SCENE_AUDIO")) return sceneAudioSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_GRAPHICS")) return graphicsRecoverySelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_CLEAR_NEW")) { // only New with the clear data from the title, in a folder of its own
        const std::string keep = app.saveDirOverride;
        app.saveDirOverride = userDir() + "clear-new-test-" + std::to_string(SDL_GetTicksNS()) + "/";
        SDL_CreateDirectory(app.saveDirOverride.c_str());
        setMode(app, Mode::Maps);
        startNewGame(app, 0);
        const int problems = clearDataNewGameSelfTest(app, captureSave(app));
        for (const char* f : {"save1.dat", "save2.dat", "data.dat", "resume.dat"}) SDL_RemovePath(savePath(app, f).c_str());
        SDL_RemovePath(app.saveDirOverride.c_str());
        app.saveDirOverride = keep;
        return problems ? 1 : 0;
    }
    if (SDL_getenv("CR_SELFTEST_CANCEL")) return inputCancelSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_LIFECYCLE")) return fieldLifecycleSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_MOBILE")) return mobileInputSelfTest(app) + mobileTransitionSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_CONTROLS")) return controlHintsSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_ACHIEVEMENTS")) return achievementsSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_FIELD")) return fieldPresentationSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_FIELD_RENDER")) return fieldRenderSelfTest(app) ? 1 : 0;
    if (SDL_getenv("CR_SELFTEST_WALK")) { // only the walking collision pass
        int spawned = 0, walked = 0, problems = 0;
        problems += walkCollisionSelfTest(app, spawned, walked, problems);
        std::printf("walk: player spawned on %d maps, walked > 5 units on %d; %d problems\n", spawned, walked, problems);
        return problems ? 1 : 0;
    }
    if (SDL_getenv("CR_SELFTEST_BATTLES")) { // only the battle pass
        int frames = 0, glErrors = 0;
        setMode(app, Mode::Maps);
        return battleSelfTest(app, frames, glErrors) ? 1 : 0;
    }
    int glErrors = controlHintsSelfTest(app) + achievementsSelfTest(app) + fieldPresentationSelfTest(app) + fieldRenderSelfTest(app), frames = 0,
        failedModels = 0;
    setMode(app, Mode::Models);
    for (size_t i = 0; i < app.models.size(); ++i) {
        showModel(app, int(i));
        int errs = 0;
        const int steps = int(app.model.actions.size()) + 1;
        for (int a = 0; a < steps; ++a) {
            for (int f = 0; f < 3; ++f) {
                animateModel(app, 0.25f);
                drawFrame(app);
                ++frames;
            }
            errs += app.renderer.takeErrors();
            selectAction(app, 1);
        }
        if (errs) {
            ++failedModels;
            glErrors += errs;
            std::printf("GL errors in %s: %d\n", app.models[i].c_str(), errs);
        }
    }
    // Every field map (collision + props)
    setMode(app, Mode::Maps);
    int mapErrors = 0, camMaps = 0, spawned = 0, walked = 0;
    for (size_t i = 0; i < app.maps.size(); ++i) {
        showMap(app, int(i));
        camMaps += app.haveStageCam;
        for (int f = 0; f < 2; ++f) { // 3D view, then the composite (background + stage camera)
            app.showBackground = f == 1;
            app.lastTicks = 0; // no game time passes here, however long the load took
            drawFrame(app);
            ++frames;
        }
        app.showBackground = false;
        int errs = 0;
        errs += app.renderer.takeErrors();
        if (errs || app.collisionPolys == 0) {
            ++mapErrors;
            std::printf("map problem in %s: %d GL errors, %d polygons\n", app.maps[i].c_str(), errs, app.collisionPolys);
        }
        glErrors += errs;
    }
    // Every exit of every map: stand in its box, fire it, check where the player arrives
    int exits = 0, arrived = 0, onFloor = 0, exitFailures = 0, shadowed = 0;
    for (int area0 = 0; area0 < cr::kNumAreas; ++area0)
    for (int map0 = 0; map0 < cr::kNumMaps; ++map0) {
        if (cr::areaForMap(map0, area0) != area0) continue;
        const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
        if (at == app.maps.end()) continue;
        const int i = int(at - app.maps.begin());
        showMap(app, i, map0, area0);
        const int area = app.area, map = app.mapNumber;
        const std::vector<cr::FieldNpc> npcs = app.npcs;
        const std::vector<bool> warps = app.npcWarps;
        for (size_t k = 0; k < npcs.size(); ++k) {
            if (!warps[k] || !npcActive(app, npcs[k])) continue;
            // RectWrapper::checkEventKick: the first box (in the file's order) with the player in it
            // kicks - an exit with an earlier one on the same spot never fires while both are shown
            if (app.mapNumber != map || app.area != area) showMap(app, i, map, area);
            if (npcs[k].touchTrigger() && triggerAt(app, {npcs[k].boxCenterX(), npcs[k].pos[1], npcs[k].boxCenterZ()}) != npcs[k].id) {
                ++shadowed;
                continue;
            }
            ++exits;
            if (app.mapNumber != map || app.area != area) showMap(app, i, map, area);
            cr::GameState trial = app.game;
            const cr::EventResult r = cr::runEvent(app.events, npcs[k].id, trial, area, map);
            if (!app.havePlayer) { ++exitFailures; continue; }
            app.walker.pos = {npcs[k].boxCenterX(), npcs[k].pos[1], npcs[k].boxCenterZ()};
            app.lastKick = 0;
            const bool warped = checkTriggers(app, true);
            const int wantArea = r.warp.area >= 0 ? r.warp.area : area;
            if (!warped || app.mapNumber != r.warp.map || app.area != wantArea) {
                ++exitFailures;
                std::printf("exit problem: %s map %d event %d -> map %d: %s (area %d map %d)\n", cr::kAreaEventFile[area], map,
                            npcs[k].id, r.warp.map, warped ? "arrived elsewhere" : "did not warp", app.area, app.mapNumber);
                continue;
            }
            ++arrived;
            float fy;
            const cr::Vec3 p = app.walker.pos;
            if (cr::floorHeight(app.collisionData, p.x, p.y + 0.01f, p.z, 0.0f, fy) && std::fabs(fy - p.y) < 0.05f) ++onFloor;
            else std::printf("exit lands off the floor: %s map %d event %d -> map %d at (%.1f %.1f %.1f)\n",
                             cr::kAreaEventFile[area], map, npcs[k].id, app.mapNumber, p.x, p.y, p.z);
        }
    }
    std::printf("exits: %d fired, %d arrived on the right map, %d on a floor, %d under an earlier one, %d problems\n", exits,
                arrived, onFloor, shadowed, exitFailures);
    // Every visible NPC of every (area, map) should stand on a floor
    int npcsDrawn = 0, npcsOnFloor = 0, npcMaps = 0;
    std::map<std::string, int> offFloor;
    for (int area0 = 0; area0 < cr::kNumAreas; ++area0)
        for (int map0 = 0; map0 < cr::kNumMaps; ++map0) {
            if (cr::areaForMap(map0, area0) != area0) continue;
            const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
            if (at == app.maps.end()) continue;
            showMap(app, int(at - app.maps.begin()), map0, area0);
            npcMaps += !app.npcDraws.empty();
            for (const auto& d : app.npcDraws) {
                ++npcsDrawn;
                const float x = d.world.m[12], y = d.world.m[13], z = d.world.m[14];
                float fy;
                if (cr::floorHeight(app.collisionData, x, y, z, 2.0f, fy) && std::fabs(fy - y) < 2.0f) ++npcsOnFloor;
                else ++offFloor[d.model];
            }
        }
    const int entered = walkCollisionSelfTest(app, spawned, walked, mapErrors);
    // Examine everything examinable on every (area, map)
    int examined = 0, gotItems = 0, lidsHidden = 0, examineWarps = 0, examineErrors = 0, boxesOpen = 0, goldBoxes = 0;
    std::map<std::string, int> examinedByModel;
    for (int area0 = 0; area0 < cr::kNumAreas; ++area0)
        for (int map0 = 0; map0 < cr::kNumMaps; ++map0) {
            if (cr::areaForMap(map0, area0) != area0) continue;
            const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map0]));
            if (at == app.maps.end()) continue;
            showMap(app, int(at - app.maps.begin()), map0, area0);
            std::vector<cr::FieldNpc> targets;
            for (const cr::FieldNpc& n : app.npcs)
                if ((n.kind & 8) && npcActive(app, n) && n.pos[1] < 900.0f &&
                    !(n.pos[0] == 0 && n.pos[1] == 0 && n.pos[2] == 0))
                    targets.push_back(n);
            for (const cr::FieldNpc& n : targets) {
                if (app.mapNumber != map0 || app.area != area0) showMap(app, int(at - app.maps.begin()), map0, area0);
                if (!app.havePlayer) break;
                const float k = n.model == 0 || n.model == 1 ? 1.0f : 0.0f;
                const float cx = n.pos[0] + k * n.offset[0] + n.offset[0], cz = n.pos[2] + k * n.offset[2] + n.offset[2];
                app.walker.pos = {cx + n.box[0] / 2 + app.walker.radius, n.pos[1], cz}; // just beside it
                const cr::FieldNpc* t = interactTarget(app);
                if (!t) continue;
                const size_t drawsBefore = app.npcDraws.size();
                // a box already opened (its lid's hide flag: the ids and flags repeat across an area's maps)
                // and the gold boxes (f0000gbx00: Uty_ChkItem for key 2, then a Yes / No) give nothing here
                cr::NpcRow lidRow;
                const bool wasOpen = (n.treasure & 3) && app.events.npcRow(n.id + 1, lidRow) && lidRow.flagHide >= 0 &&
                                     app.game.flag(lidRow.flagHide);
                const bool gold = n.model >= 0 && std::string(cr::kNpcModelFile[n.model]) == "f0000gbx00.bin";
                ++examined;
                ++examinedByModel[n.model >= 0 ? cr::kNpcModelFile[n.model] : "none"];
                const bool warped = interact(app, true);
                if (app.mapNumber != map0) { ++examineWarps; continue; }
                const bool gave = app.lastEvent.find(", got ") != std::string::npos;
                gotItems += gave;
                lidsHidden += (n.treasure & 3) && app.npcDraws.size() < drawsBefore;
                boxesOpen += wasOpen, goldBoxes += gold && !gave;
                if ((n.treasure & 3) && !gave && !wasOpen && !gold)
                    std::printf("  box without item: %s map %d box %d (lid script %d)\n", cr::kAreaEventFile[area0], map0, n.id, n.id + 1);
                (void)warped;
            }
        }
    std::printf("examine: %d examined, %d gave items (%d different, item 0 x%d), %d boxes lost their lid, %d already open, %d gold "
                "(a key asked), %d warped; by model:", examined, gotItems, int(app.game.items.size()),
                app.game.items.count(0) ? app.game.items[0] : 0, lidsHidden, boxesOpen, goldBoxes, examineWarps);
    for (const auto& [m, c] : examinedByModel) std::printf(" %s x%d", m.c_str(), c);
    std::printf("\n");
    (void)examineErrors;
    mapErrors += entered;
    std::printf("npcs: %d drawn on %d maps, %d stand on a floor; off the floor by model:", npcsDrawn, npcMaps, npcsOnFloor);
    for (const auto& [m, c] : offFloor) std::printf(" %s x%d", m.c_str(), c);
    std::printf("\n");
    mapErrors += exitFailures;
    // Stepped scripts: open a chest frame by frame, then walk into an exit
    {
        app.game = cr::GameState(); // fresh: the examine pass above opened every box
        app.treasureBoxes.clear();
        const auto at = std::find(app.maps.begin(), app.maps.end(), std::string("d0101_10.cmp"));
        showMap(app, int(at - app.maps.begin()), 0, cr::areaForMap(0, 2));
        app.pendingEntry = App::Entry::None; // these tests play one script each
        app.dialogue.clear(); // (a page left by the tests before: it would hold the field)
        if (app.havePlayer) { // the touch pad steers as the keys do: pushed up, then right - running, a quarter turn apart
            const cr::FieldWalker start = app.walker;
            float heading[2] = {0, 0}, moved[2] = {0, 0};
            bool ran = true;
            for (int k = 0; k < 2; ++k) {
                app.walker = start;
                app.walkClock = 0;
                App::TouchPad& t = app.touchPad;
                t = App::TouchPad();
                t.down = true, t.dx = k ? 40.0f : 0.0f, t.dy = k ? 0.0f : -40.0f;
                updatePlayer(app, 1.0f / kFramesPerSecond, 0.0f);
                heading[k] = app.walker.heading;
                const cr::Vec3 d = app.walker.pos - start.pos;
                moved[k] = std::sqrt(d.x * d.x + d.z * d.z);
                ran = ran && app.walker.gait == cr::Gait::Run;
            }
            touchPadUp(app);
            app.walker = start;
            const float turn = std::remainder(heading[1] - heading[0], 2 * SDL_PI_F);
            const bool ok = ran && std::fabs(turn + SDL_PI_F / 2) < 1e-3f && moved[0] > 1.0f && moved[1] > 1.0f;
            std::printf("touch pad: up and right %s, headings %.3f / %.3f (turn %.3f), moved %.2f / %.2f\n",
                        ran ? "run" : "DON'T RUN", heading[0], heading[1], turn, moved[0], moved[1]);
            if (!ok) ++mapErrors;
        }
        int lidAnimFrame = -1, windowFrame = -1, fadedFrame = -1, endFrame = -1, frames = 0;
        int bandRow = 0, closeFrame = -1, closedFrame = -1; // Uty_GetItem's band: down to row 2, out in 5 frames
        const cr::FieldNpc* box = nullptr;
        for (const cr::FieldNpc& n : app.npcs)
            if ((n.treasure & 3) && npcActive(app, n)) { box = &n; break; }
        if (box && app.havePlayer) {
            const int boxId = box->id;
            app.walker.pos = {box->pos[0] + box->box[0] / 2 + app.walker.radius, box->pos[1], box->pos[2]};
            { // opened by touch, as on the iPad: the "!" button in the corner (FollowWindow::checkTouchEasyButton)
                float l0x, l0y, l1x, l1y, ex, ey;
                app.renderer.toLogical(0, 0, l0x, l0y);
                app.renderer.toLogical(1000, 1000, l1x, l1y);
                easyButtonCentre(app, ex, ey);
                const float lx = wideRight(app) + 2 * (ex + 16), ly = 2 * (ey + 32);
                app.fieldMenu.fade = 320;
                const bool shown = easyButtonShown(app);
                touchPadDown(app, (lx - l0x) * 1000 / (l1x - l0x), (ly - l0y) * 1000 / (l1y - l0y));
                touchPadUp(app);
                std::printf("touch: the \"!\" button %s, the chest %s\n", shown ? "shown" : "NOT SHOWN",
                            app.runner.running() ? "opening" : "NOT OPENED");
                if (!shown || !app.runner.running()) ++mapErrors;
            }
            app.soundLog.clear();
            const size_t itemsBefore = app.game.items.size();
            for (frames = 0; frames < 900 && (app.runner.running() || app.itemWindowOpen); ++frames) {
                stepEvents(app);
                for (const auto& d : app.npcDraws)
                    if (d.id == boxId + 1) {
                        if (lidAnimFrame < 0 && d.action == 1) lidAnimFrame = frames;
                        if (fadedFrame < 0 && d.alpha < 0.1f) fadedFrame = frames;
                    }
                if (windowFrame < 0 && app.itemWindowOpen) windowFrame = frames;
                if (app.itemWindowOpen && !app.itemWindowClosing) bandRow = std::max(bandRow, app.itemWindowRow);
                if (app.itemWindowOpen && app.itemWindowClosing && closeFrame < 0) closeFrame = frames;
                if (!app.itemWindowOpen && closeFrame >= 0 && closedFrame < 0) closedFrame = frames;
                if (app.itemWindowOpen && frames - windowFrame == 10)
                    if (const char* path = SDL_getenv("CR_ITEM_SHOT")) { app.lastTicks = 0; drawFrame(app, false); saveFrame(app, path); }
                if (app.itemWindowOpen && frames - windowFrame == 30) app.tapPending = true; // read it, then tap
                if (!app.dialogue.empty()) app.dialogue.clear();
                if (!app.runner.running() && endFrame < 0) endFrame = frames;
            }
            const bool lidGone = std::none_of(app.npcDraws.begin(), app.npcDraws.end(),
                                              [&](const App::NpcDraw& d) { return d.id == boxId + 1; });
            std::printf("stepped chest sounds:");
            for (uint32_t id : app.soundLog) std::printf(" %u", id);
            std::printf("\n");
            std::printf("stepped chest %d: lid anim at frame %d, item window %d (band at row %d, out in %d frames: \"%s\"), "
                        "lid faded %d, done %d, item %s, lid %s\n",
                        boxId, lidAnimFrame, windowFrame, bandRow, closedFrame - closeFrame, app.itemWindowText.c_str(), fadedFrame, endFrame,
                        app.game.items.size() > itemsBefore || !app.game.items.empty() ? "gained" : "MISSING",
                        lidGone ? "removed" : "STILL THERE");
            if (lidAnimFrame < 0 || windowFrame < 0 || endFrame < 0 || !lidGone || bandRow != 2 || closedFrame - closeFrame != 5 ||
                app.itemWindowText.empty())
                ++mapErrors;
        }
    }
    { // a gold box (c101_d07 map 94, box 408): Uty_ChkItem counts key 2 into var 0 - none, a message; with
      // one, a Yes opens it and DelItem takes the key
        app.game = cr::GameState();
        const int gm = 94, ga = cr::areaForMap(94, 8);
        const auto at = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[gm]));
        int gotWithout = -1, gotWith = -1, keysLeft = -1;
        cr::NpcRow row;
        if (at != app.maps.end()) {
            showMap(app, int(at - app.maps.begin()), gm, ga);
            app.pendingEntry = App::Entry::None;
            const cr::FieldNpc* box = nullptr;
            for (const cr::FieldNpc& n : app.npcs)
                if (n.id == 408 && npcActive(app, n)) box = &n;
            if (box && app.havePlayer && app.events.npcRow(408, row) && row.item > 0) {
                const cr::FieldNpc b = *box;
                for (int withKey = 0; withKey < 2; ++withKey) {
                    app.game.items.erase(row.item);
                    if (withKey) app.game.items[2] = 1;
                    app.walker.pos = {b.pos[0] + b.box[0] / 2 + app.walker.radius, b.pos[1], b.pos[2]};
                    app.runner.stop();
                    interact(app);
                    for (int f = 0; f < 900 && (app.runner.running() || app.itemWindowOpen); ++f) {
                        stepEvents(app);
                        if (app.choiceFlag >= 0) answerChoice(app, true);
                        if (app.itemWindowOpen) app.tapPending = true;
                        if (!app.dialogue.empty()) app.dialogue.clear(), app.tapPending = true;
                    }
                    (withKey ? gotWith : gotWithout) = app.game.items.count(row.item) ? app.game.items[row.item] : 0;
                }
                keysLeft = app.game.items.count(2) ? app.game.items[2] : 0;
                app.runner.stop();
                app.dialogue.clear();
            }
        }
        const bool ok = gotWithout == 0 && gotWith == 1 && keysLeft == 0;
        std::printf("gold box 408: item %d - without a key %d, with one %d (keys left %d) %s\n", row.item, gotWithout, gotWith,
                    keysLeft, ok ? "ok" : "WRONG");
        mapErrors += !ok;
        { // the worlds' boxes (TreasureBoxManager::randomSelect): fifteen from the pools, band 2 on
            // puts slots 9..14's items into gold boxes, and each World Select entry draws again
            cr::EventFile d01;
            int tbad = 0, gold = 0, poolItems = 0;
            std::set<std::vector<int>> draws;
            if (d01.load(app.dataDir, 2)) {
                cr::GameRandom rng(7);
                for (int k = 0; k < 6; ++k) {
                    const auto boxes = cr::selectTreasureBoxesOmega(d01, [&rng](int n) { return n > 0 ? rng.below(n) : 0; }, k < 3 ? 0 : 4, 0);
                    std::vector<int> ids;
                    for (const cr::TreasureBox& b : boxes) {
                        ids.push_back(b.id);
                        gold += b.id >= 800;
                        tbad += k < 3 && b.id >= 800; // (band 0: none)
                        cr::NpcRow r;
                        tbad += !d01.npcRow(b.id, r) || !(r.box & 3);
                        poolItems += b.item > 0;
                    }
                    tbad += boxes.size() != 15;
                    draws.insert(ids);
                }
            } else ++tbad;
            tbad += gold == 0 || draws.size() < 2 || poolItems != 90;
            std::printf("world boxes: 6 draws of 15, %d into gold boxes at band 4, %zu different; %d problems\n", gold, draws.size(), tbad);
            mapErrors += tbad;
        }
        // exit: walk into trigger 2's box and let the script play
        for (const cr::FieldNpc& n : app.npcs)
            if (n.id == 2 && n.touchTrigger()) {
                app.walker.pos = {n.boxCenterX(), n.pos[1], n.boxCenterZ()};
                app.lastKick = 0;
                app.soundLog.clear();
                checkTriggers(app);
                int warpFrame = -1, maxFade = 0;
                for (frames = 0; frames < 600 && app.runner.running(); ++frames) {
                    stepEvents(app);
                    if (warpFrame < 0 && app.mapNumber != 0) warpFrame = frames;
                    maxFade = std::max(maxFade, int(screenFadeAlpha(app) * 100));
                }
                std::printf("stepped exit sounds:");
                for (uint32_t id : app.soundLog) std::printf(" %#x", id);
                std::printf("\n");
                std::printf("stepped exit: warped to map %d at frame %d, script done at frame %d, peak fade %d%%, final fade %d%%\n",
                            app.mapNumber, warpFrame, frames, maxFade, int(screenFadeAlpha(app) * 100));
                if (warpFrame < 0 || screenFadeAlpha(app) > 0.01f) ++mapErrors;
                break;
            }
    }
    { // Every area's scripts dry-run (entries 0..999, a fresh state): each message they show has its
      // text, and a name plate's name (__EvtMsg's 200 a block, PrcMsg's names at block 7 + 0x95)
        int shown = 0, missing = 0, plates = 0, badPlates = 0;
        for (int area = 0; area < cr::kNumAreas; ++area) {
            cr::EventFile ef;
            if (!ef.load(app.dataDir, area)) continue;
            std::set<int> ids;
            for (int e = 0; e < 1000; ++e) {
                cr::GameState g;
                const cr::EventResult r = cr::runEvent(ef, e, g, area, cr::kAreaFirstMap[area]);
                ids.insert(r.messages.begin(), r.messages.end());
            }
            for (int id : ids) {
                std::vector<std::vector<std::string>> pages;
                cr::MessageInfo info;
                ++shown;
                if (!ef.message(id, 1, nullptr, pages, &info) && !app.systemText.message(id, 1, nullptr, pages, &info)) {
                    if (missing++ < 5) std::printf("  message %d (area %d) has no text\n", id, area);
                    continue;
                }
                if (info.nameId >= 0) {
                    ++plates;
                    std::vector<std::vector<std::string>> name;
                    badPlates += !app.systemText.message(info.nameId, 1, nullptr, name) || name.empty() || name[0].empty();
                }
            }
        }
        std::printf("script messages: %d shown by the areas' scripts, %d without text; %d name plates, %d without a name\n", shown,
                    missing, plates, badPlates);
        mapErrors += missing + badPlates + (shown < 100);
    }
    // Entry scripts on a new game: each area's script 0 on its first room, then every room's
    // EvrStart 5, played frame by frame to the end (dialogue paged, taps given)
    {
        int areasRun = 0, roomsRun = 0, stuck = 0, errors = 0, pages = 0, warps = 0, longest = 0, choices = 0, battles = 0;
        auto play = [&](const char* what, int area, int map) {
            const int startMap = app.mapNumber;
            int f = 0;
            app.tapPending = false;
            for (; f < 20000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
                stepEvents(app);
                if (!app.dialogue.empty()) { pages += int(app.dialogue.size()); app.dialogue.clear(); }
                if (app.choiceFlag >= 0) { ++choices; answerChoice(app, true); }
                if (app.worldChoiceOpen) { ++choices; answerWorldChoice(app, 0); }
                app.tapPending = true;
                if (app.itemWindowOpen) app.game.setFlag(0, true);
                if (app.battle.active) { ++battles; endBattle(app, true); } // a script's battle: won (else every room after it runs gated)
            }
            longest = std::max(longest, f);
            if (app.mapNumber != startMap) ++warps;
            if (!app.runner.result().error.empty()) {
                ++errors;
                std::printf("  %s area %d map %d: %s\n", what, area, map, app.runner.result().error.c_str());
            }
            if (app.runner.running()) {
                ++stuck;
                std::printf("  %s area %d map %d: still running after %d frames (%s, event %d)\n", what, area, map, f,
                            app.runner.where().c_str(), app.runner.eventNo());
                if (SDL_getenv("CR_STUCK_TRACE")) { // (debugging) the loop it's in
                    std::vector<std::pair<size_t, int>> ring;
                    cr::setEventTrace([&](size_t pc, int op) { if (ring.size() < 64) ring.push_back({pc, op}); });
                    for (int k = 0; k < 3; ++k) stepEvents(app);
                    cr::setEventTrace(nullptr);
                    for (const auto& [pc, op] : ring) std::printf("    %#zx op %d\n", pc, op);
                }
                app.runner.stop();
            }
            return f;
        };
        for (int area = 0; area < cr::kNumAreas; ++area) {
            const int map = cr::kAreaFirstMap[area];
            const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map]));
            if (it == app.maps.end()) continue;
            app.game = cr::GameState();
            app.entryArea = -1;
            showMap(app, int(it - app.maps.begin()), map, area);
            const int f = play("area script", area, map);
            ++areasRun;
            std::printf("  area %2d %s: script 0 on map %d took %d frames\n", area, cr::kAreaEventFile[area], map, f);
        }
        for (int area = 0; area < cr::kNumAreas; ++area)
            for (int map = 0; map < cr::kNumMaps; ++map) {
                if (cr::areaForMap(map, area) != area) continue;
                const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map]));
                if (it == app.maps.end()) continue;
                app.game = cr::GameState();
                app.entryArea = area; // arriving from another room of the area
                showMap(app, int(it - app.maps.begin()), map, area);
                play("room script", area, map);
                ++roomsRun;
            }
        std::printf("entry scripts: %d areas, %d rooms; %d dialogue pages, %d choices, %d battles, %d warped, %d errors, %d stuck, "
                    "longest %d frames\n", areasRun, roomsRun, pages, choices, battles, warps, errors, stuck, longest);
        mapErrors += errors + stuck;
    }
    { // Select Scenario's pages: with flag 0x643 the card (serect 28) comes in once the second page has
      // settled and goes before it rises again; without, it stays off the screen
        const cr::GameState keep = app.game;
        const bool keepBooting = app.booting;
        app.booting = false;
        int problems = 0;
        float cardOn = 0, cardOff = 0, plain = 0;
        int pageOn = -1, pageOff = -1;
        for (int withCard = 0; withCard < 2; ++withCard) {
            app.game = cr::GameState();
            app.game.setFlag(31, true);
            app.game.setFlag(0x643, withCard != 0);
            openCharSelect(app);
            auto settle = [&] { for (int f = 0; f < 400 && app.startPage.phase; ++f) stepCharSelect(app); };
            charSelectTurn(app);
            settle();
            if (withCard) cardOn = app.startPage.card, pageOn = app.charSelectPage;
            else plain = app.startPage.card;
            charSelectTurn(app);
            settle();
            if (withCard) cardOff = app.startPage.card, pageOff = app.charSelectPage;
            app.charSelectOpen = false;
        }
        const bool iPad = !app.renderer.phoneLayout();
        problems += plain != -160 || cardOn != (iPad ? -16 : 0) || pageOn != 1 || cardOff != (iPad ? -176 : -160) || pageOff != 0;
        std::printf("select scenario: the card without flag 0x643 at %.0f, with it in to %.0f (page %d), out to %.0f (page %d); %d problems\n",
                    plain, cardOn, pageOn, cardOff, pageOff, problems);
        mapErrors += problems;
        app.game = keep;
        app.booting = keepBooting;
    }
    // New game: talk to everyone in the Ark's hall, frame by frame (pages turned, choices answered yes)
    {
        startNewGame(app);
        int openingFrames = 0, openingPages = 0;
        for (; openingFrames < 20000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++openingFrames) {
            stepEvents(app);
            if (!app.dialogue.empty()) {
                openingPages += int(app.dialogue.size());
                for (const auto& v : app.dialogueVoices) app.voicesPlayed += int(v.size());
                app.dialogue.clear();
            }
            if (app.choiceFlag >= 0) answerChoice(app, true);
            if (app.worldChoiceOpen) answerWorldChoice(app, 0);
            app.tapPending = true;
        }
        std::printf("new game: the opening took %d frames, %d pages, %d voice lines, ends on map %d%s\n", openingFrames,
                    openingPages, app.voicesPlayed, app.mapNumber, app.runner.running() ? " (STILL RUNNING)" : "");
        const int voicesBefore = app.voicesPlayed;
        if (app.runner.running() || app.mapNumber != 70) ++mapErrors; // (Omega: the planet 69, 71, then the Ark's deck 70)
        int talked = 0, pages = 0, choices = 0, maxPortraits = 0, plates = 0, unfinished = 0;
        std::vector<int> ids;
        for (const cr::FieldNpc& n : app.npcs)
            if ((n.kind & 8) && npcActive(app, n) && n.pos[1] < 900.0f) ids.push_back(n.id);
        for (int id : ids) {
            const cr::FieldNpc* n = findNpc(app, id);
            if (!n || !app.havePlayer) continue;
            app.walker.pos = {n->pos[0] + n->box[0] / 2 + app.walker.radius, n->pos[1], n->pos[2]};
            if (!interact(app)) continue;
            ++talked;
            int f = 0;
            for (; f < 6000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
                stepEvents(app);
                int shown = 0;
                for (const App::Portrait& p : app.portraits) shown += p.tex.id && p.alpha > 0.5f;
                maxPortraits = std::max(maxPortraits, shown);
                if (!app.dialogue.empty()) {
                    for (const auto& mi : app.dialogueInfo) plates += mi.nameId >= 0;
                    pages += int(app.dialogue.size());
                    for (const auto& v : app.dialogueVoices) app.voicesPlayed += int(v.size());
                    app.dialogue.clear();
                }
                if (app.choiceFlag >= 0) { ++choices; answerChoice(app, true); }
                if (app.worldChoiceOpen) { ++choices; answerWorldChoice(app, 0); }
                app.tapPending = true;
            }
            if (app.runner.running()) {
                ++unfinished;
                std::printf("  talk to %d: still running (%s)\n", id, app.runner.where().c_str());
                app.runner.stop();
            }
        }
        std::printf("new game: talked to %d of %zu in the hall; %d pages (%d with a name plate), %d voice lines, %d choices, "
                    "up to %d portraits at once, %d unfinished\n", talked, ids.size(), pages, plates,
                    app.voicesPlayed - voicesBefore, choices, maxPortraits, unfinished);
        mapErrors += unfinished;
    }
    mapErrors += battleSelfTest(app, frames, glErrors);
    // The menus' sounds since the last call against the original's __effgo ids (2 choose, 3 back, 4 buzzer /
    // keyboard cursor, 5 page, 11 saved, 12 loaded, 22 paid, 40 SE gauge): 1 if they differ
    // (the expected sounds in CR1's numbering of the menus' sounds: Omega's are se::kMenuBase further on)
    auto expectSounds = [&app](const char* what, std::initializer_list<uint32_t> want0) {
        std::vector<uint32_t> want;
        for (uint32_t id : want0) want.push_back(id + se::kMenuBase);
        const bool same = std::equal(app.menuSoundLog.begin(), app.menuSoundLog.end(), want.begin(), want.end());
        if (!same) {
            std::printf("menu sounds, %s:", what);
            for (uint32_t id : app.menuSoundLog) std::printf(" %u", id);
            std::printf(" (want");
            for (uint32_t id : want) std::printf(" %u", id);
            std::printf(")\n");
        }
        app.menuSoundLog.clear();
        return same ? 0 : 1;
    };
    { // The Options screen: every page builds and draws; keys change what they should
        const App::Mix mix = app.mix;
        const App::Video video = app.video;
        const int language = app.language;
        int rows = 0, problems = 0;
        app.game.setFlag(0x43, true); // ("About Option" read: the manual's test below shows it)
        for (int page = 0; page < kOptionPages; ++page) {
            openOptions(app, page);
            rows += int(optionRows(app, page).size());
            app.lastTicks = 0;
            drawFrame(app, false);
            ++frames;
        }
        app.menuSoundLog.clear();
        openOptions(app, 1);         // Sound
        optionsKey(app, SDLK_DOWN);  // BGM
        const int music = app.mix.music;
        optionsKey(app, SDLK_LEFT);
        problems += app.mix.music != std::max(0, (music + 5) / 10 * 10 - 10);
        optionsKey(app, SDLK_DOWN);  // Sound Effects
        const int effects = app.mix.effects;
        optionsKey(app, SDLK_LEFT);
        problems += app.mix.effects != std::max(0, (effects + 5) / 10 * 10 - 10);
        problems += expectSounds("options gauges", {1, 1, 0x131 - se::kMenuBase}); // the cursor's tick (0x92); the SE gauge's test is __effgo(0x131)
        { // the < Back > bar's parts: the arrows turn the page, Back alone closes (ComUty_CmdBack 2)
            const bool parts = optionsBackPart(200, kOptBackY + 12) == -1 && optionsBackPart(300, kOptBackY + 12) == 0 &&
                               optionsBackPart(400, kOptBackY + 12) == 1 && optionsBackPart(300, kOptBackY - 40) == 2;
            if (!parts) std::printf("  options: the Back bar's arrows don't turn the page\n");
            problems += !parts;
        }
        openOptions(app, 0);         // Game: Language
        optionsKey(app, SDLK_LEFT);  // Japanese
        problems += app.language != 0;
        optionsKey(app, SDLK_RIGHT); // English
        problems += app.language != 1;
        optionsKey(app, SDLK_Q);     // wraps to the last page (Graphics)
        problems += app.optionsPage != kOptionPages - 1;
        optionsKey(app, SDLK_UP);    // from row 0 to Back
        problems += app.optionsRow != kOptionsBackRow;
        problems += expectSounds("options buttons, page", {2, 2, 3, 1});
        // Graphics: Anti-aliasing steps through MSAA to FSR Native AA and DLAA (the upscaler at
        // native size), the Upscaler row on to its other modes
        if (app.renderer.scalable()) { // (OpenGL: the row is off)
            app.optionsRow = 1;
            int aaSteps = 0;
            while (app.video.upscaler != 1 && aaSteps++ < 6) optionsKey(app, SDLK_RIGHT);
            problems += app.video.upscaler != 1 || app.video.upscaleMode != 0;
            optionsKey(app, SDLK_RIGHT);
            problems += app.video.upscaler != 2 || app.video.upscaleMode != 0;
            optionsKey(app, SDLK_RIGHT); // wraps to Off
            problems += app.video.upscaler != 0 || app.video.msaa != 1;
        }
        // FSR 3.1 where it runs (Vulkan): frames come out the frame's size, not black, error free.
        // The field shown (an earlier step leaves the screen faded to black - the overlay's text alone
        // then made the frame "drawn", and a Release build, without it, failed); only the frame below
        // the overlay's band counts
        int upscaled = 0;
        const int keepFadeFrames = app.fadeFrames;
        app.fadeFrames = 0;
        if (app.renderer.upscalerProblem(cr::Upscaler::Fsr3).empty()) {
            if (const char* shot = SDL_getenv("CR_SELFTEST_FSR_SHOT")) { // (the same moment unscaled: <prefix>off.bmp)
                app.video.upscaler = 0;
                applyVideo(app);
                app.optionsOpen = false;
                for (int f = 0; f < 8; ++f) app.lastTicks = 0, drawFrame(app, false);
                saveFrame(app, (std::string(shot) + "off.bmp").c_str());
                std::printf("FSR check: mode %d, area %d map %d, field shown %d, fade %.2f, back fade %.2f, bg %d, booting %d, title %d\n",
                            int(app.mode), app.area, app.mapNumber, int(!app.fieldHidden), screenFadeAlpha(app), screenFadeBackAlpha(app),
                            int(app.background.id != 0), int(app.booting), int(app.titleOpen));
            }
            for (int mode : {0, 1, 3}) {
                app.video.upscaler = 1;
                app.video.upscaleMode = mode;
                applyVideo(app);
                app.optionsOpen = false;
                for (int f = 0; f < 8; ++f) {
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    ++frames;
                }
                std::vector<uint8_t> px;
                int w = 0, h = 0;
                uint64_t sum = 0;
                if (app.renderer.readPixels(px, w, h))
                    for (size_t i = size_t(w) * size_t(h / 8) * 4; i < px.size(); i += 4) sum += px[i] + px[i + 1] + px[i + 2];
                if (const char* shot = SDL_getenv("CR_SELFTEST_FSR_SHOT")) // (each mode's frame: <prefix><mode>.bmp)
                    saveFrame(app, (std::string(shot) + std::to_string(mode) + ".bmp").c_str());
                const int errs = app.renderer.takeErrors();
                if (!sum || errs || w != app.renderer.frameWidth()) {
                    std::printf("FSR 3.1 %s: %d errors, %dx%d, %s\n", kUpscaleModes[mode].name, errs, w, h, sum ? "drawn" : "black");
                    ++problems;
                }
                ++upscaled;
            }
        }
        app.fadeFrames = keepFadeFrames;
        { // Smooth Models + HD textures: battles load and draw with them, HD textures keep the
          // original's layout size
            app.video.smoothModels = 3;
            app.video.hdTextures = true;
            const int hdBefore = app.hdLoaded;
            int drawn = 0;
            for (int g : {1, 5, 9}) {
                startBattle(app, {g, g % 9, -1, 0}, 0);
                for (int f = 0; f < 4; ++f) {
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    ++frames;
                }
                drawn += app.battle.active;
                const int errs = app.renderer.takeErrors();
                if (errs) std::printf("smooth/hd battle %d: %d render API errors\n", g, errs);
                problems += errs;
                endBattle(app, true);
            }
            cr::Image wi;
            if (loadImage(app, "window.mct", wi) && wi.logicalW) {
                const cr::Texture t = app.renderer.upload(wi);
                problems += t.width != wi.logicalW || t.height != wi.logicalH;
                cr::Texture tt = t;
                app.renderer.destroy(tt);
            }
            std::printf("smooth models + hd textures: %d battles drawn, %d HD textures used (%s)\n", drawn,
                        app.hdLoaded - hdBefore, app.hdDir.c_str());
            problems += drawn != 3;
            app.video.smoothModels = 0;
            app.video.hdTextures = false;
        }
        openOptions(app, 2);         // Display
        app.video.widescreen = true; // "Original Presentation" restores the defaults (upscaler off)
        app.video.upscaler = 1;
        app.optionsRow = int(optionRows(app, 2).size()) - 1;
        app.menuSoundLog.clear();
        optionsKey(app, SDLK_RETURN);
        problems += app.video.widescreen || app.video.width != 1024 || app.video.upscaler != 0;
        optionsKey(app, SDLK_ESCAPE);
        problems += app.optionsOpen;
        problems += expectSounds("options Enter, Back", {2, 3});
        // The title: the boot script reaches the title menu; its Options are title mode (Reset
        // Data); Start begins a new game; in a game, Return to Title boots again
        bootTitle(app);
        int bootFrames = 0;
        for (; bootFrames < 1000 && !app.titleOpen; ++bootFrames) stepEvents(app);
        problems += !app.titleOpen;
        app.game.setFlag(0x43, true); // ("About Option" read)
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
        { // the title's Quit asks first (No under the cursor); Esc answers No, and the title stays (#16)
            titleChoose(app, 3);
            const bool asked = app.quitAsk == 1 && app.quitConfirm == 1;
            titleKey(app, SDLK_ESCAPE);
            problems += !asked || app.quitAsk != 0 || !app.titleOpen;
        }
#endif
        openOptions(app, 0);
        const std::vector<OptRow> titlePage = optionRows(app, 0);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        const size_t quitRows = 0;
#else
        const size_t quitRows = 1; // (the desktop's Quit Game after Reset Data, #16)
#endif
        problems += titlePage.size() != size_t(14 - languageSheet(app)) + quitRows || // (3.1.0: one Language row)
                    titlePage[titlePage.size() - 1 - quitRows].label != sysLabel(app, 58122, "Reset Data") ||
                    (quitRows && titlePage.back().label != tr("Quit Game"));
        if (quitRows) { // its question, answered No: Options stay (a Yes would close the app)
            titlePage.back().act(0);
            const bool asked = app.resetAsk == 4;
            resetAnswer(app, false);
            problems += !asked || app.resetAsk != 0 || !app.optionsOpen;
        }
        { // Voice Volume's sample line (battle clip 0x2b1, __MuOptionSetInfo) for a key's step and a click on the track (#20)
            openOptions(app, 1);
            const std::vector<OptRow> sound = optionRows(app, 1);
            int voice = -1;
            for (size_t i = 0; i < sound.size(); ++i)
                if (sound[i].label == tr("Voice Volume")) voice = int(i);
            auto sampled = [&app]() { return !app.sound.battleLineLog.empty() && app.sound.battleLineLog.back() == 0x2b1; };
            app.sound.battleLineLog.clear();
            app.optionsRow = voice;
            optionsKey(app, SDLK_LEFT);
            const bool byKey = sampled();
            app.sound.battleLineLog.clear();
            float l0x, l0y, l1x, l1y; // (the frame's pixels for a menu point: toLogical is linear)
            app.renderer.toLogical(0, 0, l0x, l0y);
            app.renderer.toLogical(1, 1, l1x, l1y);
            const float mx = optCtrlX(235 + 30 * 2.03f), my = optRowY(voice - app.optionsScroll) + 12;
            const float lx = mx * 2 + app.renderer.uiX(), ly = my * 2;
            optionsClick(app, (lx - l0x) / (l1x - l0x), (ly - l0y) / (l1y - l0y));
            const bool byClick = sampled() && app.mix.voice == 30;
            if (voice < 0 || !byKey || !byClick) std::printf("voice sample: row %d, key %d, click %d (voice %d)\n", voice, int(byKey), int(byClick), app.mix.voice);
            problems += voice < 0 || !byKey || !byClick;
            openOptions(app, 0);
        }
        { // Menu Language (platform/ui_text): Auto first; a step right names English or a catalog, and the port's text follows
            const std::string keepLang = app.uiLang;
            int at = -1;
            for (size_t i = 0; i < titlePage.size(); ++i)
                if (titlePage[i].label == tr("Menu Language")) at = int(i);
            bool ok = at >= 0 && app.uiLang == "auto";
            if (at >= 0) {
                std::vector<OptRow> rows = titlePage;
                rows[size_t(at)].act(1);
                ok = ok && app.uiLang != "auto" && uitext::language() == (app.uiLang == "en" ? "en" : app.uiLang);
                rows[size_t(at)].act(1);
                ok = ok && uitext::language() == app.uiLang;
                const bool translated = std::string(tr("Menu Language")) != "Menu Language";
                std::printf("menu language: row %d, Auto -> %s -> %s, %zu languages, the label %s: %s\n", at, "en", app.uiLang.c_str(),
                            uitext::languages().size(), translated ? "translated" : "English", ok && translated ? "ok" : "WRONG");
                ok = ok && translated;
            }
            problems += !ok;
            app.uiLang = keepLang;
            applyUiLanguage(app);
        }
        { // Reset Data: asked twice; No keeps the saves, Yes to both removes them (in a test folder)
            const std::string keepDir = app.saveDirOverride;
            app.saveDirOverride = userDir() + "selftest-reset/";
            SDL_CreateDirectory(app.saveDirOverride.c_str());
            const cr::SaveData sd = captureSave(app);
            cr::writeSave(dataPath(app), sd);
            cr::writeSave(slotPath(app, 1), sd); // (an older version's)
            cr::writeSave(savePath(app, "resume.dat"), sd);
            auto saves = [&] {
                int n = int(SDL_GetPathInfo(dataPath(app).c_str(), nullptr));
                for (int pair = 1; pair <= 4; ++pair) n += SDL_GetPathInfo(slotPath(app, pair).c_str(), nullptr);
                return n + int(SDL_GetPathInfo(savePath(app, "resume.dat").c_str(), nullptr));
            };
            app.optionsRow = int(titlePage.size() - 1 - quitRows); // (Reset Data: before the desktop's Quit Game)
            optionsKey(app, SDLK_RETURN);                 // "Erase all the data from your game?"
            int bad = app.resetAsk != 1 || app.resetConfirm != 1;
            optionsKey(app, SDLK_RETURN);                 // No (under the cursor)
            bad += app.resetAsk != 0 || saves() != 3;
            optionsKey(app, SDLK_RETURN);
            optionsKey(app, SDLK_LEFT), optionsKey(app, SDLK_RETURN); // Yes: "Are you sure ...?"
            bad += app.resetAsk != 2 || saves() != 3;
            optionsKey(app, SDLK_LEFT), optionsKey(app, SDLK_RETURN); // Yes
            bad += app.resetAsk != 0 || saves() != 0;
            std::printf("reset data: asked twice, No kept 3 saves, Yes removed them: %s\n", bad ? "WRONG" : "ok");
            problems += bad;
            app.saveDirOverride = keepDir;
        }
        optionsKey(app, SDLK_ESCAPE);
        problems += !app.titleOpen; // back on the title menu
        app.menuSoundLog.clear();
        int openingMap = -1;
        {
            const std::string keepDir = app.saveDirOverride; // (no save there: New alone)
            app.saveDirOverride = userDir() + "selftest-empty/";
            titleChoose(app, 0);    // Start: the boot script goes on to Character Select (Util 4)
            for (int f = 0; f < 300 && !app.charSelectOpen; ++f) stepEvents(app);
            problems += !app.charSelectOpen;
            app.charSelectCursor = 0, app.charSelectPage = 0; // Escher & Musiea
            charSelectKey(app, SDLK_RETURN); // Escher & Musiea -> the pair's save / load screen (Util 5)
            for (int f = 0; f < 300 && !app.saveScreen.open; ++f) stepEvents(app);
            problems += !app.saveScreen.open || app.saveScreen.haveSave || !app.saveScreen.load;
            saveScreenKey(app, SDLK_RETURN); // New -> "Begin new game?"
            problems += app.saveScreen.message != 57057;
            saveScreenKey(app, SDLK_RETURN); // Yes: the script plays the pair's opening
            problems += expectSounds("title Start, pair, New, Yes", {2, 2, 2, 12});
            for (int f = 0; f < 600 && app.booting; ++f) stepEvents(app);
            openingMap = app.mapNumber;
            app.saveDirOverride = keepDir;
        }
        problems += app.booting || openingMap != 69 || app.playerChar != 0; // the opening (Omega: map 69, the planet)
        if (problems) std::printf("title flow: map %d, leader %d, booting %d\n", openingMap, app.playerChar, int(app.booting));
        app.runner.stop();
        startNewGame(app); // (the hall, for the in-game Options checks below)
        app.game.setFlag(0x43, true);
        openOptions(app, 0);
        std::vector<OptRow> gamePage = optionRows(app, 0);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        const size_t quitRow = 0;
#else
        const size_t quitRow = 1; // (the desktop's Quit Game last, #16)
#endif
        const size_t titleRow = gamePage.size() - 1 - quitRow;
        problems += gamePage.size() != size_t(14 - languageSheet(app)) + quitRow || gamePage[titleRow].label != sysLabel(app, 58123, "Return to Title") ||
                    !gamePage[titleRow].enabled;
        if (gamePage.size() == size_t(14 - languageSheet(app)) + quitRow && gamePage[titleRow].act) gamePage[titleRow].act(0);
        problems += app.resetAsk != 3; // "return to the title?"
        resetAnswer(app, true);
        for (int f = 0; f < 14 && app.titleReturnIn > 0; ++f) stepEvents(app); // (twelve frames of black)
        problems += !app.booting;
        app.runner.stop();
        std::printf("options: %d pages, %d rows; FSR 3.1 modes drawn: %d; title menu after %d frames; %d problems\n",
                    kOptionPages, rows, upscaled, bootFrames, problems);
        mapErrors += problems;
        app.mix = mix; // put the settings back as they were
        app.video = video;
        app.language = language;
        applyLanguage(app);
        applyMix(app);
        applyVideo(app);
    }
    { // Saves: a field's state written, changed, read back and restored; the title's Start shows it
        int problems = 0;
        const std::string keepDir = app.saveDirOverride;
        app.saveDirOverride = userDir() + "selftest-saves/";
        SDL_CreateDirectory(app.saveDirOverride.c_str());
        startNewGame(app);
        for (int f = 0; f < 3000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
            stepEvents(app);
            app.dialogue.clear();
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        app.game.setFlag(3333, true);
        app.game.items[7] = 3;
        app.game.vars[100] = -42;
        app.game.oz = 777;
        app.game.party[1].level = 5;
        app.game.party[1].exp = 600;
        app.game.party[0].equip[cr::kJewel] = 426;
        app.game.party[0].seeds[2] = 7;
        app.game.party[0].genes[2] = 485;
        app.playSeconds = 1234.5;
        app.walker.pos.x += 3;
        const cr::SaveData before = captureSave(app);
        app.menuSoundLog.clear();
        openSaveScreen(app, false);
        saveScreenKey(app, SDLK_RETURN); // Yes
        problems += app.saveScreen.message != 57082;
        problems += expectSounds("save", {11});
        app.saveScreen.open = false;
        app.game = cr::GameState();
        app.playSeconds = 0;
        bootTitle(app);
        for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
        titleChoose(app, 0); // Start -> Character Select -> the pair's Load / New
        for (int f = 0; f < 300 && !app.charSelectOpen; ++f) stepEvents(app);
        app.charSelectCursor = 0, app.charSelectPage = 0; // Escher & Musiea
        charSelectKey(app, SDLK_RETURN);
        for (int f = 0; f < 300 && !app.saveScreen.open; ++f) stepEvents(app);
        problems += !app.saveScreen.open || !app.saveScreen.haveSave;
        saveScreenKey(app, SDLK_RETURN); // Load
        problems += app.saveScreen.message != 57013;
        saveScreenKey(app, SDLK_RETURN); // Yes
        problems += expectSounds("title Start, pair, Load, Yes", {2, 2, 2, 12});
        const bool loaded = app.saveScreen.message == 57083;
        problems += !loaded;
        const cr::SaveData after = captureSave(app);
        problems += after.map != before.map || after.area != before.area || !app.game.flag(3333) ||
                    app.game.items[7] != 3 || app.game.vars[100] != -42 || app.game.oz != 777 ||
                    app.game.party[1].level != 5 || app.game.party[1].exp != 600 || app.game.party[0].equip[cr::kJewel] != 426 || app.game.party[0].seeds[2] != 7 || app.game.party[0].genes[2] != 485 ||
                    app.game.party[2].equip[cr::kWeapon] != before.game.party[2].equip[cr::kWeapon] ||
                    std::fabs(app.playSeconds - 1234.5) > 1e-6 ||
                    std::fabs(after.pos[0] - before.pos[0]) > 1e-3f || app.titleOpen || app.booting;
        for (int f = 0; f < 60; ++f) stepSaveScreen(app);
        problems += app.saveScreen.open;
        for (int f = 0; f < 3; ++f) {
            app.lastTicks = 0;
            drawFrame(app, false);
            ++frames;
        }
        // The Progress Tracker
        app.persist = true;
        writeResume(app);
        app.persist = false;
        cr::SaveData resume;
        problems += !cr::readSave(savePath(app, "resume.dat"), resume) || resume.map != before.map;
        // ... offered at the title (TitlePrc 0x16..0x1f): the first time a launch while flag 0x44 is
        // clear, "About Progress Tracker" (56041) read through first in the message window (style 8,
        // one line), flags 0x44 and 3 set while it is up; then windows 4..7 come in over the title's
        // strip, the answer sends them back out before the load, and the resume point is used up
        int rbad = 0;
        rbad += pictAnimFrame(1, 1, 4) != 1 || pictAnimFrame(4, 1, 4) != 4 || pictAnimFrame(7, 1, 4) != 1 ||
                pictAnimFrame(8, 1, 4) != 0 || pictAnimFrame(17, 4, 4) != 3;
        app.seenFlags.clear();
        app.persist = true;
        app.interruptShown = false;
        bootTitle(app);
        for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
        app.titleStage = 1;
        titleChoose(app, 0); // Start: the Progress Tracker's offer follows it (TitlePrc 8 -> 0x16)
        const size_t noticePages = app.dialogue.size();
        rbad += app.resumeNotice != 1 || app.resumePrompt || !app.game.flag(0x44) || !app.game.flag(3) || noticePages < 4 ||
                app.msgStyle != 8 || app.msgLines != 1 || app.msgWin.state < 1 || !app.titleOpen;
        for (int f = 0; f < 2; ++f) {
            app.lastTicks = 0;
            drawFrame(app, false);
            ++frames;
        }
        for (int f = 0; f < 800 && app.resumeNotice == 1; ++f) {
            stepEvents(app);
            if (f % 8 == 7) titleKey(app, SDLK_RETURN); // (a page out, then turned)
        }
        rbad += app.game.flag(3) || app.resumeNotice != 2 || app.resumePrompt;
        for (int f = 0; f < 20 && app.resumePrompt != 1; ++f) stepEvents(app); // (the window fading out first: 0x19)
        rbad += app.resumePrompt != 1 || app.resumeNotice != 0 || app.resumeCursor != 0;
        for (int f = 0; f < 6; ++f) stepEvents(app);
        rbad += app.resumeWin < 1 || app.titleOut < 4 || !app.titleOpen;
        for (int f = 0; f < 2; ++f) {
            app.lastTicks = 0;
            drawFrame(app, false);
            ++frames;
        }
        const bool offered = app.resumePrompt == 1;
        problems += !offered;
        app.menuSoundLog.clear();
        answerResume(app, true);
        rbad += app.resumePrompt != 2 || !app.resumeYes;
        for (int f = 0; f < 4; ++f) stepEvents(app);
        rbad += app.resumeWin > 0 || !app.titleOpen; // (0x1c: the windows out, the black still coming)
        for (int f = 0; f < 20 && app.resumeIn > 0; ++f) stepEvents(app); // (ten frames of black, ten more, then the load)
        problems += expectSounds("resume", {2});
        problems += app.titleOpen || app.mapNumber != before.map || SDL_GetPathInfo(savePath(app, "resume.dat").c_str(), nullptr);
        rbad += !app.game.flag(0x44) || app.resumePrompt; // (ContData_memcpy: the message seen this launch)
        // ... and saved as you go (the Vita's): a scene's end after the load arms it, the next writes it
        app.trackerArmed = false; // (the room's entry script may have ended during the load's frames above: that armed it)
        trackerSceneEnd(app);
        const bool armedOnly = !SDL_GetPathInfo(savePath(app, "resume.dat").c_str(), nullptr);
        trackerSceneEnd(app);
        const bool asYouGo = armedOnly && cr::readSave(savePath(app, "resume.dat"), resume) && resume.map == before.map;
        problems += !asYouGo;
        // ... a second offer this launch: no message (_gbTutoFlg), the question at once; No sends the
        // windows out and Start goes on (TitleClr: the boot script on to Character Select)
        bootTitle(app);
        for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
        app.titleStage = 1;
        titleChoose(app, 0);
        rbad += app.resumeNotice || app.resumePrompt != 1 || !app.dialogue.empty();
        for (int f = 0; f < 4; ++f) stepEvents(app);
        answerResume(app, false);
        rbad += app.resumePrompt != 2 || app.resumeYes;
        for (int f = 0; f < 20 && app.resumeIn > 0; ++f) stepEvents(app);
        rbad += app.titleOpen || !app.booting || app.resumePrompt || app.game.flag(0) ||
                SDL_GetPathInfo(savePath(app, "resume.dat").c_str(), nullptr);
        // ... and none while the save holds flag 0x44, a new launch or not
        {
            cr::SaveData d;
            if (readData(app, d)) d.game.setFlag(0x44, true), cr::writeSave(dataPath(app), d);
            else rbad += 1;
        }
        cr::writeSave(savePath(app, "resume.dat"), resume);
        app.interruptShown = false;
        bootTitle(app);
        for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
        app.titleStage = 1;
        titleChoose(app, 0);
        rbad += app.resumeNotice || app.resumePrompt != 1 || app.interruptShown;
        bootTitle(app);
        rbad += app.resumePrompt || app.resumeNotice || app.resumeIn;
        std::printf("title resume: the message %zu pages once a launch (flag 0x44), the windows in and out, Yes loads, No starts: %d problems\n",
                    noticePages, rbad);
        // seen once, the message stays seen across launches (settings.ini's seen_flags), the save's flag or not
        rbad += !seenFlag(app, 0x44);
        problems += rbad;
        SDL_RemovePath(savePath(app, "resume.dat").c_str());
        app.seenFlags.clear();
        saveMix(app); // (the test's settings file without them)
        app.persist = false;
        // The title's New (pair 1, no save): New alone, at 232; Yes hands var 1 = 2 to the boot
        // script, which goes to the opening (map 88) - var 1 = 1 would be the last battle (map 82)
        for (const char* f : {"save1.dat", "data.dat", "resume.dat"}) SDL_RemovePath(savePath(app, f).c_str());
        app.game = cr::GameState();
        bootTitle(app);
        for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
        titleChoose(app, 0);
        for (int f = 0; f < 300 && !app.charSelectOpen; ++f) stepEvents(app);
        app.charSelectCursor = 0;
        charSelectKey(app, SDLK_RETURN);
        for (int f = 0; f < 300 && !app.saveScreen.open; ++f) stepEvents(app);
        problems += !app.saveScreen.open || app.saveScreen.haveSave || savePlates(app) != std::vector<int>{2} ||
                    app.saveScreen.emblems != 0;
        saveScreenKey(app, SDLK_RETURN); // New
        problems += app.saveScreen.message != 57057 || app.saveScreen.kind != 2;
        saveScreenKey(app, SDLK_RETURN); // Yes
        const int var1 = app.game.vars[1];
        int openingMap = -1;
        for (int f = 0; f < 1500 && openingMap < 0; ++f) {
            stepEvents(app);
            if (!app.booting && app.mapNumber >= 0) openingMap = app.mapNumber;
        }
        problems += var1 != 2 || openingMap != 69;
        app.runner.stop();
        // Scenario 0 cleared (its load flag 31): New asks about the clear data first; Omega shows no
        // first plate (CR1's last battle) and no emblems
        {
            cr::SaveData sd = before;
            sd.game.setFlag(31, true);
            sd.leader = 0;
            cr::writeSave(dataPath(app), sd);
        }
        app.game = cr::GameState();
        app.chosenPair = 1;
        openSaveScreen(app, true);
        App::SaveScreen& ssc = app.saveScreen;
        problems += !ssc.cleared || ssc.allClear || savePlates(app) != std::vector<int>{1, 2};
        saveScreenChoose(app, 1); // New: with the clear data?
        problems += ssc.kind != 3 || ssc.message != 57086 || ssc.lines[0] != 57087 || ssc.lines[1] != 57088;
        saveScreenKey(app, SDLK_ESCAPE); // No: a plain new game, then?
        problems += ssc.phase != 1 || ssc.kind != 2 || ssc.message != 57089 || ssc.lines[1] != 57091;
        saveScreenKey(app, SDLK_ESCAPE); // No again: back to the plates
        problems += ssc.phase != 0;
        app.saveScreen.open = false;
        // Uty_LoadPrty with the clear data (prty_init_new, Omega): mode 3 keeps the scenario's records'
        // bonuses, gear and plates and the bag less its kind 3 / 9 items, and Oz; both modes clear the
        // scenario's flags (_flgclear: 1001..1500 for scenario 0 - 1501.. stays) and its field flags
        // (3000..3015, 3300..3379; 3020 stays); mode 2 starts the records over
        int storyItem = 0;
        for (int it = 1; it < 500 && !storyItem; ++it)
            if (const cr::ItemParam* ip = app.itemTable.item(it); ip && (ip->kind == 3 || ip->kind == 9)) storyItem = it;
        {
            cr::SaveData sd = before;
            for (int f : {31, 1100, 2000, 3000, 3020, 3310}) sd.game.setFlag(f, true);
            sd.game.party[4].seeds[2] = 7, sd.game.party[4].genes[2] = 485, sd.game.party[4].equip[cr::kJewel] = 426;
            sd.game.items[storyItem] = 1, sd.game.items[7] = 3, sd.game.oz = 777;
            cr::writeSave(dataPath(app), sd);
        }
        ViewerHost host(app);
        app.game = cr::GameState();
        host.loadParty(1, true);
        const cr::PartyMember& e0 = app.game.party[4];
        int carry = (e0.seeds[2] == 7) + (e0.genes[2] == 0) + (e0.equip[cr::kJewel] == 0) + (app.game.party[0].seeds[2] == 7) + // (mode 3: the starting plate / gear worn again)
                    (app.game.items[7] == 3) + !app.game.items.count(storyItem) + (app.game.oz == 777) + !app.game.flag(1100) +
                    app.game.flag(2000) + !app.game.flag(3000) + app.game.flag(3020) + !app.game.flag(3310) +
                    (app.game.pairs[0] == std::array<int, 2>{0, 1});
        problems += (carry != 13) + (storyItem == 0);
        app.game = cr::GameState();
        host.loadParty(1, false);
        problems += app.game.party[4].seeds[2] != 0 || app.game.oz != 777 || app.game.flag(1100) || !app.game.flag(2000);
        // scenario 1 without its clear data: a fresh start (its pair Vieg and Dante, 480 / 82 / 188, 0x1024.. cleared)
        app.game = cr::GameState();
        app.game.setFlag(0x1030, true);
        host.loadParty(2, false);
        problems += app.game.scenario != 1 || app.game.pairs[1] != std::array<int, 2>{0, 2} || !app.game.flag(0xfee) ||
                    ownedCount(app, 480) < 1 || ownedCount(app, 188) < 1 || app.game.flag(0x1030);
        std::printf("new game with clear data: mode 3 carried %d of 13, mode 2 the records over, a fresh scenario 1 fresh\n", carry);
        problems += clearDataNewGameSelfTest(app, before);
        app.game = cr::GameState();
        app.playerChar = 0;
        for (int f = 0; f < 2; ++f) {
            app.lastTicks = 0;
            drawFrame(app, false);
            ++frames;
        }
        problems += app.renderer.takeErrors();
        { // One data.dat for both scenarios: the earlier versions' save1 / save2 fold into it once (the
          // newer the base, the other's own bag, Oz, records and place kept), and each scenario loads
          // from its own place
            int mbad = 0;
            for (const char* f : {"save1.dat", "save2.dat", "data.dat"}) SDL_RemovePath(savePath(app, f).c_str());
            cr::SaveData s1 = before, s2 = before;
            s1.savedAt = 100, s1.game.scenario = 0, s1.game.oz = 500, s1.game.items[7] = 3, s1.game.items[85] = 1, s1.game.setFlag(1100, true);
            s1.places = {}, s1.area = 2, s1.map = 70;
            s2.savedAt = 200, s2.game.scenario = 1, s2.game.oz = 900, s2.game.items[9] = 2, s2.game.setFlag(1600, true);
            s2.places = {}, s2.area = 10, s2.map = 214, s2.game.otherBag.clear(), s2.game.otherOz = 0;
            cr::writeSave(slotPath(app, 1), s1), cr::writeSave(slotPath(app, 2), s2);
            cr::SaveData d;
            mbad += !readData(app, d);
            mbad += d.game.scenario != 1 || d.game.oz != 900 || d.game.otherOz != 500 || d.game.otherBag[7] != 3 || d.game.items[85] < 1 ||
                    !d.game.flag(1100) || !d.game.flag(1600) || !d.places[0].has || !d.places[1].has || d.places[0].map != 70 ||
                    d.places[1].map != 214 || !SDL_GetPathInfo(dataPath(app).c_str(), nullptr);
            app.game = cr::GameState();
            app.chosenPair = 1;
            openSaveScreen(app, true); // scenario 0's Load: its own place, its own bag in play
            mbad += !app.saveScreen.haveSave || app.saveScreen.data.map != 70 || app.saveScreen.data.game.scenario != 0 ||
                    app.saveScreen.data.game.oz != 500 || app.saveScreen.data.game.items[7] != 3;
            app.saveScreen.open = false;
            // (the boards as 3.1.0 shows them: the Vita layout shows none - checked after)
            const bool vitaTitle = app.renderer.vitaScreen();
            app.renderer.setVitaScreen(false);
            // Before the clear: "New fun stuff available" once, not at the next launch (the owner's choice;
            // 3.1.0 repeats it till the clear)
            {
                cr::SaveData ud = d;
                ud.game.setFlag(0x1f, false);
                for (int f = 0x65; f <= 0x7b; ++f) ud.game.setFlag(f, false);
                cr::writeSave(dataPath(app), ud);
                app.verUpShown = false, app.titleStage = 0, app.titleOpen = true;
                titleTap(app);
                mbad += app.titleStage != 2 || app.titleBoard.lines[0] != 57134;
                for (int k = 0; k < 3 && app.titleStage == 2; ++k) titleBoardTap(app);
                mbad += app.titleStage != 1;
                app.verUpShown = false, app.titleStage = 0;
                titleTap(app);
                mbad += app.titleStage != 1;
                app.titleOpen = false;
            }
            // The title's boards (TitlePrc 3..6): with the clear flag and the trials open, each trial once,
            // then the Info Board; the marks go into data.dat; nothing the second time
            cr::SaveData td = d;
            td.game.setFlag(0x1f, true);
            for (int f = 0x65; f <= 0x7b; ++f) td.game.setFlag(f, false);
            cr::writeSave(dataPath(app), td);
            app.verUpShown = false, app.titleStage = 0, app.titleOpen = true;
            titleTap(app);
            mbad += app.titleStage != 2 || app.titleBoard.lines[0] != 57141 || app.titleBoard.lines[1] != 57145 || app.titleBoard.count != 6;
            for (int k = 0; k < 5 && app.titleStage == 2; ++k) titleBoardTap(app);
            mbad += app.titleStage != 2 || app.titleBoard.lines[1] != 57150;
            titleBoardTap(app);
            mbad += app.titleStage != 2 || app.titleBoard.lines[0] != 57143;
            titleBoardTap(app);
            mbad += app.titleStage != 1;
            cr::SaveData back;
            mbad += !readData(app, back) || !back.game.flag(0x6f) || !back.game.flag(0x74) || !back.game.flag(0x76) || !back.game.flag(0x7b);
            app.verUpShown = false, app.titleStage = 0;
            titleTap(app);
            mbad += app.titleStage != 1;
            app.titleOpen = false;
            { // The Vita's TitlePrc 3 marks what is new but goes on to the plates: no board, the marks still kept
                app.renderer.setVitaScreen(true);
                cr::SaveData vd = d;
                vd.game.setFlag(0x1f, true);
                for (int f = 0x65; f <= 0x7b; ++f) vd.game.setFlag(f, false);
                cr::writeSave(dataPath(app), vd);
                app.verUpShown = false, app.titleStage = 0, app.titleOpen = true;
                const bool phone = app.renderer.phoneLayout();
                app.renderer.setPhoneLayout(true); // (vitaScreen: the Vita's screen with the iPhone layout)
                titleTap(app);
                mbad += app.titleStage != 1;
                cr::SaveData vback;
                mbad += !readData(app, vback) || !vback.game.flag(0x6f) || !vback.game.flag(0x7b);
                app.renderer.setPhoneLayout(phone);
                app.titleStage = 0, app.titleOpen = false;
            }
            app.renderer.setVitaScreen(vitaTitle);
            std::printf("shared save: save1 + save2 fold into data.dat, scenario 0 loads its own place, the title's boards (none on the Vita): %d problems\n", mbad);

            problems += mbad;
            for (const char* f : {"save1.dat", "save2.dat", "data.dat"}) SDL_RemovePath(savePath(app, f).c_str());
        }
        std::printf("save screen: the title's New -> var 1 = %d, opening map %d; cleared: emblems %u, %zu plates; %d problems so far\n",
                    var1, openingMap, ssc.emblems, savePlates(app).size(), problems);
        ssc.open = false;
        app.menuSoundLog.clear();
        for (const char* f : {"save1.dat", "data.dat", "resume.dat"}) SDL_RemovePath(savePath(app, f).c_str());
        SDL_RemovePath(app.saveDirOverride.c_str());
        app.saveDirOverride = keepDir;
        // The Instruction Manual (Options): the manual items held, each its page (56000 + item - 419)
        // in the standard message window (SE 0x97 / 0x98), paged by tap, the list back at the item;
        // 431's (MsgUty_SetMsg's id 12) with battle_01's picture, a picture page a page turn, the
        // window at _mwin[4]. The first Options in a game open on 56040 "About Option" (flag 0x43),
        // once. Then Scenarios
        int manualTopicsHeld = 0, manualPicturePages = 0, manualTextPages = 0;
        {
            const cr::GameState keepGame = app.game;
            const bool keepType = app.typeOn;
            app.typeOn = false;
            for (int item = 419; item <= 473; ++item) app.game.items.erase(item);
            for (int item : {419, 420, 421, 428, 431, 459}) app.game.items[item] = 1;
            app.game.setFlag(0x43, false);
            const std::vector<int> keepSeen = app.seenFlags;
            app.seenFlags.clear();
            const bool keepTitle = app.titleOpen;
            app.titleOpen = false;
            App::ManualMsg& mm = app.manualMsg;
            openOptions(app, 0);
            problems += !app.manualOpen || app.manualIntro != 56040 || manualPage(app, 56040).empty() || !app.game.flag(0x43) ||
                        mm.state != 2 || mm.id != 56040 || mm.picture;
            // OptionPrc 0..2 (3.1.0's OptionSet): no menu mask, the page held back until the window has shut -
            // and the same from the title (its flag 0x43 the save's)
            int aboutHeld = 0;
            auto held = [&] {
                bool atOnce = false;
                stepOptions(app);
                aboutHeld += menuMaskWanted(app, atOnce) < 0 && app.optionsAnim == 0;
            };
            held();
            for (int f = 0; f < 6; ++f) stepManual(app);
            problems += mm.win.state != 3;
            manualKey(app, SDLK_ESCAPE); // (the port's Back: the window closes, then on to Options)
            for (int f = 0; f < 8; ++f) stepManual(app);
            problems += app.manualOpen;
            stepOptions(app);
            aboutHeld += app.optionsAnim == 1;
            app.optionsOpen = false;
            openOptions(app, 0);
            problems += app.manualOpen;
            app.optionsOpen = false;
            app.game.setFlag(0x43, false);
            // seen once, it stays seen: a game without the flag (a new launch, another save) opens on the page
            problems += !seenFlag(app, 0x43);
            openOptions(app, 0);
            problems += app.manualOpen;
            app.optionsOpen = false;
            app.seenFlags.clear(); // (a fresh install's title)
            app.titleOpen = true;
            openOptions(app, 0);
            aboutHeld += app.manualOpen && app.manualIntro == 56040 && mm.state == 2;
            held();
            for (int f = 0; f < 6; ++f) stepManual(app);
            manualKey(app, SDLK_ESCAPE);
            for (int f = 0; f < 8; ++f) stepManual(app);
            aboutHeld += !app.manualOpen && app.game.flag(0x43);
            app.optionsOpen = false;
            app.titleOpen = false;
            app.seenFlags = keepSeen;
            problems += aboutHeld != 5;
            std::printf("options: \"About Option\" (56040) alone in the message window, the page after it, from the field and "
                        "the title: %d/5\n", aboutHeld);
            app.titleOpen = keepTitle;
            openManual(app, 0);
            const std::vector<int> topics = manualTopics(app);
            manualTopicsHeld = int(topics.size());
            manualKey(app, SDLK_DOWN);
            manualKey(app, SDLK_DOWN);
            problems += topics.size() != 6 || app.manualCursor != 2 || topics[2] != 421 || manualPage(app, 56002).empty();
            manualKey(app, SDLK_PAGEDOWN); // (to the last, 459)
            manualKey(app, SDLK_UP);       // 431
            manualKey(app, SDLK_RETURN);
            problems += mm.state != 1;
            for (int f = 0; f < 5; ++f) stepManual(app); // 0x12's wait, then the window
            problems += mm.state != 2 || mm.id != 56012 || !mm.picture || mm.pic.page != -1 || mm.pages.size() != 6;
            manualKey(app, SDLK_RETURN); // (opening: no tap yet)
            for (int f = 0; f < 6; ++f) stepManual(app);
            problems += mm.page != 0 || mm.win.state != 3;
            int lastPicPage = -1, framesSeen = 0;
            for (int turn = 0; turn < 12 && mm.state == 2; ++turn) {
                ++manualTextPages;
                manualKey(app, SDLK_RETURN);
                if (mm.state != 2) break;
                if (mm.pic.on) {
                    problems += mm.pic.page != lastPicPage + 1;
                    lastPicPage = mm.pic.page;
                    ++manualPicturePages;
                    for (int f = 0; f < 70; ++f) { stepManual(app); framesSeen = std::max(framesSeen, mm.pic.frame + 1); }
                } else stepManual(app);
                app.lastTicks = 0;
                drawFrame(app, false);
            }
            problems += mm.state != 3 || mm.pic.on || manualPicturePages != 4 || manualTextPages != 6 || framesSeen < 4;
            for (int f = 0; f < 8; ++f) stepManual(app);
            problems += !app.manualOpen || mm.state != 0 || app.manualCursor != 4; // the list back at 431
            manualKey(app, SDLK_ESCAPE);
            problems += app.manualOpen || app.renderer.takeErrors();
            app.typeOn = keepType;
            app.game = keepGame;
        }
        problems += expectSounds("manual", {6, 7, 6, 7, 1, 1, 5, 1, 2, 6, 7, 3});
        // Scenarios: the title's (mode 0, to look at) then the Camp's, a cell opened with a present
        openScenarios(app);
        problems += !app.scc.open || !app.scc.sheet.id;
        app.lastTicks = 0;
        drawFrame(app, false);
        scenariosKey(app, SDLK_ESCAPE);
        problems += app.scc.open;
        problems += expectSounds("scenarios", {3});
        int bingoProblems = 0;
        {
            const cr::GameState keepGame = app.game;
            app.game.setFlag(0x642, true);
            app.game.items[bingo::kItems[5][2]] = 1;
            // From the Camp (L / the picture): its windows go, Scenarios 4 frames on
            app.menuSoundLog.clear();
            openCamp(app);
            app.menuSoundLog.clear();
            campKey(app, SDLK_Q);
            bingoProblems += app.camp.leaving != 1 || app.scc.open;
            campKey(app, SDLK_RETURN); // (ignored while they go)
            for (int f = 0; f < 3; ++f) stepCampLeaving(app);
            bingoProblems += app.scc.open;
            stepCampLeaving(app);
            bingoProblems += !app.scc.open || app.scc.mode != 1 || app.camp.leaving != 0;
            bingoProblems += expectSounds("camp to scenarios", {2});
            app.campOpen = false;
            scenariosKey(app, SDLK_RIGHT);            // the second card
            scenariosKey(app, SDLK_RETURN);
            bingoProblems += app.scc.card != 1 || !app.scc.cardTex.id || !app.scc.photo.id || !app.scc.canPush[5] ||
                             app.scc.canPush[4] || app.scc.phase != 2 || bingo::photoAlpha(app) != 0;
            scenariosKey(app, SDLK_RIGHT); // (state 7: no input yet)
            bingoProblems += app.scc.cursor != 0;
            for (int f = 0; f < 5; ++f) stepScenarios(app);
            bingoProblems += app.scc.phase != 0 || app.scc.win[1].state != 3 || bingo::winX(app.scc.win[1]) != 0;
            for (int k = 0; k < 5; ++k) scenariosKey(app, SDLK_RIGHT); // cell 0 -> 1 (wraps), down to 5
            scenariosKey(app, SDLK_DOWN);
            scenariosKey(app, SDLK_RETURN);
            for (int f = 0; f < 10; ++f) stepScenarios(app);
            bingoProblems += app.scc.cursor != 5 || app.scc.cell[5] != 2 || !app.game.flag(bingo::kOpenFlag[1] + 5) ||
                             !app.game.flag(bingo::kPushFlag[0] + 5) || app.scc.canPush[5] || bingo::presentsUsed(app) != 1;
            scenariosKey(app, SDLK_RETURN); // used: refused
            // < > (SccnariosPrc 0xc..0xe, 5..7): > slides the card's windows out left - its photo unseen,
            // the card not done - and the next one's in from the right; < the other way
            int slideProblems = 0;
            auto slide = [&](SDL_Keycode key, int to, float outSide, bool photoOut) {
                scenariosKey(app, key);
                slideProblems += app.scc.phase != 1;
                stepScenarios(app);
                stepScenarios(app);
                stepScenarios(app);
                slideProblems += app.scc.card == to || (outSide < 0) != (bingo::winX(app.scc.win[1]) < 0) ||
                                 bingo::winX(app.scc.win[0]) >= 0 || (bingo::photoAlpha(app) > 0) != photoOut;
                app.lastTicks = 0;
                drawFrame(app, false);
                for (int f = 0; f < 7 && app.scc.phase == 1; ++f) stepScenarios(app);
                slideProblems += app.scc.card != to || app.scc.phase != 2 || (outSide < 0) != (bingo::winX(app.scc.win[2]) > 0) ||
                                 bingo::photoAlpha(app) != 0;
                for (int f = 0; f < 5; ++f) stepScenarios(app);
                slideProblems += app.scc.phase != 0 || bingo::winX(app.scc.win[0]) != 0 || bingo::winX(app.scc.win[2]) != 0;
            };
            slide(SDLK_PAGEDOWN, 2, -240, false);
            slide(SDLK_PAGEUP, 1, 240, false);
            for (int i = 0; i < 16; ++i) app.game.setFlag(bingo::kOpenFlag[1] + i, true);
            bingo::refresh(app);
            slide(SDLK_PAGEUP, 0, 240, true); // (a whole card's photo goes with its window)
            slide(SDLK_PAGEDOWN, 1, -240, false);
            slideProblems += app.scc.win[1].state != 6 || bingo::photoAlpha(app) != 1; // (a whole card: no window 7)
            bingoProblems += slideProblems;
            bingoProblems += expectSounds("scenarios: cards", {1, 2, 1, 1, 1, 1, 1, 1, 0xaf - 0x91, 4, 3, 3, 3, 3});
            scenariosKey(app, SDLK_RETURN); // the whole picture: zoom
            for (int f = 0; f < 10; ++f) stepScenarios(app);
            bingoProblems += app.scc.zoom != bingo::kZoomMax;
            scenariosKey(app, SDLK_LEFT);
            bingoProblems += app.scc.panX <= 0;
            app.lastTicks = 0;
            drawFrame(app, false);
            scenariosKey(app, SDLK_ESCAPE);
            bingoProblems += app.scc.card != -1 || !app.scc.open;
            scenariosKey(app, SDLK_ESCAPE);
            bingoProblems += app.scc.open;
            // The Collection: R on the Camp's first page - the second scenario's only (window 0x15);
            // the rate over _anItemFlagTb's 171
            app.game.items.clear();
            for (int i = 201; i <= 224; ++i) app.game.items[i] = 1; // Vieg's weapons: 24 of 171
            openCamp(app);
            campKey(app, SDLK_PAGEDOWN);
            bingoProblems += app.collection.open; // (not in the first scenario)
            app.game.scenario = 1;
            campKey(app, SDLK_PAGEDOWN);
            bingoProblems += !app.collection.open || !app.collection.tex.id || collection::percent(app) != 14 ||
                             collection::have(app, 0) != 24 || collection::have(app, 1) != 0;
            app.lastTicks = 0;
            drawFrame(app, false);
            collectionKey(app, SDLK_ESCAPE);
            bingoProblems += app.collection.open || !app.campOpen;
            app.campOpen = false;
            app.game = keepGame;
        }
        problems += bingoProblems;
        std::printf("manual: %d topics held, a page in the message window (%d pages, %d with the picture); scenarios: bingo cards "
                    "drawn, a cell opened from the Camp, the cards slid, the Collection's rate %s\n",
                    manualTopicsHeld, manualTextPages, manualPicturePages, bingoProblems ? "NO" : "yes");
        std::printf("saves: save -> title Start -> load %s (map %d, flags, items, vars, Oz, party, play time, position); "
                    "resume point written, offered at the title and resumed, saved as you go %s; %d problems\n", loaded ? "ok" : "FAILED",
                    after.map, asYouGo ? "yes" : "NO", problems);
        mapErrors += problems;
        bootTitle(app);
        app.runner.stop();
    }
    { // Shops (Omega): the item table, then the Ark's shop (area 9: j001_t01's entry 251, Util 2)
      // opened by its shopkeeper's script: the first weapon bought as many as wanted, sold back
      // one for half, an armor worn at once (both staying in the bag), Talk (EvrStart 4 while the
      // script waits parked), leave
        int problems = 0;
        const cr::ItemParam* sword = app.itemTable.item(201);
        problems += app.itemTable.count() < 438 || !sword || sword->kind != cr::kKindWeapon || itemName(app, 201) == "item 201" ||
                    itemName(app, 1) == itemName(app, 2);
        cr::GameState g;
        problems += cr::shopLevel(g, 0) < 1 || cr::maxPurchase(450, 1000, 0) != 2 || cr::maxPurchase(10, 100000, 95) != 4;
        app.runner.stop();
        app.game = cr::GameState();
        app.mapNumber = -1;
        cr::WarpRequest w;
        w.valid = true;
        w.area = 9;
        w.map = 175;
        performWarp(app, w, -1);
        for (int f = 0; f < 3000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
            stepEvents(app);
            app.dialogue.clear();
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        problems += app.area != 9 || app.mapNumber != 175;
        app.runner.start(app.events, 251, app.game, app.area, app.mapNumber);
        for (int f = 0; f < 600 && app.runner.running() && !app.shop.open; ++f) {
            stepEvents(app);
            app.dialogue.clear();
            app.tapPending = true;
        }
        const bool opened = app.shop.open && app.runner.running();
        problems += !opened;
        app.game.oz = 100000;
        shopKey(app, SDLK_DOWN);   // Buy
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_DOWN);   // Weapons
        shopKey(app, SDLK_RETURN);
        problems += app.shop.phase != App::Shop::Member; // whose? (__Buy_CharChoiceProc)
        shopKey(app, SDLK_RETURN);
        const int weapon = app.shop.list.empty() ? 0 : app.shop.list[0];
        const int price = weapon ? shopUnitPrice(app, weapon) : 0;
        problems += app.shop.phase != App::Shop::List || weapon < 201 || weapon > 368 || price <= 0;
        shopKey(app, SDLK_RETURN); // how many, with Yes / No beside
        shopKey(app, SDLK_RIGHT), shopKey(app, SDLK_RIGHT); // 3
        const int count = app.shop.count;
        problems += app.shop.phase != App::Shop::Count || count != 3 || shopMessage(app).empty(); // (Omega's asks without the name)
        { // a touch held on > (_ComUty_NumCountProc): a step at once, none for 20 frames, then one a frame
            bool found = false;
            for (int y = 0; y < app.renderer.frameHeight() && !found; y += 4)
                for (int x = 0; x < app.renderer.frameWidth() && !found; x += 4) {
                    app.press = App::Press{true, kPressShop, float(x), float(y)};
                    found = shopPressIn(app, true, 446, 66, 16, 19);
                }
            const float tick = 1.0f / kFramesPerSecond;
            pressCountStep(app, tick);                                 // down: a step
            const int c1 = app.shop.count;
            for (int f = 0; f < 19; ++f) pressCountStep(app, tick);   // 19 frames held: none
            const int c20 = app.shop.count;
            for (int f = 0; f < 3; ++f) pressCountStep(app, tick);    // from the 20th: one a frame
            const int c23 = app.shop.count;
            app.press = App::Press();
            pressCountStep(app, tick);
            const bool held = found && c1 == count + 1 && c20 == count + 1 && c23 == count + 4;
            if (!held) std::printf("shops: a held > counted %d, %d, %d (want %d, %d, %d)\n", c1, c20, c23, count + 1, count + 1, count + 4);
            problems += !held;
            app.shop.count = count;
        }
        shopKey(app, SDLK_RETURN); // Yes
        if (app.shop.phase == App::Shop::EquipAsk) shopKey(app, SDLK_ESCAPE); // (not worn)
        problems += app.game.oz != 100000 - price * count || ownedCount(app, weapon) != count || app.shop.phase != App::Shop::List;
        // armor, worn at once: the bag keeps it (Omega's rule)
        shopKey(app, SDLK_ESCAPE); // (the pair's plates)
        shopKey(app, SDLK_ESCAPE);
        shopKey(app, SDLK_DOWN);   // Armor
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_RETURN); // the same character
        int armor = 0; // the first the board's member doesn't wear already
        for (size_t k = 0; k < app.shop.list.size() && !armor; ++k)
            if (app.shop.list[k] != app.game.party[size_t(shopCharacter(app))].equip[cr::kArmor])
                armor = app.shop.list[k], app.shop.listCursor = int(k);
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_RETURN); // one, Yes
        const bool asked = app.shop.phase == App::Shop::EquipAsk;
        if (asked) shopKey(app, SDLK_RETURN); // Yes, equip it
        const cr::PartyMember& wearer = app.game.party[size_t(shopCharacter(app))];
        problems += !armor || !asked || wearer.equip[cr::kArmor] != armor || ownedCount(app, armor) != 1;
        app.lastTicks = 0;
        drawFrame(app, false); // the board, the list
        ++frames;
        shopKey(app, SDLK_ESCAPE);
        shopKey(app, SDLK_ESCAPE);
        shopKey(app, SDLK_ESCAPE); // back to Talk / Buy / Sell
        shopKey(app, SDLK_DOWN);   // Sell
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_UP);     // Weapons
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_RETURN); // the same character
        const bool listed = std::find(app.shop.list.begin(), app.shop.list.end(), weapon) != app.shop.list.end();
        if (listed) app.shop.listCursor = int(std::find(app.shop.list.begin(), app.shop.list.end(), weapon) - app.shop.list.begin());
        const int ozBefore = app.game.oz;
        shopKey(app, SDLK_RETURN);
        shopKey(app, SDLK_RETURN); // one, for half the price
        const bool sold = listed && app.game.oz == ozBefore + shopUnitPrice(app, weapon) && ownedCount(app, weapon) == count - 1 &&
                          app.shop.phase == App::Shop::List;
        problems += !sold;
        app.lastTicks = 0;
        drawFrame(app, false);
        ++frames;
        shopKey(app, SDLK_ESCAPE);
        shopKey(app, SDLK_ESCAPE); // (the pair's plates)
        shopKey(app, SDLK_ESCAPE);
        shopKey(app, SDLK_UP);
        shopKey(app, SDLK_UP);     // Talk (its lines by the story's flags: 1121 for the earliest)
        app.game.setFlag(1121, true);
        shopKey(app, SDLK_RETURN);
        problems += app.shop.phase != App::Shop::Talk;
        int talkFrames = 0;
        for (; talkFrames < 3000 && app.shop.phase == App::Shop::Talk; ++talkFrames) {
            stepShop(app);
            stepEvents(app);
            app.dialogue.clear();
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        const size_t talkMessages = app.parkedRunner.result().messages.size();
        problems += app.shop.phase != App::Shop::Main || !app.runner.running() || talkMessages == 0;
        shopKey(app, SDLK_ESCAPE); // leave: the shopkeeper's script goes on to its end
        for (int f = 0; f < 600 && app.runner.running(); ++f) {
            stepEvents(app);
            app.dialogue.clear();
            app.tapPending = true;
        }
        problems += app.shop.open || app.runner.running();
        std::printf("shops: %d items; the Ark's shop opened by its script %s, bought %d of item %d and sold 1, an armor worn %s, "
                    "Talk played %zu messages in %d frames; %d problems\n", app.itemTable.count(), opened ? "ok" : "FAILED", count,
                    weapon, asked ? "at once" : "NOT offered", talkMessages, talkFrames, problems);
        mapErrors += problems;
        bootTitle(app);
        app.runner.stop();
    }
    { // The Camp menu's pages (CampMainPrc, read in the 3.1.0 code): the first Items tap shows 56016
      // once; seeds and an MP item used on a member, each followed by the board until a tap (a
      // seed's stat marked); no Back during the Yes / No, a full MP or a stat at 9999 refused there
      // with it staying up; a weapon worn (on the Ark's maps refilling the pair), the worn one
      // refused, the slot list's arrows taking the other member; the Status page's board ->
      // equipment -> skills, drawn without errors
        int problems = 0;
        auto chk = [&](bool bad, const char* what) { if (bad) { ++problems; std::printf("camp: not as expected: %s\n", what); } };
        app.game = cr::GameState();
        app.playerChar = 0;
        app.game.items = {{3, 3}, {24, 2}, {21, 1}, {202, 1}, {4, 2}};
        app.game.party[0].mp = 10;
        app.dialogue.clear();
        openCamp(app);
        auto key = [&](SDL_Keycode k) { campKey(app, k); while (app.camp.pageOut) stepCamp(app); }; // (Back to the main page: its frames run)
        auto lastSound = [&] { return app.menuSoundLog.empty() ? 0u : app.menuSoundLog.back(); };
        App::Camp& c = app.camp;
        chk(c.notice != 0, "a message at open without a plate 481..");
        const int atk0 = cr::maxStat(app.game.party[0], 0, cr::kAttack, app.itemTable);
        c.cursor = 0, key(SDLK_RETURN);                       // Items, the first time: 56016 (flag 0x39) first
        chk(c.notice != 56016 || !app.game.flag(0x39) || c.page != App::Camp::Main || app.dialogue.empty(), "56016 before Items");
        for (int k = 0; k < 40 && !app.dialogue.empty(); ++k) key(SDLK_RETURN); // (read: taps)
        stepCamp(app);
        chk(c.notice != 0 || c.page != App::Camp::Items || c.list.empty() || c.list[0] != 3, "Items after 56016: Power Seed first");
        key(SDLK_ESCAPE), c.cursor = 0, key(SDLK_RETURN);     // (once only)
        chk(c.notice != 0 || c.page != App::Camp::Items, "56016 once only");
        key(SDLK_RETURN), key(SDLK_RETURN);                   // on Escher: how many?
        chk(c.phase != App::Camp::Ask, "the Yes / No");
        key(SDLK_ESCAPE);                                     // (no Back bar here; the Vita's × answers No: the list)
        chk(c.phase != App::Camp::List || lastSound() != se::kChoose, "Esc during the Yes / No: No");
        key(SDLK_RETURN), key(SDLK_RETURN);                   // Escher again: how many?
        chk(c.phase != App::Camp::Ask, "the Yes / No again");
        key(SDLK_RIGHT), key(SDLK_RIGHT), key(SDLK_RIGHT);    // 3 (the bag's all)
        chk(c.count != 3, "count 3");
        key(SDLK_RETURN);                                     // Yes: 0xa5, then the board
        const int atk1 = cr::maxStat(app.game.party[0], 0, cr::kAttack, app.itemTable);
        chk(atk1 != atk0 + 30 || app.game.party[0].seeds[0] != 30 || ownedCount(app, 3) != 0, "3 seeds: +30"); // 10 a seed
        chk(c.phase != App::Camp::Result || c.resultItem != 3 || c.resultCount != 3 || lastSound() != campui::kSoundUse, "the board after the seeds");
        int frames = 0;
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_RETURN);                                     // the tap (0x93): the list again
        chk(c.phase != App::Camp::List || lastSound() != se::kChoose, "the list after the board");
        auto pick = [&](int it) {
            const auto at = std::find(c.list.begin(), c.list.end(), it);
            if (at != c.list.end()) c.listCursor = int(at - c.list.begin());
            return at != c.list.end();
        };
        chk(!pick(24), "Plum listed");                        // Plum: MP + 100
        key(SDLK_RETURN), key(SDLK_RETURN), key(SDLK_RETURN);
        chk(app.game.party[0].mp != std::min(110, cr::memberStat(app.game, 0, cr::kMp, app.itemTable)) ||
                ownedCount(app, 24) != 1 || c.phase != App::Camp::Result, "Plum: MP 10 + 100, capped at the most"); // 10 + 100, capped at the most
        key(SDLK_RETURN);
        const int mpAfterPlum = app.game.party[0].mp;
        app.game.party[0].mp = -1;                            // full: refused at Yes (0x95), the Yes / No staying
        chk(!pick(24), "Plum listed again");
        key(SDLK_RETURN), key(SDLK_RETURN), key(SDLK_RETURN);
        chk(c.phase != App::Camp::Ask || ownedCount(app, 24) != 1 || lastSound() != se::kBuzzer, "Plum refused on full MP");
        key(SDLK_DOWN), key(SDLK_RETURN);                     // No: the list
        chk(c.phase != App::Camp::List, "No: the list");
        app.game.party[0].seeds[1] = cr::kMaxStatBonus;       // Defense at 9999: the count still 1, refused at Yes
        chk(!pick(4), "the Defense seed listed");
        key(SDLK_RETURN), key(SDLK_RETURN);
        chk(c.phase != App::Camp::Ask || campSeedLimit(app, 0, 4) != 1, "a full stat's count");
        key(SDLK_RETURN);
        chk(c.phase != App::Camp::Ask || ownedCount(app, 4) != 2 || lastSound() != se::kBuzzer, "a seed refused at 9999");
        key(SDLK_DOWN), key(SDLK_RETURN);
        app.game.party[0].seeds[1] = 0;
        chk(!pick(21), "Chocolate listed");                   // HP items do nothing on the field
        key(SDLK_RETURN);
        chk(c.phase != App::Camp::List || ownedCount(app, 21) != 1, "an HP item refused");
        for (int f = 0; f < 3; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_ESCAPE);
        const int keepMap = app.mapNumber;
        app.mapNumber = 0xa9;                                 // the Ark: a change refills the pair
        app.game.party[0].mp = 5, app.game.party[size_t(campCharacter(app, 1))].mp = 5;
        c.cursor = 1, key(SDLK_RETURN), key(SDLK_RETURN);   // Equip: Escher's weapon
        key(SDLK_RETURN);
        chk(c.phase != App::Camp::List || c.list.empty() || c.list[0] != 202, "Escher's weapons");
        for (int f = 0; f < 3; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_RETURN);                                     // wear the Bastard Sword (the bag as it was)
        chk(app.game.party[0].equip[0] != 202 || ownedCount(app, 201) != 0 || ownedCount(app, 202) != 1, "the Bastard Sword worn");
        chk(app.game.party[0].mp != cr::memberStat(app.game, 0, cr::kMp, app.itemTable) ||
                app.game.party[size_t(campCharacter(app, 1))].mp != cr::memberStat(app.game, campCharacter(app, 1), cr::kMp, app.itemTable),
            "refilled on the Ark");
        app.mapNumber = keepMap;
        key(SDLK_RETURN), key(SDLK_RETURN);                   // the weapon again: the one worn refused (kind 5)
        chk(c.phase != App::Camp::List || app.game.party[0].equip[0] != 202 || lastSound() != se::kBuzzer, "the worn one refused");
        key(SDLK_ESCAPE);
        key(SDLK_RIGHT);                                      // the slot list's arrow: Musiea's slots
        chk(c.phase != App::Camp::Slot || c.member != 1 || lastSound() != se::kPage, "the slots' arrow");
        key(SDLK_LEFT); // (while the windows change - CampMainPrc 7 -> 10 - nothing is taken)
        chk(c.member != 1, "the slots' arrow held while the windows change");
        for (int f = 0; f < 9; ++f) stepCamp(app);
        key(SDLK_LEFT);
        chk(c.member != 0, "the slots' arrow back");
        app.shop.selling = true;                              // worn: none of it to sell
        problems += shopMaxCount(app, 202) != 0;
        app.shop.selling = false;
        { // an older save: what the records wear put in their scenario's bag
            cr::GameState old;
            for (auto& pm : old.party) pm.equip[0] = pm.equip[1] = pm.equip[2] = 0;
            old.party[4].equip[0] = 201;
            old.party[7].equip[0] = 229;
            old.items[201] = 1;
            cr::putWornInBag(old, app.itemTable);
            problems += old.items[201] != 1 || old.otherBag[229] != 1 || old.items.count(229) || !old.wornInBag;
        }
        key(SDLK_ESCAPE), key(SDLK_ESCAPE);                   // the members, the Camp's front
        chk(c.page != App::Camp::Main, "back to the front");
        c.cursor = 3, key(SDLK_RETURN), key(SDLK_RETURN);    // Status: Escher's board
        chk(c.page != App::Camp::Status || c.phase != App::Camp::Slot || c.statusView != 0 || c.member != 0, "Escher's board");
        key(SDLK_RIGHT);                                      // -2: the equipment (58100), the seven worn
        chk(c.statusView != 1 || c.list.size() != 7 || c.list[0] != 202 || c.list[2] != 0x1c6 || lastSound() != se::kPage, "the equipment list");
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_RIGHT);                                      // the skills: the worn plates' known entries
        std::vector<int> known;
        for (int g : app.game.party[0].genes)
            if (g > 0)
                for (int e : cr::plateContents(g))
                    if (e > 0 && ownedCount(app, e) > 0 && std::find(known.begin(), known.end(), e) == known.end()) known.push_back(e);
        chk(c.statusView != 2 || c.list != known, "the skill list");
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_RIGHT);                                      // ... and the board again
        chk(c.statusView != 0, "the board after the skills");
        key(SDLK_LEFT);                                       // -3 the other way: the skills
        chk(c.statusView != 2, "-3 from the board");
        key(SDLK_ESCAPE);                                     // Back: the member choice
        chk(c.phase != App::Camp::Member || c.statusView != 0, "Back to the member choice");
        key(SDLK_RIGHT), key(SDLK_RETURN);                    // Musiea's
        chk(c.page != App::Camp::Status || c.member != 1 || c.phase != App::Camp::Slot, "Musiea's board");
        for (int f = 0; f < 3; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        const int errs = app.renderer.takeErrors();
        key(SDLK_ESCAPE), key(SDLK_ESCAPE), key(SDLK_ESCAPE);
        for (int f = 0; f < 5; ++f) stepCamp(app); // (Back on the main page: four frames of the windows going)
        problems += app.campOpen + errs;
        std::printf("camp: 56016 first, 3 seeds +%d attack, Plum to MP %d, full MP / 9999 refused, Bastard Sword worn (refilled "
                    "on the Ark), the slots' arrows, status board / \"%s\" / \"%s\"; %d frames, %d render API errors; %d problems\n",
                    atk1 - atk0, mpAfterPlum, sysText(app, 58100).c_str(), sysText(app, 58101).c_str(), frames, errs, problems);
        mapErrors += problems;
        glErrors += errs;
        app.game = cr::GameState();
    }
    { // The Camp's gene plates (CampMainPrc 0x17-0x1f and the Genes page 2-6, read in the 3.1.0
      // code): a plate is chosen (0x93) and its known entries listed with the board in picture
      // mode; only the list's Equip row wears it (0xa6) and Back returns to the plate list. The
      // plate list holds the bag's plates with a known entry, one the other member wears included,
      // Remove last and only while the slot is worn (acting at once); the slot list refuses the
      // empty plate slots 2-4 until a common skill is known and, in the first scenario, the Human
      // Genes 474..476; the Genes page lists every plate in the bag, refuses one without a known
      // entry and opens the others' entries. Jewels: Remove last; the Items page leaves out the
      // held-back member's one worn jewel.
        int problems = 0;
        auto chk = [&](bool bad, const char* what) { if (bad) { ++problems; std::printf("camp plates: not as expected: %s\n", what); } };
        app.game = cr::GameState();
        app.playerChar = 0;
        app.game.scenario = 0;
        auto on = [](int plate, int e) { // e among the plate's entries
            const auto& pc = cr::plateContents(plate);
            return std::find(pc.begin(), pc.end(), e) != pc.end();
        };
        // (every entry of the plates 481.. is a "common" one, 89..113 / 139..186: the lock opens with
        // the first gene learned from one) plate 481's first entry and plate 483's skill, neither
        // on 474, 475 or 482
        const int e481 = 108, common = 146, commonPlate = 483;
        chk(!on(481, e481) || !on(commonPlate, common) || on(482, e481) || on(482, common) || on(474, e481) || on(475, e481) ||
                on(474, common) || on(475, common) || on(481, common),
            "the test plates' entries");
        app.game.items = {{481, 1}, {482, 1}, {474, 1}, {475, 1}, {commonPlate, 1}}; // the plates, nothing known yet
        for (auto& pm : app.game.party) pm.genes[0] = pm.genes[1] = pm.genes[2] = pm.genes[3] = 0;
        app.game.party[0].genes[0] = 474;                      // Escher's Human Genes (scenario 0: 474..476 are refused)
        app.game.party[1].genes[1] = 481;                      // Musiea wears 481: listed for Escher all the same
        app.dialogue.clear();
        openCamp(app);
        auto key = [&](SDL_Keycode k) { campKey(app, k); while (app.camp.pageOut) stepCamp(app); };
        App::Camp& c = app.camp;
        // CampMainSet: a plate 0x1e1.. in the bag the first time - 56010 (flag 0x38) before the Camp
        chk(c.notice != 56010 || !app.game.flag(0x38) || app.dialogue.empty(), "56010 at the first open with a plate");
        app.lastTicks = 0;
        drawFrame(app, false);
        bool maskAtOnce = false;
        chk(menuMaskWanted(app, maskAtOnce) != -1, "no mask under 56010");
        for (int k = 0; k < 40 && !app.dialogue.empty(); ++k) key(SDLK_RETURN);
        stepCamp(app);
        chk(c.notice != 0 || c.page != App::Camp::Main, "the Camp after 56010");
        openCamp(app);
        chk(c.notice != 0, "56010 once only");
        c.cursor = 1, key(SDLK_RETURN), key(SDLK_RETURN);     // Equip, Escher: the slot list
        chk(c.phase != App::Camp::Slot, "c.phase != App::Camp::Slot");
        c.slot = 3, key(SDLK_RETURN);                         // Human Genes 474 in the first scenario: refused
        chk(c.phase != App::Camp::Slot, "c.phase != App::Camp::Slot");
        c.slot = 4, key(SDLK_RETURN);                         // the empty plate slot 2: shut until a common skill is known
        chk(c.phase != App::Camp::Slot || campCommonSkill(app), "c.phase != App::Camp::Slot || campCommonSkill(app)");
        app.game.items[e481] = 1, app.game.items[common] = 1;  // ... known now (their plates are in the bag)
        chk(!campCommonSkill(app), "!campCommonSkill(app)");
        key(SDLK_RETURN);                                      // 481 (known entry), the common skill's plate; not 482, no Remove
        chk(c.phase != App::Camp::List || c.list != std::vector<int>{481, commonPlate}, "c.phase != App::Camp::List || c.list != std::vector<int>{481, commonPlate}");
        key(SDLK_RETURN);                                      // 481 chosen: its known entries, nothing worn yet
        chk(c.phase != App::Camp::Entries || c.plate != 481 || c.list != std::vector<int>{e481} || app.game.party[0].genes[1] != 0, "c.phase != App::Camp::Entries || c.plate != 481 || c.list != std::vector<int>{e481} || app.game.party[0].genes[1] != 0");
        int frames = 0;
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_ESCAPE);                                      // Back: the plate list again
        chk(c.phase != App::Camp::List || c.listCursor != 0 || c.list.size() != 2, "c.phase != App::Camp::List || c.listCursor != 0 || c.list.size() != 2");
        key(SDLK_RETURN), key(SDLK_RETURN);                    // Equip: worn
        chk(app.game.party[0].genes[1] != 481 || c.phase != App::Camp::Slot, "app.game.party[0].genes[1] != 481 || c.phase != App::Camp::Slot");
        key(SDLK_RETURN);                                      // the worn slot's list: Remove last
        chk(c.phase != App::Camp::List || c.list != std::vector<int>{481, commonPlate, campui::kRemove}, "c.phase != App::Camp::List || c.list != std::vector<int>{481, commonPlate, campui::kRemove}");
        key(SDLK_RETURN), key(SDLK_RETURN);                    // 481 again: no Equip row while Escher wears it
        chk(c.phase != App::Camp::Entries || app.game.party[0].genes[1] != 481, "c.phase != App::Camp::Entries || app.game.party[0].genes[1] != 481");
        key(SDLK_ESCAPE);
        c.listCursor = 2, key(SDLK_RETURN);                    // Remove: off at once
        chk(app.game.party[0].genes[1] != 0 || c.phase != App::Camp::Slot, "app.game.party[0].genes[1] != 0 || c.phase != App::Camp::Slot");
        app.game.party[0].genes[1] = commonPlate, key(SDLK_RETURN); // a worn slot's list opens on what it wears (SetListPosFromItemNo)
        chk(c.phase != App::Camp::List || c.listCursor != 1 || campScrollPx(app) != campui::kRowH, "the worn plate on the focus line, settled");
        key(SDLK_ESCAPE), app.game.party[0].genes[1] = 0;
        app.game.party[0].genes[0] = 477;                      // another Human Genes: the slot opens on 474.., Remove last
        c.slot = 3, key(SDLK_RETURN);
        chk(c.phase != App::Camp::List || c.list != std::vector<int>{474, 475, campui::kRemove}, "c.phase != App::Camp::List || c.list != std::vector<int>{474, 475, campui::kRemove}");
        c.listCursor = 1, key(SDLK_RETURN);                    // 475: no known entry, refused (kind 6)
        chk(c.phase != App::Camp::List, "c.phase != App::Camp::List");
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_ESCAPE), key(SDLK_ESCAPE), key(SDLK_ESCAPE);  // the list, the slots, the members: the Camp's front
        chk(c.page != App::Camp::Main || !app.campOpen, "c.page != App::Camp::Main || !app.campOpen");
        c.cursor = 2, key(SDLK_RETURN);                        // Genes: every plate in the bag, in id order
        chk(c.page != App::Camp::Genes || c.list != std::vector<int>{474, 475, 481, 482, commonPlate}, "c.page != App::Camp::Genes || c.list != std::vector<int>{474, 475, 481, 482, commonPlate}");
        c.listCursor = 3, key(SDLK_RETURN);                    // 482: nothing known, refused
        chk(c.phase != App::Camp::List, "c.phase != App::Camp::List");
        c.listCursor = 2, key(SDLK_RETURN);                    // 481: its entries
        chk(c.phase != App::Camp::Entries || c.plate != 481 || c.list != std::vector<int>{e481}, "c.phase != App::Camp::Entries || c.plate != 481 || c.list != std::vector<int>{e481}");
        for (int f = 0; f < 2; ++f) { app.lastTicks = 0; drawFrame(app, false); ++frames; }
        key(SDLK_RETURN);                                      // (no Equip row here)
        chk(c.phase != App::Camp::Entries || app.game.party[0].genes[1] != 0, "c.phase != App::Camp::Entries || app.game.party[0].genes[1] != 0");
        key(SDLK_ESCAPE);
        chk(c.phase != App::Camp::List || c.listCursor != 2, "c.phase != App::Camp::List || c.listCursor != 2");
        app.game.items[369] = 1, app.game.items[370] = 1;      // jewels: Remove last while one is worn
        app.game.party[0].equip[cr::kJewel] = 370;
        const auto jewels = campEquipList(app, 0, cr::kJewel);
        chk(jewels.size() < 2 || jewels.back() != campui::kRemove || std::find(jewels.begin(), jewels.end(), 369) == jewels.end(), "jewels.size() < 2 || jewels.back() != campui::kRemove || std::find(jewels.begin(), jewels.end(), 369) == jewels.end()");
        { // (__IsJewelChoiceSubCheck: the partner's one copy listed, refused; a second copy free)
            const int partner = campCharacter(app, 1);
            const int keepJewel = app.game.party[size_t(partner)].equip[cr::kJewel];
            app.game.items[371] = 1, app.game.party[size_t(partner)].equip[cr::kJewel] = 371;
            const auto list = campEquipList(app, campCharacter(app, 0), cr::kJewel);
            const bool listed = std::find(list.begin(), list.end(), 371) != list.end();
            const bool refused = !campJewelFree(app, 0, 371);
            app.game.items[371] = 2;
            const bool second = campJewelFree(app, 0, 371);
            chk(!listed || !refused || !second, "a jewel the partner wears: listed, refused while it is the one copy");
            app.game.items.erase(371), app.game.party[size_t(partner)].equip[cr::kJewel] = keepJewel;
        }
        app.game.setFlag(0xfed, true);                         // Musiea held back wearing 369: its one copy stays out of the bag's list
        app.game.party[1].equip[cr::kJewel] = 369;
        auto bag = campItems(app, 4);
        chk(std::find(bag.begin(), bag.end(), 369) != bag.end() || std::find(bag.begin(), bag.end(), 370) == bag.end(), "std::find(bag.begin(), bag.end(), 369) != bag.end() || std::find(bag.begin(), bag.end(), 370) == bag.end()");
        app.game.items[369] = 2;                               // a second copy: listed
        bag = campItems(app, 4);
        chk(std::find(bag.begin(), bag.end(), 369) == bag.end(), "std::find(bag.begin(), bag.end(), 369) == bag.end()");
        app.game.setFlag(0xfed, false);
        const std::string removeName = itemName(app, campui::kRemove), equipName = sysText(app, 58059);
        chk(removeName.rfind("item ", 0) == 0 || equipName.empty(), "removeName.rfind(\"item \", 0) == 0 || equipName.empty()");
        const int errs = app.renderer.takeErrors();
        key(SDLK_ESCAPE), key(SDLK_ESCAPE);
        for (int f = 0; f < 5; ++f) stepCamp(app); // (the windows going)
        chk(app.campOpen + errs, "app.campOpen + errs");
        std::printf("camp plates: common skill %d on plate %d, \"%s\" / \"%s\"; %d frames, %d render API errors; %d problems\n",
                    common, commonPlate, removeName.c_str(), equipName.c_str(), frames, errs, problems);
        mapErrors += problems;
        glErrors += errs;
        app.game = cr::GameState();
    }
    { // The Emergency Exit (item 1, read in the 3.1.0 code): __ChkCampItem passes it while
      // MuCom_CheckExitItem does - the scenario's flag (0x43e / 0x632) clear, the area not 1, 9 or 10 -,
      // else 0x95. Chosen it goes straight to its Yes / No; Yes (0x93, del_item) leaves the windows up
      // through CampMainPrc 7, 0xf-0x12 and CampMain 9, takes them off (0xd), fades to white with
      // 0x128 (0xe) and closes (0x10): CampMainClr refills the pair and warps to the scenario's Ark
      // (area 9 map 0xa9, 0xbc after flag 0x4f4; area 10 map 0xc0) at (0, 7.5, 15), whose area
      // script runs (load flags 0x26)
        int problems = 0;
        auto chk = [&](bool bad, const char* what) { if (bad) { ++problems; std::printf("camp exit: not as expected: %s\n", what); } };
        auto lastSound = [&] { return app.menuSoundLog.empty() ? 0u : app.menuSoundLog.back(); };
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar, keepIndex = int(app.mapIndex), keepMap = app.mapNumber, keepArea = app.area;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.setFlag(0x38, true), app.game.setFlag(0x39, true);
        app.game.items[1] = 2;
        app.playerChar = 0;
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        app.dialogue.clear();
        const int fromArea = app.area;
        chk(!campUsable(app, 1), "usable in area 2");
        app.game.setFlag(0x43e, true);
        chk(campUsable(app, 1), "refused with flag 0x43e");
        app.game.setFlag(0x43e, false);
        for (int a : {1, 9, 10}) {
            const int keep = app.area;
            app.area = a;
            chk(campUsable(app, 1), "refused in areas 1, 9, 10");
            app.area = keep;
        }
        app.game.scenario = 1;
        app.game.setFlag(0x632, true);
        chk(campUsable(app, 1), "refused with flag 0x632 in the second scenario");
        app.game.setFlag(0x632, false);
        app.game.scenario = 0;
        openCamp(app);
        App::Camp& c = app.camp;
        auto key = [&](SDL_Keycode k) { campKey(app, k); while (app.camp.pageOut) stepCamp(app); };
        c.cursor = 0, key(SDLK_RETURN);
        chk(c.page != App::Camp::Items || c.list.empty() || c.list[0] != 1, "the Exit first in Standard");
        app.game.setFlag(0x43e, true);
        key(SDLK_RETURN);
        chk(c.phase != App::Camp::List || lastSound() != se::kBuzzer, "the Exit refused (0x95) with flag 0x43e");
        app.game.setFlag(0x43e, false);
        key(SDLK_RETURN);                                      // straight to its Yes / No
        chk(c.phase != App::Camp::Ask, "the Exit's Yes / No");
        int frames = 0;
        app.lastTicks = 0, drawFrame(app, false), ++frames;
        key(SDLK_DOWN), key(SDLK_RETURN);                      // No: kept
        chk(c.phase != App::Camp::List || ownedCount(app, 1) != 2, "No to the Exit");
        app.game.party[0].hp = app.game.party[0].mp = 1;
        key(SDLK_RETURN), key(SDLK_RETURN);                    // Yes
        chk(c.phase != App::Camp::Exit || ownedCount(app, 1) != 1 || lastSound() != se::kChoose, "Yes to the Exit");
        key(SDLK_ESCAPE);
        chk(!app.campOpen || c.phase != App::Camp::Exit, "Esc during the Exit");
        bool shown = false, hidden = false, white = false;
        int t = 0;
        for (; t < 60 && app.campOpen; ++t) {
            stepCamp(app);
            bool atOnce = false;
            if (app.campOpen && c.exitT == kCampExitHide - 1) shown = menuMaskWanted(app, atOnce) == 1, app.lastTicks = 0, drawFrame(app, false), ++frames;
            if (app.campOpen && c.exitT == kCampExitHide) hidden = menuMaskWanted(app, atOnce) == -1, app.lastTicks = 0, drawFrame(app, false), ++frames;
            if (app.campOpen && c.exitT == kCampExitFade) white = app.fadeGrey == 1 && app.fadeFrom == 0 && app.fadeTo == 1 && lastSound() == 0x128;
        }
        const bool ark = !app.campOpen && app.area == 9 && app.mapNumber == 0xa9 && fromArea != 9 && t == kCampExitWarp;
        chk(!shown || !hidden || !white, "the windows, then none, then white with 0x128");
        chk(!ark, "the Ark (area 9, map 0xa9) after 30 frames");
        chk(app.game.party[0].hp != -1 || app.game.party[0].mp != cr::memberStat(app.game, 0, cr::kMp, app.itemTable), "the pair refilled");
        chk(app.pendingEntry != App::Entry::Area, "the area's script next");
        chk(app.fadeGrey != 1 || app.fadeFrom != 1 || app.fadeTo != 0, "the field from the white");
        const int arkArea = app.area, arkMap = app.mapNumber;
        auto exitTo = [&](int scenario, bool flag4f4) { // the other two Arks (CampMainClr's table)
            app.game.scenario = scenario;
            app.game.setFlag(0x4f4, flag4f4);
            app.campOpen = true, c.phase = App::Camp::Exit;
            campExitWarp(app);
            return std::make_pair(app.area, app.mapNumber);
        };
        const auto later = exitTo(0, true), second = exitTo(1, false);
        chk(later != std::make_pair(9, 0xbc) || second != std::make_pair(10, 0xc0), "the Ark after flag 0x4f4, the second scenario's");
        const int errs = app.renderer.takeErrors();
        std::printf("camp exit: refused by flag / area, Yes -> area %d map %d (flag 0x4f4: %d %d, second scenario: %d %d) after %d "
                    "frames; %d drawn, %d render API errors; %d problems\n", arkArea, arkMap, later.first, later.second,
                    second.first, second.second, t, frames, errs, problems + errs);
        mapErrors += problems + errs;
        glErrors += errs;
        app.campOpen = false;
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
        app.fadeGrey = app.fadeFrom = app.fadeTo = 0, app.fadeT = app.fadeFrames;
        app.game = keepGame;
        app.playerChar = keepChar;
        if (keepMap >= 0) { // (the field the tests before left)
            showMap(app, keepIndex, keepMap, keepArea);
            app.pendingEntry = App::Entry::None;
            app.runner.stop();
        }
    }
    { // The field menu (menuf_mainPrc): the corner button slides the bar out and back in 4 frames;
      // skill 170 known adds the encounter switch (per pair, refused under flag 4076), and while
      // it's off the walked distance doesn't count; skill 174 worn banks 1 EXP every 10 units,
      // paid into the next win; both are kept in the save
        int problems = 0;
        app.game = cr::GameState();
        app.playerChar = 0;
        app.menuSoundLog.clear();
        App::FieldMenu& fm = app.fieldMenu;
        fm = App::FieldMenu();
        toggleFieldMenu(app);
        float x0, y0, x4, y4;
        fieldButtonPos(app, 0, x0, y0);
        for (int f = 0; f < 4; ++f) stepFieldMenu(app);
        fieldButtonPos(app, 0, x4, y4);
        problems += !fm.open || fm.anim != 0 || x0 != 460 || y0 != -28 || x4 != 432 || y4 != 32;
        problems += fieldButtonShown(app, 3); // not known yet
        pressFieldButton(app, 3);
        problems += app.game.noEncounters[0];
        app.game.items[170] = 1;
        pressFieldButton(app, 3); // off
        const float d0 = app.encounters.walkedDistance();
        fieldWalked(app, 50);
        problems += !app.game.noEncounters[0] || app.encounters.walkedDistance() != d0;
        app.game.scenario = 1; // the other scenario's switch is its own
        problems += fieldEncountersOff(app);
        app.game.scenario = 0;
        app.game.setFlag(4076, true);
        pressFieldButton(app, 3); // refused
        problems += !app.game.noEncounters[0];
        app.game.setFlag(4076, false);
        pressFieldButton(app, 3); // on again
        fieldWalked(app, 50);
        problems += app.game.noEncounters[0] || app.encounters.walkedDistance() != d0 + 50;
        problems += expectSounds("field menu, encounter switch", {8, 3, 4, 2});
        toggleFieldMenu(app);
        for (int f = 0; f < 4; ++f) stepFieldMenu(app);
        fieldButtonPos(app, 3, x4, y4);
        problems += fm.open || x4 != 460 || y4 != -28;
        app.menuSoundLog.clear();
        // skill 174 on Escher (plate 499): 100 one-unit steps and a 25-unit one
        app.game.items[174] = 1, app.game.items[499] = 1;
        app.game.party[0].genes[1] = 499;
        app.walkExpDistance = 0;
        for (int k = 0; k < 100; ++k) fieldWalked(app, 1);
        const int bank100 = app.game.walkExp[0];
        fieldWalked(app, 25);
        fieldWalked(app, 0.5f);
        const int bank = app.game.walkExp[0];
        problems += bank100 != 10 || bank != 12;
        // kept in the save
        cr::SaveData sd = captureSave(app);
        const std::string path = savePath(app, "fieldmenu-test.dat");
        cr::SaveData back;
        problems += !cr::writeSave(path, sd) || !cr::readSave(path, back) || back.game.walkExp[0] != bank ||
                    back.game.noEncounters != sd.game.noEncounters;
        sd.game.noEncounters[3] = true;
        problems += !cr::writeSave(path, sd) || !cr::readSave(path, back) || !back.game.noEncounters[3];
        SDL_RemovePath(path.c_str());
        // paid into a win, before the Apples double it
        int paid = -1;
        startBattle(app, {1, 0, -1, 0}, 0);
        if (app.battle.active) {
            App::Battle& b = app.battle;
            for (int i = 2; i < 5; ++i) b.rules.f[size_t(i)].hp = 0;
            b.phase = App::Battle::Acting;
            b.turnOn = false;
            const int before = b.rules.expGained;
            endRound(app);
            for (int f = 0; f < 10 && b.phase == App::Battle::Won; ++f) stepBattle(app); // (1000: ten frames, then 0x3e9 settles the win)
            paid = b.rules.expGained - before;
            endBattle(app, true);
            app.runner.stop();
        }
        problems += paid != bank || app.game.walkExp[0] != 0;
        // The map screen: the button only in areas 2..5 and 8; every spot, D07's rooms' icons and the
        // story's markers on the 480 x 320 map; area 2's map 0 with the story at step 3; D07's three
        // pages, a tap each
        int spots = 0, offMap = 0, mapButton = 0;
        for (int area = 0; area < cr::kNumAreas; ++area) {
            const int keepArea = app.area;
            app.area = area;
            mapButton |= int(fieldButtonShown(app, 2)) << area;
            app.area = keepArea;
        }
        for (const auto& sp : mapui::kMapSpot)
            if (sp[0] >= 0) ++spots, offMap += sp[0] > 480 || sp[1] > 320; // (map 149's marker reaches over the top)
        for (int room = 0; room < mapui::kRooms; ++room) {
            const short* sp = mapui::kMapSpot[mapui::kFirstRoom + room];
            offMap += sp[0] < 8 || sp[0] + 8 > 480 || sp[1] + 26 > 320;
        }
        for (const auto& gate : mapui::kMapMark)
            for (const auto& step : gate)
                for (const auto& mk : step)
                    offMap += mk[0] >= 0 && (mk[0] - 8 < 0 || mk[0] + 8 > 480 || mk[1] - 16 < 0 || mk[1] > 320);
        const int keepMap = app.mapNumber, keepArea = app.area;
        app.mapNumber = 0, app.area = 2;
        app.game.setFlag(mapui::kFlagBase + 3, true);
        app.menuSoundLog.clear();
        fm.open = true, fm.anim = 0;
        pressFieldButton(app, 2);
        const App::MapScreen& ms = app.mapScreen;
        problems += !ms.open || ms.field != 0 || !ms.tex.id || ms.spot[0] != 48 || ms.spot[1] != 196 ||
                    ms.marks[0][0] != 84 || ms.marks[1][0] != 104 || mapButton != 0x13c;
        for (int f = 0; f < 6; ++f) stepMapScreen(app);
        app.lastTicks = 0;
        drawFrame(app, false);
        mapScreenTap(app); // closes
        for (int f = 0; f < 4; ++f) stepMapScreen(app);
        problems += ms.open;
        app.mapNumber = mapui::kFirstRoom, app.area = 8; // D07: three pages
        openMapScreen(app);
        problems += !ms.open || ms.field != mapui::kMazeField || !ms.tex.id || ms.spot[0] != 94 || ms.spot[1] != 244 ||
                    ms.marks[0][0] != -1;
        int pages = 0;
        for (int tap = 0; tap < 3; ++tap) {
            for (int f = 0; f < 4; ++f) stepMapScreen(app);
            pages += ms.page == tap;
            app.lastTicks = 0;
            drawFrame(app, false);
            mapScreenTap(app);
        }
        for (int f = 0; f < 4; ++f) stepMapScreen(app);
        const int mapErrs = app.renderer.takeErrors();
        problems += ms.open + mapErrs + expectSounds("map screen", {2, 2, 2, 2, 2}) + offMap + (spots != 118) + (pages != 3);
        app.mapNumber = keepMap, app.area = keepArea;
        std::printf("field menu: bar slides out and back, encounter switch per scenario (flag 4076 refuses it), skill 174 "
                    "banked %d EXP, %d paid into a win; map screen: %d spots, %d off the map; %d problems\n", bank, paid,
                    spots, offMap, problems);
        mapErrors += problems;
        app.game = cr::GameState();
        fm = App::FieldMenu();
    }
    { // Puzzle rooms (Omega's 16: 0..3 teleport, 4..7 psychokinesis, 8..11 toughness, 12..15
      // pyrokinesis). The banner as a gate's way in shows it (flag 3090); stage 0: two blocks
      // teleported onto the weigh switches bring the crystal, ESP 3 -> 1, at 0 a swap is refused;
      // stage 4: eight slides put a block on the switch (the other its stopper), a slide at an
      // edge is let go for nothing, one into the leader squashes the try; stage 8: a fragile
      // block broken (ESP 1 -> 0), a gem through its cell (ESP 1), at 0 a tap refused; stage 12:
      // a candle switch lights, and lighting the rest brings the crystal
        int problems = 0;
        App::Puzzle& pz = app.puzzle;
        cr::WarpRequest w;
        w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap;
        auto settle = [&](int most) {
            for (int quiet = 0, f = 0; f < most && quiet < 10; ++f) {
                stepEvents(app);
                app.dialogue.clear();
                if (app.choiceFlag >= 0) answerChoice(app, true);
                app.tapPending = true;
                quiet = app.runner.running() || app.pendingEntry != App::Entry::None || pz.tpState ? 0 : quiet + 1;
            }
        };
        auto room = [&](int stageNo, bool banner) {
            app.runner.stop();
            app.game = cr::GameState();
            app.mapNumber = -1;
            if (banner) app.game.setFlag(3090, true); // (j001_p01 entry 0: the banner, then the flag off)
            w.map = cr::kFirstPuzzleMap + stageNo;
            performWarp(app, w, -1);
        };
        auto crystalShown = [&] {
            for (const App::Puzzle::Obj& o : pz.objs)
                if (o.type == cr::kTypeCrystal && o.shown) return true;
            return false;
        };
        auto steps = [&](int n) { for (int f = 0; f < n; ++f) stepEvents(app); };
        // The banner
        room(0, true);
        int bannerFrames = 0, bannerPeak = 0;
        float bannerX1 = 0;
        for (int f = 0; f < 3000 && (app.runner.running() || app.pendingEntry != App::Entry::None || app.puzzleBanner.on); ++f) {
            stepEvents(app);
            if (app.puzzleBanner.on) {
                ++bannerFrames;
                if (app.puzzleBanner.alpha > bannerPeak) bannerPeak = app.puzzleBanner.alpha, bannerX1 = app.puzzleBanner.x1;
                if (bannerFrames == 20) { // a look at it held (CR_PUZZLE_SHOT_BANNER: saved)
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    if (const char* path = SDL_getenv("CR_PUZZLE_SHOT_BANNER")) saveFrame(app, path);
                }
            }
            app.dialogue.clear();
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        const bool banner = bannerFrames >= 38 && bannerFrames <= 44 && bannerPeak == 255 && bannerX1 == 60 && !app.puzzleBanner.on &&
                            !app.game.flag(3090);
        problems += !banner;
        std::printf("puzzle room: \"Puzzle Start\" banner %s (%d frames, alpha up to %d, at x %.0f)\n",
                    banner ? "shown" : "WRONG", bannerFrames, bannerPeak, bannerX1);
        // Stage 0 (teleport)
        settle(3000);
        int blocks = 0, switches = 0;
        for (const App::Puzzle::Obj& o : pz.objs) blocks += o.type == cr::kTypeNormal, switches += o.type == cr::kTypeWeighSwitch;
        const bool start0 = pz.stage == 0 && blocks == 3 && switches == 2 && pz.esp == 3 && !crystalShown();
        auto swapOnto = [&](int sr, int sc, int br, int bc) { // the leader on (sr, sc) swaps with the block at (br, bc)
            setActor(app, 0, puzzle::centre(app, sr, sc), nullptr, false);
            const int b = puzzle::objectAt(app, 1, br, bc);
            if (b >= 0) puzzle::teleportTo(app, b);
            settle(3000);
            return b >= 0 && pz.objs[size_t(b)].row == sr && pz.objs[size_t(b)].col == sc;
        };
        const bool tp1 = swapOnto(0, 2, 1, 1);
        const bool onePressed = !puzzle::allSwitchesOn(app) && !crystalShown();
        const bool tp2 = swapOnto(4, 2, 2, 1);
        steps(3);
        const bool solved0 = tp1 && tp2 && onePressed && puzzle::allSwitchesOn(app) && crystalShown() && pz.esp == 1 &&
                             app.game.vars[13] == 2;
        pz.esp = 0;
        setActor(app, 0, puzzle::centre(app, 0, 0), nullptr, false);
        const int b3 = puzzle::objectAt(app, 1, 3, 1);
        if (b3 >= 0) puzzle::teleportTo(app, b3);
        const bool refused = b3 >= 0 && !pz.busy && pz.tpState == 0 && pz.punished > 0 && pz.objs[size_t(b3)].row == 3;
        for (int f = 0; f < 3; ++f) stepPuzzleHud(app);
        app.lastTicks = 0;
        drawFrame(app, false);
        const int errs = app.renderer.takeErrors();
        problems += !start0 || !solved0 || !refused || errs;
        std::printf("puzzle room: stage 0 (teleport) - start %s, two blocks onto the switches %s, at 0 ESP %s\n", start0 ? "right" : "WRONG",
                    solved0 ? "bring the crystal" : "DON'T bring the crystal", refused ? "refused" : "NOT refused");
        // Stage 4 (psychokinesis): A (3, 2), B (4, 4); the leader at (1, 1), out of every path
        room(4, false);
        settle(3000);
        int poseOk = 0, quakes = 0, slides = 0;
        auto slide = [&](int r, int c, int dir) { // dir 1 up, 2 down, 3 left, 4 right: the block's cell after it
            const int t = puzzle::objectAt(app, 1, r, c);
            if (t < 0) return std::pair<int, int>{-1, -1};
            puzzle::pkGrab(app, t);
            for (int f = 0; f < 20 && pz.objs[size_t(t)].pk != 2; ++f) stepEvents(app);
            updatePlayer(app, 0, 0); // the leader's pose while it's held
            poseOk += app.player.action == 3 && app.player.loopFrom == 2;
            puzzle::pkSend(app, dir);
            bool quaked = false;
            for (int f = 0; f < 200 && pz.pkState && pz.scene == 4; ++f) {
                stepEvents(app);
                quaked |= app.fieldQuake.mode == 1 && app.fieldQuake.frames == 8 && app.fieldQuake.amp == 0.5f;
            }
            ++slides, quakes += quaked;
            updatePlayer(app, 0, 0);
            return std::pair<int, int>{pz.objs[size_t(t)].row, pz.objs[size_t(t)].col};
        };
        using RC = std::pair<int, int>;
        const bool pkRoom = pz.stage == 4 && pz.esp == -1;
        setActor(app, 0, puzzle::centre(app, 1, 1), nullptr, false);
        const RC m1 = slide(3, 2, 3), m2 = slide(4, 4, 1);
        const RC edge = slide(0, 4, 1); // at the top already: let go, not counted
        const bool letGo = edge == RC{0, 4} && app.game.vars[13] == 2 && !pz.busy;
        const RC m3 = slide(0, 4, 3), m4 = slide(0, 0, 2), m5 = slide(2, 0, 4), m6 = slide(3, 0, 2), m7 = slide(4, 0, 4);
        const RC m8 = slide(2, 1, 2);
        const bool path = m1 == RC{3, 0} && m2 == RC{0, 4} && m3 == RC{0, 0} && m4 == RC{2, 0} && m5 == RC{2, 1} && m6 == RC{4, 0} &&
                          m7 == RC{4, 1} && m8 == RC{3, 1};
        const bool pressed4 = path && puzzle::allSwitchesOn(app) && app.game.vars[13] == 8;
        settle(3000); // the switch crystal's scene
        steps(3);
        const bool crystal4 = crystalShown();
        // the squash: a fresh room, the leader at (3, 1), A sent left through it
        room(4, false);
        settle(3000);
        setActor(app, 0, puzzle::centre(app, 3, 1), nullptr, false);
        slide(3, 2, 3);
        const bool squashed = pz.scene == 2;
        for (int f = 0; f < 40 && pz.scene != 4; ++f) stepEvents(app);
        const bool restored = pz.scene == 4 && puzzle::objectAt(app, 1, 3, 2) >= 0 && app.game.vars[13] == 0;
        problems += !pkRoom || !letGo || !pressed4 || !crystal4 || !squashed || !restored || poseOk < 8 || quakes < 8;
        std::printf("puzzle room: stage 4 (psychokinesis) - %d slides (holding pose %d, landing quakes %d), the path %s, at an edge %s, "
                    "the switch %s, crystal %s, squash %s, room %s\n", slides, poseOk, quakes, path ? "ok" : "WRONG",
                    letGo ? "let go free" : "COUNTED", pressed4 ? "pressed" : "NOT pressed", crystal4 ? "came" : "DIDN'T come",
                    squashed ? "resets" : "DIDN'T reset", restored ? "restored" : "NOT restored");
        // Stage 8 (toughness, ESP 1): from (0, 0) facing down the fragile block at (1, 0) breaks;
        // through it, the gem at (1, 1)
        room(8, false);
        settle(3000);
        auto walkTo = [&](int r, int c) {
            setActor(app, 0, puzzle::centre(app, r, c), nullptr, true);
            steps(3);
        };
        walkTo(0, 0);
        const int frag = puzzle::objectAt(app, 1, 1, 0);
        const bool fragOk0 = pz.stage == 8 && pz.esp == 1 && frag >= 0 && pz.objs[size_t(frag)].type == cr::kTypeFragile &&
                             pz.objs[size_t(frag)].shown;
        app.walker.heading = 0; // facing row + 1
        puzzle::abilityKey(app);
        steps(3);
        const bool broke = frag >= 0 && !pz.objs[size_t(frag)].shown && puzzle::objectAt(app, 1, 1, 0) < 0 && pz.esp == 0 &&
                           app.game.vars[13] == 1 && !npcVisible(app, size_t(pz.objs[size_t(frag)].npc));
        walkTo(1, 0);
        walkTo(1, 1);
        const bool gem = pz.esp == 1;
        pz.esp = 0;
        const int frag2 = puzzle::objectAt(app, 1, 1, 2);
        puzzle::toughTap(app, frag2);
        steps(3);
        const bool refusedT = frag2 >= 0 && pz.objs[size_t(frag2)].shown && app.game.vars[13] == 1 && pz.punished > 0;
        problems += !fragOk0 || !broke || !gem || !refusedT;
        std::printf("puzzle room: stage 8 (toughness) - fragile block %s, gem %s, at 0 ESP %s\n", fragOk0 && broke ? "breaks" : "DIDN'T break",
                    gem ? "ok" : "MISSED", refusedT ? "refused" : "NOT refused");
        // Stage 12 (pyrokinesis): the switch at (2, 2) lights (lit 15 frames on, a flame); then
        // every one still out tapped until all are lit, which brings the crystal
        room(12, false);
        settle(3000);
        setActor(app, 0, puzzle::centre(app, 0, 2), nullptr, false);
        const int sw = puzzle::objectAt(app, 1, 2, 2);
        auto csOf = [&](int t) { return t >= 0 ? pz.objs[size_t(t)].cs : -1; };
        const bool start12 = pz.stage == 12 && pz.esp == -1 && sw >= 0 && pz.objs[size_t(sw)].type == cr::kTypeCandleSwitch && csOf(sw) == 0;
        puzzle::pyroTap(app, sw);
        steps(2);
        const bool lighting = csOf(sw) == 2 && app.game.vars[13] == 1;
        steps(40);
        auto flames = [&] {
            int n = 0;
            for (const Effect& e : app.fieldEffects) n += e.id == 0x92 && e.loop; // (CR1's 0xc + 0x86)
            return n;
        };
        const bool lit12 = csOf(sw) == 3 && flames() >= 1;
        app.lastTicks = 0;
        drawFrame(app, false); // the flames (and any beam) drawn
        const int fxErrs = app.renderer.takeErrors();
        int taps = 1;
        { // Omega's pyrokinesis stages link their switches (NPC table +22..+28): a tap toggles the switch
          // and, five frames on, each it links to - a lights-out puzzle. The taps that light them all,
          // found by a search over the switches' states; 45 frames between taps let every chain settle
            std::vector<int> sws;
            for (size_t i = 0; i < pz.objs.size(); ++i)
                if (pz.objs[i].type == cr::kTypeCandleSwitch) sws.push_back(int(i));
            const int n = int(sws.size());
            auto toggleOf = [&](int k) { // the state bits a tap on switch k flips
                unsigned m = 1u << k;
                const cr::FieldNpc& row = app.npcs[size_t(pz.objs[size_t(sws[size_t(k)])].npc)];
                for (int l = 0; l < 4 && row.link[l] != 0; ++l)
                    for (int j = 0; j < n; ++j)
                        if (app.npcs[size_t(pz.objs[size_t(sws[size_t(j)])].npc)].id == row.link[l]) m |= 1u << j;
                return m;
            };
            unsigned start = 0;
            for (int k = 0; k < n; ++k) start |= (pz.objs[size_t(sws[size_t(k)])].cs == 3 ? 1u : 0u) << k;
            const unsigned goal = n >= 32 ? 0xffffffffu : (1u << n) - 1;
            std::vector<int> from(size_t(1) << std::min(n, 20), -1), via(from.size(), -1);
            std::vector<unsigned> queue{start};
            from[start] = int(start);
            for (size_t q = 0; q < queue.size() && from[goal] < 0; ++q)
                for (int k = 0; k < n; ++k) {
                    const unsigned next = queue[q] ^ toggleOf(k);
                    if (from[next] >= 0) continue;
                    from[next] = int(queue[q]), via[next] = k;
                    queue.push_back(next);
                }
            std::vector<int> seq;
            for (unsigned s = goal; from[goal] >= 0 && s != start; s = unsigned(from[s])) seq.push_back(via[s]);
            for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
                puzzle::pyroTap(app, sws[size_t(*it)]);
                ++taps;
                steps(45);
            }
        }
        const bool allLit = puzzle::allCandlesLit(app);
        settle(3000);
        steps(3);
        const bool crystal12 = crystalShown();
        problems += !start12 || !lighting || !lit12 || fxErrs || !allLit || !crystal12;
        glErrors += fxErrs + errs;
        std::printf("puzzle room: stage 12 (pyrokinesis) - start %s, a switch %s, all lit %s (%d taps), crystal %s; %d problems\n",
                    start12 ? "right" : "WRONG", lighting && lit12 ? "lights" : "DOESN'T light", allLit ? "yes" : "NO", taps,
                    crystal12 ? "came" : "DIDN'T come", problems);
        { // The touches as 3.1.0 reads them: psychokinesis' arrows and its swipe decided while held,
          // CheckTap and the windows, the pad lock and the third button, ScrollCamera, a Retry's
          // entry angle, cleared rooms (no objects) and the PK blocks' empty init
            int tp = 0;
            auto pointOf = [&](int t, float dy, float& x, float& y) { // an object's point (tex2d), or its top + dy
                const cr::FieldNpc& n = app.npcs[size_t(pz.objs[size_t(t)].npc)];
                float lx = 0, ly = 0;
                x = y = -1000;
                if (fieldToLogical(app, {n.pos[0], n.pos[1] + dy, n.pos[2]}, lx, ly)) logicalToTex2d(app, lx, ly, x, y);
            };
            // Stage 4: block A at (3, 2), the leader at (1, 1)
            room(4, false);
            settle(3000);
            setActor(app, 0, puzzle::centre(app, 1, 1), nullptr, false);
            steps(2);
            int pkInit = 0, pkBlocks = 0;
            for (const App::Puzzle::Obj& o : pz.objs)
                if (o.type == cr::kTypeNormal) ++pkBlocks, pkInit += o.shown == npcActive(app, app.npcs[size_t(o.npc)]) && !o.onSwitch;
            const int a = puzzle::objectAt(app, 1, 3, 2);
            float bx = 0, by = 0;
            if (a >= 0) pointOf(a, app.npcs[size_t(pz.objs[size_t(a)].npc)].box[1], bx, by);
            const bool window = a >= 0 && puzzle::pick(app, bx, by) == a && puzzle::pick(app, bx + 4, by + 6) == a &&
                                puzzle::inWindow(cr::kTypeNormal, 20, 21) && puzzle::inWindow(cr::kTypeNormal, -20, -16) &&
                                !puzzle::inWindow(cr::kTypeNormal, 21, 0) && !puzzle::inWindow(cr::kTypeNormal, 0, 22) &&
                                !puzzle::inWindow(cr::kTypeNormal, 0, -17);
            bool arrowsOn = false, arrowsAt = false, decided = false, flicker = false, letGo = false, fade5 = false, locked = false;
            if (a >= 0) {
                puzzle::pkGrab(app, a, bx, by);
                for (int f = 0; f < 10 && pz.objs[size_t(a)].pk != 2; ++f) steps(1);
                static const int kStep[4][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
                arrowsOn = true, arrowsAt = true;
                float ax = 0, ay = 0;
                const App::Puzzle::Obj& A = pz.objs[size_t(a)];
                pointOf(a, 5 - (app.npcs[size_t(A.npc)].pos[1] - (A.baseY + 5)), ax, ay); // (x, rise's y + 5, z)
                static const float kOff[4][2] = {{-36, 0}, {0, -24}, {36, 0}, {0, 24}};
                for (int k = 0; k < 4; ++k) {
                    const int r = 3 + kStep[k][0], c = 2 + kStep[k][1];
                    const bool want = puzzle::cellSlot(app, 1, r, c) && puzzle::objectAt(app, 1, r, c) < 0;
                    arrowsOn &= (pz.arrows[k].on == 1) == want;
                    if (want) arrowsAt &= std::fabs(pz.arrows[k].x - (ax + kOff[k][0])) < 1.5f && std::fabs(pz.arrows[k].y - (ay + kOff[k][1])) < 1.5f;
                }
                // still held, the touch 40 to the left: sent at once (onCheckSlide), the pad locked
                pz.pkAtX = bx - 40, pz.pkAtY = by;
                steps(1);
                decided = pz.pkState == 2 && pz.pkHeld && pz.objs[size_t(a)].toCol < 2;
                steps(1);
                for (int k = 1; k < 4; ++k) flicker |= pz.arrows[k].timer > 0;
                flicker = !flicker && pz.arrows[0].on == -1 && pz.arrows[0].timer == 10 && pz.arrows[0].mode == 2;
                stepPuzzleHud(app);
                flicker &= pz.arrows[0].alpha == 0xaf;
                for (int f = 0; f < 3; ++f) stepPuzzleHud(app);
                flicker &= pz.arrows[0].alpha == 0xbf; // (0xff - 4 x 0x50, the byte wrapped)
                for (int f = 0; f < 6; ++f) stepPuzzleHud(app);
                flicker &= pz.arrows[0].timer == 0; // (gone after its 10)
                locked = app.padLock == 10 && !puzzle::ask(app, 0);
                pz.pkHeld = false;
                for (int f = 0; f < 200 && pz.pkState; ++f) steps(1);
                locked &= app.padLock == 2;
                // taken hold of again and let go inside the dead zone: down where it was, nothing counted
                const int uses = app.game.vars[13], ar = pz.objs[size_t(a)].row, ac = pz.objs[size_t(a)].col;
                pointOf(a, app.npcs[size_t(pz.objs[size_t(a)].npc)].box[1], bx, by);
                puzzle::pkGrab(app, a, bx, by);
                const bool regrab = pz.pkState == 1;
                for (int f = 0; f < 10 && pz.objs[size_t(a)].pk != 2; ++f) steps(1);
                int shown = 0;
                for (const App::Puzzle::Arrow& w : pz.arrows) shown += w.on == 1;
                if (!regrab || !shown) std::printf("puzzle touches: the second hold at (%d, %d): taken %d, block %d, arrows %d\n", ar, ac, regrab, pz.objs[size_t(a)].pk, shown);
                pz.pkAtX = bx + 20, pz.pkAtY = by + 10;
                steps(1);
                pz.pkHeld = false;
                steps(1);
                bool anyOn = false;
                for (const App::Puzzle::Arrow& w : pz.arrows) anyOn |= w.timer == 5 && w.mode == 1;
                stepPuzzleHud(app);
                fade5 = anyOn;
                for (const App::Puzzle::Arrow& w : pz.arrows) fade5 &= w.timer == 0 || w.alpha == 0xcc;
                for (int f = 0; f < 60 && pz.pkState; ++f) steps(1);
                letGo = pz.pkState == 0 && app.game.vars[13] == uses && pz.objs[size_t(a)].row == ar && pz.objs[size_t(a)].col == ac;
            }
            tp += !window || !arrowsOn || !arrowsAt || !decided || !flicker || !locked || !letGo || !fade5 || pkInit != pkBlocks;
            std::printf("puzzle touches: psychokinesis - window %s, arrows %s at %s, swipe decided while held %s, the chosen arrow "
                        "flickers out %s, pad lock %s, a let-go fades them %s and costs nothing %s, blocks as the map has them %d/%d\n",
                        window ? "ok" : "WRONG", arrowsOn ? "on" : "WRONG", arrowsAt ? "their points" : "WRONG POINTS", decided ? "yes" : "NO",
                        flicker ? "yes" : "NO", locked ? "10 then 2" : "WRONG", fade5 ? "over 5" : "NO", letGo ? "yes" : "NO", pkInit, pkBlocks);
            // Stage 0: CheckTap - held 7 frames no tap; the leader moving holds it; let go in time, a teleport (pad lock 12)
            room(0, false);
            settle(3000);
            setActor(app, 0, puzzle::centre(app, 0, 2), nullptr, false);
            steps(2);
            const int b = puzzle::objectAt(app, 1, 1, 1);
            bool longNo = false, movingWaits = false, tapped = false, gemWindow = true;
            if (b >= 0) {
                pointOf(b, app.npcs[size_t(pz.objs[size_t(b)].npc)].box[1], bx, by);
                auto touch = [&] { pz.tap = App::Puzzle::Tap(); pz.tap.state = 1, pz.tap.x = bx, pz.tap.y = by + 10; };
                touch();
                steps(7);
                pz.tap.released = true;
                steps(1);
                longNo = pz.tpState == 0 && pz.tap.state == 0;
                touch();
                app.walker.gait = cr::Gait::Walk;
                steps(3);
                movingWaits = pz.tap.frames == 0 && pz.tap.state == 1;
                app.walker.gait = cr::Gait::Stand;
                steps(2);
                pz.tap.released = true;
                steps(1);
                tapped = pz.tpState == 1 && app.padLock == 12;
                settle(3000);
            }
            bool realTap = false, realHold = false;
            { // the same through SDL's mouse events, as a finger's (CR1-U 11b1731, a player's report there: a tap on a block never
              // took - the pad under the finger refused it on any touch): a quick tap teleports, a long hold doesn't
                auto send = [&](SDL_EventType type, float lx, float ly) { // (logical -> the window's points: toLogical's inverse)
                    float x0, y0, x1, y1;
                    app.renderer.toLogical(0, 0, x0, y0);
                    app.renderer.toLogical(1000, 1000, x1, y1);
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(app.window, &ww, &wh);
                    SDL_Event e{};
                    e.type = type;
                    e.button.x = (lx - x0) * 1000 / (x1 - x0) * float(ww) / float(app.renderer.frameWidth());
                    e.button.y = (ly - y0) * 1000 / (y1 - y0) * float(wh) / float(app.renderer.frameHeight());
                    e.button.button = SDL_BUTTON_LEFT;
                    SDL_AppEvent(&app, &e);
                };
                for (int hold : {2, 9}) {
                    room(0, false);
                    settle(3000);
                    setActor(app, 0, puzzle::centre(app, 0, 2), nullptr, false);
                    steps(2);
                    const int t = puzzle::objectAt(app, 1, 1, 1);
                    if (t < 0) break;
                    const cr::FieldNpc& n = app.npcs[size_t(pz.objs[size_t(t)].npc)];
                    float lx = 0, ly = 0;
                    if (!fieldToLogical(app, {n.pos[0], n.pos[1] + n.box[1], n.pos[2]}, lx, ly)) break;
                    send(SDL_EVENT_MOUSE_BUTTON_DOWN, lx, ly + 20);
                    steps(hold);
                    send(SDL_EVENT_MOUSE_BUTTON_UP, lx, ly + 20);
                    steps(2);
                    (hold == 2 ? realTap : realHold) = hold == 2 ? pz.tpState == 1 : pz.tpState == 0;
                    settle(3000);
                }
            }
            tp += !realTap + !realHold;
            std::printf("puzzle touches: a block tapped through SDL's mouse events (a finger's) - a quick tap teleports %s, a 9-frame hold doesn't %s\n",
                        realTap ? "yes" : "NO", realHold ? "yes" : "NO");
            for (size_t i = 0; i < pz.objs.size(); ++i) // a gem's window: 7..33 below its point, |dx| <= 17
                if (pz.objs[i].type == cr::kTypeGem && puzzle::target(app, pz.objs[i])) {
                    float gx, gy;
                    pointOf(int(i), app.npcs[size_t(pz.objs[i].npc)].box[1], gx, gy);
                    gemWindow &= puzzle::pick(app, gx, gy + 20) == int(i) && puzzle::pick(app, gx, gy + 6) < 0 &&
                                 puzzle::inWindow(cr::kTypeGem, 17, 7) && puzzle::inWindow(cr::kTypeGem, -17, 33) &&
                                 !puzzle::inWindow(cr::kTypeGem, 18, 20) && !puzzle::inWindow(cr::kTypeGem, 0, 34);
                }
            // the third button: var 64 + stage above 2 (EvrStart 8), refused while the pad is locked
            app.game.vars[64] = 2;
            const bool hidden = !puzzle::thirdButton(app);
            app.game.vars[64] = 3;
            app.padLock = 5;
            const bool lockRefuses = !puzzle::ask(app, 2);
            app.padLock = 0;
            const bool third = hidden && lockRefuses && puzzle::thirdButton(app) &&
                               (!app.events.returnEntry(8) || (puzzle::ask(app, 2) && app.runnerWhat == "puzzle button 8"));
            app.runner.stop();
            app.game.vars[64] = 0;
            // ScrollCamera: one finger 5 frames, a second within 6 - the view goes to it, the ability waits
            app.fingers = App::Fingers();
            app.fingers.held = 1;
            steps(5);
            app.fingers.held = 2, app.fingers.fresh = 1;
            app.fingers.x = float(app.renderer.frameWidth()) / 4, app.fingers.y = float(app.renderer.frameHeight()) / 4;
            steps(1);
            const bool scrolled = pz.scroll.state == 1;
            pz.tap = App::Puzzle::Tap();
            pz.tap.state = 1;
            steps(2);
            const bool paused = pz.tap.frames == 0;
            app.fingers.held = 1;
            steps(12);
            const bool back = pz.scroll.state == 0;
            app.fingers = App::Fingers();
            pz.tap = App::Puzzle::Tap();
            // a Retry: the leader back where and as it came in, standing; the pad locked 24
            const cr::Vec3 start = app.puzzleStart;
            const float heading = app.puzzleStartHeading;
            setActor(app, 0, puzzle::centre(app, 4, 4), nullptr, false);
            app.walker.heading = heading + 1.5f, app.walker.gait = cr::Gait::Walk;
            puzzle::reset(app);
            const bool lock24 = app.padLock == 24;
            for (int f = 0; f < 40 && pz.scene != 4; ++f) steps(1);
            const bool retry = lock24 && pz.scene == 4 && std::fabs(app.walker.heading - heading) < 1e-4f &&
                               app.walker.gait == cr::Gait::Stand && std::fabs(app.walker.pos.x - start.x) < 0.01f &&
                               std::fabs(app.walker.pos.z - start.z) < 0.01f;
            // cleared rooms: no objects, no Retry / Help, the count 0 (MfMain_ClrPzlData)
            int clearedOk = 0, crystals = 0;
            for (int st = 0; st < cr::kPuzzleStages; ++st) {
                room(st, false);
                settle(3000);
                app.game.setFlag(3000 + st, true);
                app.npcs = app.events.npcs(app.mapNumber);
                puzzle::enter(app);
                clearedOk += pz.stage == st && pz.objs.empty() && !pz.help && pz.esp == 0 && !puzzle::thirdButton(app) && !puzzle::ask(app, 0);
                int have = 0, shown = 0;
                for (size_t i = 0; i < app.npcs.size(); ++i)
                    if (app.npcs[i].puzzle & (cr::kPuzzleObject40 | cr::kPuzzleObject100 | cr::kPuzzleObject800)) {
                        ++have;
                        shown += npcVisible(app, i);
                    }
                crystals += shown > 0;
                clearedOk -= have && !shown; // (the way on: a crystal the map shows)
                if (have && !shown) std::printf("puzzle touches: cleared stage %d - %d crystal NPCs, none shown by the map\n", st, have);
            }
            tp += !longNo || !movingWaits || !tapped || !gemWindow || !third || !scrolled || !paused || !back || !retry || clearedOk != 16;
            std::printf("puzzle touches: CheckTap - held 7 frames %s, the leader moving %s, a tap %s; gem window %s; third button %s; "
                        "ScrollCamera %s, the ability %s, %s; Retry %s; cleared rooms bare %d/16 (a crystal shown in %d - the toughness rooms have none); %d problems\n",
                        longNo ? "no tap" : "TAPPED", movingWaits ? "waits" : "DOESN'T WAIT", tapped ? "teleports (pad lock 12)" : "DOESN'T",
                        gemWindow ? "ok" : "WRONG", third ? "ok" : "WRONG", scrolled ? "scrolls" : "DOESN'T SCROLL", paused ? "paused" : "NOT PAUSED",
                        back ? "let go" : "STUCK", retry ? "back as it came in" : "WRONG", clearedOk, crystals, tp);
            problems += tp;
        }
        mapErrors += problems;
        app.runner.stop();
        app.game = cr::GameState();
        app.puzzle = App::Puzzle();
    }
    { // The field camera: the doubled view follows the player within the painting, the scripts pan it
        int problems = 0;
        const int keepPaintings = app.video.paintings;
        app.video.paintings = 0;
        app.runner.stop();
        app.game = cr::GameState();
        app.mapNumber = -1;
        cr::WarpRequest w;
        w.valid = true;
        w.area = 2;
        w.map = 0; // d0101, the first gate's first field
        performWarp(app, w, -1);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        app.cam = App::ScriptCam();
        const float SW = app.renderer.screenW(), H = cr::Renderer::kLogicalH;
        auto view = [&](float& x, float& y) { // the view's top-left on screen
            x = (SW - fieldViewW(app)) / 2 + app.scroll.offX;
            y = (H - fieldViewH(app)) / 2 - app.scroll.offY;
        };
        auto inside = [&] { // the screen stays inside the doubled view
            float x, y;
            view(x, y);
            return x <= 0.5f && y <= 0.5f && x + fieldViewW(app) >= SW - 0.5f && y + fieldViewH(app) >= H - 0.5f;
        };
        problems += !scrollingField(app) || fieldViewW(app) != 2048 || fieldViewH(app) != 2048;
        int followed = 0;
        for (const cr::Vec3 p : {cr::Vec3{120.4f, 20.0f, 69.5f}, cr::Vec3{100, 20, 60}, cr::Vec3{80, 20, 50}}) {
            app.walker.pos = p;
            followPlayerScroll(app, 1.0f);
            float hx, hy, x, y;
            fieldViewPoint(app, p + cr::Vec3{0, 7, 0}, hx, hy);
            view(x, y);
            const bool onScreen = x + hx >= 0 && x + hx <= SW && y + hy >= 0 && y + hy <= H;
            followed += onScreen && inside();
        }
        problems += followed != 3;
        // CameraFree: FrmPanCam moves twice its deltas over its frames, kept in the painting (Omega: the
        // camera's way - the painting the other way)
        ViewerHost host(app);
        host.freeCamera(true);
        const float startY = app.scroll.offY;
        const float pan[3] = {0, -100, 0};
        host.panCamera(pan, 10); // (its first step is the pan's own frame: ScrollAutoCtl runs after PrcEvnt)
        for (int f = 0; f < 4; ++f) stepFieldScroll(app);
        const float halfway = app.scroll.offY;
        for (int f = 0; f < 10; ++f) stepFieldScroll(app);
        const float wantY = std::clamp(startY + 200, -(fieldViewH(app) - H) / 2, (fieldViewH(app) - H) / 2);
        const float panned = app.scroll.offY;
        problems += std::fabs(halfway - (startY + wantY) / 2) > 1.0f || std::fabs(panned - wantY) > 0.5f ||
                    app.scroll.sliding || !inside();
        // FrmAdjCam: slides to a shot's (x, y); FrmSetCam: back to the centre; Mapscroll: centre on an NPC (the player)
        const float shot[3] = {-200, 150, 0};
        host.setCamera(shot, shot, 4);
        for (int f = 0; f < 4; ++f) stepFieldScroll(app);
        problems += std::fabs(app.scroll.offX + 200) > 0.5f || std::fabs(app.scroll.offY - 150) > 0.5f;
        host.setCamera(shot, shot, 0);
        problems += app.scroll.offX != 0 || app.scroll.offY != 0;
        host.mapScroll(0, 0, 0, 1.0f);
        float hx, hy, x, y;
        fieldViewPoint(app, app.walker.pos, hx, hy);
        view(x, y);
        problems += !inside() || x + hx < 0 || x + hx > SW;
        // CameraPlayer: following again
        host.freeCamera(false);
        followPlayerScroll(app, 1.0f);
        problems += !inside();
        app.lastTicks = 0;
        drawFrame(app, false);
        ++frames;
        std::printf("field camera: d0101's view %gx%g follows the player (%d of 3 spots), pans %g -> %g, shots and "
                    "Mapscroll; %d problems\n",
                    fieldViewW(app), fieldViewH(app), followed, startY, panned, problems);
        // Scene effects (Omega): set 5 (the battleship's deck: the steam p0001_fog, the sky d0530_bg01),
        // its effects 10 and 30 stepping within their counters; set 1's smoke and frame (6): UpdateRandom's
        // channel 0 breathing between 0x70 and 0x70 + 3 x 0x25 (InitRandom(0, 6, 0x70, 0, 3, 0x25, ...))
        int fxProblems = 0;
        endSceneEffects(app);
        loadSceneEffectSet(app, 5);
        int loaded = 0;
        for (const cr::Texture& t : app.fx.tex) loaded += t.id != 0;
        fxProblems += app.fx.set != 5 || loaded != 2;
        setSceneEffect(app, true, 10);
        setSceneEffect(app, true, 30);
        for (int f = 0; f < 3000; ++f) {
            stepSceneEffects(app);
            fxProblems += app.fx.timerA[10] > 0xf0 || app.fx.timerA[30] > 0x808 || app.fx.timerB[30] > 0x78;
        }
        app.lastTicks = 0;
        drawFrame(app, false);
        ++frames;
        endSceneEffects(app);
        loadSceneEffectSet(app, 1);
        setSceneEffect(app, true, 6);
        int lo = 255, hi = 0;
        for (int f = 0; f < 600; ++f) {
            stepSceneEffects(app);
            lo = std::min(lo, app.fx.ch[0].alpha);
            hi = std::max(hi, app.fx.ch[0].alpha);
        }
        fxProblems += lo != 0x70 || hi != 0x70 + 3 * 0x25;
        app.lastTicks = 0;
        drawFrame(app, false);
        ++frames;
        setSceneEffect(app, false, 6);
        fxProblems += app.fx.on[6];
        std::printf("scene effects: set 5 (%d textures), steam and sky on, set 1's smoke alpha %d..%d; %d problems\n", loaded, lo, hi,
                    fxProblems);
        problems += fxProblems;
        endSceneEffects(app);
        app.video.paintings = keepPaintings;
        mapErrors += problems;
        bootTitle(app);
        app.runner.stop();
    }
    { // The puzzle tutorial (Uty_TutoTexDisp): entering stage 0 from a dungeon, its pages turned as a
      // player would (advanceDialogue): the picture's pages follow; the frames step
        int problems = 0, maxPage = -1, framesSeen = 0, calls0 = app.tutoCalls;
        bool shot = false;
        app.runner.stop();
        app.game = cr::GameState();
        app.mapNumber = -1;
        app.game.setFlag(3070, true);
        cr::WarpRequest w;
        w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap;
        performWarp(app, w, -1);
        for (int f = 0; f < 4000 && (app.runner.running() || app.pendingEntry != App::Entry::None || app.tutoTex.on); ++f) {
            stepEvents(app);
            if (app.tutoTex.on) {
                maxPage = std::max(maxPage, app.tutoTex.page);
                framesSeen = std::max(framesSeen, app.tutoTex.frame + 1);
                if (!shot && app.tutoTex.page >= 0 && app.tutoTex.frame >= 1) {
                    shot = true;
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    if (const char* path = SDL_getenv("CR_TUTO_SHOT")) saveFrame(app, path);
                }
            }
            if (f % 60 == 59 && !app.dialogue.empty()) advanceDialogue(app); // a page read, then turned
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        const bool ran = app.tutoCalls > calls0;
        problems += !ran || maxPage < 1 || framesSeen < 2 || app.tutoTex.on || app.renderer.takeErrors();
        std::printf("puzzle tutorial: %s, pages shown up to %d, frames stepped %d, ended %s; %d problems\n",
                    ran ? "started" : "NOT started", maxPage, framesSeen, app.tutoTex.on ? "NO" : "yes", problems);
        mapErrors += problems;
        app.runner.stop();
        app.game = cr::GameState();
        app.puzzle = App::Puzzle();
    }
    { // Music: the AAC decoder (the system's FAAD2 or the bundled copy) decodes a whole track
        int64_t frames = 0;
        uint64_t hash = 0;
        std::string err;
#ifdef CR_HAVE_FAAD2
        if (app.vitaOnly || app.androidOnly) std::printf("music: the %s's (no AAC tracks)\n", app.vitaOnly ? "Vita" : "Android release");
        else {
        const bool ok = cr::decodeTrack(app.dataDir, 10, frames, hash, &err) && frames > 1000000;
        mapErrors += !ok;
        std::printf("music: track 10 decoded %s (%lld frames)\n", ok ? "ok" : ("FAILED: " + err).c_str(), (long long)frames);
        }
#else
        std::printf("music: built without an AAC decoder\n");
#endif
    }
    if (app.android.isOpen()) { // --android: its j001mNN.mca for bgmNN (Ogg Vorbis, looping at its own sample)
        int problems = 0, decoded = 0, loops = 0;
        double minutes = 0;
        const auto read = [&app](const std::string& n, std::vector<uint8_t>& out) { return app.android.read(n, out); };
        for (int t = 0; t < cr::kBgmTracks; ++t) {
            int64_t samples = 0;
            bool loopOk = false;
            if (cr::checkAndroidTrack(read, t, samples, loopOk)) ++decoded, loops += loopOk, minutes += double(samples) / 44100 / 60;
        }
        problems += decoded < cr::kBgmTracks - 1 || loops != decoded; // (CR1's: no j001m31)
        // the layout's message strip (window_cutin_<w>) by the width of a 720-tall screen; each in the data
        int strips = 0;
        for (const int w : {480, 800, 854, 960, 1024, 1280, 1366}) strips += app.android.contains("window_cutin_" + std::to_string(w) + ".mct");
        problems += strips != 7 || androidCutinWidth(1080) != 1280 || androidCutinWidth(1280.0f * 0.9999f) != 1280 ||
                    androidCutinWidth(1281) != 1366 || androidCutinWidth(1680) != 0;
        std::printf("android music: %d of %d tracks decode (%.1f minutes), %d loop points inside them; %d problems\n", decoded,
                    cr::kBgmTracks, minutes, loops, problems);
        mapErrors += problems;
    }
    if (app.vita.isOpen()) { // --vita: the Vita's scripts (eight languages), voices (sorted names = 2.2.0's
                             // clips) and music (cr_mNN.at9 for bgmNN, looping between its smpl points)
        int problems = 0;
        int languages = 0;
        for (int l = 0; l < cr::kLanguages; ++l) languages += app.systemText.hasLanguage(l);
        problems += languages != cr::kLanguages;
        if (app.sound.voiceClips(false) == 0 && app.vitaVoice[0].isOpen()) startSound(app), vitaAudio(app); // (tests run before it)
        const bool voices = app.sound.vitaVoices() && app.sound.voiceClips(false) == 2557 && app.sound.voiceClips(true) == 1732; // (Omega's: _no_silent and the story's 2556, the battles' 1732)
        problems += !voices;
        int decoded = 0, rate = 0, ch = 0;
        std::vector<uint8_t> f;
        std::vector<int16_t> pcm;
        for (int b = 0; b < 2; ++b) {
            const std::vector<std::string> names = app.vitaVoice[b].names();
            if (names.size() > 1 && app.vitaVoice[b].read(names[1], f) && cr::decodeAt9(f, pcm, rate, ch) && rate == 48000 && ch == 1 && pcm.size() > 4800)
                ++decoded;
        }
        problems += decoded != 2;
        cr::At9File track;
        int64_t frames = 0;
        const bool music = app.vitaBgm.read("cr_m01.at9", f) && cr::parseAt9(f, track) && track.loopStart > 0 &&
                           track.loopEnd > track.loopStart && cr::decodeAt9(f, pcm, rate, ch) && (frames = int64_t(pcm.size()) / std::max(ch, 1)) == track.samples;
        problems += !music;
        // Omega's own tracks are the Vita's cr2_mNN.at9: the title's (Bgm 29 = bgm41) must resolve to one
        const std::string titleFile = cr::MusicPlayer::vitaTrackFile(29, [&app](const std::string& n, std::vector<uint8_t>& out) { return app.vitaBgm.read(n, out); }, nullptr);
        problems += titleFile != "cr2_m41.at9";
        std::printf("vita assets: %d languages, voices %s (%d / 2 clips decoded), track 1 %s (%lld frames, loop %.3f..%.3f s), the title's track 29 %s; %d problems\n",
                    languages, voices ? "yes" : "NO", decoded, music ? "decoded" : "NOT decoded", (long long)frames,
                    double(track.loopStart) / 48000, double(track.loopEnd) / 48000, titleFile.empty() ? "NOT found" : titleFile.c_str(), problems);
        mapErrors += problems;
    }
    { // The scripts' small natives: 2018 the pair's HP / MP, 2024 / 2026 the music by var, 2025 the shop
      // ended without its music put back, 2027 the pair, 2028 the loading spinner; the shop's music
        int problems = 0;
        ViewerHost host(app);
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar;
        app.game.scenario = 1; // the second scenario, its pair records 0 and 2 (Omega: the scenario's pair, _prty + 8)
        app.game.pairs[1] = {0, 2};
        for (auto& m : app.game.party) m.hp = 1, m.mp = 1;
        host.recoverPair();
        const bool healed = app.game.party[0].hp < 0 && app.game.party[2].mp == cr::memberStat(app.game, 2, cr::kMp, app.itemTable) &&
                            app.game.party[1].hp == 1;
        const bool pair = host.pairNo() == 1;
        host.playMusic(7);
        const bool bgm = host.bgmNo() == 7;
        host.playMusic(1u << 27 | 7); // a fade-out leaves no track (Uty_GetBgmNo: 255)
        const bool fadeKept = host.bgmNo() == 255;
        host.playMusic(7);
        app.game.vars[0] = 7;
        openShop(app);
        const bool shopTune = host.bgmNo() == 0x13 && app.shop.bgmBack == 7;
        closeShop(app);
        const bool back = host.bgmNo() == 7;
        openShop(app);
        host.endMenu(2);
        const bool ended = !app.shop.open && host.bgmNo() == 0x13; // SetShopBgmNoRecovery
        // 2030 / 2031: outside a battle nothing (the original's byte is wiped as a battle starts)
        host.battleParamUp(1);
        const bool queued = app.paramUp[1] || app.paramUp[0];
        app.game = keepGame;
        startBattle(app, {1, 0, -1, 0}, 0);
        bool boosted = false;
        if (app.battle.active) { // Uty_Btl_ParamUp01 / 02 set the fighter's "doubled" byte (+0x97, the Ward skill's) for the
            // battle: nothing before it (prc's memset wipes it), fighter 1 by 2030, fighter 0 by 2031
            const cr::Fighter& f1 = app.battle.rules.f[1];
            boosted = f1.status[cr::kStatusWard] == 0 && app.battle.rules.f[0].status[cr::kStatusWard] == 0;
            const int atkBefore = app.battle.rules.atkOf(0);
            host.battleParamUp(0); // in a battle: at once, fighter 0 doubled
            boosted &= app.battle.rules.f[0].status[cr::kStatusWard] == 1 && app.battle.rules.atkOf(0) == atkBefore * 2 &&
                       f1.status[cr::kStatusWard] == 0;
            endBattle(app, false);
        }
        problems += queued || !boosted;
        // 2029 / 2008: the logos once a run; the pad's picture while flag 0 is set
        app.logoSeen = false;
        host.setLogoSeen(); // (var 0 set: the logos shown this run)
        const bool logoOnce = host.logoSeen();
        app.game.setFlag(0, true);
        host.padArt();
        app.lastTicks = 0;
        drawFrame(app, false);
        if (const char* path = SDL_getenv("CR_PAD_SHOT")) saveFrame(app, path);
        app.game.setFlag(0, false);
        drawFrame(app, false);
        const bool padGone = !app.padArt;
        problems += !logoOnce || !padGone || app.renderer.takeErrors();
        std::printf("natives: battle stat ups (2030 / 2031) %s\n", !queued && boosted ? "ok" : "WRONG");
        app.runner.stop(); // (a script of the run before might turn it off: flag 4086 clear)
        host.nowLoading(true);
        const int frame0 = app.nowLoading.frame;
        for (int f = 0; f < 3; ++f) stepEvents(app);
        const bool spin = app.nowLoading.on && app.nowLoading.frame == ((frame0 + 3) & 15);
        app.lastTicks = 0;
        drawFrame(app, false);
        host.nowLoading(false);
        problems += !healed || !pair || !bgm || !fadeKept || !shopTune || !back || !ended || !spin || app.renderer.takeErrors();
        std::printf("natives: pair recovery %s, pair number %s, music by number %s, shop music %s / put back %s / kept by EndMenu %s, "
                    "loading spinner %s; %d problems\n", healed ? "ok" : "WRONG", pair ? "ok" : "WRONG", bgm && fadeKept ? "ok" : "WRONG",
                    shopTune ? "on" : "OFF", back ? "yes" : "NO", ended ? "yes" : "NO", spin ? "turns" : "STILL", problems);
        mapErrors += problems;
        app.game = keepGame;
        app.playerChar = keepChar;
        playBgm(app, 1u << 29);
    }
    { // The staff roll (Util 7, mode 0): the lines through, the picture down, the end picture in,
      // the fade to black, then it closes by itself
        int problems = 0;
        openStaffRoll(app, 0);
        App::StaffRoll& st = app.staffRoll;
        int lines = 0;
        for (int e = 0; e < staffLines(app); ++e) lines += staffLine(app, e).size() >= 3;
        const bool tex = st.tex.id != 0 && st.endTex.id != 0;
        int frames = 0, shot = 0, endAt = -1, endAlpha = 0;
        while (st.open && frames < 6000) {
            st.fast = true; // (drawFrame reads the keys)
            stepStaffRoll(app);
            ++frames;
            if (st.state == 8 && endAt < 0) endAt = frames;
            endAlpha = std::max(endAlpha, st.endAlpha);
            if (frames == 150 || (st.state == 9 && shot == 1)) { // a look mid-way, then at the end picture
                app.lastTicks = 0;
                drawFrame(app, false);
                const char* path = SDL_getenv(shot == 0 ? "CR_STAFF_SHOT_A" : "CR_STAFF_SHOT_B");
                if (path && shot < 2) saveFrame(app, path);
                ++shot;
            }
        }
        problems += !tex || lines < 100 || endAt < 0 || endAlpha != 255 || st.open || app.renderer.takeErrors();
        std::printf("staff roll: pictures %s, %d of block 9's %d lines with text, the end picture after %d fast frames, closed "
                    "after %d %s; %d problems\n", tex ? "read" : "MISSING", lines, staffLines(app), endAt, frames,
                    st.open ? "NO" : "yes", problems);
        mapErrors += problems;
    }
    { // The last battle's pair choice (Util 8): four rounds, each pair once; a No goes back to the
      // plates; the camp menu opens for the chosen pair; flag 1 clears after the fourth
        int problems = 0;
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar;
        app.game.setFlag(1, true);
        App::LastBattle& lb = app.lastBattle;
        lb.count = lb.mask = 0;
        auto stepTo = [&](int state) { for (int f = 0; f < 60 && lb.open && lb.state != state; ++f) stepLastBattle(app); };
        auto look = [&](const char* var) {
            if (const char* path = SDL_getenv(var)) {
                for (int f = 0; f < 4; ++f) stepLastBattle(app);
                app.lastTicks = 0;
                drawFrame(app, false);
                saveFrame(app, path);
            }
        };
        std::vector<int> order;
        bool campFor = true, backOk = true, texOk = true, healed = true;
        for (int round = 0; round < 4; ++round) {
            app.game.party[3].hp = 1;
            openLastBattle(app);
            texOk &= lb.tex.id != 0;
            healed &= app.game.party[3].hp < 0;
            stepTo(3);
            if (round == 0) look("CR_LASTBT_SHOT_A");
            const int left = int(lb.pairs.size());
            backOk &= left == 4 - round;
            lastBattleChoose(app, left - 1); // the last one listed
            stepTo(7);
            campFor &= app.campOpen && app.playerChar == 2 * lb.chosen;
            app.campOpen = false;
            stepTo(10);
            if (round == 0) {
                look("CR_LASTBT_SHOT_B");
                lastBattleAnswer(app, false); // No: the plates again, the same pairs
                stepTo(3);
                backOk &= lb.state == 3 && int(lb.pairs.size()) == left;
                lastBattleChoose(app, left - 1);
                stepTo(7);
                app.campOpen = false;
                stepTo(10);
            }
            const int chosen = lb.chosen;
            lastBattleAnswer(app, true);
            stepTo(-1);
            order.push_back(chosen);
            problems += lb.open || app.game.vars[0] != chosen;
        }
        bool leaders = true; // the plates name the leaders, not the partners (entry 0x1e + 2p)
        for (int p = 0; p < 4; ++p)
            leaders &= !lastBattlePlate(app, p).empty() && lastBattlePlate(app, p) != sysText(app, 58030 + 2 * p);
        problems += !leaders;
        const bool reset = lb.count == 0 && lb.mask == 0 && !app.game.flag(1);
        std::sort(order.begin(), order.end());
        // (Omega keeps CR1's screen and its picture names - _gkpachMonsImage hi / mizu / kaze / mu.mct -,
        // but its data has none of them: nothing reaches Util 8 there)
        (void)texOk;
        problems += !healed || !campFor || !backOk || !reset || order != std::vector<int>{0, 1, 2, 3} || app.renderer.takeErrors();
        std::printf("last battle: 4 rounds, each pair once %s, its camp menu %s, No back to the plates %s, HP / MP restored %s, "
                    "flag 1 and the rounds clear %s, plates name the leaders %s; %d problems\n", order == std::vector<int>{0, 1, 2, 3} ? "yes" : "NO",
                    campFor ? "opens" : "DOESN'T open", backOk ? "yes" : "NO", healed ? "yes" : "NO", reset ? "yes" : "NO", leaders ? "yes" : "NO", problems);
        mapErrors += problems;
        app.game = keepGame;
        app.playerChar = keepChar;
    }
    { // The battle's presentation as 3.1.0's: btlcamera_set / _prc and btl_prc's easing, the attack
      // spots (_btlact_attack_init), the reactions (_btlcalc_chk_dead), the effect gating (0404),
      // the number slots (__dmg_set / __dmg_prc)
        int problems = 0;
        std::string why;
        auto check = [&](bool ok, const char* what) { if (!ok) ++problems, why += std::string(" ") + what; };
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        auto near = [](float x, float y) { return std::fabs(x - y) < 0.01f; };
        auto nearV = [&](cr::Vec3 p, cr::Vec3 q) { return near(p.x, q.x) && near(p.y, q.y) && near(p.z, q.z); };
        bool camOk = false, spotsOk = false, reactOk = false, gateOk = false, slotsOk = false;
        if (b.active) {
            for (int f = 0; f < 60 && b.phase != App::Battle::Command; ++f) stepBattle(app);
            const int before = problems;
            // a formation camera (0x02: turning -20 degrees over 15): its first frame cut, then a fifth a frame
            setBattleCamera(app, 2);
            stepBattleCamera(app);
            check(b.camSnapped && nearV(b.camEye, b.camWantEye) && b.camFrame == 1, "cam-first-cut");
            const cr::BattleCamera* c2 = app.battleData.camera(2);
            if (c2) { // the eye at the whole 3D distance from the target, its height kept
                const cr::Vec3 e{float(c2->raw[0]), float(c2->raw[1]), float(c2->raw[2])}, t{float(c2->raw[3]), float(c2->raw[4]), float(c2->raw[5])};
                const cr::Vec3 d = e - t, w = b.camWantEye - b.camWantTarget;
                check(near(std::sqrt(w.x * w.x + w.z * w.z), std::sqrt(cr::dot(d, d))) && near(b.camWantEye.y, e.y), "cam-orbit-radius");
            }
            cr::Vec3 prev = b.camEye;
            stepBattleCamera(app);
            check(!b.camSnapped && b.camEase == 5 && nearV(b.camEye, prev + (b.camWantEye - prev) * 0.2f), "cam-ease-5");
            for (int f = 0; f < 12; ++f) stepBattleCamera(app);
            check(b.camLive && !cameraDone(app) && b.camFrame == 14, "cam-live-14");
            stepBattleCamera(app);
            check(!b.camLive && cameraDone(app), "cam-done-15");
            // a cast camera (0x49, 10001): btl +0xae8's 25 frames, then held
            b.camHold = 25;
            setBattleCamera(app, 0x49, 0);
            check(b.camDur == 25, "cam-cast-25");
            for (int f = 0; f < 40; ++f) stepBattleCamera(app);
            check(b.camLive && b.camFrame == 24 && !cameraDone(app), "cam-cast-held");
            // the defeat's 0x48 turns on for ever
            setBattleCamera(app, 0x48);
            for (int f = 0; f < 1001; ++f) stepBattleCamera(app);
            check(b.camLive && b.camFrame == 1, "cam-48-loops");
            // a still one (0x1e) is over at once and cut; actor -1 leaves the count alone
            setBattleCamera(app, 2);
            for (int f = 0; f < 5; ++f) stepBattleCamera(app);
            setBattleCamera(app, 0x1e, -1);
            stepBattleCamera(app);
            check(!b.camLive && b.camFrame == 5 && b.camSnapped, "cam-still");
            // an actor under -98: one step of the turn (0x45, -30 over 90)
            setBattleCamera(app, 0x45, -100);
            check(b.camDur == -90 && b.camActor == 0, "cam-minus-100-set");
            stepBattleCamera(app);
            check(!b.camLive && b.camFrame == 1, "cam-minus-100-step");
            // the target cursor: 1/2 a frame, left alone by btlcamera_prc
            cameraForTarget(app, 2, false, true);
            stepBattleCamera(app);
            stepBattleCamera(app);
            check(b.camHeld && b.camEase == 2 && !b.camSnapped, "cam-cursor");
            // moving 0x203: the roll -10 -> 5 over 30; 0x1fe's eye 6 higher, 3 nearer
            setBattleCamera(app, 0x203);
            for (int f = 0; f < 15; ++f) stepBattleCamera(app);
            check(near(b.camRoll, -2.5f), "cam-roll");
            if (const cr::BattleCamera* c = app.battleData.camera(0x1fe)) {
                setBattleCamera(app, 0x1fe);
                stepBattleCamera(app);
                const float k = 1.0f / float(c->raw[15]);
                check(near(b.camWantEye.y, c->raw[1] + (c->raw[8] - c->raw[1]) * k + 6) && near(b.camWantEye.z, c->raw[2] + (c->raw[9] - c->raw[2]) * k + 3),
                      "cam-1fe");
            }
            // the gene / item hit cameras (_btlact_gene / _btlact_item at the hit)
            {
                int single = -1, wide = -1;
                for (int id = 0x100; id < app.battleActions.count() && (single < 0 || wide < 0); ++id)
                    if (const cr::BattleActionDef* d = app.battleActions.get(id)) {
                        if (single < 0 && d->scope != 3 && d->scope != 5 && id != 0x197 && id != 0x19a && id != 0x1b1 && id != 0x1b6 && id != 0x1fc && id != 0x1fe) single = id;
                        if (wide < 0 && (d->scope == 3 || d->scope == 5)) wide = id;
                    }
                const bool keepPair = b.rules.pair, keepEPair = b.rules.enemyPair;
                b.rules.pair = b.rules.enemyPair = false;
                cr::BattleTurn g;
                g.kind = cr::kCmdGene, g.action = single, g.actors[0] = 0;
                hitCamera(app, g, 2, false);
                const int sz = monsterSize(app, 2);
                check(single >= 0 && b.cam == (sz == 1 ? 0x60 : sz + 0x61) && b.camActor == 2, "hitcam-single");
                setBattleCamera(app, 0x49, 0);
                hitCamera(app, g, 0, false);
                check(b.cam == 0x49, "hitcam-self-keeps");
                hitCamera(app, g, 1, false);
                check(b.cam == 0x5e && b.camActor == 1, "hitcam-member");
                b.rules.pair = true;
                hitCamera(app, g, 1, false);
                check(b.cam == 0x5f && b.camActor == 0, "hitcam-pair");
                b.rules.pair = false;
                g.action = -1;
                for (int id : {0xd7, 0xd8, 0xd9, 0xde, 0xe8, 0xf0, 0xf2, 0x197, 0x19a, 0x1b1, 0x1b6, 0x1fc, 0x1fe})
                    if (const cr::BattleActionDef* d = app.battleActions.get(id); g.action < 0 && d && d->scope != 3 && d->scope != 5) g.action = id;
                if (g.action >= 0) {
                    hitCamera(app, g, 2, false);
                    check(b.cam == b.formation + 0xc, "hitcam-still");
                }
                g.action = wide;
                hitCamera(app, g, 0, false);
                check(wide >= 0 && b.cam == 0x67, "hitcam-wide");
                b.rules.pair = keepPair, b.rules.enemyPair = keepEPair;
            }
            camOk = problems == before;
            // the attack spots (formation table 0xdec80)
            const int spotsBefore = problems;
            const int fm = b.formation;
            auto spotIs = [&](int user, int k, int target, int row, int entry, float dz) {
                cr::Vec3 o;
                return attackSpot(app, user, k, target, o) && near(o.x, cr::kBattlePosition[row][entry][0]) &&
                       near(o.z, cr::kBattlePosition[row][entry][1] + dz);
            };
            const bool keepPair = b.rules.pair, keepEPair = b.rules.enemyPair;
            b.rules.pair = false, b.rules.enemyPair = false;
            check(spotIs(0, 0, 2, fm, 3, 0) && spotIs(1, 0, 3, fm, 4, 0), "spot-member");
            const int sz = monsterSize(app, 2);
            const float dz = sz == 3 ? -10.0f : sz == 2 ? 5.0f : 0.0f;
            check(spotIs(2, 0, 1, 0, 4, dz), "spot-monster");
            b.rules.pair = true;
            check(spotIs(0, 0, 2, fm, 6, 0) && spotIs(1, 1, 2, fm, 7, 0), "spot-pair");
            { cr::Vec3 o; check(attackSpot(app, 2, 0, 0, o) && near(o.x, cr::kBattlePosition[1][3][0]) && near(o.z, o.x + dz), "spot-lone-at-pair"); }
            b.rules.enemyPair = true;
            check(spotIs(2, 0, 0, 1, 6, dz) && spotIs(3, 1, 0, 1, 7, 0), "spot-enemy-pair");
            b.rules.pair = keepPair, b.rules.enemyPair = keepEPair;
            spotsOk = problems == spotsBefore;
            // the reactions and the 0404 gating, through showHits
            const int reactBefore = problems;
            App::Battle::Actor* m0 = battleActor(app, 0);
            App::Battle::Actor* e2 = battleActor(app, 2);
            int attack = -1; // (an action of 0404's)
            for (int id = 0; id < app.battleActions.count() && attack < 0; ++id)
                if (const cr::BattleActionDef* d = app.battleActions.get(id); d && d->calc == 0) attack = id;
            auto hitOnce = [&](cr::BattleHit h) {
                b.turn = cr::BattleTurn();
                b.turn.kind = cr::kCmdAttack, b.turn.action = attack, b.turn.actors[0] = h.target < 2 ? 2 : 0;
                b.turn.hits = {h};
                for (App::Battle::Popup& p : b.popups) p.on = false;
                b.quake = App::Battle::Quake();
                b.effects.clear();
                showHits(app);
            };
            if (m0 && e2 && attack >= 0) {
                const LoadedModel& lm0 = b.models[m0->model];
                cr::BattleHit h;
                h.target = 0, h.amount = 0, h.type = cr::kHitMiss;
                playSlot(app, *m0, btlui::kSlotWin);
                hitOnce(h);
                check(m0->slot == realSlot(lm0, btlui::kSlotWin), "react-miss");
                check(b.quake.mode != 0 && !b.effects.empty(), "gate-miss-jolts");
                h.type = cr::kHitGuard;
                hitOnce(h);
                check(b.quake.mode == 0 && b.effects.empty(), "gate-fended");
                h.type = cr::kHitNormal, h.amount = 10;
                playSlot(app, *m0, btlui::kSlotGuard);
                const int guardSlot = m0->slot;
                hitOnce(h);
                check(m0->slot == guardSlot, "react-guarding");
                playSlot(app, *m0, btlui::kSlotIdle);
                hitOnce(h);
                check(m0->slot == realSlot(lm0, btlui::kSlotHit) && m0->then == btlui::kSlotIdle, "react-hit");
                h.revived = true;
                hitOnce(h);
                check(m0->slot == realSlot(lm0, btlui::kSlotRise) && m0->then == btlui::kSlotIdle, "react-revive");
                h.revived = false, h.fell = true;
                hitOnce(h);
                check(m0->slot == realSlot(lm0, btlui::kSlotHit) && m0->then == btlui::kSlotDown, "react-member-falls");
                h.target = 2;
                const bool boss = b.rules.f[2].def && b.rules.f[2].def->strongGrowth;
                hitOnce(h);
                if (!boss) check(e2->slot == realSlot(b.models[e2->model], btlui::kSlotHit) && e2->then == btlui::kSlotIdle && e2->fading, "react-monster-falls");
                reactOk = problems == reactBefore;
                gateOk = std::string(why).find("gate") == std::string::npos;
                // six blows at once: five numbers
                b.turn = cr::BattleTurn();
                b.turn.kind = cr::kCmdAttack, b.turn.action = attack, b.turn.actors[0] = 2;
                cr::BattleHit six;
                six.target = 0, six.amount = 5;
                b.turn.hits.assign(6, six);
                for (App::Battle::Popup& p : b.popups) p.on = false;
                showHits(app);
                const int on = int(std::count_if(std::begin(b.popups), std::end(b.popups), [](const App::Battle::Popup& p) { return p.on; }));
                check(on == 5, "numbers-five");
            }
            // the numbers' places (480 x 320 units)
            const int slotsBefore = problems;
            for (App::Battle::Popup& p : b.popups) p = App::Battle::Popup();
            b.popups[0].on = true, b.popups[0].target = 0, b.popups[0].amount = 7;
            float x = 10, y = 10;
            placePopup(b, 0, x, y, 2);
            check(near(x, 64) && near(y, 32 + 48), "place-slot0");
            b.popups[1].on = true, b.popups[1].amount = 12345;
            x = 470, y = 400;
            placePopup(b, 1, x, y, 2);
            check(near(x, 460 - 80) && near(y, 272), "place-right");
            b.popups[3].on = true, b.popups[3].amount = -50;
            x = 200, y = 100;
            placePopup(b, 3, x, y, 3);
            check(near(y, 128), "place-slot3-heal");
            b.popups[2].on = true, b.popups[2].amount = -9;
            x = 200, y = 90;
            placePopup(b, 2, x, y, 4);
            check(near(y, 60) && near(b.popups[2].y, 90), "place-slot2-heal");
            slotsOk = problems == slotsBefore;
            endBattle(app, false);
        } else
            check(false, "no-battle");
        problems += app.renderer.takeErrors();
        std::printf("battle presentation: cameras (cut, eased 1/5, orbit at the 3D distance, cast held, 0x48 looping, still, -100, cursor 1/2, roll, "
                    "0x1fe, gene / item hits) %s, attack spots %s, reactions %s, 0404 gating %s, number slots and places %s; %d problems%s\n",
                    camOk ? "yes" : "NO", spotsOk ? "yes" : "NO", reactOk ? "yes" : "NO", gateOk ? "yes" : "NO", slotsOk ? "yes" : "NO", problems,
                    why.c_str());
        mapErrors += problems;
        app.runner.stop();
        app.game = keepGame;
    }
    { // The battle scene as 3.1.0's: the opening (CRView::prc's white, btl_main 0..300, _btl_in_eff),
      // read_btlcpl's idle phases and names, _btl_shadow's fans, the casts' loops, mdl_put's turning,
      // btl_prc's projection
        int problems = 0;
        std::string why;
        auto check = [&](bool ok, const char* what) { if (!ok) ++problems, why += std::string(" ") + what; };
        auto near = [](float x, float y, float e = 0.01f) { return std::fabs(x - y) < e; };
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        bool openOk = false, soundOk = false, idleOk = false, shadowOk = false, castOk = false, turnOk = false, projOk = false, nameOk = false;
        int shadowsDrawn = 0, castFrames = 0, phases = 0, formation2 = 0;
        std::vector<float> introSteps;
        std::string strikeBanner;
        // _btl_in_eff's table
        soundOk = battleInSound(1) == 0xa8 && battleInSound(9) == 0xa9 && battleInSound(0x2b) == 0xa9 && battleInSound(0xae) == 0xa8 &&
                  battleInSound(0x105) == 0xaa && battleInSound(0x107) == 0xa8 && battleInSound(0x11b) == 0xaa && battleInSound(0x15f) == 0xa9 &&
                  battleInSound(0x165) == 0xab && battleInSound(0x170) == 0xaa && battleInSound(0x171) == 0xa8 && battleInSound(0x179) == 0xa9;
        check(soundOk, "in-eff-table");
        // a group of two or more monsters (their idles out of step)
        int group = 1;
        for (int g = 1; g <= app.battleData.groupCount(); ++g) {
            const std::array<int, 3> ids = app.battleData.group(g);
            const cr::MonsterDef *m0 = app.battleData.monster(ids[0]), *m1 = app.battleData.monster(ids[1]);
            if (m0 && m1 && m0->model >= 0 && m1->model >= 0 && m0->model < cr::kNumBattleModels && m1->model < cr::kNumBattleModels &&
                app.battleData.formation(ids) != 2) {
                group = g;
                break;
            }
        }
        app.soundLog.clear();
        app.rng.seed(5);
        startBattle(app, {group, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        if (b.active) {
            soundOk &= std::find(app.soundLog.begin(), app.soundLog.end(), uint32_t(battleInSound(group))) != app.soundLog.end();
            check(soundOk, "in-eff-played");
            // monsters' idles: each frame below slot 0's max frame, not all alike
            std::set<int> seen;
            bool inRange = true;
            for (const App::Battle::Actor& a : b.actors)
                if (a.fighter >= 2 && a.fighter <= 4) {
                    seen.insert(int(a.time));
                    inRange &= a.time >= 0 && a.time < std::max(1.0f, actorDuration(app, a) - 1.0f);
                    ++phases;
                }
            idleOk = phases >= 2 && seen.size() >= 2 && inRange;
            check(idleOk, "idle-phases");
            // the white held, 200's fade and still camera, 300's turning one - cut to on its first frame,
            // eased in under the fade (the steps growing to the turn's 15th frame, then shrinking by about a
            // fifth a frame), settled before the commands; the commands 32 frames on (300's counter 30,
            // then 400, then __menub_mainPrc 0) with their camera (formation + 0xc) cut to, nothing set between
            const int fm = std::max(0, b.formation - 2);
            bool white = true;
            for (int f = 1; f < kIntroWhite; ++f) {
                stepBattle(app);
                white &= app.fadeFrom == 1 && app.fadeTo == 1 && app.fadeGrey == 1;
            }
            check(white, "white-held");
            stepBattle(app); // 200
            check(app.fadeFrom == 1 && app.fadeTo == 0 && app.fadeT == 0 && app.fadeFrames == 20 && b.cam == fm && b.camActor == -1, "state-200");
            stepBattle(app); // 300's first
            const cr::BattleCamera* c = app.battleData.camera(fm);
            check(b.cam == fm && b.camActor == 0 && c && b.camDur == c->raw[15] && b.camFrame == 1 && b.camSnapped, "state-300-turn");
            if (c) check(fm <= 0xd && c->raw[14] == -20 && c->raw[15] == 15, "formation-camera-turns");
            std::vector<float> steps; // the shown eye's move each frame, 300's second frame on
            cr::Vec3 last = b.camEye;
            const int sets = b.camSets;
            int toCommand = kIntroWhite + 1;
            for (; toCommand < 100 && b.phase == App::Battle::Intro; ++toCommand) {
                stepBattle(app);
                if (b.phase != App::Battle::Intro) break;
                const cr::Vec3 d = b.camEye - last;
                steps.push_back(std::sqrt(cr::dot(d, d)));
                last = b.camEye;
            }
            ++toCommand;
            check(toCommand == kIntroWhite + 32, "intro-32");
            check(b.camSets == sets + 1 && b.cam == b.formation + 12 && b.camSnapped, "command-camera");
            bool rising = steps.size() >= 27, falling = rising;
            for (size_t i = 1; i < 14 && rising; ++i) rising = steps[i] > steps[i - 1]; // 300's frames 2..15
            for (size_t i = 15; i < 27 && falling; ++i) falling = steps[i] < steps[i - 1] && steps[i] > steps[i - 1] * 0.7f;
            const float peak = steps.empty() ? 0.0f : *std::max_element(steps.begin(), steps.end());
            check(rising && falling && peak > 0.5f && steps[13] == peak && steps.back() < peak * 0.05f, "intro-move");
            introSteps = steps;
            // ambushed: 400 goes to 500 - the enemies' round, no menu, the turned camera kept
            endBattle(app, false);
            app.rng.seed(5);
            startBattle(app, {group, 0, -1, 2}, 0);
            int ambushAt = 0;
            for (; ambushAt < 100 && b.phase == App::Battle::Intro; ++ambushAt) stepBattle(app);
            check(ambushAt == kIntroWhite + 31 && b.phase == App::Battle::Acting && b.cam == fm && b.camActor == 0, "ambush-400");
            // a first strike: "<the lead>'s team attacks first!" (sys 0x36, var 0 the lead's name) for 60 frames
            endBattle(app, false);
            startBattle(app, {group, 0, -1, 1}, 0);
            for (int f = 0; f < kIntroWhite + 1 && b.phase == App::Battle::Intro; ++f) stepBattle(app);
            {
                const App::Battle::Actor* lead = battleActor(app, 0);
                check(lead && !lead->name.empty() && b.banner == sysText(app, 57054, lead->name) && b.banner.rfind(lead->name, 0) == 0,
                      "strike-banner");
                strikeBanner = b.banner;
            }
            // formation 2 (a boss's): 300's first frame camera 0x21b (-25 over 30), the commands 62 on
            const cr::BattleCamera* c2 = app.battleData.camera(0x21b);
            check(c2 && c2->raw[14] == -25 && c2->raw[15] == 30, "camera-0x21b");
            for (int g = 1; g <= app.battleData.groupCount(); ++g)
                if (app.battleData.formation(app.battleData.group(g)) == 2) {
                    endBattle(app, false);
                    startBattle(app, {g, 0, -1, 0}, 0);
                    if (!b.active || b.formation != 2) break;
                    for (int f = 0; f < kIntroWhite; ++f) stepBattle(app);
                    check(b.cam == 0 && b.camActor == -1, "formation-2-200");
                    stepBattle(app);
                    check(b.cam == 0x21b && b.camActor == 0 && b.camDur == 30 && b.camFrame == 1, "formation-2-0x21b");
                    int at = kIntroWhite + 1;
                    for (; at < 150 && b.phase == App::Battle::Intro; ++at) stepBattle(app);
                    check(at == kIntroWhite + 62 && b.cam == 0xe, "formation-2-62");
                    formation2 = g;
                    break;
                }
            check(formation2 > 0, "formation-2-group");
            endBattle(app, false);
            app.rng.seed(5);
            startBattle(app, {group, 0, -1, 0}, 0);
            for (int f = 0; f < 100 && b.phase == App::Battle::Intro; ++f) stepBattle(app);
            openOk = why.find("white") == std::string::npos && why.find("state-") == std::string::npos && why.find("intro") == std::string::npos &&
                     why.find("formation-") == std::string::npos && why.find("command-") == std::string::npos &&
                     why.find("ambush") == std::string::npos && why.find("0x21b") == std::string::npos;
            // the shadows: one fan per fighter, the pair at 204, a monster's fading with it
            App::Battle::Actor* m0 = battleActor(app, 0);
            App::Battle::Actor* e2 = battleActor(app, 2);
            app.lastTicks = 0;
            drawFrame(app, false);
            auto shadowOf = [&](int k) -> const App::Battle::Shadow* {
                for (const App::Battle::Shadow& sh : b.shadows)
                    if (sh.fighter == k) return &sh;
                return nullptr;
            };
            shadowsDrawn = int(b.shadows.size());
            const App::Battle::Shadow* s0 = shadowOf(0);
            check(s0 && near(s0->alpha, 204.0f / 255.0f, 0.001f) && s0->rx > 0 && b.shadowTris.size() == b.shadows.size() * 32 * 9, "shadow-member");
            if (s0 && m0) check(near(s0->at.y, m0->pos.y + 0.1f), "shadow-height");
            const cr::MonsterDef* d2 = b.rules.f[2].def;
            const bool e2Has = d2 && (d2->raw[0x42] | d2->raw[0x43]) != 0;
            check((shadowOf(2) != nullptr) == e2Has, "shadow-monster-gated");
            if (e2 && e2Has) {
                e2->alpha = 0.5f;
                drawFrame(app, false);
                const App::Battle::Shadow* s2 = shadowOf(2);
                check(s2 && near(s2->alpha, float(80 * 128 / 100) / 255.0f, 0.001f), "shadow-monster-fade");
                e2->hidden = true, e2->alpha = 1;
                drawFrame(app, false);
                check(!shadowOf(2), "shadow-hidden");
                e2->hidden = false;
            }
            shadowOk = why.find("shadow") == std::string::npos;
            { // btl_prc's projection: near 1, far 3500
                const cr::Mat4 want = cr::Mat4::perspective(b.fov * 3.14159265f / 180.0f, app.renderer.screenW() / cr::Renderer::kLogicalH, 1.0f, 3500.0f);
                projOk = near(b.proj.m[10], want.m[10], 1e-5f) && near(b.proj.m[14], want.m[14], 1e-4f);
                check(projOk, "projection");
            }
            // mdl_put's turning: 15 degrees a frame toward the wanted yaw; an attacker's wanted yaw, not snapped
            if (m0) {
                const float y0 = m0->yaw, quarter = 3.14159265f / 2.0f, deg = 3.14159265f / 180.0f, full = 2 * 3.14159265f;
                m0->wantYaw = y0 + quarter;
                turnActors(app);
                bool ok = near(std::remainder(m0->yaw - y0, full), 15 * deg, 1e-4f);
                for (int f = 0; f < 5; ++f) turnActors(app);
                ok &= near(std::remainder(m0->yaw - (y0 + quarter), full), 0, 1e-4f) && m0->turnStep == 15;
                m0->yaw = m0->wantYaw = y0;
                faceFighter(app, *m0, 2);
                ok &= m0->yaw == y0 && (!e2 || near(m0->wantYaw, std::atan2(e2->pos.x - m0->pos.x, e2->pos.z - m0->pos.z)));
                turnOver(*m0, y0, 3); // (the attack's end: home over 3 frames)
                for (int f = 0; f < 3; ++f) turnActors(app);
                ok &= near(std::remainder(m0->yaw - y0, full), 0, 1e-4f);
                turnOk = ok;
                check(turnOk, "turning");
            }
            // a gene's cast: the slot played to its loop's end, then that loop over and over
            int gene = -1;
            for (int id = 1; id < app.battleActions.count() && gene < 0; ++id)
                if (const cr::BattleActionDef* d = app.battleActions.get(id); d && d->cls == 11 && d->motion != 1) gene = id;
            if (m0 && gene >= 0) {
                const LoadedModel& lm = b.models[m0->model];
                const int slot = realSlot(lm, 11);
                const cr::BattleAnim ca = app.battleData.anim(m0->modelId, slot);
                b.turn = cr::BattleTurn();
                b.turn.kind = cr::kCmdGene, b.turn.action = gene, b.turn.actors[0] = 0;
                startTurn(app);
                bool ok = m0->slot == slot && m0->then == -1;
                if (ca.loopEnd > ca.loopStart) {
                    ok &= m0->loopFrom == ca.loopStart && m0->loopTo == ca.loopEnd;
                    float top = 0, prev = m0->time;
                    bool wrapped = false;
                    b.phase = App::Battle::Command, b.turnOn = false; // (the actor alone steps on)
                    for (int f = 0; f < ca.loopEnd + 2 * (ca.loopEnd - ca.loopStart) + 4; ++f) {
                        stepBattle(app);
                        top = std::max(top, m0->time);
                        wrapped |= m0->time < prev;
                        prev = m0->time;
                        ++castFrames;
                    }
                    ok &= wrapped && top < float(ca.loopEnd) && m0->slot == slot && m0->time >= float(ca.loopStart);
                } else
                    castFrames = -1;
                castOk = ok;
                check(castOk, "cast-loop");
            }
            endBattle(app, false);
        } else
            check(false, "no-battle");
        { // read_btlcpl's name row: model 0xa5 Dante's (0x106) in the second scenario
            bool ok = true;
            int found = 0;
            for (int id : {126, 141})
                if (const cr::MonsterDef* m = app.battleData.monster(id); m && m->model == 0xa5) {
                    ++found;
                    app.game.scenario = 1;
                    ok &= monsterNameRow(app, *m) == 0x106;
                    app.game.scenario = 0;
                    ok &= monsterNameRow(app, *m) == id - 1;
                }
            nameOk = ok && found == 2;
            check(nameOk, "name-0x106");
        }
        problems += app.renderer.takeErrors();
        std::printf("battle scene: the opening (white %d steps, 200's fade and still camera, 300's turning one eased in under the fade - "
                    "eye steps %.2f / %.2f (peak, 300's 15th) / %.3f (the last) -, 32 to the commands and their camera, an ambush's 31 to the "
                    "enemies' round, formation 2's 0x21b and 62 (group %d), a first strike's \"%s\") %s, "
                    "start sound %s, idle phases (%d monsters) %s, shadows (%d fans, 204 / 80%% of the fade, hidden none) %s, cast loop "
                    "(%d frames) %s, turning 15/frame %s, near 1 far 3500 %s, m010's name row %s; %d problems%s\n",
                    kIntroWhite, introSteps.empty() ? 0.0f : introSteps[0], introSteps.size() > 13 ? introSteps[13] : 0.0f,
                    introSteps.empty() ? 0.0f : introSteps.back(), formation2, strikeBanner.c_str(), openOk ? "yes" : "NO", soundOk ? "yes" : "NO", phases, idleOk ? "yes" : "NO", shadowsDrawn, shadowOk ? "yes" : "NO",
                    castFrames, castOk ? "yes" : "NO", turnOk ? "yes" : "NO", projOk ? "yes" : "NO", nameOk ? "yes" : "NO", problems, why.c_str());
        mapErrors += problems;
        app.runner.stop();
        app.game = keepGame;
    }
    { // The Break changing hands (btlord_chk -1, btlbreak_effect_*): the effect plays 41 frames,
      // the battle waiting; half way the pair takes the Break at 4
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        int frames = 0, gaugeAt = -1;
        bool waited = true;
        if (b.active) {
            for (int f = 0; f < 60 && b.phase != App::Battle::Command; ++f) stepBattle(app);
            b.phase = App::Battle::Acting, b.turnOn = false;
            b.rules.breakStarted = true;
            b.rules.breakGauge = b.rules.breakTarget = 0;
            b.rules.breakFlags |= 0x400 | 0x800; // the enemies lost it
            startBreakFx(app);
            for (; frames < 200 && b.breakFx.on; ++frames) {
                const int round = b.rules.round;
                stepBattle(app);
                waited &= !b.turnOn && b.rules.round == round;
                if (gaugeAt < 0 && b.rules.breakGauge == 4) gaugeAt = frames;
                if (frames == 20 && SDL_getenv("CR_BREAK_SHOT")) {
                    app.lastTicks = 0;
                    drawFrame(app, false);
                    saveFrame(app, SDL_getenv("CR_BREAK_SHOT"));
                }
            }
            endBattle(app, false);
        }
        problems += frames != 41 || gaugeAt != 19 || !waited || b.rules.breakChanging();
        std::printf("break effect: %d frames, the pair holds 4 from frame %d, the battle waited %s; %d problems\n", frames,
                    gaugeAt, waited ? "yes" : "NO", problems);
        mapErrors += problems;
        app.runner.stop();
        app.game = keepGame;
    }
    { // The Vita's screen (layout 3): 960 x 544; the layout's 2D as the iPhone layout's (x 1.2: the Vita's
      // 0.85 of 960 x 640), its pictures' layer 1 / 0.85 of that - tex2d x 0..480 the screen's whole
      // width -, 12 Vita pixels up; the picture's centre
        int problems = 0;
        app.video.layout = 3;
        applyVideo(app);
        cr::Renderer& r = app.renderer;
        drawFrame(app, false);
        const float px = cr::Renderer::kLogicalH / 544.0f; // logical units a Vita pixel
        const bool screen = r.vitaScreen() && std::fabs(r.screenW() - 960 * px) < 0.5f;
        float ux = 32 + r.uiX(), uy = 64;
        r.layoutToScreen(ux, uy); // the UI: tex2d (0, 0) 72 Vita px in, 48 above the top (0.85 of 960 x 640)
        r.setLayer(3);
        float lx = 32 + r.uiX(), ly = 64, rx = 992 + r.uiX(), ry = 704;
        r.layoutToScreen(lx, ly);
        r.layoutToScreen(rx, ry);
        float bx = 500, by = 300;
        r.layoutToScreen(bx, by);
        r.screenToLayout(bx, by);
        r.setLayer(0);
        const bool ui = std::fabs(ux - 72 * px) < 0.5f && std::fabs(uy - 0) < 0.5f;
        const bool pictures = std::fabs(lx) < 0.5f && std::fabs(rx - r.screenW()) < 0.5f && std::fabs(ly + 60 * px) < 0.5f &&
                              std::fabs(ry - (544 + 36) * px) < 0.5f && std::fabs(bx - 500) < 0.01f && std::fabs(by - 300) < 0.01f;
        problems += !screen + !ui + !pictures;
        std::printf("vita screen: %.1f wide %s, the UI's tex2d (0, 0) at (%.1f, %.1f) %s, the pictures' x 0..480 at %.1f..%.1f, y 0..320 at "
                    "%.1f..%.1f %s; %d problems\n", r.screenW(), screen ? "yes" : "NO", ux, uy, ui ? "yes" : "NO", lx, rx, ly, ry,
                    pictures ? "yes" : "NO", problems);
        mapErrors += problems;
        app.video.layout = 1;
        applyVideo(app);
    }
    { // The iPhone layout (2.2.0's screens): a 3:2 screen, the layout's safe area filling it, taps
      // mapped back; the field menu's corner button where 2.2.0 has it, and the converted screens
      // drawing without render API errors
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.video.layout = 2;
        applyVideo(app);
        cr::Renderer& r = app.renderer;
        drawFrame(app, false); // (the frame maps the 3:2 screen)
        float x0 = 32, y0 = 64, x1 = 992 + r.uiX(), y1 = 704;
        x0 += r.uiX();
        r.layoutToScreen(x0, y0);
        r.layoutToScreen(x1, y1);
        const bool frame = r.phoneLayout() && std::fabs(r.screenW() / cr::Renderer::kLogicalH - 1.5f) < 0.01f;
        const bool fills = std::fabs(x0) < 0.5f && std::fabs(y0) < 0.5f && std::fabs(x1 - r.screenW()) < 0.5f &&
                           std::fabs(y1 - cr::Renderer::kLogicalH) < 0.5f;
        float bx = 500, by = 300;
        r.layoutToScreen(bx, by);
        r.screenToLayout(bx, by);
        const bool inverse = std::fabs(bx - 500) < 0.01f && std::fabs(by - 300) < 0.01f;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        int errs = 0;
        // the corner button: 2.2.0's box (440, 0, 40, 40) - tex2d -> layout -> screen -> pixels
        float cx = 2.0f * (440 + 20) + 32 + r.uiX(), cy = 2.0f * 20 + 64;
        r.layoutToScreen(cx, cy);
        const float kx = float(r.frameWidth()) / r.screenW();
        app.fieldMenu = App::FieldMenu();
        app.fieldMenu.fade = 320;
        const float ox = (float(r.frameWidth()) - r.screenW() * std::min(kx, float(r.frameHeight()) / cr::Renderer::kLogicalH)) / 2;
        const float sc = std::min(kx, float(r.frameHeight()) / cr::Renderer::kLogicalH);
        const float oy = (float(r.frameHeight()) - cr::Renderer::kLogicalH * sc) / 2;
        const bool corner = fieldMenuClick(app, ox + cx * sc, oy + cy * sc) && app.fieldMenu.open;
        app.fieldMenu = App::FieldMenu();
        openCamp(app);
        const int entries = campEntries(app);
        app.lastTicks = 0;
        drawFrame(app, false);
        errs += r.takeErrors();
        app.campOpen = false;
        startBattle(app, {1, 0, -1, 0}, 0);
        for (int f = 0; f < 40 && app.battle.active; ++f) stepBattle(app);
        app.lastTicks = 0;
        drawFrame(app, false);
        errs += r.takeErrors();
        if (app.battle.active) endBattle(app, false);
        showMessages(app, {57006});
        drawFrame(app, false);
        errs += r.takeErrors();
        app.dialogue.clear();
        openScenarios(app);
        const bool chart = app.scc.sheet.id != 0;
        drawFrame(app, false);
        errs += r.takeErrors();
        closeScenarios(app);
        problems += !frame || !fills || !inverse || !corner || entries != 5 || !chart || errs;
        std::printf("iphone layout: 3:2 screen %s, the safe area fills it %s, taps map back %s, the corner button %s, "
                    "camp %d plates, Scenarios drawn %s, %d render API errors; %d problems\n", frame ? "yes" : "NO",
                    fills ? "yes" : "NO", inverse ? "yes" : "NO", corner ? "opens" : "DOESN'T open", entries,
                    chart ? "yes" : "NO", errs, problems);
        mapErrors += problems;
        app.video.layout = 1;
        applyVideo(app);
        app.runner.stop();
        app.game = keepGame;
    }
    { // Widescreen (16:9): the screens' left- and right-hand groups at the screen's edges, taps
      // following them - the field's corner button, the Camp's plates, the title's, the save
      // screen's - and full-screen pictures covering the whole width
        int problems = 0;
        const cr::GameState keepGame = app.game;
        const App::Video keepVideo = app.video;
        app.video.widescreen = true, app.video.layout = 1, app.video.display = 0;
        app.video.width = 1280, app.video.height = 720;
        applyVideo(app);
        if (app.window) SDL_SyncWindow(app.window);
        cr::Renderer& r = app.renderer;
        app.lastTicks = 0;
        drawFrame(app, false);
        const float ux = r.uiX();
        float l0x, l0y, l1x, l1y; // logical <-> the frame's pixels (straight lines off the iPhone layout)
        r.toLogical(0, 0, l0x, l0y);
        r.toLogical(1000, 1000, l1x, l1y);
        auto pixel = [&](float lx, float ly, float& px, float& py) {
            px = (lx - l0x) * 1000 / (l1x - l0x), py = (ly - l0y) * 1000 / (l1y - l0y);
        };
        auto tap = [&](float origin, float mx, float my, float& px, float& py) { // (menu units: PhoneMenu's offset)
            if (phoneMenus(app)) mx += 16, my += 32;
            pixel(origin + 2 * mx, 2 * my, px, py);
        };
        float px, py;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        app.fieldMenu = App::FieldMenu();
        app.fieldMenu.fade = 320;
        tap(wideRight(app), 456 + 20, -32 + 20, px, py); // the corner button's box (tex2d; tap adds the (16, 32))
        const bool corner = fieldMenuClick(app, px, py) && app.fieldMenu.open;
        app.fieldMenu = App::FieldMenu();
        openCamp(app);
        app.camp.anim = 100;
        app.game.setFlag(0x39, true); // (Items' once-only message seen)
        tap(wideRight(app), campui::kPlateX + 60, campui::kPlateY + 20, px, py); // Items
        campClick(app, px, py);
        bool camp = app.camp.page == App::Camp::Items;
        app.lastTicks = 0;
        drawFrame(app, false);
        int errs = r.takeErrors();
        { // the Camp's other boxes: a face board in a member choice (the left's), the Back bar's
          // arrows (centred: 0xc0 / 0x175), the leader's face board in the second scenario
            App::Camp& c = app.camp;
            campOpenPage(app, 1); // Equip: the second member's face board (8, 0x30 + 0x58, 0xfc x 0x50)
            c.anim = 100;
            tap(wideLeft(app), 8 + 120, 48 + 88 + 40, px, py);
            campClick(app, px, py);
            const bool face = c.phase == App::Camp::Slot && c.member == 1;
            tap(ux, 0x175 + 20, 286 + 17, px, py); // -2: the other member's slots
            campClick(app, px, py);
            const bool swapArrow = c.phase == App::Camp::Slot && c.member == 0;
            campOpenPage(app, 3), c.anim = 100;
            campKey(app, SDLK_RETURN);
            tap(ux, 0xc0 + 20, 286 + 17, px, py); // -3 on the Status board: the skills
            campClick(app, px, py);
            const bool statusArrow = c.page == App::Camp::Status && c.phase == App::Camp::Slot && c.statusView == 2;
            openCamp(app), c.anim = 100; // the main screen, the second scenario after flag 0x636
            const cr::GameState keepLeader = app.game;
            const int keepChar = app.playerChar;
            app.game.scenario = 1;
            app.game.setFlag(0x636, true);
            const int second = campCharacter(app, 1);
            app.game.leaders[1] = campCharacter(app, 0);
            tap(wideLeft(app), 120, 48 + 88 + 40, px, py);
            campClick(app, px, py);
            const bool leader = campMembers(app) < 2 || (app.game.leaders[1] == second && app.playerChar == second);
            app.game = keepLeader;
            if (app.playerChar != keepChar) app.playerChar = keepChar, placePlayer(app);
            camp = camp && face && swapArrow && statusArrow && leader;
            if (!camp)
                std::printf("widescreen: Camp taps: a face board %s, the slots' arrow %s, the Status arrow %s, the leader %s\n",
                            face ? "yes" : "NO", swapArrow ? "yes" : "NO", statusArrow ? "yes" : "NO", leader ? "yes" : "NO");
        }
        app.campOpen = false;
        openSaveScreen(app, false);
        tap(wideRight(app), 328 + 60, 136 + 48.0f * float(savePlates(app).empty() ? 0 : savePlates(app)[0]) + 20, px, py);
        saveScreenClick(app, px, py);
        bool save = app.saveScreen.phase == 1; // asking "Save data?"
        drawFrame(app, false);
        tap(wideRight(app), 336 + 68, 160 + 20, px, py); // No's plate (drawn at (336, 160) from the right)
        saveScreenClick(app, px, py);
        if (app.saveScreen.open) std::printf("widescreen: the save question's No plate DIDN'T answer\n");
        save = save && !app.saveScreen.open;
        errs += r.takeErrors();
        app.saveScreen.open = false;
        app.titleOpen = true;
        app.titleStage = 1; // (past the title's tap-to-start)
        app.titleCursor = 2;
        tap(wideRight(app), 328 + 60, titlePlateY(0) + 20, px, py); // Start's plate
        const int before = app.titleCursor;
        titleClick(app, px, py);
        const bool title = app.titleCursor == 0 && before == 2;
        app.titleOpen = false;
        app.saveScreen.open = false; // (Start went on to the save / load screen)
        if (app.charSelectOpen) closeCharSelect(app, 0);
        app.optionsOpen = false;
        problems += ux <= 0 || !corner || !camp || !save || !title || errs;
        std::printf("widescreen: the screen %.0f wide, the corner button %s, Camp's Items %s, the save plate %s, the title's Start %s, "
                    "%d render API errors; %d problems\n", r.screenW(), corner ? "opens" : "DOESN'T open", camp ? "yes" : "NO",
                    save ? "yes" : "NO", title ? "yes" : "NO", errs, problems);
        mapErrors += problems;
        app.video = keepVideo;
        applyVideo(app);
        if (app.window) SDL_SyncWindow(app.window);
        app.runner.stop();
        app.game = keepGame;
    }
    { // These scripts show a loaded actor at the origin without a Setc (__EvtBorn only clears its hide
      // bit, wherever it stands): the scenes' people, and a stage model as d0662's.
        int problems = 0;
        struct Backdrop { int area, map, npc; const char* model; };
        static const Backdrop backdrops[] = {
            {3, 19, 425, "c212e.bin"}, {7, 72, 467, "c604.bin"}, {7, 72, 421, "c124.bin"}, {7, 72, 524, "d0662bt.bin"}
        };
        const cr::GameState keepGame = app.game;
        for (const Backdrop& b : backdrops) {
            const auto map = std::find(app.maps.begin(), app.maps.end(), cr::kMapCollisionFile[b.map]);
            if (map == app.maps.end()) { ++problems; continue; }
            showMap(app, int(map - app.maps.begin()), b.map, b.area);
            app.runner.stop();
            app.pendingEntry = App::Entry::None;
            ViewerHost host(app);
            auto drawn = [&](int id) { // (drawn: a model mdl_put puts; a hidden NPC's stays held, out of npcDraws)
                int k = 0;
                for (const App::NpcDraw& d : app.npcDraws) k += d.id == id;
                return k;
            };
            host.loadNpc(b.npc);
            host.showNpc(b.npc, false);
            const bool hidden = drawn(b.npc) == 0 && host.npc(b.npc).size() == 1;
            // Killed, its model still takes Anim (__EvtAnim: npc ctl +0, the hidden bit not looked at) and holds
            // it - _mdl_put steps nothing of a model at colour 0
            auto frame = [&] { stepEvents(app); animateNpcs(app, 1.0f / kFramesPerSecond); };
            host.playAnim(b.npc, 1, 0, 0);
            frame();
            const auto one = [&] { const auto v = host.npc(b.npc); return v.size() == 1 ? v[0] : nullptr; };
            bool held = drawn(b.npc) == 0 && one() && one()->action == 1 && one()->time == 0;
            // NpcFade over frames on it: _mdl_set_alpha's colour 0 -> 1 puts it back in mdl_put - drawn, fading
            host.fadeNpc(b.npc, 0, 1, 40);
            held = held && drawn(b.npc) == 1 && one() && one()->fadeLeft == 40;
            host.showNpc(b.npc, false); // (a Kill mid-fade: colour 0 - not drawn, the fade where it was)
            frame();
            held = held && drawn(b.npc) == 0 && one() && one()->fadeLeft == 40 && one()->alpha < 0.05f;
            host.showNpc(b.npc, true);
            const auto draws = host.npc(b.npc);
            const bool shown = drawn(b.npc) == 1 && draws.size() == 1 && draws[0]->model == b.model && draws[0]->world.m[12] == 0 &&
                               draws[0]->world.m[13] == 0 && draws[0]->world.m[14] == 0 &&
                               draws[0]->alpha < 0.05f && draws[0]->fadeLeft == 40 && draws[0]->action == 1;
            for (auto* d : host.npc(b.npc)) d->alpha = 1, d->fadeLeft = 0;
            host.showNpc(b.npc, false);
            const bool killed = drawn(b.npc) == 0;
            const int sceneProblems = !hidden + !held + !shown + !killed + app.renderer.takeErrors();
            std::printf("cutscene actor %s area %d: hidden on Kill %s, its Anim held while killed, a fade drawing it, a Kill "
                        "holding it %s, shown at origin on Born as it was left %s, hidden again %s; %d problems\n",
                        b.model, b.area, hidden ? "yes" : "NO", held ? "yes" : "NO", shown ? "yes" : "NO", killed ? "yes" : "NO",
                        sceneProblems);
            problems += sceneProblems;
        }
        app.game = keepGame;
        mapErrors += problems;
    }
    { // Placed actors' floor (__EvtFrmSetc2 / __EvtSetc: _get_floor(x, y, z, 10) - getFloor within 20 of the
      // record's y, in the KD node's floor list) and the hall's news marks (FlagWindow type 1)
        int problems = 0;
        const cr::GameState keepGame = app.game;
        auto show = [&](int area, int map) {
            const auto it = std::find(app.maps.begin(), app.maps.end(), cr::kMapCollisionFile[map]);
            if (it == app.maps.end()) return false;
            showMap(app, int(it - app.maps.begin()), map, area);
            app.runner.stop();
            app.pendingEntry = App::Entry::None;
            return true;
        };
        ViewerHost host(app);
        // j001_t01's stage (map 191, t0150_10: its one square listed at node +10, no floor): npc 419 stays
        // at the record's 12.7 over it (Part 1 1631 s)
        bool stageY = false, near = false, far = false;
        if (show(9, 191)) {
            host.loadNpc(419);
            const float rec[6] = {0, 12.7f, -1.9f, 0, 0, 0};
            host.placeNpc(419, rec, 0, cr::EventHost::Facing::Keep);
            const cr::FieldNpc* n = findNpc(app, 419);
            stageY = n && std::fabs(n->pos[1] - 12.7f) < 1e-3f && app.collisionData.nodes.size() == 1 &&
                     app.collisionData.nodes[0].floors.empty();
        } else ++problems;
        // the hall (map 192): a person placed 15 over the floor the leader stands on is put on it, 25 over it
        // stays where the record has it
        const cr::FieldNpc* person = nullptr;
        if (show(10, 192)) {
            for (const cr::FieldNpc& n : app.npcs)
                if ((n.kind & 0x10) && n.link[0] > 0 && n.model >= 0) { person = &n; break; }
            float h;
            const cr::Vec3 at = app.walker.pos;
            if (person && app.havePlayer && cr::getFloor(app.collisionData, cr::searchNode(app.collisionData, at, 10), at, 1000, h) >= 0) {
                const int id = person->id;
                const float rec15[6] = {at.x, h + 15, at.z, 0, 0, 0}, rec25[6] = {at.x, h + 25, at.z, 0, 0, 0};
                host.placeNpc(id, rec25, 0, cr::EventHost::Facing::Keep);
                far = std::fabs(findNpc(app, id)->pos[1] - (h + 25)) < 1e-3f;
                host.placeNpc(id, rec15, 0, cr::EventHost::Facing::Keep);
                near = std::fabs(findNpc(app, id)->pos[1] - h) < 1e-3f;
            }
        } else ++problems;
        // its news marks: up while the row's +22 flag holds, none once talked to until back in reach (state 2),
        // up again at the next load (FlagWindow::init)
        bool marks = false;
        if (person) {
            const int id = person->id, flag = person->link[0];
            app.game.setFlag(flag, true);
            app.npcShown[id] = true; // (standing in the room whatever this state's create flags say)
            const bool up = flagMarkUp(app, *person);
            app.flagTalked.insert(id);
            const bool talked = !flagMarkUp(app, *person);
            npcSetup(app);
            app.npcShown[id] = true;
            const bool reloaded = flagMarkUp(app, *person);
            app.game.setFlag(flag, false);
            marks = up && talked && reloaded && !flagMarkUp(app, *person);
            npcSetup(app);
        }
        problems += !stageY + !near + !far + !marks + app.renderer.takeErrors();
        std::printf("placed actors: npc 419 over the stage's unlisted square at 12.7 %s, 15 over a floor put on it %s, "
                    "25 over it kept %s; the hall's news mark up on its flag, not after a talk, back on load %s; %d problems\n",
                    stageY ? "yes" : "NO", near ? "yes" : "NO", far ? "yes" : "NO", marks ? "yes" : "NO", problems);
        app.game = keepGame;
        mapErrors += problems;
    }
    { // A controller, as the Vita's buttons: a virtual one - the stick walks the leader, △ opens the
      // Camp and closes it, ○ confirms, × goes back (and does nothing on the field), the D-pad
      // moves the cursor; ×-confirm swaps the two
        int problems = 0;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = 6, desc.nbuttons = 15;
        desc.axis_mask = (1u << 6) - 1, desc.button_mask = (1u << 15) - 1; // (the gamepad's own order)
        desc.name = "selftest pad";
        // (an outside pad - attached first, so SDL lists it ahead of the test's own - that no case may take: a
        // stand-in for another program's virtual controller; the cases' pads in padAllow)
        SDL_VirtualJoystickDesc foreignDesc = desc;
        foreignDesc.name = "Xbox 360 Controller";
        const SDL_JoystickID foreign = SDL_AttachVirtualJoystick(&foreignDesc);
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        const std::vector<SDL_JoystickID> keepAllow = app.padAllow;
        app.padAllow = {id};
        SDL_Gamepad* pad = id ? SDL_OpenGamepad(id) : nullptr;
        SDL_Joystick* js = pad ? SDL_GetGamepadJoystick(pad) : nullptr;
        if (!pad) {
            std::printf("controller: no virtual controller here (%s), skipped\n", SDL_GetError());
        } else {
            const cr::GameState keepGame = app.game;
            SDL_Gamepad* keepPad = app.pad;
            app.pad = pad;
            padLive(app); // (a frame with nothing held)
            app.game = cr::GameState();
            cr::giveStartingItems(app.game, app.itemTable);
            showMap(app, 0, 0, 2);
            app.pendingEntry = App::Entry::None;
            app.runner.stop();
            app.dialogue.clear();
            const cr::Vec3 from = app.walker.pos;
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, 32767);
            SDL_UpdateJoysticks();
            for (int f = 0; f < 15; ++f) updatePlayer(app, 1.0f / kFramesPerSecond, 0);
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_UpdateJoysticks();
            const cr::Vec3 d = app.walker.pos - from;
            const bool walked = std::sqrt(cr::dot(d, d)) > 1.0f;
            app.walker.pos = from;
            app.runner.stop(); // (anything the walk set off)
            app.dialogue.clear();
            app.pendingEntry = App::Entry::None;
            padButton(app, SDL_GAMEPAD_BUTTON_SOUTH); // × on the field: nothing
            const bool fieldQuiet = !app.campOpen && app.havePlayer;
            padButton(app, SDL_GAMEPAD_BUTTON_NORTH); // △: the Camp
            const bool opened = app.campOpen;

            const int c0 = app.camp.cursor;
            padButton(app, SDL_GAMEPAD_BUTTON_DPAD_UP);
            const bool moved = app.camp.cursor != c0;
            app.camp.cursor = 0;
            app.game.setFlag(0x39, true); // (Items' once-only message seen)
            padButton(app, SDL_GAMEPAD_BUTTON_EAST); // ○: Items
            const bool confirmed = app.camp.page == App::Camp::Items;
            padButton(app, SDL_GAMEPAD_BUTTON_SOUTH); // ×: back to the plates
            while (app.camp.pageOut) stepCamp(app); // (the page's windows going first)
            const bool backed = app.camp.page == App::Camp::Main && app.campOpen;
            bool swapRow = false; // Options, Game: "Swap ○ and ×" On - the other way round
            for (const OptRow& o : optionRows(app, 0))
                if (o.label.rfind("Swap", 0) == 0 && o.act) { o.act(0); swapRow = true; }
            swapRow = swapRow && !app.padConfirmEast;
            app.camp.cursor = 0;
            padButton(app, SDL_GAMEPAD_BUTTON_SOUTH);
            const bool swapped = swapRow && app.camp.page == App::Camp::Items;
            for (const OptRow& o : optionRows(app, 0))
                if (o.label.rfind("Swap", 0) == 0 && o.act) o.act(1); // Off: as on the Vita
            app.padConfirmEast = true;
            const auto held = [&](int button, bool down) { SDL_SetJoystickVirtualButton(js, button, down); SDL_UpdateJoysticks(); };
            // (half a second of a D-pad direction held in the Camp's Items list - a scroll list, the only
            // menus that repeat on the Vita: the cursor's moves)
            const auto repeats = [&] {
                app.camp.listCursor = 0, app.camp.listTop = 0;
                for (int f = 0; f < 30; ++f) padStep(app, 1.0f / 60.0f);
                return app.camp.listCursor != 0;
            };
            const auto focusLost = [&] {
                SDL_Event e{};
                e.type = SDL_EVENT_WINDOW_FOCUS_LOST, e.window.windowID = SDL_GetWindowID(app.window);
                SDL_AppEvent(&app, &e);
                e.type = SDL_EVENT_USER; // (the next event finds it)
                SDL_AppEvent(&app, &e);
            };
            app.camp.page = App::Camp::Main;
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); // the main page's plates: no repeat
            app.camp.cursor = 0;
            for (int f = 0; f < 30; ++f) padStep(app, 1.0f / 60.0f);
            const bool platesHeld = app.camp.cursor == 0;
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
            padStep(app, 1.0f / 60.0f);
            for (int n = 21; n <= 40; ++n) app.game.items[n] = 1; // (a list longer than it shows)
            campOpenPage(app, 0, false);
            while (app.camp.pageOut) stepCamp(app);
            app.camp.phase = App::Camp::List;
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
            const bool repeated = repeats();
            focusLost();
            const bool repeatStopped = !repeats();
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
            padStep(app, 1.0f / 60.0f);
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
            const bool repeatFresh = repeats();
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
            padStep(app, 1.0f / 60.0f);
            const int last = int(app.camp.list.size()) - 1; // held at the list's end: it stays there
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
            padStep(app, 1.0f / 60.0f);
            app.camp.listCursor = last;
            for (int f = 0; f < 60; ++f) padStep(app, 1.0f / 60.0f);
            const bool endHeld = last > 0 && app.camp.listCursor == last;
            held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
            padStep(app, 1.0f / 60.0f);
            app.camp.page = App::Camp::Main, app.camp.phase = App::Camp::List;
            app.camp.cursor = 0;
            problems += !repeated || !repeatStopped || !repeatFresh || !platesHeld || !endHeld;
            std::printf("controller: a held direction repeats in a list %s (not over the plates %s), stops with the focus %s, "
                        "repeats again once let go %s, stops at the list's end %s\n", repeated ? "yes" : "NO", platesHeld ? "yes" : "NO",
                        repeatStopped ? "yes" : "NO", repeatFresh ? "yes" : "NO", endHeld ? "yes" : "NO");
            padButton(app, SDL_GAMEPAD_BUTTON_NORTH); // △ again: closed, from any page
            const bool closed = !app.campOpen;
            { // The Vita's screen (its menuf_mainPrc / Put): △ the bar, the cursor on its last entry (Camp),
              // ← ↑ / → ↓ round the entries, ○ the one under it, × the bar shut
                const int keepLayout = app.video.layout;
                app.video.layout = 3;
                applyVideo(app);
                app.fieldMenu = App::FieldMenu();
                stepFieldMenu(app);
                const std::vector<int> e = fieldBarEntries(app);
                padButton(app, SDL_GAMEPAD_BUTTON_NORTH);
                while (app.fieldMenu.anim > 0) stepFieldMenu(app);
                const bool barOpen = app.fieldMenu.open && !app.campOpen && !e.empty() && e.back() == 0 &&
                                     app.fieldMenu.cursor == int(e.size()) - 1;
                SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, -32767); // the stick a push left: a step
                SDL_UpdateJoysticks();
                padStep(app, 1.0f / 60.0f), padStep(app, 1.0f / 60.0f);
                SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, 0);
                SDL_UpdateJoysticks();
                padStep(app, 1.0f / 60.0f);
                const bool stickStepped = e.size() > 1 && e[size_t(app.fieldMenu.cursor)] == e[e.size() - 2];
                padButton(app, SDL_GAMEPAD_BUTTON_DPAD_RIGHT); // (back on Camp)
                padButton(app, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                const bool stepped = stickStepped && e[size_t(app.fieldMenu.cursor)] == e[e.size() - 2];
                padButton(app, SDL_GAMEPAD_BUTTON_DPAD_DOWN), padButton(app, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
                const bool wrapped = app.fieldMenu.cursor == (e.size() > 1 ? 0 : -1);
                padButton(app, SDL_GAMEPAD_BUTTON_SOUTH); // × (○ confirms on the Vita)
                while (app.fieldMenu.anim > 0) stepFieldMenu(app);
                const bool shut = !app.fieldMenu.open;
                padButton(app, SDL_GAMEPAD_BUTTON_NORTH);
                while (app.fieldMenu.anim > 0) stepFieldMenu(app);
                padButton(app, SDL_GAMEPAD_BUTTON_EAST); // ○ on Camp
                const bool camp = app.campOpen;
                for (int f = 0; f < 20; ++f) stepCamp(app); // (its windows in)
                padButton(app, SDL_GAMEPAD_BUTTON_NORTH); // △: the Vita's Camp reads none - still open
                const bool triangleKept = app.campOpen;
                padButton(app, SDL_GAMEPAD_BUTTON_SOUTH); // × on its main page closes it (over a few frames)
                for (int f = 0; f < 20 && app.campOpen; ++f) stepCamp(app);
                app.fieldMenu = App::FieldMenu();
                app.video.layout = keepLayout;
                applyVideo(app);
                const bool ok = barOpen && stepped && wrapped && shut && camp && triangleKept && !app.campOpen;
                problems += !ok;
                std::printf("controller, the Vita's bar: △ opens it on Camp %s, the cursor steps %s and wraps %s, × shuts it %s, ○ opens Camp %s, "
                            "△ leaves the Camp %s and × closes it %s\n",
                            barOpen ? "yes" : "NO", stepped ? "yes" : "NO", wrapped ? "yes" : "NO", shut ? "yes" : "NO", camp ? "yes" : "NO",
                            triangleKept ? "open" : "SHUT", !app.campOpen ? "yes" : "NO");
            }
            { // The Vita's pad elsewhere: L / R on the field leave the map be (they reached the viewer's
              // map stepping, ten at a time); the D-pad runs (CrossKey: a full push); × on the title's
              // "touch to start" starts, and never quits
                const int mapBefore = app.mapIndex;
                const bool keepViewer = app.viewer;
                app.viewer = false;
                padButton(app, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), padButton(app, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
                const bool mapKept = app.mapIndex == mapBefore;
                const cr::Vec3 at = app.walker.pos;
                held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
                updatePlayer(app, 1.0f / kFramesPerSecond, 0);
                const bool runs = app.walker.gait == cr::Gait::Run;
                held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
                updatePlayer(app, 1.0f / kFramesPerSecond, 0);
                app.walker.pos = at, app.walker.gait = cr::Gait::Stand;
                app.runner.stop(), app.dialogue.clear(), app.pendingEntry = App::Entry::None;
                const bool keepTitle = app.titleOpen;
                const int keepStage = app.titleStage;
                app.titleOpen = true, app.titleStage = 0;
                const bool front = titleTapFront(app);
                SDL_Event e{};
                e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, e.gbutton.which = SDL_GetGamepadID(pad), e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
                const bool stays = SDL_AppEvent(&app, &e) != SDL_APP_SUCCESS;
                const bool started = app.titleStage != 0 || !app.titleOpen;
                app.titleOpen = keepTitle, app.titleStage = keepStage;
                app.viewer = keepViewer;
                // Character Select: Down held pulls the cord, the page turning at 12 frames (__menuu_StartPrc)
                const cr::GameState keepCs = app.game;
                const bool keepPointer = app.pointerLast;
                app.pointerLast = false;
                app.game.setFlag(31, true);
                openCharSelect(app);
                for (int f = 0; f < 400 && app.startPage.phase; ++f) stepCharSelect(app);
                const int page0 = app.charSelectPage;
                held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
                for (int f = 0; f < 11; ++f) stepCharSelect(app);
                const bool notYet = app.charSelectPage == page0 && !app.startPage.phase;
                for (int f = 0; f < 3; ++f) stepCharSelect(app);
                const bool pulled = app.startPage.phase != 0 || app.charSelectPage != page0;
                held(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
                for (int f = 0; f < 400 && app.startPage.phase; ++f) stepCharSelect(app);
                app.charSelectOpen = false;
                app.game = keepCs, app.pointerLast = keepPointer;
                const bool ok = mapKept && runs && front && stays && started && notYet && pulled;
                std::printf("controller, the Vita's Select Scenario: Down held turns the page at 12 frames %s\n", notYet && pulled ? "yes" : "NO");
                problems += !ok;
                std::printf("controller, the Vita's pad: L / R keep the map %s, the D-pad runs %s, × on the title starts %s and stays %s\n",
                            mapKept ? "yes" : "NO", runs ? "yes" : "NO", started ? "yes" : "NO", stays ? "yes" : "NO");
            }
            { // A puzzle room's touch from the controller: R3 on a psychokinesis block lifts it, the right
              // stick slides the finger, R3 let go sends it; outside a puzzle room the pointer stays away
                const bool fieldAway = !stickPointerWanted(app);
                const bool keepPointerLast = app.pointerLast, keepPointerCancelled = app.pointerCancelled;
                int pkStage = -1;
                for (int i = 0; i < cr::kPuzzleStages; ++i)
                    if (cr::kPuzzleStage[i].ability == cr::kAbilityPsychokinesis) { pkStage = i; break; }
                const cr::GameState keepRoomGame = app.game;
                cr::WarpRequest w;
                w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + pkStage;
                const bool entered = pkStage >= 0 && performWarp(app, w, -1);
                app.runner.stop(), app.pendingEntry = App::Entry::None, app.dialogue.clear();
                for (int f = 0; f < 30; ++f) stepEvents(app), stepFieldMenu(app);
                app.runner.stop(), app.pendingEntry = App::Entry::None, app.dialogue.clear();
                int target = -1;
                for (size_t i = 0; entered && i < app.puzzle.objs.size(); ++i)
                    if (app.puzzle.objs[i].type == cr::kTypeNormal && app.puzzle.objs[i].shown) { target = int(i); break; }
                bool lifted = false, followed = false, sent = false;
                if (target >= 0) {
                    const App::Puzzle::Obj& o = app.puzzle.objs[size_t(target)];
                    const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
                    const int row = o.row, col = o.col;
                    float lx = 0, ly = 0, ax, ay, bx, by;
                    fieldToLogical(app, {n.pos[0], n.pos[1] + n.box[1], n.pos[2]}, lx, ly);
                    app.renderer.toLogical(0, 0, ax, ay), app.renderer.toLogical(1000, 1000, bx, by); // (the frame's pixels back)
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(app.window, &ww, &wh);
                    App::StickPointer& sp = app.stickPointer;
                    sp = App::StickPointer();
                    sp.x = (lx - ax) * 1000 / (bx - ax) * float(ww) / float(app.renderer.frameWidth());
                    sp.y = (ly - ay) * 1000 / (by - ay) * float(wh) / float(app.renderer.frameHeight());
                    sp.shown = true, sp.stage = app.puzzle.stage;
                    const float px = sp.x, py = sp.y;
                    SDL_Event pe{};
                    pe.gbutton.which = SDL_GetGamepadID(pad), pe.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_STICK;
                    // (each way in turn until the block goes: a wall or another block may stand in one)
                    static const int kWay[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto& wv : kWay) {
                        sp.x = px, sp.y = py;
                        pe.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                        SDL_AppEvent(&app, &pe);
                        lifted = lifted || (sp.down && app.puzzle.pkState == 1 && app.puzzle.pkTarget == target);
                        for (int f = 0; f < 6; ++f) stepEvents(app);
                        const float x0 = sp.x, y0 = sp.y;
                        SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_RIGHTX, Sint16(wv[0] * 32767));
                        SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_RIGHTY, Sint16(wv[1] * 32767));
                        SDL_UpdateJoysticks();
                        for (int f = 0; f < 4; ++f) stickPointerStep(app, 0.05f), stepEvents(app);
                        SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_RIGHTX, 0);
                        SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_RIGHTY, 0);
                        SDL_UpdateJoysticks();
                        followed = followed || std::fabs(sp.x - x0) + std::fabs(sp.y - y0) > 20;
                        pe.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
                        SDL_AppEvent(&app, &pe);
                        for (int f = 0; f < 90; ++f) stepEvents(app);
                        sent = !sp.down && (o.row != row || o.col != col);
                        if (sent) break;
                    }
                }
                stickPointerRelease(app);
                app.stickPointer = App::StickPointer();
                app.pointerLast = keepPointerLast, app.pointerCancelled = keepPointerCancelled;
                app.game = keepRoomGame;
                showMap(app, 0, 0, 2);
                app.pendingEntry = App::Entry::None, app.runner.stop(), app.dialogue.clear();
                const bool ok = fieldAway && entered && lifted && followed && sent;
                problems += !ok;
                std::printf("controller, a puzzle room's touch: away from puzzles %s, R3 lifts a block %s, the right stick slides %s, let go sends it %s\n",
                            fieldAway ? "yes" : "NO", lifted ? "yes" : "NO", followed ? "yes" : "NO", sent ? "yes" : "NO");
            }
            app.runner.stop();
            app.dialogue.clear();
            app.pendingEntry = App::Entry::None;
            const cr::Vec3 heldFrom = app.walker.pos;
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, 32767);
            SDL_UpdateJoysticks();
            focusLost();
            for (int f = 0; f < 15; ++f) updatePlayer(app, 1.0f / kFramesPerSecond, 0);
            const cr::Vec3 heldMove = app.walker.pos - heldFrom;
            const bool walkStopped = std::sqrt(cr::dot(heldMove, heldMove)) < 0.01f;
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_UpdateJoysticks();
            padStep(app, 1.0f / 60.0f);
            app.walker.pos = heldFrom;
            app.runner.stop();
            app.dialogue.clear();
            app.pendingEntry = App::Entry::None;
            problems += !walkStopped;
            std::printf("controller: a stick held through a lost focus stops walking %s\n", walkStopped ? "yes" : "NO");
            problems += !walked || !fieldQuiet || !opened || !moved || !confirmed || !backed || !swapped || !closed;
            std::printf("controller: the stick walks %s, △ opens / closes the Camp %s / %s, the D-pad moves %s, ○ confirms %s, × back %s "
                        "(on the field nothing %s), ×-confirm %s; %d problems\n", walked ? "yes" : "NO", opened ? "yes" : "NO",
                        closed ? "yes" : "NO", moved ? "yes" : "NO", confirmed ? "yes" : "NO", backed ? "yes" : "NO",
                        fieldQuiet ? "yes" : "NO", swapped ? "yes" : "NO", problems);
            app.campOpen = false;
            // hot-plug: the pad in use taken out while another is connected - that one goes on (not none
            // until a new one comes); a pad that comes while the held one is gone (a wireless pad's
            // receiver: a dead handle) taken
            SDL_JoystickID second = SDL_AttachVirtualJoystick(&desc);
            app.padAllow.push_back(second);
            SDL_Event ev{};
            ev.type = SDL_EVENT_GAMEPAD_REMOVED, ev.gdevice.which = id;
            SDL_AppEvent(&app, &ev); // (app.pad: `pad`, the first)
            pad = nullptr; // (closed by the event)
            const bool handedOver = app.pad && SDL_GetGamepadID(app.pad) == second;
            SDL_Gamepad* stale = SDL_OpenGamepad(id); // (the first again, held as the second goes quiet)
            if (app.pad) SDL_CloseGamepad(app.pad);
            app.pad = stale;
            SDL_DetachVirtualJoystick(id), id = 0; // (the held handle dead now, no REMOVED seen)
            ev.type = SDL_EVENT_GAMEPAD_ADDED, ev.gdevice.which = second;
            SDL_AppEvent(&app, &ev);
            const bool takenOver = app.pad && SDL_GetGamepadID(app.pad) == second;
            problems += !handedOver + !takenOver;
            std::printf("controller: the pad in use taken out - another connected goes on %s; one come while the held one is dead taken %s\n",
                        handedOver ? "yes" : "NO", takenOver ? "yes" : "NO");
            if (app.pad) SDL_CloseGamepad(app.pad);
            if (second) SDL_DetachVirtualJoystick(second);
            app.pad = nullptr;
            { // one pad as two (CRO-U #4): both opened as they come; a button on the second makes it the one in use,
              // a stick pushed on the first takes it back, and with the one in use taken out the other goes on
                const SDL_JoystickID a = SDL_AttachVirtualJoystick(&desc), b = SDL_AttachVirtualJoystick(&desc);
                app.padAllow.push_back(a), app.padAllow.push_back(b);
                SDL_Event e{};
                e.type = SDL_EVENT_GAMEPAD_ADDED, e.gdevice.which = a;
                SDL_AppEvent(&app, &e);
                e.gdevice.which = b;
                SDL_AppEvent(&app, &e);
                const bool bothOpen = app.pad && SDL_GetGamepadID(app.pad) == a && app.morePads.size() == 1 && SDL_GetGamepadID(app.morePads[0]) == b;
                e = SDL_Event{};
                e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, e.gbutton.which = b, e.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN, e.gbutton.down = true;
                SDL_AppEvent(&app, &e);
                const bool pressedTakes = app.pad && SDL_GetGamepadID(app.pad) == b;
                e = SDL_Event{};
                e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION, e.gaxis.which = a, e.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX, e.gaxis.value = 3000;
                SDL_AppEvent(&app, &e);
                const bool nudgeKeeps = app.pad && SDL_GetGamepadID(app.pad) == b;
                e.gaxis.value = 30000;
                SDL_AppEvent(&app, &e);
                const bool pushedTakes = app.pad && SDL_GetGamepadID(app.pad) == a;
                e = SDL_Event{};
                e.type = SDL_EVENT_GAMEPAD_REMOVED, e.gdevice.which = a;
                SDL_AppEvent(&app, &e);
                const bool otherGoesOn = app.pad && SDL_GetGamepadID(app.pad) == b && app.morePads.empty();
                problems += !bothOpen + !pressedTakes + !nudgeKeeps + !pushedTakes + !otherGoesOn;
                std::printf("controller: one pad as two - both open %s, a button on the second takes it %s (a nudge doesn't %s), a stick "
                            "pushed on the first takes it back %s, the one in use taken out: the other goes on %s\n", bothOpen ? "yes" : "NO",
                            pressedTakes ? "yes" : "NO", nudgeKeeps ? "yes" : "NO", pushedTakes ? "yes" : "NO", otherGoesOn ? "yes" : "NO");
                if (app.pad) SDL_CloseGamepad(app.pad);
                for (SDL_Gamepad* g : app.morePads) SDL_CloseGamepad(g);
                app.morePads.clear();
                SDL_DetachVirtualJoystick(a), SDL_DetachVirtualJoystick(b);
            }
            { // the outside pad: never opened, never taken by its buttons, never the one handed over to
                app.pad = nullptr;
                const SDL_JoystickID c = SDL_AttachVirtualJoystick(&desc);
                app.padAllow.push_back(c);
                SDL_Event e{};
                e.type = SDL_EVENT_GAMEPAD_ADDED, e.gdevice.which = foreign;
                SDL_AppEvent(&app, &e);
                const bool notOpened = !app.pad && app.morePads.empty();
                e.gdevice.which = c;
                SDL_AppEvent(&app, &e);
                e = SDL_Event{};
                e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, e.gbutton.which = foreign, e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH, e.gbutton.down = true;
                SDL_AppEvent(&app, &e);
                const bool notTaken = app.pad && SDL_GetGamepadID(app.pad) == c;
                e = SDL_Event{};
                e.type = SDL_EVENT_GAMEPAD_REMOVED, e.gdevice.which = c;
                SDL_AppEvent(&app, &e);
                const bool notHanded = !app.pad; // (the outside pad still connected: not the one taken)
                problems += !notOpened + !notTaken + !notHanded;
                std::printf("controller: an outside pad (another program's) - not opened %s, its button doesn't take over %s, not handed "
                            "over to %s\n", notOpened ? "yes" : "NO", notTaken ? "yes" : "NO", notHanded ? "yes" : "NO");
                if (app.pad) SDL_CloseGamepad(app.pad), app.pad = nullptr;
                SDL_DetachVirtualJoystick(c);
            }
            app.pad = keepPad;
            app.game = keepGame;
            if (pad) SDL_CloseGamepad(pad);
        }
        if (id) SDL_DetachVirtualJoystick(id);
        if (foreign) SDL_DetachVirtualJoystick(foreign);
        app.padAllow = keepAllow;
        mapErrors += problems;
    }
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    { // F11 / Alt+Enter: windowed <-> fullscreen, the kind last chosen (borderless at first; exclusive once
      // Options chose it), for the session: settings.ini keeps Options' Display (#15); a held key's repeats ignored
        const App::Video keepVideo = app.video;
        const bool keepPersist = app.persist;
        app.persist = false; // (settings.ini left as it was)
        auto press = [&app](SDL_Keycode key, SDL_Keymod mod, bool repeat = false) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_KEY_DOWN, ev.key.key = key, ev.key.mod = mod, ev.key.down = true, ev.key.repeat = repeat;
            SDL_AppEvent(&app, &ev);
        };
        app.video.display = app.video.savedDisplay = 0, app.video.fullscreenKind = 2, app.video.toggleKind = 0;
        // (#15: the keys' choice lasts the session - settings.ini's display= stays Options' through any save)
        auto saved = [&app](int d) { return settingsText(app).find("\ndisplay=" + std::to_string(d) + "\n") != std::string::npos; };
        press(SDLK_F11, SDL_KMOD_NONE);
        const bool borderless = app.video.display == 2 && saved(0) && app.video.fullscreenKind == 2;
        press(SDLK_F11, SDL_KMOD_NONE, true);
        const bool repeatIgnored = app.video.display == 2;
        press(SDLK_RETURN, SDL_KMOD_LALT);
        const bool windowed = app.video.display == 0 && saved(0);
        adjustVideo(app, 1, 1); // (Options: Fullscreen)
        const bool chosen = app.video.display == 1 && saved(1);
        press(SDLK_F11, SDL_KMOD_NONE);
        const bool keptSaved = app.video.display == 0 && saved(1) && app.video.fullscreenKind == 1;
        press(SDLK_F11, SDL_KMOD_NONE);
        const bool exclusive = app.video.display == 1;
        const int problems = !borderless + !repeatIgnored + !windowed + !chosen + !keptSaved + !exclusive;
        std::printf("fullscreen key: F11 borderless %s (a repeat ignored %s), Alt+Enter windowed %s, Options' Fullscreen saved %s, "
                    "F11 windowed with Fullscreen still saved %s, then back to it %s; %d problems\n",
                    borderless ? "yes" : "NO", repeatIgnored ? "yes" : "NO", windowed ? "yes" : "NO", chosen ? "yes" : "NO",
                    keptSaved ? "yes" : "NO", exclusive ? "yes" : "NO", problems);
        app.video = keepVideo;
        applyVideo(app);
        app.persist = keepPersist;
        mapErrors += problems;
    }
#endif
    { // btl_lose: an ordinary battle lost goes back to the pair's Ark, a fifth of the Oz gone
        int problems = 0;
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.oz = 1005;
        app.playerChar = 2; // Zhamo and Eluca: area 13
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        const bool ordinary = app.battle.active && !app.battle.eventBattle;
        if (app.battle.active) loseToArk(app);
        const bool ark = app.area == 9 && app.mapNumber == 175 && !app.battle.active; // (Omega: scenario 0's Ark)
        const bool oz = app.game.oz == 1005 - 201;
        // btl_lose's load flags 0x27: the Ark's area script (entry 0) runs, not the room's EvrStart 5
        const bool areaScript = app.pendingEntry == App::Entry::Area;
        stepEvents(app);
        const bool script0 = areaScript && (!app.runner.running() || app.runner.eventNo() == 0) && app.runnerWhat == "area entry script";
        // the defeat's flag 0xffd: set, and the Ark's script plays its scene (a talk) and clears it
        const bool flagSet = app.game.flag(0xffd);
        bool talked = false;
        for (int f = 0; f < 4000 && (app.runner.running() || !app.dialogue.empty()); ++f) {
            if (!app.dialogue.empty()) talked = true, tapDialogue(app);
            stepEvents(app);
        }
        const bool afterScene = flagSet && talked && !app.game.flag(0xffd);
        problems += !ordinary || !ark || !oz || !script0 || !afterScene;
        std::printf("battle lost: back to the Ark (area %d, room %d) %s, Oz %d (a fifth gone %s), the area's script 0 %s, "
                    "the defeat's scene (flag 0xffd) %s; %d problems\n",
                    app.area, app.mapNumber, ark ? "yes" : "NO", app.game.oz, oz ? "yes" : "NO", script0 ? "yes" : "NO",
                    afterScene ? "played" : flagSet ? (talked ? "played, the flag LEFT SET" : "NOT PLAYED") : "NOT SET", problems);
        mapErrors += problems;
        app.runner.stop();
        app.pendingEntry = App::Entry::None;
        app.game = keepGame;
        app.playerChar = keepChar;
    }
    { // The Vita's Camp main page (FUN_81053c6c 5): right to Scenarios, on to Collection (second scenario),
      // off the end back to the plates; left from the plates to the last button; ○ there opens it
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.game.setFlag(0x38, true), app.game.setFlag(0x39, true);
        app.game.scenario = 0;
        openCamp(app);
        App::Camp& c = app.camp;
        c.notice = 0, app.dialogue.clear();
        campKey(app, SDLK_RIGHT);
        const bool onScenarios = c.btnRow == 0;
        campKey(app, SDLK_RIGHT);
        const bool offFirst = c.btnRow == -1; // (one button in the first scenario)
        app.game.scenario = 1;
        campKey(app, SDLK_LEFT);
        const bool lastFirst = c.btnRow == 1;
        campKey(app, SDLK_LEFT), campKey(app, SDLK_LEFT);
        const bool backToPlates = c.btnRow == -1;
        campKey(app, SDLK_RIGHT), campKey(app, SDLK_RIGHT), campKey(app, SDLK_RETURN);
        const bool collection = app.collection.open;
        app.collection.open = false;
        app.campOpen = false;
        app.game = keepGame;
        problems += !onScenarios || !offFirst || !lastFirst || !backToPlates || !collection;
        std::printf("camp buttons (the Vita's pad): right to Scenarios %s, off the end %s, left to the last %s, back to the plates %s, "
                    "○ on Collection %s; %d problems\n", onScenarios ? "yes" : "NO", offFirst ? "yes" : "NO", lastFirst ? "yes" : "NO",
                    backToPlates ? "yes" : "NO", collection ? "yes" : "NO", problems);
        mapErrors += problems;
    }
    { // The Vita's battle cursors (__menub_mainPrc): the first page's kept across its pages and rounds,
      // a member's command kept, Items stepped over with no battle item, × on "Run from battle?" a No
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        for (int n = 21; n <= 47; ++n) app.game.items.erase(n); // (no battle item)
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        bool escapeKept = false, cmdKept = false, itemsSkipped = false, firstKept = false;
        auto key = [&](SDL_Keycode k) { // (the page as shown: battleMenuReady)
            b.phase = App::Battle::Command, b.viewState = 2, b.view.page = b.page;
            battleKey(app, k);
        };
        if (b.active) {
            battleMenu(app, App::Battle::Top);
            const bool escapeOk = !b.rules.escapeBlocked;
            b.cursor = 2;
            key(SDLK_RETURN);   // Escape ...
            key(SDLK_ESCAPE);   // ... × : No
            escapeKept = !escapeOk || (b.menu == App::Battle::Top && b.cursor == 2);
            b.cursor = 0;
            key(SDLK_RETURN);   // Solo
            b.cursor = 1;
            key(SDLK_DOWN);     // from Genes past Items (nothing in the bag) to Defend
            itemsSkipped = b.menu == App::Battle::Commands && b.cursor == 3;
            b.cursor = 0;
            key(SDLK_RETURN);   // Attack: the targets
            key(SDLK_ESCAPE);   // back: Attack again
            cmdKept = b.menu == App::Battle::Commands && b.cursor == 0;
            b.cmdCursor[0] = 3;
            battleMenu(app, App::Battle::Top); // the first page again: Solo kept, the commands cleared
            firstKept = b.cursor == 0 && b.cmdCursor[0] == 0;
            endBattle(app, true);
        }
        problems += !escapeKept || !cmdKept || !itemsSkipped || !firstKept;
        std::printf("battle cursors (the Vita's): Escape kept after No %s, the command kept %s, Items stepped over %s, "
                    "the first page kept and the commands cleared %s; %d problems\n", escapeKept ? "yes" : "NO", cmdKept ? "yes" : "NO",
                    itemsSkipped ? "yes" : "NO", firstKept ? "yes" : "NO", problems);
        app.game = keepGame;
        mapErrors += problems;
    }
    mapErrors += fieldEventsSelfTest(app);
    mapErrors += storyLossSelfTest(app);
    { // PrcMsg's typing: a character a frame, a tap brings the page out (up to its next stop) and the
      // next turns it; 0x1a pauses it; 0x14 closes the message by itself (Omega's j001_d06 6717:
      // three pauses, then it closes after the last)
        int problems = 0;
        const cr::EventFile keepEvents = app.events;
        const bool keepType = app.typeOn;
        app.typeOn = true;
        app.dialogue.clear();
        showMessages(app, {57006}); // a page with no codes
        const int chars = app.dialogue.empty() ? 0 : pageChars(app.dialogue[0]);
        for (int f = 0; f < 10; ++f) stepTyping(app);
        const bool rate = chars > 10 && app.dialogueTyped == 10 && !pageTyped(app);
        tapDialogue(app);
        for (size_t f = 0; f < 8 && !app.dialogue.empty() && !pageTyped(app); ++f) stepTyping(app); // (a frame a line)
        const bool whole = pageTyped(app) && !app.dialogue.empty();
        tapDialogue(app);
        const bool turned = app.dialogue.empty();
        problems += !rate || !whole || !turned;
        int frames = 0, taps = 0, pauses = 0, pauseFrames = 0, total = 0, closeAfter = -2, turnsSeen = 0;
        bool closed = false;
        int typedId = 6717; // (the Vita's block starts with three more: its 6720)
        if (app.events.load(app.dataDir, 7)) {
            for (int id = 6700; id < 6800; ++id) {
                std::vector<std::vector<std::string>> pg;
                cr::MessageInfo mi;
                if (app.events.message(id, textLanguage(app), [](int) { return 0; }, pg, &mi) && mi.pauses.size() >= 3 && mi.closeAfter > 0) {
                    typedId = id;
                    break;
                }
            }
            showMessages(app, {typedId});
            for (size_t k = 0; k < app.dialogueInfo.size() && k < app.dialogue.size(); ++k) // (those at a page's end
                for (const auto& pz : app.dialogueInfo[k].pauses)                           // hold its turn or closing)
                    ++pauses, pauseFrames += pauseHold(pz.frames);
            for (const auto& pg : app.dialogue) total += pageChars(pg);
            closeAfter = app.dialogueInfo.empty() ? -2 : app.dialogueInfo.back().closeAfter;
            const size_t pages = app.dialogue.size();
            int& turns = turnsSeen; // pages the window clears by itself (0x1a then 0x10)
            for (size_t k = 0; k + 1 < app.dialogueInfo.size() && k + 1 < pages; ++k) turns += app.dialogueInfo[k].closeAfter >= 0;
            for (; frames < 4000 && !app.dialogue.empty(); ++frames) {
                stepTyping(app);
                const cr::MessageInfo* pinfo = pageInfo(app);
                if (!app.dialogue.empty() && pageTyped(app) && app.dialoguePage + 1 < app.dialogue.size() &&
                    !(pinfo && pinfo->closeAfter >= 0)) {
                    tapDialogue(app); // the reader turns each page once it's out
                    ++taps;
                }
            }
            closed = app.dialogue.empty() && size_t(taps + turns) + 1 == pages;
        }
        const int least = total + pauseFrames;
        problems += pauses == 0 || closeAfter <= 0 || !closed || frames < least || frames > least + 2 * (taps + turnsSeen + 2);
        std::printf("message typing: a character a frame %s, a tap brings it out %s, the next turns it %s; %d: %d "
                    "characters, %d pauses (%d frames), then closes by itself (%d frames after the last) %s in %d frames; %d problems\n",
                    rate ? "yes" : "NO", whole ? "yes" : "NO", turned ? "yes" : "NO", typedId, total, pauses, pauseFrames, closeAfter,
                    closed ? "yes" : "NO", frames, problems);
        mapErrors += problems;
        app.dialogue.clear();
        app.events = keepEvents;
        app.typeOn = keepType;
    }
    { // _PrcMsg's pages (3.1.0 decomp 28468-28780): 0x1a's length, a tap stopping at the next pause /
      // line break, 0x1b, 0x12 keeping the lines (0x11 after it) or going on along the same line out
      // at once (anything else after it), and the 0x1b messages' voice and name
        int problems = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) ++problems, std::printf("message pages FAILED: %s\n", what);
        };
        const cr::EventFile keepEvents = app.events;
        const bool keepType = app.typeOn;
        const int keepLanguage = app.language;
        const App::MsgWin keepWin = app.msgWin;
        app.typeOn = true, app.language = 1, app.msgWin = App::MsgWin();
        auto page = [&](const std::vector<std::string>& lines, const std::vector<cr::MessageInfo::Pause>& pz, bool noSkip) {
            app.dialogue = {lines};
            app.dialoguePage = 0;
            cr::MessageInfo mi;
            mi.pauses = pz, mi.noSkipOn = noSkip;
            app.dialogueInfo = {mi};
            app.dialogueVoices = {{}};
            ++app.dialogueSerial;
            resetTyping(app);
        };
        page({"abcdef"}, {{0, 2, 0}, {0, 4, 5}}, false);
        std::vector<int> at; // the frame each character comes out
        for (int f = 0; f < 20 && !pageTyped(app); ++f) {
            const int before = app.dialogueTyped;
            stepTyping(app);
            if (app.dialogueTyped > before) at.push_back(f);
        }
        check(at == std::vector<int>{0, 1, 3, 4, 9, 10}, "0x1a n: the next character n frames after the one before (2 for n = 0)");
        page({"abcdef", "ghij"}, {{0, 3, 10}}, false);
        stepTyping(app);
        tapDialogue(app);
        stepTyping(app);
        const bool atPause = app.dialogueTyped == 3 && app.dialogueInstant && !pageTyped(app);
        for (int f = 0; f < 9; ++f) stepTyping(app);
        const bool atBreak = app.dialogueTyped == 7 && !pageTyped(app);
        stepTyping(app);
        check(atPause && atBreak && pageTyped(app), "a tap: out at once up to the next pause, after it up to the line break, then the rest");
        page({"abcdef"}, {}, true);
        stepTyping(app);
        tapDialogue(app);
        stepTyping(app);
        check(!app.dialogueInstant && app.dialogueTyped == 2 && !pageTyped(app), "0x1b: a tap can't bring the typing out at once");
        auto typeOut = [&] { for (int f = 0; f < 400 && !app.dialogue.empty() && !pageTyped(app); ++f) stepTyping(app); };
        // c101_d07 3434: "What the--!?" 0x12 0x11 "You mean it was a trap!?" - the second page keeps the first line
        bool keptLines = false, keptTyping = false;
        if (app.events.load(app.dataDir, 8)) {
            app.dialogue.clear();
            showMessages(app, {3434});
            if (app.dialogue.size() == 2 && app.dialogueInfo.size() == 2) {
                const std::vector<std::string> first = app.dialogue[0];
                const int keep = pageChars(first);
                typeOut();
                tapDialogue(app);
                keptLines = app.dialoguePage == 1 && app.dialogue[1].size() == first.size() + 1 &&
                            app.dialogueTyped == keep && typedPage(app.dialogue[1], app.dialogueTyped) == first;
                stepTyping(app); // (the line break)
                stepTyping(app);
                keptTyping = !app.dialogueInstant && app.dialogueTyped == keep + 2;
            }
        }
        check(keptLines && keptTyping, "c101_d07 3434: 0x12 0x11 keeps the lines above and types on from the next");
        // j001_d06 4811: "Vahti... Vieg" 0x12 (3.1.0: 0x1e 1601) "... Kill us..." - on along the line, out at once
        bool sameLine = false, atOnce = false;
        if (app.events.load(app.dataDir, 7)) {
            app.dialogue.clear();
            showMessages(app, {4811});
            if (app.dialogue.size() == 2 && app.dialogueInfo.size() == 2) {
                const std::vector<std::string> first = app.dialogue[0];
                typeOut();
                tapDialogue(app);
                const std::vector<std::string>& second = app.dialogue[1];
                sameLine = app.dialoguePage == 1 && second.size() == 1 && first.size() == 1 && second[0].size() > first[0].size() &&
                           second[0].compare(0, first[0].size(), first[0]) == 0 && app.dialogueInstant;
                stepTyping(app);
                atOnce = pageTyped(app);
            }
        }
        check(sameLine && atOnce, "j001_d06 4811: a 0x12 without 0x10 / 0x11 goes on along the same line, out at once");
        // 3.1.0: c101_t01 1000 "0x1b 0x1e 362 0x1c 1 0x15 40 What the...?" and j001_d06 3400 "0x1b 0x15 39 VAHTI"
        bool noSkipVoice = true, noSkipName = true, noSkipTap = true;
        if (multilingualData(app)) {
            std::vector<std::vector<std::string>> pg;
            cr::MessageInfo mi;
            noSkipVoice = app.events.load(app.dataDir, 10) && app.events.message(1000, 1, nullptr, pg, &mi) && mi.voices.size() == 1 &&
                          mi.voices[0].clip == 362 && mi.voices[0].at == 0 && mi.nameId == 58000 + 0x95 + 40 && mi.noSkip.size() == 1 &&
                          !pg.empty() && !pg[0].empty() && pg[0][0].compare(0, 4, "What") == 0;
            if (noSkipVoice) {
                app.dialogue.clear();
                app.sound.stopLine(), app.voiceKeep = false; // (no earlier clip to wait for)
                showMessages(app, {1000});
                stepTyping(app);
                tapDialogue(app);
                stepTyping(app);
                noSkipTap = !app.dialogueInstant && app.dialogueTyped == 2;
            }
            noSkipName = app.events.load(app.dataDir, 7) && app.events.message(3400, 1, nullptr, pg, &mi) &&
                         mi.nameId == 58000 + 0x95 + 39 && !pg.empty() && !pg[0].empty() && pg[0][0].compare(0, 5, "VAHTI") == 0;
        }
        check(noSkipVoice && noSkipTap, "c101_t01 1000: 0x1b a word alone - its voice clip 362, no stray glyph, the tap can't hurry it");
        check(noSkipName, "j001_d06 3400: 0x1b then the name plate");
        std::printf("message pages: 0x1a's frames, a tap out to the next pause / line break, 0x12 0x11 keeping the lines %s, "
                    "0x12 on along the line %s, 0x1b %s; %d problems\n", keptLines && keptTyping ? "yes" : "NO",
                    sameLine && atOnce ? "yes" : "NO",
                    !multilingualData(app) ? "(none in this data)" : noSkipVoice && noSkipName && noSkipTap ? "yes" : "NO", problems);
        mapErrors += problems;
        app.dialogue.clear();
        app.dialogueInfo.clear();
        app.dialogueVoices.clear();
        resetTyping(app);
        app.events = keepEvents;
        app.typeOn = keepType, app.language = keepLanguage, app.msgWin = keepWin;
    }
    { // The calcs' queued messages (btl_add_msgque / menub_mainPut): a monster's poison queues
      // "<name> was poisoned!" for its target (one per fighter); the banner shows each 60 frames
      // once free and the battle waits for the queue before the next action
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        bool queued = false, oneEach = false, waited = true, shown = false;
        int held = 0, nextAt = -1;
        if (b.active) {
            for (int f = 0; f < 60 && b.phase != App::Battle::Command; ++f) stepBattle(app);
            b.rules.forceAction = 122; // enemy 2 casts poison
            cr::BattleCommand cmd[2];
            cmd[0].kind = cmd[1].kind = cr::kCmdDefend;
            b.rules.beginRound(cmd, app.battleActions);
            cr::BattleTurn t;
            while (b.rules.nextTurn(app.battleActions, t) && t.action != 122) {}
            int target = -1;
            for (const cr::BattleHit& h : t.hits) target = h.target;
            queued = t.action == 122 && target >= 0 && t.notices.size() == 1 && t.notices[0].fighter == target &&
                     t.notices[0].entry == 6 && b.rules.f[size_t(target)].status[cr::kStatusPoison] == 5;
            t.notices.push_back({target, 0x19}); // the same fighter again: dropped
            t.notices.push_back({2, 0x1d});
            b.phase = App::Battle::Acting;
            b.turn = t;
            b.turnOn = false;
            b.bannerFrames = 0;
            showHits(app);
            oneEach = b.notices.size() == 2;
            const std::string want = target >= 0 ? sysText(app, 57006, battleActor(app, target) ? battleActor(app, target)->name : "") : "";
            for (int f = 0; f < 400 && nextAt < 0; ++f) {
                const bool before = !b.notices.empty() || b.bannerFrames > 1;
                stepBattle(app);
                if (b.banner == want && b.bannerFrames > 0) shown = true, ++held;
                if (b.turnOn || b.phase != App::Battle::Acting) nextAt = f;
                else if (!before) break;
                waited &= !(b.turnOn && before);
            }
            endBattle(app, false);
        }
        problems += !queued || !oneEach || !shown || held != btlui::kMessageFrames || !waited || nextAt < 2 * btlui::kMessageFrames;
        std::printf("battle messages: poison queued for its target %s, one per fighter %s, shown %d frames, the next action after "
                    "frame %d, waited %s; %d problems\n", queued ? "yes" : "NO", oneEach ? "yes" : "NO", held, nextAt,
                    waited ? "yes" : "NO", problems);
        mapErrors += problems;
        app.runner.stop();
        app.game = keepGame;
    }
    { // The battle's menus (menub_mainPrc / Put, 3.1.0): each page slides in from the right over 4
      // frames (320 -> 0) and takes taps only once in; Escape asks with "Run from battle?" on the
      // banner (kept up) and Yes / No plates; the round's first action 6 frames after the last
      // command (the page's 4 out, then 0x270e / 500 / 600); the left HUD slides 480 out in 4 frames
      // while a talk runs; Analyze (btlact_gene 0xc1c..): the name, the HP, the weak and the strong
      // elements, a line a tap, the action over 40 frames after the last
        int problems = 0;
        const cr::GameState keepGame = app.game;
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        for (int fl : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(fl, true); // (the tutorials seen)
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        startBattle(app, {1, 0, -1, 0}, 0);
        App::Battle& b = app.battle;
        bool touchLook = false;
        bool gated = false, slides = false, askBanner = false, askHeld = false, noBack = false, hudOut = false, hudIn = false;
        int inFrames = -1, firstAt = -1, analyzePages = 0, analyzeHeld = 0, analyzeEnd = -1;
        std::string slideSeq, firstLine, hpLine;
        bool analyzeOk = false;
        if (b.active) {
            for (int f = 0; f < 60 && b.phase != App::Battle::Command; ++f) stepBattle(app);
            // the first page comes in: at 320 on the frame it is set (with its camera's cut), then 240, 160,
            // 80, 0 - a key before it's in does nothing
            const bool atCut = b.slide == 320 && b.viewState == 1;
            const int page = b.page;
            battleKey(app, SDLK_RETURN);
            gated = b.menu == App::Battle::Top && b.page == page;
            for (int f = 1; f <= 8 && inFrames < 0; ++f) {
                stepBattle(app);
                slideSeq += (slideSeq.empty() ? "" : " ") + std::to_string(b.slide);
                if (battleMenuReady(app)) inFrames = f;
            }
            slides = atCut && slideSeq == "240 160 80 0"; // (Part 2 1248.62-1248.82 s: in 4 frames after the cut)
            // touch play's look: no plate lit while the page is in after a click, the plate just chosen as
            // it goes; keys light the cursor's (the flag the events set)
            {
                const bool keyLit = battleCursorLit(app);
                app.pointerLast = true;
                const bool inDark = b.viewState == 2 && !battleCursorLit(app);
                const int keepState = b.viewState;
                b.viewState = 3;
                const bool goingDark = !battleCursorLit(app); // (the video: dark as the page leaves)
                b.viewState = keepState;
                // a touch held on a plate lights it (battlePressIn: the plate's box from battleClick)
                float lx = 0, ly = 0;
                { // (the box's middle back to the frame's pixels)
                    const float uiX = app.renderer.uiX(), rightX = (app.renderer.phoneLayout() ? uiX : 2 * uiX) + 2 * vitaEdge43(app);
                    lx = rightX + 2 * (btlui::kButtonX + 68), ly = 2 * (btlui::kButtonY + btlui::kButtonStep + 20);
                }
                float ax, ay, bx, by;
                app.renderer.toLogical(0, 0, ax, ay), app.renderer.toLogical(1000, 1000, bx, by);
                app.press = App::Press{true, kPressBattle, (lx - ax) * 1000 / (bx - ax), (ly - ay) * 1000 / (by - ay)};
                const bool heldLit = battlePressIn(app, btlui::kButtonX, btlui::kButtonY + btlui::kButtonStep, 136, 40) &&
                                     !battlePressIn(app, btlui::kButtonX, btlui::kButtonY, 136, 40);
                app.press = App::Press();
                app.pointerLast = false;
                touchLook = keyLit && inDark && goingDark && heldLit;
            }
            // Escape: Top goes (4), the ask comes (4) with its line on the banner, held
            b.cursor = 2;
            battleKey(app, SDLK_RETURN);
            int f = 0;
            for (; f < 20 && !battleMenuReady(app); ++f) stepBattle(app);
            askBanner = b.menu == App::Battle::EscapeAsk && f == 8 && b.banner == sysText(app, 57015) && b.bannerFrames > 0;
            for (int g = 0; g < 100; ++g) stepBattle(app);
            askHeld = b.banner == sysText(app, 57015) && b.bannerFrames > 0 && b.phase == App::Battle::Command;
            b.confirm = 1; // No: the ask goes, the first page again, the banner cleared
            battleKey(app, SDLK_RETURN);
            for (f = 0; f < 20 && !battleMenuReady(app); ++f) stepBattle(app);
            noBack = b.menu == App::Battle::Top && f == 8 && b.bannerFrames == 0;
            // the left HUD while a talk runs (its frames as stepBattle's first steps take them)
            app.game.setFlag(battletalk::kFlag, true);
            std::string hudSeq;
            for (int g = 0; g < 4; ++g) stepMenuView(app), hudSeq += std::to_string(b.hudSlide) + " ";
            hudOut = hudSeq == "120 240 360 480 " && b.view.page < 0;
            app.game.setFlag(battletalk::kFlag, false);
            for (int g = 0; g < 4; ++g) stepMenuView(app);
            hudIn = b.hudSlide == 0;
            for (f = 0; f < 20 && !battleMenuReady(app); ++f) stepBattle(app);
            // Yes: the round's first action (the escape) 6 frames on
            b.cursor = 2;
            battleKey(app, SDLK_RETURN);
            for (f = 0; f < 20 && !battleMenuReady(app); ++f) stepBattle(app);
            b.confirm = 0;
            battleKey(app, SDLK_RETURN);
            for (f = 1; f <= 30 && firstAt < 0; ++f) {
                stepBattle(app);
                if (b.turnOn) firstAt = f;
            }
            endBattle(app, false);
        }
        // Analyze on the first enemy: its lines, a tap each
        startBattle(app, {1, 0, -1, 0}, 0);
        if (b.active) {
            for (int f = 0; f < 60 && b.phase != App::Battle::Command; ++f) stepBattle(app);
            const int target = 2;
            const cr::Fighter& tf = b.rules.f[size_t(target)];
            const size_t want = 2 + std::max<size_t>(1, cr::analyzeElements(tf, true).size()) +
                                std::max<size_t>(1, cr::analyzeElements(tf, false).size());
            cr::BattleTurn t;
            t.kind = cr::kCmdGene;
            t.action = 0xe9;
            t.actors[0] = 0;
            cr::BattleHit h;
            h.target = target;
            h.type = cr::kHitQuiet;
            t.hits.push_back(h);
            b.phase = App::Battle::Acting;
            b.turn = t;
            b.turnOn = true;
            b.turnFrame = 0, b.hitFrame = 1, b.hitsShown = false, b.bounceAt = -1, b.stageAction = 0;
            b.analyze = {};
            stepBattle(app);
            stepBattle(app);
            firstLine = b.banner;
            const App::Battle::Actor* u = battleActor(app, target);
            bool each = b.analyze.on && u && firstLine == u->name;
            while (b.analyze.on && analyzePages < 20) {
                ++analyzePages;
                const std::string line = b.banner;
                if (analyzePages == 2) hpLine = line;
                bool held = true;
                for (int g = 0; g < 50; ++g) {
                    stepBattle(app);
                    held &= b.banner == line && b.bannerFrames > 0 && b.turnOn;
                }
                analyzeHeld += held;
                battleKey(app, SDLK_RETURN);
            }
            each &= hpLine.find(std::to_string(tf.hp)) != std::string::npos;
            for (int g = 1; g <= 60 && analyzeEnd < 0; ++g) {
                stepBattle(app);
                if (!b.turnOn) analyzeEnd = g;
            }
            analyzeOk = each && size_t(analyzePages) == want && analyzeHeld == analyzePages && analyzeEnd == 40;
            endBattle(app, false);
        }
        problems += !touchLook + !gated + !slides + (inFrames != 4) + !askBanner + !askHeld + !noBack + !hudOut + !hudIn + (firstAt != 6) + !analyzeOk;
        std::printf("battle menus: touch play's unlit plates %s; a page in over %d frames (%s), keys before %s; Escape's ask on the banner %s, held %s, No "
                    "back %s; the left HUD out for a talk %s, back %s; the first action %d frames after the last command; "
                    "Analyze %d lines (\"%s\", \"%s\"), each held %d, over %d frames after %s; %d problems\n",
                    touchLook ? "yes" : "NO", inFrames, slideSeq.c_str(), gated ? "ignored" : "TAKEN", askBanner ? "yes" : "NO", askHeld ? "yes" : "NO",
                    noBack ? "yes" : "NO", hudOut ? "yes" : "NO", hudIn ? "yes" : "NO", firstAt, analyzePages, firstLine.c_str(),
                    hpLine.c_str(), analyzeHeld, analyzeEnd, analyzeOk ? "yes" : "NO", problems);
        mapErrors += problems;
        app.runner.stop();
        app.game = keepGame;
    }
    { // The World Select (Util 1): the gates the pair has reached in its order, the level bands,
      // "Skip the first half?" where its flag is set, Back, and the sixth gate without bands
        int problems = 0;
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar;
        App::WorldSelect& ws = app.worldSelect;
        auto look = [&](const char* var) {
            if (const char* path = SDL_getenv(var)) {
                app.lastTicks = 0;
                drawFrame(app, false);
                saveFrame(app, path);
            }
        };
        auto settle = [&] { for (int f = 0; f < 12 && app.worldChoiceOpen; ++f) stepWorldSelect(app); };
        auto finish = [&] { for (int f = 0; f < 30 && app.worldChoiceOpen; ++f) stepWorldSelect(app); };
        app.game = cr::GameState();
        app.playerChar = 0;
        for (int f : {1076, 1077, 1163}) app.game.setFlag(f, true); // (Omega) worlds 0, 1; two bands
        openWorldSelect(app);
        settle();
        look("CR_WORLD_SHOT_A");
        const bool offered = ws.worlds == std::vector<int>{0, 1} && ws.levels == 2;
        ws.cursor = 1;
        worldSelectChoose(app, 1);
        settle();
        const bool bands = ws.state == 6;
        look("CR_WORLD_SHOT_B");
        worldSelectLevel(app, 1);
        settle();
        const bool asked = ws.state == 8;
        look("CR_WORLD_SHOT_C");
        worldSelectAnswer(app, true);
        finish();
        const bool chosen = !app.worldChoiceOpen && app.game.vars[0] == 1 && app.game.vars[1] == 1 && app.game.vars[2] == 0 &&
                            !app.game.flag(0) && !app.game.flag(1) && app.game.bands[0] == 1;
        openWorldSelect(app); // Back
        settle();
        worldSelectChoose(app, -1);
        finish();
        const bool back = !app.worldChoiceOpen && app.game.flag(0);
        app.game.setFlag(1162, true); // the first world asks to skip its first half (_gkashStgSkipTbl)
        openWorldSelect(app);
        settle();
        worldSelectChoose(app, 0);
        settle();
        const bool skipAsked = ws.state == 4 && ws.asking;
        worldSelectAnswer(app, true);
        settle();
        worldSelectLevel(app, 0);
        settle();
        worldSelectAnswer(app, true);
        finish();
        const bool skipped = app.game.vars[0] == 0 && app.game.flag(1) && !app.game.flag(0);
        app.game.setFlag(1082, true); // the sixth world (no bands)
        openWorldSelect(app);
        settle();
        const int last = int(ws.worlds.size()) - 1;
        worldSelectChoose(app, last);
        settle();
        const bool sixthAsked = ws.state == 8;
        worldSelectAnswer(app, true);
        finish();
        const bool sixth = last >= 0 && app.game.vars[0] == 5 && app.game.vars[1] == 0 && !app.game.flag(1);
        app.game = cr::GameState(); // scenario 1: its own worlds (6..12), numbered within it
        app.game.scenario = 1;
        for (int f : {1576, 1577}) app.game.setFlag(f, true);
        openWorldSelect(app);
        const bool order = ws.worlds == std::vector<int>{6, 7};
        answerWorldChoice(app, 1);
        const bool second = app.game.vars[0] == 1 && app.game.vars[2] == 1;
        problems += !offered + !bands + !asked + !chosen + !back + !skipAsked + !skipped + !sixthAsked + !sixth + !order + !second;
        problems += app.renderer.takeErrors();
        std::printf("world select: gates offered %s, bands %s, okay? %s, chosen %s, Back %s, skip asked %s and kept %s, "
                    "the sixth %s / %s, the second pair's order %s / %s; %d problems\n",
                    offered ? "yes" : "NO", bands ? "yes" : "NO", asked ? "yes" : "NO", chosen ? "yes" : "NO",
                    back ? "yes" : "NO", skipAsked ? "yes" : "NO", skipped ? "yes" : "NO", sixthAsked ? "yes" : "NO",
                    sixth ? "yes" : "NO", order ? "yes" : "NO", second ? "yes" : "NO", problems);
        mapErrors += problems;
        app.game = keepGame;
        app.playerChar = keepChar;
    }
    { // A script's camera in a 3D room (setCamera3D): map 113's 1024 x 1024 stage view, doubled,
      // centred on the 768-row screen - the target at the screen's centre, the frustum's top edge
      // 640 rows above the screen, its sides 512 past the 1024 layout's edges
        int problems = 0;
        const cr::StageCamera keep = app.stageCam;
        app.stageCam.fovDegrees = 53.9f, app.stageCam.viewWidth = app.stageCam.viewHeight = 1024;
        const cr::Vec3 eye{0, 10, 50}, tgt{0, 10, 0};
        const cr::Mat4 vp = scriptCameraView(app, eye, tgt);
        auto toScreen = [&](const cr::Vec3& p, float& x, float& y) {
            const float* m = vp.m;
            const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
            x = (cx / cw * 0.5f + 0.5f) * app.renderer.screenW();
            y = (0.5f - cy / cw * 0.5f) * cr::Renderer::kLogicalH;
        };
        const float t = std::tan(53.9f * 3.14159265f / 360.0f);
        float x0, y0, x1, y1, x2, y2;
        toScreen(tgt, x0, y0);
        toScreen({0, 10 + 50 * t, 0}, x1, y1);   // the frustum's top edge at the target's distance
        toScreen({50 * t, 10, 0}, x2, y2);       // its right edge
        const float cx = app.renderer.uiX() + 512;
        problems += std::fabs(x0 - cx) > 0.5f || std::fabs(y0 - 384) > 0.5f;
        problems += std::fabs(y1 - (-640)) > 0.5f || std::fabs(x2 - (cx + 1024)) > 0.5f;
        std::printf("script camera: target at (%.1f, %.1f), top edge at row %.1f, right edge at %.1f; %d problems\n", x0 - app.renderer.uiX(),
                    y0, y1, x2 - app.renderer.uiX(), problems);
        app.stageCam = keep;
        mapErrors += problems;
    }
    { // The battles' talks: the first battle's tutorial (EvrStart 2, var 1 = 0xff1) holds the
      // battle and hides its HUD until its pages are read; an event battle's script waits behind
      // the gate, the battle talks in its moments and the script goes on after it
        int problems = 0;
        const cr::GameState keepGame = app.game;
        const int keepChar = app.playerChar;
        App::Battle& b = app.battle;
        auto look = [&](const char* var) {
            if (const char* path = SDL_getenv(var)) {
                app.lastTicks = 0;
                drawFrame(app, false);
                saveFrame(app, path);
            }
        };
        auto mapIndex = [&](const char* name) {
            const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(name));
            return it == app.maps.end() ? -1 : int(it - app.maps.begin());
        };
        auto playScript = [&](int area, int map, int script) {
            app.battle.active = false;
            app.dialogue.clear();
            showMap(app, mapIndex(cr::kMapCollisionFile[map]), map, area);
            app.pendingEntry = App::Entry::None;
            app.runner.stop();
            app.runner.setGate(-1);
            return app.runner.start(app.events, script, app.game, app.area, app.mapNumber);
        };
        auto readOn = [&](int f) { // pages turn, item windows close
            if (!app.dialogue.empty() && f % 3 == 2) advanceDialogue(app);
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        };
        // 1. The first battle's tutorial
        app.testNoTutorials = false;
        app.game = cr::GameState();
        app.playerChar = 0;
        cr::giveStartingItems(app.game, app.itemTable);
        showMap(app, 0, 0, 2);
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        app.puzzleBanner = App::PuzzleBanner(); // (an earlier test's)
        startBattle(app, {1, 0, -1, 0}, 0);
        int talkFrames = 0, pagesSeen = 0, pageFrames = 0;
        bool tutorialSeen = false;
        for (int f = 0; f < 2000 && b.active; ++f) {
            stepBattle(app);
            if (battletalk::on(app)) {
                ++talkFrames;
                pagesSeen = std::max(pagesSeen, int(app.dialogue.size()));
                if (!app.dialogue.empty() && ++pageFrames == 12) look("CR_TALK_SHOT_A"); // the first page and its picture
                if (pageFrames >= 12) readOn(f);
            } else if (talkFrames > 0) { // (the talk's end: btl_main 400, then the menu a frame on)
                for (int k = 0; k < 2 && b.phase == App::Battle::Intro; ++k) stepBattle(app);
                tutorialSeen = app.game.flag(0xff1) && !app.runner.running() && b.phase == App::Battle::Command;
                break;
            }
        }
        problems += !tutorialSeen || pagesSeen == 0;
        if (b.active) endBattle(app, false);
        // 2. An event battle with its talk (Omega): j001_d05 script 203 - the scene, Battle 651 (stage 11),
        // then flag 1403 (0x57b) and on to area 9 map 169. Group 651 talks as round 3 ends
        // (chk_karaok_between) and can't fall below 1 HP before flag 0x57b (chk_karaok_dmg_hp)
        app.game = cr::GameState();
        cr::giveStartingItems(app.game, app.itemTable);
        app.game.setFlag(0xff1, true);
        bool waited = true, continued = false, gated = false, bossWon = false, scriptEnded = false;
        int talk1 = 0, heldAt1 = 0, lateRound = 0, healedWhole = 0, talk2 = 0, laterBattles = 0;
        int scriptMap = -1;
        for (int m = 0; m < cr::kNumMaps && scriptMap < 0; ++m) {
            if (cr::areaForMap(m, 6) != 6 || mapIndex(cr::kMapCollisionFile[m]) < 0) continue;
            showMap(app, mapIndex(cr::kMapCollisionFile[m]), m, 6);
            for (const cr::FieldNpc& n : app.events.npcs(m))
                if (n.id == 203) scriptMap = m;
            if (scriptMap < 0) scriptMap = m; // (no NPC of its own: started from another script - the area's first map)
        }
        if (scriptMap >= 0 && playScript(6, scriptMap, 203)) {
            int f = 0;
            for (; f < 6000 && !b.active && app.runner.running(); ++f) stepEvents(app), readOn(f);
            gated = b.active && b.eventBattle && b.start.group == 651 && app.runner.running() &&
                    app.runner.gate() == battletalk::kFlag;
            const std::string where = app.runner.where();
            if (gated) {
                for (int k = 0; k < 2; ++k) b.rules.f[size_t(k)].hp = b.rules.f[size_t(k)].hpMax = 999999;
                bool shot = false;
                for (f = 0; f < 30000 && b.active; ++f) {
                    if (battletalk::on(app)) {
                        if (!talk1) talk1 = b.rules.round;
                        if (!shot && !app.dialogue.empty()) look("CR_TALK_SHOT_C"), shot = true;
                        readOn(f);
                    } else if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) { // the pair attacks
                        if (!talk1) waited &= app.runner.running() && app.runner.where() == where; // (its talks run on in it)
                        battleChooseTop(app, b.rules.f[1].alive() ? 1 : 0);
                        battleChooseCommand(app, 0);
                        if (b.menu == App::Battle::Targets && !b.picks.empty()) battleChooseTarget(app, b.picks[0]);
                        if (b.phase == App::Battle::Command && b.menu == App::Battle::Commands) { // (solo: the second member)
                            battleChooseCommand(app, 0);
                            if (b.menu == App::Battle::Targets && !b.picks.empty()) battleChooseTarget(app, b.picks[0]);
                        }
                    } else if (b.phase == App::Battle::Results) {
                        bossWon = true;
                        endBattle(app, true);
                        break;
                    }
                    stepBattle(app);
                    if (!app.game.flag(1403) && b.rules.f[2].present && b.rules.f[2].hp == 1) ++heldAt1;
                }
                if (b.active) endBattle(app, true);
                for (f = 0; f < 20000 && app.runner.running(); ++f) { // the scene goes on (its later battles won)
                    if (b.active) {
                        ++laterBattles;
                        for (int k = 2; k < 5; ++k) b.rules.f[size_t(k)].hp = 0;
                        endBattle(app, true);
                        continue;
                    }
                    stepEvents(app), readOn(f);
                }
                continued = app.area == 9 && app.mapNumber == 169 && app.game.flag(1403);
                scriptEnded = !app.runner.running();
            }
        }
        (void)lateRound, (void)healedWhole, (void)talk2;
        problems += !gated + !waited + !continued + !scriptEnded + (talk1 != 3) + !bossWon;
        if (b.active) endBattle(app, false);
        app.runner.stop();
        app.runner.setGate(-1);
        problems += app.renderer.takeErrors();
        std::printf("battle talks: tutorial %s (%d pages, %d frames held), event battle 651 %s (script 203 on map %d), waits %s, "
                    "talks after round %d, held at 1 HP %d frames, won %s, %d later battles, on to area 9 map 169 %s, script ended %s; %d problems\n",
                    tutorialSeen ? "shown" : "NOT shown", pagesSeen, talkFrames, gated ? "gated" : "NOT gated", scriptMap,
                    waited ? "yes" : "NO", talk1, heldAt1, bossWon ? "yes" : "NO", laterBattles, continued ? "yes" : "NO",
                    scriptEnded ? "yes" : "NO", problems);
        mapErrors += problems;
        app.testNoTutorials = true;
        app.game = keepGame;
        app.playerChar = keepChar;
    }
    { // The NPC model table's story models (197..304: the scenes' bosses and the party's battle
      // models - rows 501.. of every dungeon and town use 198, 200..214): each loads
        int loaded = 0, failed = 0, animated = 0, absent = 0;
        for (int k = 197; k < cr::kNumNpcModels; ++k) {
            const std::string file = cr::kNpcModelFile[k];
            if (app.npcModels.count(file)) { ++loaded; continue; }
            if (app.vitaOnly ? !app.vita.contains(file) : app.androidOnly ? !app.android.contains(file) : !SDL_GetPathInfo((app.dataDir + file).c_str(), nullptr)) { ++absent; continue; } // 197: not shipped, unused
            LoadedModel lm;
            loadModelFile(app, file, lm, true);
            if (lm.meshes.empty()) ++failed;
            else ++loaded, animated += !lm.actions.empty();
            unloadLoaded(app, lm);
        }
        mapErrors += failed;
        std::printf("NPC model table: %d story models (197..%d) load, %d with fitting actions, %d not in the data, %d failed\n",
                    loaded, cr::kNumNpcModels - 1, animated, absent, failed);
    }
    { // eff3d on the field over the whole run: the puzzle rooms', the footsteps' dust, the scripts'
        // The dust: walk the leader on the maps in turn until a step lands on a floor with a sound
        int stepMap = -1, dust = 0, steps = 0;
        const bool keepPlaying = app.playing;
        const float keepSpeed = app.speed;
        app.playing = true, app.speed = 1.0f; // (the animations run)
        for (size_t m = 0; m < app.maps.size() && stepMap < 0; ++m) {
            app.runner.stop();
            showMap(app, int(m));
            app.runner.stop();
            app.pendingEntry = App::Entry::None;
            if (!app.havePlayer || app.player.actions.size() < 2) continue;
            const int before = app.fieldEffectCount[0x87];
            const size_t sounds = app.soundLog.size();
            app.walker.gait = cr::Gait::Walk;
            for (int f = 0; f < 60; ++f) {
                const int want = std::min(int(app.walker.gait), int(app.player.actions.size()) - 1);
                if (want != app.player.action) app.player.action = want, app.player.time = 0, app.footPrev = 0;
                animate(app, app.player, 1.0f / kFramesPerSecond);
                playFootsteps(app);
                stepEffects(app, app.fieldEffects);
            }
            app.walker.gait = cr::Gait::Stand;
            dust = app.fieldEffectCount[0x87] - before;
            steps = int(app.soundLog.size() - sounds);
            if (dust > 0) stepMap = int(m);
        }
        app.playing = keepPlaying, app.speed = keepSpeed;
        mapErrors += dust == 0 || dust != steps;
        std::printf("footstep dust: %d puffs for %d step sounds on %s\n", dust, steps,
                    stepMap >= 0 ? app.maps[size_t(stepMap)].c_str() : "no map");
        std::string ids;
        int kinds = 0;
        for (int i = 0; i < int(app.fieldEffectCount.size()); ++i)
            if (app.fieldEffectCount[size_t(i)]) ids += (kinds++ ? " " : "") + std::to_string(i) + "x" + std::to_string(app.fieldEffectCount[size_t(i)]);
        // (Omega: the dust 0x87, the candles' flame 0x92)
        const int fxProblems = !app.fieldEffectCount[0x87] + !app.fieldEffectCount[0x92] + !app.fieldEffectCount[5];
        std::printf("field effects: %d kinds started (%s); FrmPlayEff %d, Uty_eff3d_set_fld %d, script quakes %d; %d problems\n",
                    kinds, ids.c_str(), app.frameEffectCalls, app.scriptEffectCalls, app.scriptQuakes, fxProblems);
        std::printf("pictures: TexDisp %d, TutoTexDisp %d (pages up to %d), the \"!\" %d\n", app.texDispCalls, app.tutoCalls,
                    app.tutoPage, app.exclamationCalls);
        std::string props;
        for (const auto& [name, n] : app.matAnimDraws) props += " " + name + "x" + std::to_string(n);
        std::printf("props with animated materials drawn:%s\n", props.empty() ? " none" : props.c_str());
        mapErrors += fxProblems;
    }
    std::printf("selftest: %zu models, %zu maps (%d with a stage camera), %d frames, %d render API errors in %d models, "
                "%d map problems; player spawned on %d maps, walked > 5 units on %d\n",
                app.models.size(), app.maps.size(), camMaps, frames, glErrors, failedModels, mapErrors, spawned, walked);
    glErrors += mapErrors;
    return glErrors ? 1 : 0;
}

// A new game (scenario 0) without the title: prty_init, prty_init_new(0), then the opening's
// field (j001_d05's map 69, the planet) as the title's New reaches it.
// (scenario 1: the boot script's var 0 = 2 - Select Scenario's second page - Warp 10, 214)
// CR_CAPTURE_VARS="k:v,...": script variables a capture sets (before the map's entry, and before --event's script)
void captureVars(App& app) {
    if (const char* cv = SDL_getenv("CR_CAPTURE_VARS"))
        for (const char* p = cv; *p;) {
            int k = 0, v = 0, used = 0;
            if (std::sscanf(p, "%d:%d%n", &k, &v, &used) != 2) break;
            app.game.vars[k] = v;
            p += used;
            if (*p == ',') ++p;
        }
}

// --explore n: plays a new game by itself - talks to everyone and walks into every trigger not yet
// tried with the current story flags (people first, exits last), answering yes and winning
// battles - and logs how far the story gets and what it asked for that isn't there yet.
int runExplore(App& app, int maxActions, int scenario) {
    app.persist = false;
    app.typeOn = false;
    setMode(app, Mode::Maps);
    startNewGame(app, scenario);
    auto flagsKey = [&] {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t w : app.game.flags) h = (h ^ w) * 1099511628211ull;
        return h;
    };
    std::map<uint64_t, std::set<int>> worldsTaken; // story state -> World Select positions chosen in it
    int worldCount = 0;
    uint64_t worldKey = 0;                         // the state the last World Select opened in
    int totalFrames = 0, pagesRead = 0;
    bool worldAsked = false;                  // the last action opened the World Select
    std::set<std::pair<int, int>> deadExits;           // ways that didn't lead where the dry run said
    bool hopping = false;                               // the step taken is the way back's
    std::map<int, std::map<int, int>> roomExits;          // map -> (id x 2 + examine -> the room it leads to)
    std::map<int, std::pair<bool, uint64_t>> roomLeft;  // map -> (something untried there, in that state)
    std::set<std::pair<int, int>> triedEver;  // (map, id x 2 + examine): done before, in any state
    std::set<std::pair<int, uint64_t>> escaped; // (map, state) where the way out to another area was taken
    std::set<std::pair<int, int>> worldDoors; // (map, id) that do: tried last, once the rooms about are done
    auto settle = [&](int limit) {
        int f = 0;
        for (; f < limit && (app.runner.running() || app.pendingEntry != App::Entry::None || app.battle.active); ++f) {
            if (app.battle.active) { // its talks play, then it's won at its first command
                stepBattle(app);
                if (!battletalk::on(app) && app.battle.phase == App::Battle::Command) {
                    app.battle.rules.defeatEnemies(); // (with its EXP, Oz and drops)
                    std::printf("     battle %d won\n", app.battle.start.group);
                    endBattle(app, true);
                }
                if (!app.dialogue.empty()) { pagesRead += int(app.dialogue.size()); advanceDialogue(app); }
                continue;
            }
            stepEvents(app);
            if (!app.dialogue.empty()) { pagesRead += int(app.dialogue.size()); app.dialogue.clear(); }
            if (app.choiceFlag >= 0) answerChoice(app, true);
            if (app.worldChoiceOpen) { // the newest world not yet taken in this state (then the newest), its
                // first half skipped once done
                const int n = int(app.worldSelect.worlds.size());
                worldKey = flagsKey();
                std::set<int>& done = worldsTaken[worldKey];
                int pos = n - 1;
                while (pos >= 0 && done.count(pos)) --pos;
                if (pos < 0) pos = n - 1;
                done.insert(pos);
                worldCount = n;
                answerWorldChoice(app, pos, true);
                worldAsked = true;
            }
            if (app.shop.open && !app.shop.talkRunning) { // keys for the gold boxes (up to 9, as Oz allows), then out
                const std::vector<int> stock = cr::shopStock(app.game, app.itemTable, shopCharacter(app), 0, app.shop.level);
                const cr::ItemParam* key = app.itemTable.item(2);
                if (key && std::find(stock.begin(), stock.end(), 2) != stock.end()) {
                    const int n = std::min(9 - ownedCount(app, 2), cr::maxPurchase(cr::buyPrice(*key), app.game.oz, ownedCount(app, 2)));
                    if (n > 0) {
                        app.game.items[2] += n;
                        cr::addOz(app.game, -n * cr::buyPrice(*key));
                        std::printf("     bought %d key%s\n", n, n > 1 ? "s" : "");
                    }
                }
                closeShop(app);
            }
            if (app.staffRoll.open) { // the ending's credits, fast, the end card tapped away
                if (!app.staffRoll.fast) std::printf("     staff roll (flags 31 %d, 32 %d)\n", int(app.game.flag(31)), int(app.game.flag(32)));
                app.staffRoll.fast = true;
                stepStaffRoll(app);
                if (app.staffRoll.state >= 7) staffRollTap(app);
            }
            if (app.saveScreen.open && !app.booting) closeSaveScreen(app);
            if (app.itemWindowOpen) app.game.setFlag(0, true);
            app.tapPending = true;
        }
        totalFrames += f;
        if (SDL_getenv("CR_EXPLORE_DRAW")) drawFrame(app, false); // (a frame drawn: the device's deferred frees run, for measuring what a play holds)
        return f;
    };
    auto flagCount = [&] {
        int c = 0;
        for (uint32_t w : app.game.flags) c += int(std::bitset<32>(w).count());
        return c;
    };
    auto stateKey = [&] {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t w : app.game.flags) h = (h ^ w) * 1099511628211ull;
        return h;
    };
    settle(20000);
    std::set<std::tuple<int, int, uint64_t>> tried;
    std::set<int> mapsSeen{app.mapNumber};
    std::map<int, int> visits{{app.mapNumber, 1}}; // (rooms seen: the less visited first)
    int step = 0, stuckReason = 0;
    bool presentsGiven = false;
    for (; step < maxActions; ++step) {
        // candidates: people / objects (kind bit 3), then touch boxes that don't warp, then exits
        struct Cand { int id; bool examine; int rank; };
        std::vector<Cand> cands;
        // a dry run on a copy of the state tells where a script leads: new rooms first, back last
        int lastTarget = -1;
        auto rankOf = [&](int id, int base) {
            cr::GameState trial = app.game;
            const cr::EventResult r = cr::runEvent(app.events, id, trial, app.area, app.mapNumber);
            lastTarget = r.warp.valid && (r.warp.area < 0 || r.warp.area == app.area) ? r.warp.map : -1;
            if (!r.warp.valid) return base;
            if (!mapsSeen.count(r.warp.map)) return 2;
            const bool away = r.warp.area >= 0 && r.warp.area != app.area; // (another area: the Ark - not held back by its many visits)
            return away ? 25 : 5 + std::min(visits[r.warp.map], 50); // (away: once the rooms about are seen ~20 times, or when stuck)
        };
        for (size_t i = 0; i < app.npcs.size(); ++i) {
            const cr::FieldNpc& n = app.npcs[i];
            if (!npcActive(app, n)) continue;
            // (NpcMdlRead's story actors, 400 on, wait at the origin or out of sight; a table NPC may
            // stand at a room's origin)
            const bool parked = (n.pos[1] >= 900.0f || (n.id >= 400 && n.pos[0] == 0 && n.pos[1] == 0 && n.pos[2] == 0)) &&
                                !app.placedNpcs.count(n.id);
            if ((n.kind & 8) && !parked) {
                lastTarget = -1;
                cands.push_back({n.id, true, worldDoors.count({app.mapNumber, n.id}) ? 60 : rankOf(n.treasure & 3 ? n.id + 1 : n.id, 0)});
                if (lastTarget >= 0 && !deadExits.count({app.mapNumber, n.id * 2 + 1})) roomExits[app.mapNumber][n.id * 2 + 1] = lastTarget;
            }
            if (n.touchTrigger()) {
                cands.push_back({n.id, false, rankOf(n.id, 1)});
                if (lastTarget >= 0 && !deadExits.count({app.mapNumber, n.id * 2})) roomExits[app.mapNumber][n.id * 2] = lastTarget;
            }
        }
        // a talk or touch that leads nowhere, done here before (a lever, often): after the ways into new rooms
        for (Cand& c : cands)
            if (c.rank < 2 && triedEver.count({app.mapNumber, c.id * 2 + c.examine})) c.rank = 3;
        // (at the same rank walk-in triggers first: they're the rooms' ways on more often than doors)
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.rank != b.rank ? a.rank < b.rank : (!a.examine && b.examine);
        });
        if (app.puzzle.stage >= 0 && !app.game.flag(3000 + app.puzzle.stage) && !app.runner.running()) {
            // a puzzle room it can't play: solved on the spot (its clear flag), so the room's ways on open
            std::printf("     puzzle stage %d taken as solved (flag %d)\n", app.puzzle.stage, 3000 + app.puzzle.stage);
            app.game.setFlag(3000 + app.puzzle.stage, true);
            puzzle::enter(app); // the room again as solved: its crystal out (the way on)
            buildNpcDraws(app);
            tried.clear();
            continue;
        }
        if (const int opened = bingo::openAll(app)) { // the Camp's Scenarios: cells the presents open (cards done: var 0x16)
            std::printf("     scenarios: %d cells opened, var 0x16 = %d\n", opened,
                        app.game.vars.count(0x16) ? app.game.vars[0x16] : 0);
            tried.clear();
        }
        const uint64_t key = stateKey();
        { // what's left here, for the way back when stuck elsewhere
            bool left = false;
            for (const Cand& c : cands) left |= !tried.count({app.mapNumber, c.id * 2 + c.examine, key}) && c.rank < 60;
            roomLeft[app.mapNumber] = {left, key};
        }
        if (const char* cm = SDL_getenv("CR_EXPLORE_CANDS"); cm && SDL_atoi(cm) == app.mapNumber) { // (debugging)
            std::printf("     candidates on map %d:", app.mapNumber);
            for (const Cand& c : cands)
                std::printf(" %s%d(r%d%s)", c.examine ? "t" : "x", c.id, c.rank, tried.count({app.mapNumber, c.id * 2 + c.examine, key}) ? ",tried" : "");
            std::printf("\n");
        }
        const Cand* pick = nullptr;
        for (const Cand& c : cands)
            if (!tried.count({app.mapNumber, c.id * 2 + c.examine, key})) { pick = &c; break; }
        if (!pick && app.puzzle.stage >= 0 && !app.game.flag(3000 + app.puzzle.stage)) {
            // a puzzle it can't solve: as if its crystal were reached (the clear flag), then on
            std::printf("     puzzle stage %d taken as solved (flag %d)\n", app.puzzle.stage, 3000 + app.puzzle.stage);
            app.game.setFlag(3000 + app.puzzle.stage, true);
            tried.clear();
            continue;
        }
        if (!pick && !escaped.count({app.mapNumber, key})) {
            // all tried here: once for this story state, a way out to another area (the Ark, most
            // often - least wanted by the ranking, as the room seen most), whatever was tried
            // ... else any way out (the ranking's best): back towards rooms with something untried
            const Cand* anyExit = nullptr;
            for (const Cand& c : cands) {
                cr::GameState trial = app.game;
                const cr::EventResult r = cr::runEvent(app.events, c.examine && (findNpc(app, c.id) && (findNpc(app, c.id)->treasure & 3))
                                                                      ? c.id + 1 : c.id, trial, app.area, app.mapNumber);
                if (!r.warp.valid || worldDoors.count({app.mapNumber, c.id})) continue;
                if (r.warp.area >= 0 && r.warp.area != app.area) { pick = &c; break; }
                if (!anyExit) anyExit = &c;
            }
            if (!pick) pick = anyExit;
            escaped.insert({app.mapNumber, key});
            if (pick) std::printf("     nothing new here: out again\n");
        }
        if (!pick) { // the nearest room (by the doors known) with something left - or seen in another state
            std::map<int, int> firstHop{{app.mapNumber, -1}};
            std::vector<int> queue{app.mapNumber};
            int goal = -1;
            for (size_t qi = 0; qi < queue.size() && goal < 0; ++qi) {
                const int at = queue[qi];
                for (const auto& [ck, to] : roomExits[at]) {
                    if (firstHop.count(to)) continue;
                    firstHop[to] = at == app.mapNumber ? ck : firstHop[at];
                    queue.push_back(to);
                    const auto rl = roomLeft.find(to);
                    if (rl == roomLeft.end() || rl->second.first || rl->second.second != key) { goal = to; break; }
                }
            }
            if (goal >= 0)
                for (const Cand& c : cands)
                    if (c.id * 2 + c.examine == firstHop[goal]) { pick = &c; break; }
            if (pick) std::printf("     towards map %d\n", goal), hopping = true;
        }
        auto presentsMissing = [&] {
            for (int item = 551; item <= 630; ++item)
                if (ownedCount(app, item) == 0) return true;
            return false;
        };
        if (!pick && app.game.flag(0x642) && !presentsGiven && presentsMissing() && SDL_getenv("CR_EXPLORE_NO_GIFTS")) {
            std::printf("     out of ideas with presents still missing (CR_EXPLORE_NO_GIFTS: stopping)\n");
            stuckReason = 1;
            break;
        }
        if (!pick && app.game.flag(0x642) && !presentsGiven && presentsMissing()) {
            // out of ideas with the cards up: the presents (551..630) a full collection would hold,
            // taken as found (the worlds' hunt for them isn't played), once - their cards then fill
            presentsGiven = true;
            int given = 0;
            for (int item = 551; item <= 630; ++item)
                if (ownedCount(app, item) == 0) app.game.items[item] = 1, ++given;
            std::printf("     presents taken as found: %d\n", given);
            tried.clear();
            continue;
        }
        if (!pick) { stuckReason = 1; break; }
        const int hopKey = pick->id * 2 + pick->examine;
        tried.insert({app.mapNumber, pick->id * 2 + pick->examine, key});
        triedEver.insert({app.mapNumber, pick->id * 2 + pick->examine});
        const int pickMap = app.mapNumber, pickId = pick->id;
        worldAsked = false;
        const int fromMap = app.mapNumber, flagsBefore = flagCount(), pagesBefore = pagesRead;
        const cr::GameState flagsWere = app.game;
        const size_t unsupportedBefore = app.unsupported.size();
        const cr::FieldNpc* n = findNpc(app, pick->id);
        if (!n || !app.havePlayer) continue;
        const char* traceStep = SDL_getenv("CR_EXPLORE_TRACE"); // debugging: every instruction of one step
        if (traceStep && SDL_atoi(traceStep) == step)
            cr::setEventTrace([](size_t pc, int op) { std::printf("    pc %#zx op %d\n", pc, op); });
        bool started;
        if (pick->examine) { // from each side of its box, facing it, until one starts it
            started = false;
            const float dx = n->box[0] / 2 + app.walker.radius, dz = n->box[2] / 2 + app.walker.radius;
            const float side[4][3] = {{dx, 0, 3.14159265f * 1.5f}, {-dx, 0, 3.14159265f * 0.5f}, {0, dz, 3.14159265f}, {0, -dz, 0}};
            // (only where it's the one in reach: two side by side would answer for each other)
            for (int k = 0; k < 5 && !started; ++k) {
                app.walker.pos = k < 4 ? cr::Vec3{n->pos[0] + side[k][0], n->pos[1], n->pos[2] + side[k][1]}
                                       : cr::Vec3{n->pos[0], n->pos[1], n->pos[2]};
                if (k && k < 4) app.walker.heading = side[k][2];
                if (interactTarget(app) == n) started = interact(app);
            }
        } else {
            app.walker.pos = {n->boxCenterX(), n->pos[1], n->boxCenterZ()};
            app.lastKick = 0;
            checkTriggers(app);
            started = app.runner.running();
        }
        const int frames = settle(20000);
        cr::setEventTrace(nullptr);
        if (!started) {
            if (pick->examine) std::printf("     %d on map %d: out of reach\n", pick->id, app.mapNumber);
            continue;
        }
        if (app.mapNumber != fromMap) ++visits[app.mapNumber];
        mapsSeen.insert(app.mapNumber);
        std::string extra;
        for (size_t u = unsupportedBefore; u < app.unsupported.size(); ++u) extra += ", " + app.unsupported[u];
        if (app.runner.running()) extra += ", STILL RUNNING (" + app.runner.where() + ")";
        if (worldAsked) { // a terminal: tried last, and again while it has worlds not taken in this state
            worldDoors.insert({pickMap, pickId});
            if (int(worldsTaken[worldKey].size()) < worldCount) tried.erase({pickMap, hopKey, key});
        }
        if (hopping && app.mapNumber == pickMap) roomExits[pickMap].erase(hopKey), deadExits.insert({pickMap, hopKey}); // (it didn't lead there: forgotten)
        hopping = false;
        std::printf("%4d map %3d %s %4d: %+d flags, %d pages, %d frames%s%s\n", step, fromMap,
                    pick->examine ? "talk " : "touch", pick->id, flagCount() - flagsBefore, pagesRead - pagesBefore,
                    frames, app.mapNumber != fromMap ? (" -> map " + std::to_string(app.mapNumber)).c_str() : "",
                    extra.c_str());
        { // keys (2) and presents (551..630) found, taken (a card's cell, a trade)
            std::string got;
            for (int item : {2}) {
                const int d = ownedCount(app, item) - (flagsWere.items.count(item) ? flagsWere.items.at(item) : 0);
                if (d) got += (d > 0 ? " +" : " ") + std::to_string(d) + " key";
            }
            for (int item = 551; item <= 630; ++item) {
                const int was = flagsWere.items.count(item) ? flagsWere.items.at(item) : 0, now = ownedCount(app, item);
                if (now != was) got += (now > was ? " +" : " -") + std::to_string(item);
            }
            if (!got.empty()) std::printf("     items:%s\n", got.c_str());
        }
        if (flagCount() != flagsBefore && SDL_getenv("CR_EXPLORE_FLAGS")) { // (debugging: which)
            std::printf("     flags:");
            for (int fl = 0; fl < cr::GameState::kFlagCount; ++fl)
                if (app.game.flag(fl) != flagsWere.flag(fl)) std::printf(" %s%d", app.game.flag(fl) ? "+" : "-", fl);
            std::printf("\n");
        }
        if (app.runner.running()) { app.runner.stop(); stuckReason = 2; break; }
    }
    std::printf("explore: area %d, puzzle stage %d\n", app.area, app.puzzle.stage);
    std::printf("explore: %d actions, %d frames, %d pages, %zu maps seen, %d flags set, ends on map %d (%s)\n", step,
                totalFrames, pagesRead, mapsSeen.size(), flagCount(), app.mapNumber,
                stuckReason == 1 ? "nothing new to try" : stuckReason == 2 ? "a script never finished" : "action limit");
    { // the collection (551..630): which presents it found
        std::string missing;
        int held = 0;
        for (int item = 551; item <= 630; ++item)
            if (ownedCount(app, item) > 0) ++held;
            else missing += " " + std::to_string(item);
        std::printf("explore: presents %d / 80%s%s%s\n", held, presentsGiven ? " (some taken as found)" : "",
                    missing.empty() ? "" : ", missing:", missing.c_str());
    }
    std::printf("explore: flags set:");
    for (int fl = 0; fl < cr::GameState::kFlagCount; ++fl)
        if (app.game.flag(fl)) std::printf(" %d", fl);
    std::printf("\n");
    std::map<std::string, int> counts;
    for (const std::string& u : app.unsupported) counts[u.substr(0, u.find(" on map"))]++;
    for (const auto& [what, c] : counts) std::printf("  not implemented: %s x%d\n", what.c_str(), c);
    return 0;
}

// --capture <map.cmp> <out.bmp>: renders that map's composite (background + collision through
// the stage camera, no HUD) and saves the frame — for checking alignment without a person.
// --export-textures [dir] (without one, textures/export/ in the game folder): every distinct picture
// the game draws (its .mct files and the textures inside model packages) as <hash>.png (upright), for tools/hd_textures.py to upscale - or any editor - into textures/hd/.
// With --vita, the Vita's lossless version of each picture it has alike (same size, alpha kept, the
// same picture: within 12/255 a channel on average - not the art it redrew) is exported instead,
// and <ios hash>.cralias (the Vita's hash, 16 hex digits) lets the one upscale serve the iOS
// version too: the game finds it whichever it decoded
// --export-models [dir] (without one, models/export/ in the game folder): every model package as
// <name>.glb - its skeleton, meshes at rest skinned to it, materials, textures and motions (the
// package's and its <name>e*.mca files'), for editing and models/hd/ (assets/gltf_model.hpp)
int runExportModels(App& app, const char* dirArg) {
    std::string dir = dirArg && *dirArg ? dirArg : cr::modelsFolder("export");
    if (dir.back() != '/') dir += '/';
    if (!SDL_CreateDirectory(dir.c_str()) && !(dirArg && *dirArg)) dir = cr::saveDir() + "models/export/", SDL_CreateDirectory(dir.c_str());
    std::printf("exporting to %s\n", dir.c_str());
    int written = 0, failed = 0;
    size_t bytes = 0;
    for (const std::string& name : app.models) {
        std::vector<uint8_t> data;
        cr::Model m;
        std::string err;
        const bool fromVita = app.vita.isOpen() && app.vita.read(name, data) && cr::loadModelPackage(data, m, &err);
        if (!fromVita && (!cr::readFile(app.dataDir + name, data) || !cr::loadModelPackage(data, m, &err))) {
            std::printf("  %s: %s\n", name.c_str(), err.c_str());
            ++failed;
            continue;
        }
        std::vector<cr::Action> actions; // (as loadModelFile keeps them)
        const std::vector<bool> kept = packageActionsKept(m);
        for (size_t k = 0; k < m.actions.size(); ++k)
            if (kept[k]) actions.push_back(m.actions[k]);
        const std::string stem = name.substr(0, name.size() - 4);
        for (const auto& file : app.animFiles) {
            if (file.rfind(stem + "e", 0) != 0) continue;
            std::vector<uint8_t> ad;
            std::vector<cr::Action> acts;
            if (!cr::readFile(app.dataDir + file, ad) || !cr::parseMca(ad.data(), ad.size(), 0, acts)) continue;
            for (size_t i = 0; i < acts.size(); ++i)
                if (cr::actionFits(m.skeleton, acts[i])) {
                    acts[i].name = file.substr(0, file.size() - 4) + "." + std::to_string(i + 1);
                    actions.push_back(std::move(acts[i]));
                }
        }
        cr::GltfExportStats gs;
        if (cr::exportGlb(m, actions, dir + stem + ".glb", &err, &gs)) {
            ++written;
            bytes += gs.bytes;
            if (SDL_getenv("CR_EXPORT_VERBOSE"))
                std::printf("  %s: %d meshes, %d triangles, %d pictures, %d motions (%d channels), %zu KB\n", name.c_str(), gs.meshes,
                            gs.triangles, gs.images, gs.animations, gs.channels, gs.bytes / 1024);
        } else {
            std::printf("  %s: %s\n", name.c_str(), err.c_str());
            ++failed;
        }
    }
    std::printf("%d models written (%zu MB), %d failed\n", written, bytes >> 20, failed);
    return failed && !written ? 1 : 0;
}

int runExportTextures(App& app, const char* dirArg) {
    // (no folder given: textures/export/ in the game folder, else the profile folder's when that can't be written)
    std::string dir = dirArg && *dirArg ? dirArg : cr::texturesFolder("export");
    if (dir.back() != '/') dir += '/';
    if (!SDL_CreateDirectory(dir.c_str()) && !(dirArg && *dirArg)) dir = cr::saveDir() + "textures/export/", SDL_CreateDirectory(dir.c_str());
    std::printf("exporting to %s\n", dir.c_str());
    std::set<uint64_t> seen;
    int written = 0, failed = 0, dupes = 0, fromVita = 0, aliases = 0;
    auto put = [&](const cr::Image& img) {
        const uint64_t h = cr::imageHash(img);
        if (!seen.insert(h).second) {
            ++dupes;
            return h;
        }
        const std::string path = dir + cr::hashName(h) + ".png";
        if (SDL_GetPathInfo(path.c_str(), nullptr)) return h; // already exported
        if (cr::savePngImage(path, img)) ++written;
        else ++failed;
        return h;
    };
    // The Vita's picture for the iOS one, when it is the same picture losslessly
    auto alike = [](const cr::Image& ios, const cr::Image& vita) {
        if (ios.alphaOnly || ios.width != vita.width || ios.height != vita.height || ios.rgba.size() != vita.rgba.size()) return false;
        double sum = 0;
        for (size_t i = 0; i < ios.rgba.size(); ++i) sum += std::abs(int(ios.rgba[i]) - int(vita.rgba[i]));
        return sum / double(ios.rgba.size()) < 12;
    };
    auto pair = [&](const cr::Image& ios, const cr::Image* vita) {
        if (!vita || !alike(ios, *vita)) { // (the art the Vita redrew: both, each for its own)
            put(ios);
            if (vita) put(*vita);
            return;
        }
        const uint64_t v = put(*vita), i = cr::imageHash(ios);
        ++fromVita;
        if (i == v) return;
        const std::string path = dir + cr::hashName(i) + ".cralias", target = cr::hashName(v);
        if (SDL_SaveFile(path.c_str(), target.data(), target.size())) ++aliases;
    };
    for (const std::string& name : app.textures) {
        cr::Image ios, vita;
        std::vector<uint8_t> data;
        const bool haveIos = !app.vitaOnly && cr::readFile(app.dataDir + name, data) && cr::decodeMct(data, ios);
        bool haveVita = false;
        if (app.vita.isOpen() && name.size() > 4) {
            std::vector<uint8_t> g;
            haveVita = app.vita.read(name.substr(0, name.size() - 4) + ".gxt", g) && cr::decodeGxt(g, vita);
        }
        if (haveIos) pair(ios, haveVita ? &vita : nullptr);
        else if (haveVita) put(vita);
        else ++failed;
    }
    if (app.vita.isOpen() && !app.vitaOnly) // the Vita's own pictures (its warning screen, the titles in other languages ...)
        for (const std::string& n : app.vita.names()) {
            if (n.size() < 5 || n.compare(n.size() - 4, 4, ".gxt") != 0) continue;
            if (std::find(app.textures.begin(), app.textures.end(), n.substr(0, n.size() - 4) + ".mct") != app.textures.end()) continue;
            std::vector<uint8_t> g;
            cr::Image img;
            if (app.vita.read(n, g) && cr::decodeGxt(g, img)) put(img);
        }
    for (const std::string& name : app.models) {
        std::vector<uint8_t> data;
        cr::Model ios, vita;
        const bool haveIos = !app.vitaOnly && cr::readFile(app.dataDir + name, data) && cr::loadModelPackage(data, ios, nullptr);
        const bool haveVita = app.vita.isOpen() && app.vita.read(name, data) && cr::loadModelPackage(data, vita, nullptr);
        if (haveIos) {
            const bool paired = haveVita && vita.textures.size() == ios.textures.size();
            for (size_t k = 0; k < ios.textures.size(); ++k) pair(ios.textures[k], paired ? &vita.textures[k] : nullptr);
            if (haveVita && !paired)
                for (const cr::Image& img : vita.textures) put(img);
        } else if (haveVita)
            for (const cr::Image& img : vita.textures) put(img);
    }
    if (app.vita.isOpen() && !app.vitaOnly) // the Vita's own model packages (t0101_1..3.bin)
        for (const std::string& n : app.vita.names()) {
            if (n.size() < 5 || n.compare(n.size() - 4, 4, ".bin") != 0) continue;
            if (std::find(app.models.begin(), app.models.end(), n) != app.models.end()) continue;
            std::vector<uint8_t> data;
            cr::Model m;
            if (app.vita.read(n, data) && cr::loadModelPackage(data, m, nullptr))
                for (const cr::Image& img : m.textures) put(img);
        }
    std::printf("export: %zu distinct pictures (%d written, %d already there, %d repeats, %d failed; %d from the Vita, %d "
                "iOS hashes aliased to them) -> %s\n", seen.size(), written, int(seen.size()) - written - failed, dupes,
                failed, fromVita, aliases, dir.c_str());
    return failed ? 1 : 0;
}

// Through the title to a new game as a player would (the boot script runs on, so the opening is the
// title's own: --title-new p in a capture, --new-game in a window): Start, the pair p, New, Yes. stopAt
// "chars" / "save" / "ask" stays on Character Select / the save screen / New's "Begin new game?"
void titleToNewGame(App& app, int pair, const std::string& stopAt) {
    app.saveDirOverride = userDir() + "capture-empty/"; // (no saves: New)
    bootTitle(app);
    for (int f = 0; f < 1000 && !app.titleOpen; ++f) stepEvents(app);
    if (pair == 2) app.game.setFlag(31, true); // (the second page open: scenario 1 cleared)
    titleChoose(app, 0);
    for (int f = 0; f < 300 && !app.charSelectOpen; ++f) stepEvents(app);
    app.charSelectCursor = pair - 1;
    app.charSelectPage = pair - 1; // (pair 2: the second page, "Extra" - the choice is the page's)
    if (stopAt == "chars") { app.charSelectAnim = 100; return; }
    charSelectKey(app, SDLK_RETURN);
    for (int f = 0; f < 300 && !app.saveScreen.open; ++f) stepEvents(app);
    if (stopAt == "ask") { // (on New's "Begin new game?")
        saveScreenKey(app, SDLK_RETURN);
        app.saveScreen.anim = 100;
    } else if (stopAt != "save") {
        saveScreenKey(app, SDLK_RETURN);
        saveScreenKey(app, SDLK_RETURN);
        app.saveDirOverride.clear();
    } else app.saveScreen.anim = 100; // (faded in: a capture draws one frame)
}

int runCapture(App& app, const char* mapName, const char* outPath) {
    app.persist = false;
    if (const char* sz = SDL_getenv("CR_CAPTURE_SIZE")) { // CR_CAPTURE_SIZE=WxH: the window's size (e.g. 1280x720, a phone's 16:9)
        int cw = 0, ch = 0;
        if (std::sscanf(sz, "%dx%d", &cw, &ch) == 2 && cw >= 320 && ch >= 240) {
            SDL_SetWindowSize(app.window, cw, ch);
            SDL_SyncWindow(app.window);
        }
    }
    { // the logical screen as the first frame will have it (--frames runs step before drawing)
        int ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(app.window, &ww, &wh);
        if (ww > 0 && wh > 0) app.renderer.fitScreen(ww, wh);
    }
    app.ach.live = false; // (no banners of their own in captures: CR_CAPTURE_ACHIEVEMENT shows one)
    app.typeOn = SDL_getenv("CR_CAPTURE_TYPED") != nullptr; // CR_CAPTURE_TYPED=n: n frames of typing
    setMode(app, Mode::Maps);
    int idx = -1;
    for (size_t i = 0; i < app.maps.size(); ++i)
        if (app.maps[i] == mapName) idx = int(i);
    if (idx < 0) {
        std::printf("unknown map %s\n", mapName);
        return 1;
    }
    if (app.captureTitlePair) { // --title-new p: through the title as a player would
        // (CR_TITLE_NEW_STOP=chars / save / ask: stays on Character Select / the save screen / New's question)
        const char* stop = SDL_getenv("CR_TITLE_NEW_STOP");
        titleToNewGame(app, app.captureTitlePair, stop ? stop : "");
    } else if (app.captureTitle) bootTitle(app); // --title: the boot script, --frames n into it
    else if (app.newGame) startNewGame(app, app.captureScenario);
    else {
        for (int f : app.captureFlags) app.game.setFlag(f, true);
        captureVars(app);
        for (const auto& [from, to] : app.captureItems)
            for (int i = from; i <= to; ++i) app.game.items[i] = std::max(1, ownedCount(app, i));
        if (const char* w = SDL_getenv("CR_CAPTURE_WEAR")) { // "slot:item[,slot:item]": the leader's slots 0-2 gear, 3-6 genes
            for (const char* q = w; *q;) {
                const int slot = SDL_atoi(q), item = std::strchr(q, ':') ? SDL_atoi(std::strchr(q, ':') + 1) : 0;
                cr::PartyMember& pm = app.game.party[size_t(campCharacter(app, 0))];
                if (slot >= 0 && slot < 3) pm.equip[size_t(slot)] = item;
                else if (slot >= 3 && slot < 7) pm.genes[size_t(slot - 3)] = item;
                const char* comma = std::strchr(q, ',');
                q = comma ? comma + 1 : q + std::strlen(q);
            }
        }
        const std::vector<int> numbers = cr::mapNumbersFor(mapName);
        if (app.captureArea >= 0 && !numbers.empty()) { // (the map number of that area's, when it has one)
            int pick = numbers[0];
            for (int n : numbers)
                if (cr::areaForMap(n, app.captureArea) == app.captureArea) { pick = n; break; }
            showMap(app, idx, pick, app.captureArea);
        } else showMap(app, idx);
    }
    app.showBackground = true;
    app.hideHud = SDL_getenv("CR_CAPTURE_HUD") == nullptr; // (CR_CAPTURE_HUD=1: as the build shows it)
    if (const char* oz = SDL_getenv("CR_CAPTURE_OZ")) app.game.oz = SDL_atoi(oz); // (the purse the scripts' gold() reads)
    if (app.captureEvent >= 0) { // --event n: as if something in the room had started it
        app.pendingEntry = App::Entry::None;
        app.runner.stop();
        captureVars(app); // (again: the map's own entry may have used them as counters)
        if (const char* at = SDL_getenv("CR_CAPTURE_AT")) { // (an address in the area's file instead: a branch reached by state)
            if (!app.runner.startAt(app.events, uint32_t(std::strtoul(at, nullptr, 0)), app.captureEvent, app.game, app.area, app.mapNumber))
                std::printf("area %d: no code at %s\n", app.area, at);
        } else if (!app.runner.start(app.events, app.captureEvent, app.game, app.area, app.mapNumber))
            std::printf("area %d has no script %d\n", app.area, app.captureEvent);
    }
    const bool traceRunner = SDL_getenv("CR_CAPTURE_TRACE") != nullptr; // the script's state each frame it changes
    std::string lastWhere;
    int lastMsgShown = -1;
    // CR_CAPTURE_EVENTS="frame:script,...": from that frame on, once nothing runs, start the room's
    // script (as the player touching its trigger would) - to play on past the free walks
    std::vector<std::pair<int, int>> touches;
    if (const char* ev = SDL_getenv("CR_CAPTURE_EVENTS"))
        for (const char* p = ev; *p;) {
            int fr = 0, sc = 0, used = 0;
            if (std::sscanf(p, "%d:%d%n", &fr, &sc, &used) != 2) break;
            touches.push_back({fr, sc});
            p += used;
            if (*p == ',') ++p;
        }
    // CR_CAPTURE_UNTIL_MSG="id[:n]": stop the --frames run n frames (60 if not given) after message id
    // comes up, with no taps meanwhile
    // ("id:n:p": p pages turned first, 45 frames apart; "id:n:p:area": only in that area - ids repeat across areas)
    int untilMsg = -1, untilAfter = 60, untilPages = 0, untilArea = -1, untilLeft = -1;
    if (const char* um = SDL_getenv("CR_CAPTURE_UNTIL_MSG"))
        std::sscanf(um, "%d:%d:%d:%d", &untilMsg, &untilAfter, &untilPages, &untilArea);
    for (int f = 0; f < app.captureFrames; ++f) { // --frames: play the scripts, reading each page for 45 frames
        if (untilMsg >= 0 && untilLeft < 0 && app.lastMessageId == untilMsg && !app.dialogue.empty() &&
            (untilArea < 0 || app.area == untilArea)) {
            untilLeft = untilAfter + 45 * untilPages;
            std::printf("frame %d: message %d up\n", f, untilMsg);
        }
        if (untilLeft > untilAfter && (untilLeft - untilAfter) % 45 == 0 && !app.dialogue.empty()) advanceDialogue(app);
        if (untilLeft >= 0 && untilLeft-- == 0) break;
        if (!touches.empty() && f >= touches.front().first && !app.runner.running() && app.dialogue.empty()) {
            const int sc = touches.front().second;
            touches.erase(touches.begin());
            if (app.runner.start(app.events, sc, app.game, app.area, app.mapNumber)) {
                app.runnerWhat = "touched " + std::to_string(sc);
                if (traceRunner) std::printf("frame %d: touched script %d on map %d\n", f, sc, app.mapNumber);
            }
        }
        if (app.battle.active && app.captureWinBattles) { // its talks play; then it's won
            stepBattle(app);
            if (traceRunner && f % 100 == 0)
                std::printf("frame %d: battle phase %d frames %d flag %d\n", f, int(app.battle.phase), app.battle.phaseFrames,
                            int(app.game.flag(battletalk::kFlag)));
            if (traceRunner && app.battle.phaseFrames == 1)
                std::printf("frame %d: battle %d phase %d%s\n", f, app.battle.start.group, int(app.battle.phase),
                            battletalk::on(app) ? " talk" : "");
            if (!battletalk::on(app) && app.battle.phase == App::Battle::Command) {
                for (int k = 2; k < 5; ++k) app.battle.rules.f[size_t(k)].hp = 0;
                endBattle(app, true);
            }
            if (f % 45 == 44 && !app.dialogue.empty()) advanceDialogue(app);
            continue;
        }
        stepEvents(app);
        if (traceRunner) {
            const std::string where = (app.runner.running() ? app.runner.where()
                                                             : "done (" + std::to_string(app.runner.result().instructions) +
                                                                   " instructions" +
                                                                   (app.runner.result().error.empty() ? "" : ", " + app.runner.result().error) +
                                                                   ")") + " map " +
                                      std::to_string(app.mapNumber) + " (" + app.runnerWhat + ")" +
                                      (app.battle.active ? " battle " + std::to_string(app.battle.start.group) + " phase " +
                                                               std::to_string(int(app.battle.phase)) +
                                                               (battletalk::on(app) ? " talk" : "")
                                                         : std::string()) +
                                      " gate " + std::to_string(app.runner.gate());
            if (where != lastWhere) std::printf("frame %d: %s\n", f, where.c_str());
            lastWhere = where;
            if (app.lastMessageId != lastMsgShown) std::printf("frame %d: message %d (area %d)\n", f, app.lastMessageId, app.area);
            lastMsgShown = app.lastMessageId;
        }
        animateNpcs(app, 1.0f / kFramesPerSecond);
        followPlayerScroll(app, 1.0f); // (drawFrame's, every frame the game shows: a capture draws only its last)
        if (f % 45 == 44 && f + 1 < app.captureFrames && !SDL_getenv("CR_CAPTURE_NOTAP") && untilLeft < 0) { // (CR_CAPTURE_NOTAP: no taps)
            if (app.choiceFlag >= 0) answerChoice(app, true);
            else if (app.worldChoiceOpen) answerWorldChoice(app, 0);
            else advanceDialogue(app);
            app.tapPending = true;
            if (app.itemWindowOpen) app.game.setFlag(0, true);
        }
    }
    if (SDL_getenv("CR_CAPTURE_OZ")) // (what a script's purchase left: Ohm's milk - the purse, var 40, var 41, flag 1644)
        std::printf("after the run: %d oz, var 40 = %d, var 41 = %d, flag 1644 %s\n", app.game.oz, app.game.vars[40], app.game.vars[41],
                    app.game.flag(1644) ? "set" : "clear");
    if (const char* sr = SDL_getenv("CR_CAPTURE_STAFF")) { // "mode:frames[:fast]": the staff roll, frames in
        int mode = 0, frames = 100, fast = 0;
        std::sscanf(sr, "%d:%d:%d", &mode, &frames, &fast);
        openStaffRoll(app, mode);
        app.staffRoll.fast = fast != 0;
        for (int f = 0; f < frames && app.staffRoll.open; ++f) stepStaffRoll(app);
        std::printf("staff roll: state %d, picture y %.1f, next line %d\n", app.staffRoll.state, app.staffRoll.picY, app.staffRoll.next);
    }
    if (const char* ab = SDL_getenv("CR_CAPTURE_ACHIEVEMENT")) { // "n:frames[:tap]": achievement n's banner, frames in (a press at frame tap)
        int n = 1, frames = 4, tap = -1;
        std::sscanf(ab, "%d:%d:%d", &n, &frames, &tap);
        app.ach.banner.start(n);
        for (int f = 0; f < frames && app.ach.banner.on; ++f) app.ach.banner.step(f == tap);
        std::printf("achievement banner: %d, state %d, level 0x%x, alpha %d%s\n", n, app.ach.banner.state, app.ach.banner.level,
                    app.ach.banner.alphaByte(), app.ach.banner.on ? "" : " (closed)");
        app.ach.clock = -1e9f; // (held as it is: the capture's own frame doesn't step it)
    }
    if (const char* al = SDL_getenv("CR_CAPTURE_ACHLIST")) { // "scroll[:flags hex]": the title's achievement list, scrolled (units)
        float scroll = 0;
        char hex[40] = {};
        std::sscanf(al, "%f:%32s", &scroll, hex);
        if (hex[0]) app.ach.flags.fromHex(hex);
        openAchievementList(app);
        app.ach.listAnim = 10, app.ach.listDrag = true;
        app.ach.listScroll = std::clamp(scroll, 0.0f, achListMaxScroll(app));
        app.ach.listClock = -1e9f;
        settleMenuMask(app);
        std::printf("achievement list: %zu rows, %d earned, focus %d, icons %dx%d\n", app.ach.listRows.size(), app.ach.flags.earned(),
                    achListFocus(app), app.ach.icons.width, app.ach.icons.height);
    }
    if (const char* it = SDL_getenv("CR_CAPTURE_ITEM")) { // "item[:frames]": Uty_GetItem's band, frames (3) in
        int item = 0, frames = 3;
        std::sscanf(it, "%d:%d", &item, &frames);
        openItemWindow(app, item);
        app.game.setFlag(0, false); // (_Uty_GetItem_Init: ClrFlag(0))
        // (stepEvents' Uty_GetItem_Proc: no script runs here, which would close it) - n > 2: the tap's,
        // a row and a fade step a frame
        for (int f = 0; f < frames; ++f) {
            if (app.itemWindowRow < 2) ++app.itemWindowRow;
            else if (f >= 2) ++app.itemWindowRow, ++app.itemWindowFade;
        }
        std::printf("item window: %s, row %d, fade %d, \"%s\"\n", app.itemWindowOpen ? "open" : "closed", app.itemWindowRow,
                    app.itemWindowFade, app.itemWindowText.c_str());
    }
    if (app.captureTalk >= 0) { // the opening to its end, then the hall's people until a portrait is up
        for (int f = 0; f < 20000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
            stepEvents(app);
            app.dialogue.clear();
            if (app.choiceFlag >= 0) answerChoice(app, true);
            app.tapPending = true;
        }
        int found = -1;
        std::vector<int> ids;
        for (const cr::FieldNpc& n : app.npcs)
            if ((n.kind & 8) && npcActive(app, n) && n.pos[1] < 900.0f) ids.push_back(n.id);
        for (int id : ids) {
            const cr::FieldNpc* n = findNpc(app, id);
            if (!n || !app.havePlayer) continue;
            app.walker.pos = {n->pos[0] + n->box[0] / 2 + app.walker.radius, n->pos[1], n->pos[2]};
            if (!interact(app)) continue;
            for (int f = 0; f < 6000 && app.runner.running(); ++f) {
                stepEvents(app);
                bool up = false;
                for (const App::Portrait& p : app.portraits) up = up || (p.tex.id && p.alpha > 0.99f && p.t >= p.frames && p.aT >= p.aFrames);
                if (up && !app.dialogue.empty() && ++found == app.captureTalk) break;
                if (!app.dialogue.empty()) advanceDialogue(app);
                if (app.choiceFlag >= 0) answerChoice(app, true);
                app.tapPending = true;
            }
            if (found >= app.captureTalk) {
                std::printf("talk-portrait: npc %d\n", id);
                break;
            }
            app.runner.stop();
        }
        if (found < app.captureTalk) std::printf("talk-portrait: only %d moments with a portrait\n", found + 1);
    }
    if (app.captureTitle && SDL_getenv("CR_CAPTURE_TITLE_TAP") && app.titleOpen && app.titleStage == 0) {
        app.verUpShown = true;  // (the plates, not a version-up board)
        titleTap(app);          // --title: the title's tap - its plates, slid in
        app.titleAnim = 30;
        if (SDL_getenv("CR_CAPTURE_TITLE_QUIT")) app.quitAsk = 1, app.quitConfirm = 1; // (Quit's question)
    }
    if (app.vita.isOpen()) std::printf("vita textures used: %d\n", app.vitaTextures);
    if (app.android.isOpen()) std::printf("android textures used: %d\n", app.androidTextures);
    if (app.ipadImages || app.hdImages) std::printf("images by chk_hd_name: %d _ipad, %d _hd\n", app.ipadImages, app.hdImages);
    if (app.video.hdTextures) std::printf("hd textures: %d replaced, %d without one (%s)\n", app.hdLoaded, app.hdMissing, app.hdDir.c_str());
    if (app.captureTitle) {
        std::printf("title: script %s, title menu %s, fade grey %.2f alpha %.2f", app.runner.running() ? app.runner.where().c_str() : "done",
                    app.titleOpen ? "open" : "closed", app.fadeGrey, screenFadeAlpha(app));
        for (int i = 0; i < 16; ++i)
            if (app.portraits[i].tex.id) std::printf(", slot %d image %d (%d x %d) alpha %.2f at (%.0f %.0f)", i, app.portraits[i].image, app.portraits[i].tex.width,
                                                    app.portraits[i].tex.height, app.portraits[i].alpha, app.portraits[i].x, app.portraits[i].y);
        std::printf("\n");
    }
    if (app.captureFrames) {
        const size_t pi = std::min(app.dialoguePage, app.dialogue.size() ? app.dialogue.size() - 1 : 0);
        const cr::MessageInfo mi = pi < app.dialogueInfo.size() ? app.dialogueInfo[pi] : cr::MessageInfo();
        int shown = 0;
        for (const App::Portrait& p : app.portraits) shown += p.tex.id && p.alpha > 0.5f;
        std::printf("after %d frames: area %d, map %d, %s, %zu dialogue pages open (name plate %d, side %d), %d portraits shown, "
                    "fade %.0f%%\n", app.captureFrames, app.area, app.mapNumber,
                    app.runner.running() ? app.runner.where().c_str() : "scripts done", app.dialogue.size(), mi.nameId,
                    mi.side, shown, screenFadeAlpha(app) * 100);
        {
            const cr::GameState& g = app.game;
            std::printf("party: scenario %d, pairs (%d %d) (%d %d), leaders %d %d, solo flags %d %d, levels", g.scenario,
                        g.pairs[0][0], g.pairs[0][1], g.pairs[1][0], g.pairs[1][1], g.leaders[0], g.leaders[1], g.flag(0xfed),
                        g.flag(0xfee));
            for (const cr::PartyMember& m : g.party) std::printf(" %d", m.level);
            std::printf(", %zu + %zu bag entries, %d / %d Oz, %d battles\n", g.items.size(), g.otherBag.size(), g.oz, g.otherOz,
                        app.battlesFought);
            if (pi < app.dialogueVoices.size() && !app.dialogueVoices[pi].empty()) {
                std::printf("voices on this page:");
                for (const App::VoiceCue& v : app.dialogueVoices[pi]) std::printf(" %d@%d", v.clip, v.at);
                std::printf("\n");
            }
        }
        std::printf("field: %s, background %dx%d, %zu stage cameras (%s), scroll (%.0f, %.0f)%s\n",
                    app.mapIndex >= 0 && app.mapIndex < int(app.maps.size()) ? app.maps[size_t(app.mapIndex)].c_str() : "?",
                    app.background.width, app.background.height, app.stageCams.size(),
                    scrollingField(app) ? "a scrolling painting" : paintedField(app) ? "a painting shown whole" : "3D",
                    app.scroll.offX, app.scroll.offY, app.cam.free ? ", camera free" : "");
        if (app.fx.set >= 0) {
            std::printf("scene effects: set %d, on", app.fx.set);
            for (int id = 1; id < 31; ++id)
                if (app.fx.on[size_t(id)]) std::printf(" %d", id);
            std::printf("%s\n", app.environmentEffects ? "" : " (Environment Effects off)");
        }
        if (SDL_getenv("CR_CAPTURE_TRACE")) {
            const cr::StageCamera& sc = app.stageCam;
            std::printf("camera: stage %s fov %.1f view %dx%d; script %s eye (%.1f %.1f %.1f) target (%.1f %.1f %.1f)\n",
                        app.haveStageCam ? "yes" : "no", sc.fovDegrees, sc.viewWidth, sc.viewHeight,
                        app.cam.free && app.cam.set ? "on" : "off", app.cam.eye.x, app.cam.eye.y, app.cam.eye.z,
                        app.cam.target.x, app.cam.target.y, app.cam.target.z);
        }
    }
    if (app.captureSceneFx[0] >= 0) { // --scene-fx set effect [frames]: that weather / backdrop on
        loadSceneEffectSet(app, app.captureSceneFx[0]);
        setSceneEffect(app, true, app.captureSceneFx[1]);
        for (int f = 0; f < app.captureSceneFx[2]; ++f) stepSceneEffects(app);
        std::printf("scene fx: set %d, effect %d on, %d frames, counters %d / %d\n", app.fx.set, app.captureSceneFx[1],
                    app.captureSceneFx[2], app.fx.timerA[size_t(std::clamp(app.captureSceneFx[1], 0, 30))],
                    app.fx.timerB[size_t(std::clamp(app.captureSceneFx[1], 0, 30))]);
    }
    if (app.captureBattle[0] > 0) { // --battle group stage: that battle, a second in
        cr::ensureStartingGenes(app.game);
        if (app.captureBattleView == "genes" || app.captureBattleView.rfind("genehit", 0) == 0 || app.captureBattleView.rfind("reflect", 0) == 0) { // a plate with known genes (genehit@n too)
            app.game.items[481] = 1, app.game.items[108] = 1, app.game.items[94] = 1;
            app.game.party[0].genes[1] = 481;
        }
        if (!SDL_getenv("CR_CAPTURE_TUTORIALS")) // the battles' tutorials seen (else the first one holds the view)
            for (int f : {0xff1, 0xff2, 0xff3, 0xff4, 0xff5, 0xfa8, 0xfa9}) app.game.setFlag(f, true);
        // (CR_BATTLE_STRIKE: 1 the party strikes first, 2 an ambush)
        startBattle(app, {app.captureBattle[0], app.captureBattle[1], -1, SDL_getenv("CR_BATTLE_STRIKE") ? SDL_atoi(SDL_getenv("CR_BATTLE_STRIKE")) : 0}, 0);
        // --battle-view top|commands|targets|items|escape|hit|results: a screen of it; intro@n: the
        // opening n battle frames in (btl_main 200 at kIntroWhite)
        const std::string& v = app.captureBattleView;
        App::Battle& b = app.battle;
        const int introAt = v.rfind("intro@", 0) == 0 ? SDL_atoi(v.c_str() + 6) : -1;
        // top@n: n frames after the first page starts in (0: set at its distance, not drawn yet)
        const int topAt = v.rfind("top@", 0) == 0 ? SDL_atoi(v.c_str() + 4) : -1;
        if (topAt >= 0) {
            for (int f = 0; f < 600 && b.active && b.viewState != 1; ++f) stepBattle(app);
            for (int f = 0; f < topAt && b.active; ++f) stepBattle(app);
            std::printf("battle view %s: slide %d of %d, state %d\n", v.c_str(), b.slide, battleSlideDistance(app), b.viewState);
        }
        for (int f = 0; topAt < 0 && f < (introAt >= 0 ? introAt : 30) && app.battle.active; ++f) stepBattle(app);
        for (int f = 0; introAt < 0 && topAt < 0 && f < 10 && b.active && b.phase != App::Battle::Command; ++f) stepBattle(app);
        if (v == "items") app.game.items[21] = 3, app.game.items[22] = 1, app.game.items[24] = 2;
        if (v == "commands" || v == "targets" || v == "items") battleChooseTop(app, 0);
        if (v == "targets") battleChooseCommand(app, 0);
        if (v == "items") battleChooseCommand(app, 2);
        if (v == "escape") battleChooseTop(app, 2);
        if (v == "options") battleChooseTop(app, 3), app.optionsAnim = 100;
        if (v == "top" || v == "commands" || v == "targets" || v == "items" || v == "escape") // (the page slid in)
            for (int f = 0; f < 10 && b.active; ++f) stepBattle(app);
        if (v.rfind("effect", 0) == 0) { // effect<id>[@frame]: that effect on the first enemy, some frames in
            const int id = SDL_atoi(v.c_str() + 6);
            const size_t at = v.find('@');
            const int frames = at == std::string::npos ? 8 : SDL_atoi(v.c_str() + at + 1);
            setBattleCamera(app, b.formation + 12);
            spawnEffect(app, id, 2);
            for (int f = 0; f < frames; ++f) stepBattle(app);
            std::printf("battle view %s: %zu effects playing\n", v.c_str(), b.effects.size());
        }
        if (v.rfind("status", 0) == 0) { // status<k>[.<k2>...][:<element>]: those statuses (and element) on the pair and the first enemy
            for (size_t at = 6; at < v.size() && v[at] != ':';) {
                const int k = SDL_atoi(v.c_str() + at);
                for (int i : {0, 1, 2})
                    if (k >= 0 && k < cr::kStatusCount) b.rules.f[size_t(i)].status[k] = 3;
                at = v.find_first_of(".:", at);
                if (at == std::string::npos || v[at] == ':') break;
                ++at;
            }
            if (const size_t c = v.find(':'); c != std::string::npos)
                for (int i : {0, 1, 2}) b.rules.f[size_t(i)].element = SDL_atoi(v.c_str() + c + 1);
        }
        if (v.rfind("label", 0) == 0) { // label<type>[@n]: a hit's label (and number) of that type over each member, n frames in
            const int type = SDL_atoi(v.c_str() + 5);
            const size_t at = v.find('@');
            const int frames = at == std::string::npos ? 20 : SDL_atoi(v.c_str() + at + 1);
            setBattleCamera(app, b.formation + 12);
            for (int k = 0; k < 2; ++k)
                if (App::Battle::Actor* a = battleActor(app, k)) {
                    App::Battle::Popup& p = b.popups[k];
                    p = App::Battle::Popup();
                    const bool number = type == cr::kHitNormal || type == cr::kHitCritical || type == cr::kHitWeak || type == cr::kHitResist;
                    p.on = true, p.at = actorHead(app, *a), p.amount = number ? 123 : 0;
                    p.type = type, p.target = k, p.timer = btlui::kPopupTime;
                }
            for (int f = 0; f < frames; ++f) stepBattle(app);
            std::printf("battle view %s: popup timers %d %d\n", v.c_str(), b.popups[0].timer, b.popups[1].timer);
        }
        if (v.rfind("break@", 0) == 0) { // break@n: the Break effect (btlbreak_effect_prc), its n-th frame
            startBreakFx(app);
            for (int f = 0, n = SDL_atoi(v.c_str() + 6); f < n; ++f) stepBreakFx(app);
            std::printf("battle view %s: state %d, slide %d, alpha %d\n", v.c_str(), b.breakFx.state, b.breakFx.slide, b.breakFx.alpha);
        }
        if (v.rfind("stage", 0) == 0) { // stage<action>[@frame]: the first enemy's staged action, that frame of it
            const int action = SDL_atoi(v.c_str() + 5);
            const size_t at = v.find('@');
            const int frame = at == std::string::npos ? -1 : SDL_atoi(v.c_str() + at + 1);
            b.rules.forceAction = action;
            // (CR_BATTLE_PAIR: the pair stands in pair mode - a single blow on it hits both, as the videos'
            // 508 / 510 at Part 14 861 s and Part 15 2260 s; the battle may open on the enemy's turn,
            // so it is set again before each one)
            const bool asPair = SDL_getenv("CR_BATTLE_PAIR") && !battleTopBarred(app, 1);
            for (int f = 0; f < 6000 && b.active; ++f) {
                if (asPair && !b.turnOn && !b.rules.pair) b.rules.pair = true, placePair(app, true);
                if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) { // the pair defends
                    battleChooseTop(app, asPair ? 1 : 0);
                    battleChooseCommand(app, 3);
                    if (b.menu == App::Battle::Commands) battleChooseCommand(app, 3);
                }
                if (b.turnOn && b.turn.action == action && b.turnFrame >= (frame >= 0 ? frame : b.hitFrame + 6)) break;
                if (b.phase == App::Battle::Won || b.phase == App::Battle::Lost) break;
                stepBattle(app);
            }
            std::printf("battle view %s: action %d frame %d (hit %d), camera %d (%d so far), flash %.0f, quake %.1f %.1f, "
                        "%zu effects, hp %d/%d %d/%d, user slot %d\n", v.c_str(), b.turn.action, b.turnFrame, b.hitFrame, b.cam, b.stageCams,
                        b.flash.on ? b.flash.alpha : 0.0f, b.quake.off[0], b.quake.off[1], b.effects.size(), b.rules.f[0].hp,
                        b.rules.f[0].hpMax, b.rules.f[1].hp, b.rules.f[1].hpMax,
                        battleActor(app, b.turn.actors[0]) ? battleActor(app, b.turn.actors[0])->slot : -1);
            if (SDL_getenv("CR_BATTLE_DEBUG"))
                if (const App::Battle::Actor* u = battleActor(app, b.turn.actors[0])) {
                    const LoadedModel& lm = b.models[u->model];
                    std::printf("user %s: %d built-in of %zu actions:", u->model.c_str(), lm.builtin, lm.actions.size());
                    for (const auto& ac : lm.actions) std::printf(" '%s' (%.0f)", ac.name.c_str(), ac.duration);
                    std::printf("\n");
                }
        }
        if (v.rfind("cam", 0) == 0) { // cam<id>[@fighter]
            setBattleCamera(app, SDL_atoi(v.c_str() + 3), v.find('@') != std::string::npos ? SDL_atoi(v.c_str() + v.find('@') + 1) : 0);
            stepBattleCamera(app);
        }
        if (SDL_getenv("CR_BATTLE_DEBUG")) {
            cr::Vec3 eye{}, target{};
            battleCameraView(app, eye, target);
            std::printf("battle camera %d (formation %d): eye %.0f %.0f %.0f target %.0f %.0f %.0f\n", b.cam, b.formation, eye.x, eye.y,
                        eye.z, target.x, target.y, target.z);
        }
        // reflect@n: genehit against a barrier on every enemy, n frames into the gene's turn
        const bool reflect = v.rfind("reflect", 0) == 0;
        const int reflectAt = reflect && v.find('@') != std::string::npos ? SDL_atoi(v.c_str() + v.find('@') + 1) : 60;
        if (reflect)
            for (int i = 2; i < 5; ++i) b.rules.f[size_t(i)].status[cr::kStatusBarrier] = 5;
        const bool geneHit = v == "genehit" || v.rfind("genehit@", 0) == 0; // (genehit@n: n battle frames on instead of the hit)
        if (v == "genes" || geneHit || reflect) battleChooseTop(app, 0), battleChooseCommand(app, 1);
        if (v == "genes")
            for (int f = 0; f < 10 && b.active; ++f) stepBattle(app);
        if (reflect) {
            for (size_t i = 0; i < b.picks.size(); ++i)
                if (b.picks[i] == 94) b.pick = int(i);
            battleChooseGene(app, b.picks[size_t(b.pick)]);
            if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
            if (b.menu == App::Battle::Commands) battleChooseCommand(app, 3);
            for (int f = 0; f < 2000 && b.active; ++f) {
                if (b.turnOn && b.turn.kind == cr::kCmdGene && b.turnFrame >= reflectAt) break;
                stepBattle(app);
            }
            std::printf("battle view reflect: frame %d, reflected %d, back at %d, hits at %d (shown %d), escher hp %d/%d, enemy hp %d/%d\n",
                        b.turnFrame, int(b.turn.reflected), b.bounceAt, b.hitFrame, int(b.hitsShown), b.rules.f[0].hp,
                        b.rules.f[0].hpMax, b.rules.f[2].hp, b.rules.f[2].hpMax);
        }
        if (geneHit) {
            const int geneFrames = v.size() > 8 ? SDL_atoi(v.c_str() + 8) : -1;
            for (size_t i = 0; i < b.picks.size(); ++i)
                if (b.picks[i] == 94) b.pick = int(i);
            if (const char* g = SDL_getenv("CR_BATTLE_GENE")) { // (that gene instead, its MP given)
                b.rules.f[0].mp = b.rules.f[0].mpMax = 999;
                battleChooseGene(app, SDL_atoi(g));
                std::printf("battle view genehit: gene %d '%s'\n", SDL_atoi(g), itemName(app, SDL_atoi(g)).c_str());
            } else
                battleChooseGene(app, b.picks[size_t(b.pick)]);
            if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
            if (b.menu == App::Battle::Commands) battleChooseCommand(app, 3); // Musiea defends
            for (int f = 0; f < (geneFrames >= 0 ? geneFrames : 2000) && b.active; ++f) {
                if (geneFrames < 0 && b.turnOn && b.turn.kind == cr::kCmdGene && b.hitsShown && b.turnFrame >= b.hitFrame + 6) break;
                stepBattle(app);
            }
            std::printf("battle view genehit: escher mp %d, enemy hp %d/%d\n", b.rules.f[0].mp, b.rules.f[2].hp, b.rules.f[2].hpMax);
        }
        const int hitWanted = v.rfind("hit", 0) == 0 ? std::max(1, SDL_atoi(v.c_str() + 3)) : 0;
        int hitsSeen = 0;
        bool wasShown = false;
        // results[@frames][:taps]: the results that many frames in, after that many taps
        // (bossdown@n: n frames into the boss's end; won@n: into the victory; setpiece@n: into a story
        // battle's set piece, every side held)
        const bool setPiece = v.rfind("setpiece", 0) == 0, won = v.rfind("won", 0) == 0;
        const bool results = v.rfind("results", 0) == 0 || v.rfind("bossdown", 0) == 0 || setPiece || won;
        const bool bossDown = v.rfind("bossdown", 0) == 0 || won;
        int resultFrames = 10, resultTaps = 0;
        if (results) {
            if (const size_t at = v.find('@'); at != std::string::npos) resultFrames = SDL_atoi(v.c_str() + at + 1);
            if (const size_t c = v.find(':'); c != std::string::npos) resultTaps = SDL_atoi(v.c_str() + c + 1);
        }
        if (const char* fa = SDL_getenv("CR_BATTLE_FORCE")) b.rules.forceAction = SDL_atoi(fa); // (the first enemy acts so)
        if (hitWanted || results) {
            // pair attacks on the first enemy standing until the first hit lands (or the battle's won)
            const char* useItem = SDL_getenv("CR_BATTLE_ITEM"); // (the first member uses that item on the second, who defends)
            for (int f = 0; f < 6000 && b.active; ++f) {
                if (useItem && b.phase == App::Battle::Command && b.menu == App::Battle::Top) {
                    const int item = SDL_atoi(useItem);
                    app.game.items[item] = std::max(3, app.game.items[item]);
                    battleChooseTop(app, 0);
                    battleChooseCommand(app, 2);
                    battleChooseItem(app, item);
                    if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks.size() > 1 ? b.picks[1] : b.picks[0]);
                    if (b.menu == App::Battle::Commands) battleChooseCommand(app, 3);
                    useItem = nullptr;
                } else if (b.phase == App::Battle::Command && b.menu == App::Battle::Top) {
                    battleChooseTop(app, b.rules.f[0].alive() && b.rules.f[1].alive() ? 1 : 0);
                    battleChooseCommand(app, 0);
                    if (b.menu == App::Battle::Targets) battleChooseTarget(app, b.picks[0]);
                    if (b.menu == App::Battle::Commands) { battleChooseCommand(app, 0); battleChooseTarget(app, b.picks[0]); }
                }
                if (bossDown && (b.phase == App::Battle::Command || b.phase == App::Battle::Acting)) // (the test: the pair holds, the monsters fall to a blow)
                    for (int k = 0; k < 5; ++k)
                        if (b.rules.f[size_t(k)].alive()) {
                            b.rules.f[size_t(k)].hp = k < 2 ? b.rules.f[size_t(k)].hpMax : 1;
                            if (k < 2) b.rules.f[size_t(k)].status[cr::kStatusInvincible] = 9;
                        }
                if (setPiece && (b.phase == App::Battle::Command || b.phase == App::Battle::Acting))
                    for (int k = 0; k < 5; ++k) // (the boss's HP as the test sets it: --state, else held at most)
                        if (b.rules.f[size_t(k)].alive() && (k < 2 || !SDL_getenv("CR_SETPIECE_HP"))) {
                            b.rules.f[size_t(k)].hp = b.rules.f[size_t(k)].hpMax;
                            if (k < 2) b.rules.f[size_t(k)].status[cr::kStatusInvincible] = 9; // (no blow fells them)
                        }
                if (setPiece && SDL_getenv("CR_SETPIECE_HP") && b.rules.f[2].alive()) // (a percentage for fighter 2)
                    b.rules.f[2].hp = std::max(1, b.rules.f[2].hpMax * SDL_atoi(SDL_getenv("CR_SETPIECE_HP")) / 100);
                if (results) {
                    if (setPiece && b.phase == App::Battle::SetPiece && b.phaseFrames >= resultFrames) break;
                    if (bossDown && !won && b.phase == App::Battle::BossDown && b.phaseFrames >= resultFrames) break;
                    if (bossDown && !won && b.phase == App::Battle::Won) break;
                    if (won && b.phase == App::Battle::Won && b.phaseFrames >= resultFrames) break;
                    if (b.phase == App::Battle::Results && b.result.frames >= resultFrames) break;
                    if (b.phase == App::Battle::Lost) break;
                } else {
                    if (b.turnOn && b.hitsShown && !wasShown) ++hitsSeen;
                    wasShown = b.turnOn && b.hitsShown;
                    if (hitsSeen >= hitWanted && b.hitsShown && b.turnFrame >= b.hitFrame + 6) break;
                    if (b.phase == App::Battle::Won || b.phase == App::Battle::Lost) break;
                }
                stepBattle(app);
            }
            for (int t = 0; t < resultTaps && b.phase == App::Battle::Results; ++t) {
                resultsTap(app);
                for (int f = 0; f < 6; ++f) stepBattle(app);
            }
            if (results)
                std::printf("results: stage %d, line %d, exp shown %.0f of %d, oz %.0f of %d, %zu level-ups, %zu items\n",
                            b.result.stage, b.result.message, b.result.shownExp, b.result.expTotal, b.result.shownOz,
                            b.result.ozTotal, b.result.ups.size(), b.result.items.size());
            if (results)
                for (const auto& [item, n] : b.result.items) std::printf("results: item %d '%s' x%d\n", item, itemName(app, item).c_str(), n);
            std::printf("battle view %s: phase %d (frame %d, boss model %d, camera %d, set piece %#x stage %d), round %d, break %d, hp",
                        v.c_str(), int(b.phase), b.phaseFrames, b.bossModel, b.cam, b.setPiece, b.start.stage, b.rules.round, b.rules.breakGauge);
            for (int i = 0; i < 5; ++i) std::printf(" %d/%d", b.rules.f[size_t(i)].hp, b.rules.f[size_t(i)].hpMax);
            std::printf(", exp %d oz %d\n", b.rules.expGained, b.rules.ozGained);
        }
        if (SDL_getenv("CR_BATTLE_DEBUG"))
            for (const App::Battle::Actor& a : app.battle.actors) {
                LoadedModel& lm = app.battle.models[a.model];
                const cr::Action* act = a.action >= 0 && a.action < int(lm.actions.size()) ? &lm.actions[size_t(a.action)] : nullptr;
                cr::computeWorld(lm.cpu.skeleton, act, a.time, lm.world);
                cr::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
                for (const auto& mesh : lm.cpu.meshes) {
                    cr::skinMesh(lm.cpu, mesh, lm.world, lm.skinned);
                    for (size_t v = 0; v + 2 < lm.skinned.size(); v += 3) {
                        lo = {std::min(lo.x, lm.skinned[v]), std::min(lo.y, lm.skinned[v + 1]), std::min(lo.z, lm.skinned[v + 2])};
                        hi = {std::max(hi.x, lm.skinned[v]), std::max(hi.y, lm.skinned[v + 1]), std::max(hi.z, lm.skinned[v + 2])};
                    }
                }
                std::printf("actor %s (fighter %d, alpha %.2f, hidden %d) at (%.0f %.0f) action %d '%s' t=%.0f: posed bounds (%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n",
                            a.model.c_str(), a.fighter, a.alpha, int(a.hidden), a.pos.x, a.pos.z, a.action, act ? act->name.c_str() : "-", a.time, lo.x, lo.y,
                            lo.z, hi.x, hi.y, hi.z);
            }
    }
    if (app.captureMessage > 0) showMessages(app, {app.captureMessage}); // --message <id>
    if (const char* st = SDL_getenv("CR_CAPTURE_STYLE")) app.msgStyle = SDL_atoi(st), app.msgLines = 2; // Open's style
    if (const char* pg = SDL_getenv("CR_CAPTURE_PAGE"))
        app.dialoguePage = size_t(std::clamp(SDL_atoi(pg), 0, std::max(0, int(app.dialogue.size()) - 1)));
    if (SDL_getenv("CR_CAPTURE_LINES") && !app.dialogue.empty()) // the page's lines' first bytes (tests)
        for (const std::string& l : app.dialogue[std::min(app.dialoguePage, app.dialogue.size() - 1)])
            std::printf("line: %02x %02x %02x len %zu\n", l.size() > 0 ? (unsigned char)l[0] : 0,
                        l.size() > 1 ? (unsigned char)l[1] : 0, l.size() > 2 ? (unsigned char)l[2] : 0, l.size());
    if (const char* ty = SDL_getenv("CR_CAPTURE_TYPED")) { // the page n frames into its typing
        resetTyping(app);
        if (const cr::MessageInfo* info = app.dialogue.empty() ? nullptr : pageInfo(app)) // (a page keeping the lines before it)
            app.dialogueTyped = info->keep, app.dialogueInstant = info->instant;
        for (int f = SDL_atoi(ty); f > 0 && !app.dialogue.empty(); --f) stepTyping(app);
        std::printf("typed: %d of %d characters\n", app.dialogueTyped,
                    app.dialogue.empty() ? 0 : pageChars(app.dialogue[std::min(app.dialoguePage, app.dialogue.size() - 1)]));
    }
    if (const char* ch = SDL_getenv("CR_CAPTURE_CHOICE")) { // the event Yes / No over it (Util 3), settled
        app.msgStyle = 64, app.msgLines = 1;
        app.choiceFlag = 0, app.choiceCursor = 0, app.choiceAnim = 4;
        if (SDL_strcmp(ch, "touch") == 0) app.pointerLast = true; // (touch play's look)
    }
    if (!app.captureShop.empty()) { // the Ark's shop (Omega: j001_t01, area 9), with some Oz and things to sell
        if (app.area != 9 && app.mapNumber >= 169) {
            const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[app.mapNumber]));
            if (it != app.maps.end()) showMap(app, int(it - app.maps.begin()), app.mapNumber, 9);
        }
        app.game.oz = 45070;
        app.game.items[21] = 3;
        app.game.items[1] = 1;
        app.game.items[202] = 1;
        // main category member buy items[@n] sell count equip talk omu (items@n: the cursor on row n,
        // scrolled to it; omu: scenario 2's fifth plate, its OMU items, 521 owned - ShopItemPlate's 0x66)
        const std::string p = app.captureShop.substr(0, app.captureShop.find('@'));
        if (p == "omu") app.game.scenario = 1, app.game.items[521] = 1;
        openShop(app);
        App::Shop& s = app.shop;
        if (p == "talk") { // (talk@n: n frames on, 40 by default; the area's EvrStart 4 - --area 7 / 13 / 19 / 25)
            shopChooseMain(app, 0);
            const size_t at = app.captureShop.find('@');
            const int frames = at == std::string::npos ? 40 : SDL_atoi(app.captureShop.c_str() + at + 1);
            for (int f = 0; f < frames; ++f) {
                stepShop(app);
                stepEvents(app);
                if (SDL_getenv("CR_CAPTURE_TRACE"))
                    std::printf("shop talk frame %d: phase %d running %d runner %d pages %zu\n", f, int(s.phase), int(s.talkRunning),
                                int(app.runner.running()), app.dialogue.size());
            }
            std::printf("shop talk: entry %u, phase %d, running %d, %zu pages up\n", unsigned(app.events.returnEntry(4)), int(s.phase),
                        int(s.talkRunning), app.dialogue.size());
        } else if (p != "main") {
            shopChooseMain(app, p == "sell" ? 2 : 1);
            if (p != "category") shopChooseCategory(app, p == "sell" || p == "items" ? 0 : p == "omu" ? 4 : 1);
            if (s.phase == App::Shop::Member && p != "member") shopChooseMember(app, s.member);
            if (p == "items" && app.captureShop.find('@') != std::string::npos) {
                s.listCursor = std::clamp(SDL_atoi(app.captureShop.c_str() + app.captureShop.find('@') + 1), 0,
                                          std::max(0, int(s.list.size()) - 1));
                shopFollowCursor(s);
                s.scroll = shopui::kRowH * float(s.listCursor); // (settled on the line)
            }
            if (p == "count" || p == "equip") {
                shopChooseItem(app, 1);
                if (p == "count") s.count = 2;
                else { s.count = 1; shopPay(app); }
            } else if (p == "buy") {
                s.listCursor = 1; // Bastard Sword: Attack up
            }
        }
        s.anim = 100;
        std::printf("shop: level %d, %zu in the list, phase %d, %d oz\n", s.level, s.list.size(), int(s.phase), app.game.oz);
    }
    if (app.puzzle.stage >= 0) {
        int counts[11] = {};
        for (const App::Puzzle::Obj& o : app.puzzle.objs) ++counts[std::clamp(o.type, 0, 10)];
        std::printf("puzzle: stage %d (map %d), ability %d, ESP %d, %zu objects: %d blocks, %d fixed, %d gems, %d switches, %d crystals\n",
                    app.puzzle.stage, app.mapNumber, cr::kPuzzleStage[app.puzzle.stage].ability, app.puzzle.esp,
                    app.puzzle.objs.size(), counts[1], counts[2], counts[9], counts[8], counts[10]);
        if (SDL_getenv("CR_PUZZLE_POINTS")) { // (tests: where taps find each object - its top's point, frame pixels)
            float ax, ay, bx, by;
            app.renderer.toLogical(0, 0, ax, ay), app.renderer.toLogical(1000, 1000, bx, by);
            for (const App::Puzzle::Obj& o : app.puzzle.objs) {
                if (!o.shown || o.npc < 0 || size_t(o.npc) >= app.npcs.size()) continue;
                const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
                float lx, ly;
                if (fieldToLogical(app, {n.pos[0], n.pos[1] + n.box[1], n.pos[2]}, lx, ly))
                    std::printf("puzzle point: type %d at %.0f %.0f\n", o.type, (lx - ax) * 1000 / (bx - ax), (ly - ay) * 1000 / (by - ay));
            }
        }
    }
    if (!app.captureTouchPad.empty()) { // --touch-pad dx,dy[,chest]: the pad held from the screen's middle (tex2d)
        float dx = 0, dy = 0;
        SDL_sscanf(app.captureTouchPad.c_str(), "%f,%f", &dx, &dy);
        app.fieldMenu.fade = 320;
        if (app.captureTouchPad.find("chest") != std::string::npos) // (beside the first chest: the "!" button)
            for (const cr::FieldNpc& n : app.npcs)
                if ((n.treasure & 3) && npcActive(app, n)) {
                    app.walker.pos = {n.pos[0] + n.box[0] / 2 + app.walker.radius, n.pos[1], n.pos[2]};
                    break;
                }
        App::TouchPad& t = app.touchPad;
        t = App::TouchPad();
        t.down = t.shown = true, t.ox = 240, t.oy = 200, t.dx = dx, t.dy = dy;
        for (int f = 0; f < 12; ++f) stepTouchPad(app);
        float sx = 0, sy = 0;
        bool run = false;
        const bool moving = touchPadStick(app, sx, sy, run);
        std::printf("touch pad: %s, offset (%g %g), knob (%.1f %.1f), %s pad art, \"!\" button %s\n",
                    moving ? (run ? "running" : "walking") : "standing", dx, dy, t.knob[0], t.knob[1],
                    app.bigPad ? "the larger" : "the small", easyButtonShown(app) ? "shown" : "hidden");
    }
    if (!app.captureFieldMenu.empty()) { // --field-menu closed|open|known|off: the corner button, the bar
        const std::string& v = app.captureFieldMenu;
        if (v != "closed") app.fieldMenu.open = true;
        if (v == "known" || v == "off") app.game.items[170] = 1;
        if (v == "off") app.game.noEncounters[size_t(fieldRoute(app))] = true;
        app.fieldMenu.fade = 320;
        app.fieldMenu.cursor = int(fieldBarEntries(app).size()) - 1; // (as the bar opens: the Vita's cursor on Camp)
        if (v.rfind("map", 0) == 0) { // map[step][:page]: the map screen, the story at that step
            if (v.size() > 3) app.game.setFlag(mapui::kFlagBase + mapui::kFlagStride * fieldRoute(app) + SDL_atoi(v.c_str() + 3), true);
            openMapScreen(app);
            app.mapScreen.fade = 4, app.mapScreen.alpha = 255;
            if (const size_t c = v.find(':'); c != std::string::npos) app.mapScreen.page = SDL_atoi(v.c_str() + c + 1); // map[step]:page (D07)
            const App::MapScreen& ms = app.mapScreen;
            std::printf("map screen: map %d, field %d, texture %dx%d, spot (%d %d), marks (%d %d) (%d %d)\n", app.mapNumber, ms.field,
                        ms.tex.width, ms.tex.height, ms.spot[0], ms.spot[1], ms.marks[0][0], ms.marks[0][1], ms.marks[1][0],
                        ms.marks[1][1]);
        }
        std::printf("field menu: %s, %s, route %d encounters %s\n", fieldMenuWanted(app) ? "shown" : "hidden",
                    app.fieldMenu.open ? "open" : "closed", fieldRoute(app), fieldEncountersOff(app) ? "off" : "on");
    }
    if (app.captureMenu == 1) {
        openCamp(app);
        app.camp.anim = 100;
        // --camp-view items|use|result|equip|slots|wear|status|status-equip|status-skills|genes|plate|entries|row|row2: a page, with a bag to show (row / row2: the pad on Scenarios / Collection)
        const std::string& v = app.captureCampView;
        if (!v.empty()) {
            app.game.setFlag(0x38, true), app.game.setFlag(0x39, true); // (the once-only messages seen)
            app.camp.notice = 0;
            app.dialogue.clear();
            app.game.items[3] = 4, app.game.items[24] = 2, app.game.items[21] = 3, app.game.items[27] = 1;
            app.game.items[202] = 1, app.game.items[216] = 1, app.game.items[427] = 1;
            app.game.party[size_t(campCharacter(app, 0))].mp = 10;
            auto key = [&](SDL_Keycode k) { campKey(app, k); };
            if (v == "row" || v == "row2") key(SDLK_RIGHT);           // (the Vita's pad: the buttons below the boards)
            if (v == "row2") app.game.scenario = 1, key(SDLK_RIGHT);
            if (v == "items" || v == "use") app.camp.cursor = 0, key(SDLK_RETURN);
            if (v == "use") key(SDLK_RIGHT), key(SDLK_LEFT), key(SDLK_RETURN), key(SDLK_RETURN), key(SDLK_RIGHT), key(SDLK_RIGHT);
            if (v == "equip" || v == "slots" || v == "wear") app.camp.cursor = 1, key(SDLK_RETURN);
            if (v == "slots" || v == "wear") key(SDLK_RETURN);
            if (v == "wear") key(SDLK_RETURN);
            if (v == "result") app.camp.cursor = 0, key(SDLK_RETURN), key(SDLK_RETURN), key(SDLK_RETURN), key(SDLK_RIGHT), key(SDLK_RETURN);
            if (v == "status" || v == "status-equip" || v == "status-skills") app.camp.cursor = 3, key(SDLK_RETURN), key(SDLK_RETURN);
            if (v == "status-equip") key(SDLK_RIGHT);
            if (v == "status-skills") key(SDLK_LEFT);
            if (v == "genes" || v == "plate" || v == "entries") app.game.items[481] = 1, app.game.items[108] = 1, app.game.items[94] = 1;
            if (v == "genes") app.camp.cursor = 2, key(SDLK_RETURN), key(SDLK_DOWN), key(SDLK_DOWN);
            if (v == "plate" || v == "entries") {
                app.camp.cursor = 1, key(SDLK_RETURN), key(SDLK_RETURN);
                for (int k = 0; k < 4; ++k) key(SDLK_DOWN); // Gene Plate 1
                key(SDLK_RETURN);
            }
            if (v == "entries") key(SDLK_RETURN); // (the first plate's known entries, Back / OK under them)
            if (const char* d = SDL_getenv("CR_CAPTURE_CAMP_DOWN")) // (tests: the list's focus that many rows down)
                for (int k = SDL_atoi(d); k > 0; --k) { key(SDLK_DOWN); for (int f = 0; f < 8; ++f) stepCamp(app); }
            app.camp.anim = 100;
            campTrackPhase(app.camp);
            app.camp.phaseT = 100;
            // (tests: CR_CAPTURE_CAMP_STEP=<keys>@n - on the view settled, keys pressed - R Return, B Back, L / G
            // Left / Right, U / D Up / Down - then n frames: a state change's windows on the way, "R@3")
            if (const char* st = SDL_getenv("CR_CAPTURE_CAMP_STEP")) {
                const std::string steps = st;
                const size_t at = steps.find('@');
                for (char k : steps.substr(0, at))
                    key(k == 'R' ? SDLK_RETURN : k == 'B' ? SDLK_ESCAPE : k == 'L' ? SDLK_LEFT : k == 'G' ? SDLK_RIGHT : k == 'U' ? SDLK_UP : SDLK_DOWN);
                campTrackPhase(app.camp);
                for (int f = at == std::string::npos ? 0 : SDL_atoi(steps.c_str() + at + 1); f > 0; --f) stepCamp(app);
                std::printf("camp: phase %d (from %d) t %d, swap %d\n", int(app.camp.phase), int(app.camp.prevPhase), app.camp.phaseT, app.camp.swapT);
            }
            // open@n / genes-open@n / equip-open@n: the main page's Items (Genes, Equip) chosen, n frames on; back@n
            // (genes-back@n, equip-back@n): Back from it; ask@n: the Emergency Exit chosen in the list; exit@n:
            // its Yes
            if (const size_t at = v.find('@'); at != std::string::npos) {
                const std::string what = v.substr(0, at);
                const int n = SDL_atoi(v.c_str() + at + 1);
                App::Camp& c = app.camp;
                const int page = what.rfind("genes", 0) == 0 ? 2 : what.rfind("equip", 0) == 0 ? 1 : 0;
                if (page == 2) app.game.items[481] = 1;
                app.game.items[1] = 3;
                c.cursor = page, key(SDLK_RETURN);
                if (what.find("open") == std::string::npos) {
                    c.anim = 100, campTrackPhase(c), c.phaseT = 100;
                    if (what.find("back") != std::string::npos) key(SDLK_ESCAPE);
                    else {
                        c.category = 0, campRefreshList(app);
                        c.listCursor = 0, c.listTop = 0, c.scroll = 0;
                        key(SDLK_RETURN); // (the Emergency Exit: its Yes / No)
                        if (what == "exit") campTrackPhase(c), c.phaseT = 100, c.confirm = 0, key(SDLK_RETURN);
                    }
                }
                campTrackPhase(c);
                for (int f = 0; f < n; ++f) stepCamp(app);
                std::printf("camp: page %d phase %d anim %d pageOut %d phaseT %d exitT %d ease %.2f\n", int(c.page), int(c.phase), c.anim,
                            c.pageOut, c.phaseT, c.exitT, campEase(app));
            }
        }
    }
    if (app.captureMenu == 7) { // --save-screen clear: the title's, pair 1 with every story cleared (a save of its own)
        app.saveDirOverride = userDir() + "capture-clear/";
        SDL_CreateDirectory(app.saveDirOverride.c_str());
        cr::SaveData sd = captureSave(app);
        for (int f : {31, 32, 34, 36, 38, 39, 40}) sd.game.setFlag(f, true);
        sd.leader = 0;
        sd.playSeconds = 3600 * 23 + 60 * 7;
        cr::writeSave(dataPath(app), sd);
        app.chosenPair = 1;
        app.captureMenu = 3;
    }
    if (app.captureMenu == 2 || app.captureMenu == 3) { openSaveScreen(app, app.captureMenu == 3); app.saveScreen.anim = 100; }
    if (app.captureMenu == 4) { // --char-select [--at frames 0]: the second page pulled, that many frames on
        openCharSelect(app);
        app.charSelectAnim = 100;
        if (app.captureAt[0] == app.captureAt[0] && app.captureAt[0] >= 0) {
            charSelectTurn(app);
            for (int f = 0; f < int(app.captureAt[0]); ++f) stepCharSelect(app);
        }
    }
    if (app.captureMenu == 8) { // (stepped in: a capture draws one frame) [--at gate [1: chosen, 2 + n: band n chosen]]
        openWorldSelect(app);
        for (int f = 0; f < 40 && app.worldChoiceOpen; ++f) stepWorldSelect(app);
        if (app.captureAt[0] == app.captureAt[0]) {
            app.worldSelect.cursor = int(app.captureAt[0]);
            for (int f = 0; f < 40 && app.worldChoiceOpen; ++f) stepWorldSelect(app);
            if (app.captureAt[1] >= 1) worldSelectChoose(app, int(app.captureAt[0]));
            for (int f = 0; f < 40 && app.worldChoiceOpen; ++f) stepWorldSelect(app);
            if (app.captureAt[1] >= 2) { // (2 + n: band n chosen too - the question)
                app.worldSelect.levelCursor = int(app.captureAt[1]) - 2;
                for (int f = 0; f < 40 && app.worldChoiceOpen; ++f) stepWorldSelect(app);
                worldSelectLevel(app, int(app.captureAt[1]) - 2);
                for (int f = 0; f < 40 && app.worldChoiceOpen; ++f) stepWorldSelect(app);
            }
        }
    }
    if (app.captureMenu == 9) {
        openLastBattle(app);
        for (int f = 0; f < 40 && app.lastBattle.open; ++f) stepLastBattle(app);
    }
    if (app.captureMenu == 12) { // --door-dump area: each room's NPC / trigger scripts dry-run, where they warp (with the flags given)
        const int area = app.captureArea >= 0 ? app.captureArea : 0;
        const cr::GameState start = app.game;
        for (int map = 0; map < cr::kNumMaps; ++map) {
            if (cr::areaForMap(map, area) != area) continue;
            app.runner.stop();
            app.game = start;
            app.mapNumber = -1;
            cr::WarpRequest w;
            w.valid = true, w.area = area, w.map = map;
            performWarp(app, w, -1);
            for (int f = 0; f < 2000 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
                stepEvents(app);
                app.dialogue.clear();
                if (app.choiceFlag >= 0) answerChoice(app, true);
                app.tapPending = true;
                if (app.battle.active) endBattle(app, true);
            }
            if (app.mapNumber != map) { std::printf("map %d: left at once for %d\n", map, app.mapNumber); continue; }
            std::printf("map %d:", map);
            for (const cr::FieldNpc& n : app.npcs) {
                cr::GameState trial = app.game;
                const int script = n.treasure & 3 ? n.id + 1 : n.id;
                const cr::EventResult r = cr::runEvent(app.events, script, trial, app.area, app.mapNumber);
                if (r.warp.valid)
                    std::printf(" %d%s%s->%d/%d", n.id, npcActive(app, n) ? "" : "(off)", n.touchTrigger() ? "x" : (n.kind & 8) ? "t" : "",
                                r.warp.area, r.warp.map);
            }
            std::printf("\n");
        }
    }
    if (app.captureMenu == 11) { // --puzzle-dump: each stage's ability, ESP and objects (types by row / column)
        static const char* const kName[11] = {"-", "block", "fixed", "fragile", "candle", "red", "blue", "cswitch", "wswitch",
                                              "gem", "crystal"};
        for (int st = 0; st < cr::kPuzzleStages; ++st) {
            app.runner.stop();
            app.game = cr::GameState();
            cr::WarpRequest w;
            w.valid = true, w.area = cr::kPuzzleArea, w.map = cr::kFirstPuzzleMap + st;
            performWarp(app, w, -1);
            for (int f = 0; f < 400 && (app.runner.running() || app.pendingEntry != App::Entry::None); ++f) {
                stepEvents(app);
                app.dialogue.clear();
                if (app.choiceFlag >= 0) answerChoice(app, true);
                app.tapPending = true;
            }
            const cr::PuzzleStage& ps = cr::kPuzzleStage[st];
            std::printf("puzzle stage %d (map %d): ability %d, esp %d, %dx%d:", st, cr::kFirstPuzzleMap + st, ps.ability, ps.esp,
                        ps.rows, ps.cols);
            for (const App::Puzzle::Obj& o : app.puzzle.objs)
                std::printf(" %s@%d,%d%s", kName[std::clamp(o.type, 0, 10)], o.row, o.col, o.shown ? "" : "(hidden)");
            std::printf("\n");
        }
    }
    if (app.captureMenu == 10) { // --collection
        openCollection(app);
        for (int f = 0; f < 30; ++f) stepCollection(app);
    }
    if (app.captureMenu == 5) { // --scenarios [--at card [mode]]
        openScenarios(app, app.captureAt[1] == app.captureAt[1] ? int(app.captureAt[1]) : 0);
        if (app.captureAt[0] == app.captureAt[0] && app.captureAt[0] >= 0) {
            bingo::loadCard(app, std::clamp(int(app.captureAt[0]), 0, 4));
            bingo::cardWindowsOn(app, 0);
            for (int f = 0; f < 6; ++f) stepScenarios(app);
        }
        app.scc.anim = 100;
    }
    if (app.captureMenu == 6) {
        openManual(app, 0);
        app.manualAnim = 100;
        if (app.captureAt[0] == app.captureAt[0]) app.manualCursor = int(app.captureAt[0]);
        if (manualTopics(app).empty()) // (none held yet: every manual item, the topic the item's number - 419)
            for (int item = kManualFirstItem; item <= kManualLastItem; ++item) app.game.items[item] = 1;
        if (app.captureAt[1] == app.captureAt[1]) { // --at topic page: that topic's page open (its picture 30 frames on)
            app.typeOn = false;
            manualChoose(app);
            for (int f = 0; f < 12; ++f) stepManual(app);
            for (int k = 0; k < int(app.captureAt[1]); ++k) manualTap(app);
            for (int f = 0; f < 30; ++f) stepManual(app);
        }
        std::printf("manual: %zu topics\n", manualTopics(app).size());
    }
    if (app.captureOptions) { // --volume-menu (page 1) / --video-menu (page 4)
        loadMix(app);
        const int page = app.optionsPage;
        // (the page itself: "About Option" - flag 0x43's once-only message, which holds it back - seen;
        // CR_CAPTURE_OPTIONS_ABOUT=1 keeps it)
        if (!SDL_getenv("CR_CAPTURE_OPTIONS_ABOUT")) app.game.setFlag(0x43, true);
        openOptions(app, page);
        app.optionsAnim = 100;
        if (const char* ra = SDL_getenv("CR_CAPTURE_RESET")) app.resetAsk = SDL_atoi(ra); // (Reset Data's question 1 / 2)
        if (const char* row = SDL_getenv("CR_CAPTURE_OPTIONS_ROW")) { // (that row in focus, the page scrolled to it)
            app.optionsRow = SDL_atoi(row);
            optionsScrollFix(app, int(optionRows(app, app.optionsPage).size()), true);
        }
        // Exercise the actual native Options handlers in automated captures.
        if (const char* keys = SDL_getenv("CR_CAPTURE_OPTIONS_KEYS")) {
            std::istringstream sequence(keys);
            for (std::string key; sequence >> key;) {
                if (key == "enter") optionsKey(app, SDLK_RETURN);
                else if (key == "left") optionsKey(app, SDLK_LEFT);
                else if (key == "right") optionsKey(app, SDLK_RIGHT);
                else if (key == "back") optionsKey(app, SDLK_ESCAPE);
            }
            std::printf("import UI: state %d, succeeded %d, options %d, title %d\n", int(app.saveImport.state),
                        int(app.saveImport.succeeded), int(app.optionsOpen), int(app.titleOpen));
        }
    }
    if (app.havePlayer && app.captureAt[0] == app.captureAt[0]) { // --at x z: put the player there
        float y;
        if (cr::floorHeight(app.collisionData, app.captureAt[0], 1000.0f, app.captureAt[1], 0.0f, y)) {
            app.walker.pos = {app.captureAt[0], y, app.captureAt[1]};
            app.between.valid = false;         // (shown where it is now)
            app.scroll.snap = true;            // a painted field's view on it at once
            followPlayerScroll(app, 1.0f);
        } else std::printf("--at %.1f %.1f: no floor there\n", app.captureAt[0], app.captureAt[1]);
    }
    if (const char* pr = SDL_getenv("CR_CAPTURE_PRESS")) { // (tests: a touch held there - fractions of the frame - as drawn)
        float fx = 0, fy = 0;
        if (std::sscanf(pr, "%f,%f", &fx, &fy) == 2) {
            app.pointerLast = true;
            app.press = App::Press{true, pressScreen(app), fx * float(app.renderer.frameWidth()), fy * float(app.renderer.frameHeight())};
            if (app.campOpen) { // (the Camp's own pointer)
                app.campPointer = App::CampPointer();
                app.campPointer.down = true, app.campPointer.page = app.camp.page, app.campPointer.phase = app.camp.phase;
                app.campPointer.curPx = app.press.px, app.campPointer.curPy = app.press.py;
            }
        }
    }
    settleMenuMask(app);
    // The capture shows exactly the frames stepped above; --settle draws that moment first (an
    // upscaler's history converges on it)
    const Uint64 frame = Uint64(SDL_NS_PER_SECOND / (app.captureSettleFps > 0 ? float(app.captureSettleFps) : kFramesPerSecond));
    const char* every = SDL_getenv("CR_CAPTURE_EVERY"); // (debugging: each --settle-moving frame as <dir>/sNNNN.bmp)
    const int everyN = std::max(1, SDL_getenv("CR_CAPTURE_EVERY_N") ? SDL_atoi(SDL_getenv("CR_CAPTURE_EVERY_N")) : 1); // (every nth only)
    for (int f = 0; f < app.captureSettle; ++f) {
        app.lastTicks = app.captureSettleMoving ? SDL_GetTicksNS() - frame : 0;
        drawFrame(app, false);
        int fw = 0, fh = 0;
        std::vector<uint8_t> fp;
        if (every && f % everyN == 0 && app.renderer.readPixels(fp, fw, fh)) {
            char name[32];
            SDL_snprintf(name, sizeof name, "/s%04d.bmp", f);
            saveFrameBmp(fp, fw, fh, (std::string(every) + name).c_str());
        }
    }
    app.lastTicks = app.captureSettleMoving && app.captureSettle ? SDL_GetTicksNS() - frame : 0;
    drawFrame(app, false);
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    if (!app.renderer.readPixels(px, w, h)) {
        std::printf("FAILED to read the frame back\n");
        return 1;
    }
    const bool ok = saveFrameBmp(px, w, h, outPath);
    std::printf("%s %s -> %s (%dx%d)\n", ok ? "captured" : "FAILED", mapName, outPath, w, h);
    if (app.hdModelsLoaded) std::printf("hd models: %d replaced (%s)\n", app.hdModelsLoaded, app.modelsDir.c_str());
    if (app.battle.active) std::printf("battle: %d steps (speed %d)\n", app.battle.frames, app.battleSpeed);
    if (app.havePlayer && !app.battle.active) std::printf("player: at %.1f %.1f (field speed %d)\n", app.walker.pos.x, app.walker.pos.z, app.fieldSpeed);
    if (app.haveStageCam) {
        const cr::StageCamera& sc = app.stageCam;
        const cr::Mat4 vp = cr::Mat4::perspective(sc.fovDegrees * 3.14159265f / 180.0f, 1.0f, 10.0f, 4000.0f) *
                            cr::Mat4::lookAt({sc.eye[0], sc.eye[1], sc.eye[2]}, {sc.target[0], sc.target[1], sc.target[2]},
                                             {0, 1, 0});
        for (const auto& d : app.npcDraws) {
            const float o[3] = {0, 0, 0};
            const cr::Vec3 p = d.world.transformPoint(o);
            const float* m = vp.m;
            const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
            const float sx = (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / cw * 512 + 512;
            const float sy = 512 - (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / cw * 512;
            const LoadedModel& lm = app.npcModels[d.model];
            const std::string act = d.action >= 0 && d.action < int(lm.actions.size())
                                        ? lm.actions[d.action].name + (d.action < lm.builtin ? " (own)" : "") : "-";
            std::printf("npc %-16s at (%.1f %.1f %.1f) view (%.0f, %.0f) action %s t %.0f (id %d)\n", d.model.c_str(), p.x, p.y,
                        p.z, sx, sy, act.c_str(), d.time, d.id);
        }
    }
    if (SDL_getenv("CR_CAPTURE_NPCS")) // (debugging) every NPC of the room: id, kind, +0x12, active, where
        for (const cr::FieldNpc& n : app.npcs)
            std::printf("npc id %d kind %#x treasure %#x active %d (show %d hide %d) at (%.1f %.1f %.1f) touch %d\n", n.id, n.kind,
                        n.treasure, npcActive(app, n), n.flagShow, n.flagHide, n.pos[0], n.pos[1], n.pos[2], n.touchTrigger());
    if (app.havePlayer && app.haveStageCam) {
        // Where the leader's bounding box lands in the 1024x1024 stage view
        const cr::StageCamera& sc = app.stageCam;
        const float aspect = sc.viewHeight > 0 ? float(sc.viewWidth) / float(sc.viewHeight) : 1.0f;
        const cr::Mat4 mvp = cr::Mat4::perspective(sc.fovDegrees * 3.14159265f / 180.0f, aspect, 10.0f, 4000.0f) *
                             cr::Mat4::lookAt({sc.eye[0], sc.eye[1], sc.eye[2]}, {sc.target[0], sc.target[1], sc.target[2]},
                                              {0, 1, 0}) * playerWorld(app.walker);
        const auto& b0 = app.player.cpu.boundsMin;
        const auto& b1 = app.player.cpu.boundsMax;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int c = 0; c < 8; ++c) {
            const float p[3] = {c & 1 ? b1[0] : b0[0], c & 2 ? b1[1] : b0[1], c & 4 ? b1[2] : b0[2]};
            const float* m = mvp.m;
            const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            const float sx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * 512 + 512;
            const float sy = 512 - (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * 512;
            x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        std::printf("player %s at (%.1f %.1f %.1f) r=%.0f: view box x %.0f..%.0f y %.0f..%.0f (%.0f px tall)\n",
                    app.playerName.c_str(), app.walker.pos.x, app.walker.pos.y, app.walker.pos.z, app.walker.radius,
                    x0, x1, y0, y1, y1 - y0);
    }
    return ok ? 0 : 1;
}

} // namespace crapp
