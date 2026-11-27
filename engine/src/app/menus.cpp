#include "app/app.hpp"

namespace crapp {

// __RenewalGetSccnariosState: the Camp's "new" mark on Scenarios - flag 0x642 and no cell open on
// cards 0..3, or flag 0x665 and none open on all five
bool campScenariosNew(const App& app) {
    static const int kOpen[5] = {2101, 2117, 2133, 2149, 2165};
    const cr::GameState& g = app.game;
    int open = 0;
    bool n = false;
    if (g.flag(0x642)) {
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < 16; ++i) open += g.flag(kOpen[c] + i);
        n = open == 0;
    }
    if (g.flag(0x665)) {
        for (int i = 0; i < 16; ++i) open += g.flag(kOpen[4] + i);
        if (open == 0) n = true;
    }
    return n;
}



// ---- Achievements (game/achievements.hpp; docs/ENGINE.md "Achievements") ----
// When one is earned follows 3.1.0 (its checks and their callers; the flag kept at once, as Game
// Center's answer sets it there); how it shows follows Android 1.1.6 (achievement_prc's banner -
// 3.1.0 had Game Center's own). The earned set is global, in settings.ini (Android: system.dat)

// SetSystemConfFlag, kept
void setSystemConfFlag(App& app, int f) {
    if (app.ach.flags.has(f)) return;
    app.ach.flags.set(f);
    saveMix(app);
}

// checkAchievements(n): earned; its banner waits its turn (one at a time, in the order earned)
void earnAchievement(App& app, int n) {
    App::Achievements& a = app.ach;
    if (!a.live || n < 1 || n > cr::achievements::kCount || a.flags.has(n)) return;
    a.flags.set(n);
    a.queue.push_back(n);
    a.earnedLog.push_back(n);
    SDL_Log("achievement %d earned", n);
    saveMix(app);
}

// fld_checkAchievements (3.1.0: a script's end without a tutorial kicked, the shop's purchase;
// Android also as a banner closes). One at a time: nothing while a banner is up or waiting (3.1.0
// skips it for 600 ticks after one, while Game Center shows its own; the banner's close checks again)
void fieldCheckAchievements(App& app) {
    App::Achievements& a = app.ach;
    if (app.booting || app.titleOpen || a.banner.on || !a.queue.empty()) return;
    earnAchievement(app, cr::achievements::fieldCheck(app.game, a.flags));
}

// btlcalc_chk_dead's system flags, then 3.1.0's battle check (FUN_0008fa80; Android's is empty -
// there the five wait for the next field check). The banner waits for the field
void battleConfFlag(App& app, int f) {
    if (!f) return;
    setSystemConfFlag(app, f);
    earnAchievement(app, cr::achievements::battleCheck(app.ach.flags));
}

// Where Android's banner can come up: over the field or the shop (achievement_prc runs every
// frame; Android starts it only at a script's end, a purchase or a banner's close), not in a battle
bool achievementBannerCanShow(const App& app) {
    return app.mode == Mode::Maps && !app.booting && !app.titleOpen && !app.battle.active &&
           (!app.runner.running() || app.shop.open);
}
bool achievementBannerUp(const App& app) { return app.ach.banner.on; }

// achievement_prc, at 20 Hz: SE 0xf7 as one comes in; at its close (pad_all_clear, the flag set,
// isAchievementDraw off) the field check again - another one stops the SE (0x200000f7) and follows
void stepAchievements(App& app, float dt) {
    App::Achievements& a = app.ach;
    for (a.clock += dt; a.clock >= 1.0f / kFramesPerSecond; a.clock -= 1.0f / kFramesPerSecond) {
        if (!a.banner.on && !a.queue.empty() && achievementBannerCanShow(app)) {
            a.banner.start(a.queue.front());
            a.queue.erase(a.queue.begin());
        }
        const auto e = a.banner.step(a.tap);
        a.tap = false;
        if (e == cr::achievements::Banner::Sound) app.sound.play(0xf7);
        if (e == cr::achievements::Banner::Closed) {
            fieldCheckAchievements(app);
            if (!a.queue.empty()) app.sound.play(0x200000f7);
        }
    }
    if (!a.banner.on) a.clock = std::min(a.clock, 1.0f / kFramesPerSecond);
}

// ---- Saves (SaveData_Save / ContData_Save, the Camp menu, __menuu_SaveLoad*) ----

std::string savePath(const App& app, const char* file) {
    return (app.saveDirOverride.empty() ? cr::saveDir() : app.saveDirOverride) + file;
}

cr::SaveData captureSave(const App& app) {
    cr::SaveData s;
    s.area = app.area;
    s.map = app.mapNumber;
    s.pos[0] = app.walker.pos.x;
    s.pos[1] = app.walker.pos.y;
    s.pos[2] = app.walker.pos.z;
    s.yaw = app.walker.heading;
    if (app.area == 1 && app.fieldLoadSet) // (a puzzle room: where it was loaded - the try starts over there)
        for (int k = 0; k < 3; ++k) s.pos[k] = app.fieldLoad[k];
    if (app.area == 1 && app.fieldLoadSet) s.yaw = app.fieldLoad[3];
    s.leader = app.playerChar;
    s.playSeconds = app.playSeconds;
    s.savedAt = int64_t(std::time(nullptr));
    s.game = app.game;
    s.game.frmScene = s.game.frmRecord = 0;
    const int sc = std::clamp(s.game.scenario, 0, cr::kScenarios - 1); // ContData_Save_set: the records in play back to their block
    for (int k = 0; k < 3; ++k) s.game.party[size_t(4 + 3 * sc + k)] = s.game.party[size_t(k)];
    s.setPlace(sc, s.placeInPlay());
    return s;
}



// Back on the field the save was made on, as arriving there by warp (the room's entry scripts
// run as they would)
bool applySave(App& app, const cr::SaveData& s) {
    if (s.map < 0 || s.map >= cr::kNumMaps) return false;
    bootTitle(app); // everything closed and torn down
    app.runner.stop();
    app.booting = false;
    app.titleOpen = false;
    app.game = s.game;
    // ContData_memcpy / Load_SaveData: once "About Progress Tracker" has been shown this launch
    // (MuTitle_GetInInterruptFlg), the state taken up keeps flag 0x44 - the message isn't offered again
    if (app.interruptShown) app.game.setFlag(0x44, true);
    // _SaveData_Load_init (Load and the Progress Tracker's resume alike): flag 0xff7 - every area's
    // entry 0 then clears it and 4076 and stops short of its arrival (no title card, no "Save data?"
    // again on the Ark, no arrival triggers)
    app.game.setFlag(0xff7, true);
    if (!app.game.wornInBag) cr::putWornInBag(app.game, app.itemTable);
    app.treasureBoxes.clear();
    app.entryArea = -1;
    app.playSeconds = s.playSeconds;
    app.playerChar = std::clamp(app.game.leaders[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))], 0, 3);
    app.mapNumber = -1; // so the warp loads the field even if it's the one showing
    cr::WarpRequest w;
    w.valid = true;
    w.area = s.area;
    w.map = s.map;
    std::memcpy(w.pos, s.pos, sizeof w.pos);
    w.angleDegrees = s.yaw * 180.0f / 3.14159265f;
    if (!performWarp(app, w, -1)) return false;
    app.walker.pos = {s.pos[0], s.pos[1], s.pos[2]};
    app.walker.heading = s.yaw;
    return true;
}


// __ComUty_FaceBoardFaceDraw: a record's face, picture 29 + record (32 for record 0 while it is Olgar)
int facePicture(const App& app, int character) {
    character = std::clamp(character, 0, cr::kCharacters - 1);
    return character == 0 && viegIsOlgar(app) ? 32 : 29 + character;
}
// __ComUty_FaceBoardBackDraw: whose name picture (45 / 53 + n) a record's board shows - record 1's
// for record 0 while it is Olgar, record 3's (Dante) for record 1 in scenario 1
// tex2d slot 0x30 (tex2d_setup_tex: window_char, the _ipad / _hd sheet 2048 x 1024 for 1024 x 512
// tex2d units): 3.1.0's languages' names (88 x 24 at u = language x 88, v 0) and the face boards' names
// (MuTex table 8: 92 x 20 at u = language x 92, v = 32 + row x 20). 1.0.0's data has no such sheet
const cr::Texture* charSheet(App& app) {
    if (!app.charSheetTried && app.rendererReady) {
        app.charSheetTried = true;
        cr::Image img;
        if (loadImage(app, "window_char.mct", img) && img.width >= 1024 && img.height > 0) {
            img.logicalW = 1024, img.logicalH = 1024 * img.height / img.width;
            app.charSheet = app.renderer.upload(img);
        }
    }
    return app.charSheet.id ? &app.charSheet : nullptr;
}
// 3.1.0's Options and boards: the data's eight languages and its window_char sheet
bool languageSheet(App& app) { return app.systemText.hasLanguage(2) && charSheet(app); }

int boardNameIndex(const App& app, int c) {
    if (c == 0 && viegIsOlgar(app)) return 1;
    if (c == 1 && olgarIsDante(app)) return 3;
    return c;
}

std::string playTimeText(double seconds) {
    const long t = long(seconds);
    char b[32];
    SDL_snprintf(b, sizeof b, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
    return b;
}

std::string dateText(int64_t when) {
    const std::time_t t = std::time_t(when);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[48];
    std::strftime(b, sizeof b, "%Y/%m/%d  %H:%M", &tm);
    return b;
}

// The older releases' one save per pair (save1.dat .. save4.dat): read once, to fold into data.dat
std::string slotPath(const App& app, int pair) {
    const std::string name = "save" + std::to_string(std::clamp(pair, 1, 4)) + ".dat";
    return savePath(app, name.c_str());
}
// The one save, as the original's data.dat (_prty): both scenarios' records, bags and Oz, the flags
// and variables, and each scenario's own saved place (SaveData::places)
std::string dataPath(const App& app) { return savePath(app, "data.dat"); }

// Reads data.dat. Without one, the per-pair saves of the earlier versions are folded into it once:
// the newest is the base; the other scenario's slot brings its own records, bag, Oz, band, pair,
// leader, treasure pick and saved place, its flags are added to the one flag array (the original
// holds both scenarios' in it) and its shared genes / skills / manual / plates counted in
bool readData(const App& app, cr::SaveData& d) {
    if (cr::readSave(dataPath(app), d)) { cr::splitSharedGenes(d.game, app.itemTable); return true; }
    std::vector<std::pair<int, cr::SaveData>> slots;
    for (int pair = 1; pair <= 4; ++pair) {
        cr::SaveData s;
        if (cr::readSave(slotPath(app, pair), s)) slots.push_back({pair - 1, s});
    }
    if (slots.empty()) return false;
    size_t base = 0;
    for (size_t i = 1; i < slots.size(); ++i)
        if (slots[i].second.savedAt > slots[base].second.savedAt) base = i;
    d = slots[base].second;
    d.places = {};
    for (size_t i = 0; i < slots.size(); ++i) {
        const int sc = slots[i].first;
        const cr::SaveData& s = slots[i].second;
        if (sc < cr::kScenarios) d.setPlace(sc, s.placeInPlay());
        if (i == base) continue;
        for (size_t w = 0; w < d.game.flags.size() && w < s.game.flags.size(); ++w) d.game.flags[w] |= s.game.flags[w];
        for (const auto& [k, v] : s.game.vars) d.game.vars.emplace(k, v);
        if (sc >= cr::kScenarios) continue; // (the first game's third and fourth pairs: nothing of Omega's)
        const bool live = s.game.scenario == sc; // its scenario was the one in play in that file
        for (int k = 0; k < 3; ++k) d.game.party[size_t(4 + 3 * sc + k)] = live ? s.game.party[size_t(k)] : s.game.party[size_t(4 + 3 * sc + k)];
        d.game.bands[size_t(sc)] = s.game.bands[size_t(sc)];
        d.game.pairs[size_t(sc)] = s.game.pairs[size_t(sc)];
        d.game.leaders[size_t(sc)] = s.game.leaders[size_t(sc)];
        d.game.treasure[size_t(sc)] = s.game.treasure[size_t(sc)];
        d.game.noEncounters[size_t(sc)] = s.game.noEncounters[size_t(sc)];
        d.game.walkExp[size_t(sc)] = s.game.walkExp[size_t(sc)];
        std::unordered_map<int, int> own;
        if (live) {
            for (const auto& [item, n] : s.game.items)
                if (cr::sharedItem(app.itemTable, item)) d.game.items[item] = std::max(d.game.items[item], n);
                else own[item] = n;
        } else own = s.game.otherBag;
        const int oz = live ? s.game.oz : s.game.otherOz;
        if (d.game.scenario == sc) {
            for (const auto& [item, n] : own) d.game.items[item] = n;
            d.game.oz = oz;
        } else d.game.otherBag = own, d.game.otherOz = oz;
    }
    cr::splitSharedGenes(d.game, app.itemTable);
    cr::writeSave(dataPath(app), d);
    return true;
}

// SaveData_Save: the game into data.dat, the scenario in play's place with it (the other's kept)
bool saveGame(App& app) {
    cr::SaveData s = captureSave(app);
    cr::SaveData old;
    if (readData(app, old)) s.places = old.places;
    s.setPlace(s.game.scenario, s.placeInPlay());
    s.game.vars[9] = 0; // (SaveData_Save: the saved copy's var 9, +0x2c8, written as 0)
    if (!cr::writeSave(dataPath(app), s)) return false;
    SDL_RemovePath(savePath(app, "resume.dat").c_str()); // (_removeContData: a save ends the Progress Tracker's)
    return true;
}

int currentPair(const App& app) { return app.game.scenario + 1; } // (Omega: the scenario + 1)

// The title's plates, by what they are (0 the last battle: every pair cleared; 1 Load: a save;
// 2 New), drawn at (328, 136 + 48 x that number) - New alone sits at 232
std::vector<int> savePlates(const App& app) {
    const App::SaveScreen& ss = app.saveScreen;
    if (ss.allClear) return {0, 1, 2};
    if (!ss.haveSave) return {2};
    return {1, 2};
}

void openSaveScreen(App& app, bool load) {
    App::SaveScreen& ss = app.saveScreen;
    ss = App::SaveScreen();
    ss.open = true;
    ss.load = load;
    if (load) cr::changeScenario(app.game, app.itemTable, app.chosenPair - 1); // SaveLoadSet: __prty_chg(var 0 - 1)
    // the one save, as scenario p would load it (SaveData_Load_chk: its "saved" mark, +0x1546 + p;
    // SaveData_Load_init: its own place); shown on the left with p's records
    const int p = std::clamp((load ? app.chosenPair : currentPair(app)) - 1, 0, cr::kScenarios - 1);
    cr::SaveData d;
    const bool haveData = readData(app, d);
    ss.haveSave = haveData && d.places[size_t(p)].has;
    if (ss.haveSave) {
        d.takePlace(p);
        cr::changeScenario(d.game, app.itemTable, p);
        ss.data = d;
    } else if (load) { // no save of p: the boards are still drawn (ComUty_CharInfoInit), from the party
        // as the title holds it - the data's, else a fresh prty_init's - switched to p (__prty_chg)
        if (!haveData) { // (prty_init: its items, and flag 0xfee - scenario 1's second held back)
            cr::giveStartingItems(d.game, app.itemTable);
            d.game.setFlag(0xfee, true);
        }
        cr::changeScenario(d.game, app.itemTable, p);
        ss.data.game = d.game;
    }
    if (load) { // __menuu_SaveLoadSet, the title's (Omega): the scenario's load flag (_gkanLoadFlgTbl:
        // 31 / 32, its story cleared) makes New ask about the clear data; no emblems, and the first
        // plate (CR1's last battle) is never shown
        ss.allClear = false;
        ss.cleared = haveData && d.game.flag(31 + p);
    }
    if (!load) { // save: straight to "Save data?"
        ss.phase = 1;
        ss.pending = 1;
        ss.message = 57012;
    }
}

void closeSaveScreen(App& app) { app.saveScreen.open = false; }

// Util 6's save menu (mode 3): SaveLoadClr with its "to the title" mark - the music stopped
// (MuCom_PreReturnTitle: __bgmgo(0x8000000)), the scene's effects ended, and MuCom_ReturnTitle:
// the field torn down and the boot script from the start
void saveScreenToTitle(App& app) {
    app.saveScreen.open = false;
    playBgm(app, 1u << 27);
    app.runner.stop();
    bootTitle(app);
}
// SaveLoadPrc 8 -> No: message 0x3c, "return to the title without saving?" (its Yes the title, its No
// back to "Save data?"); the menu never closes into the field
void saveScreenNoToTitle(App& app) {
    App::SaveScreen& ss = app.saveScreen;
    ss.askLeave = !ss.askLeave;
    ss.message = ss.askLeave ? 57060 : 57012;
    ss.confirm = 0;
}

// Yes in the confirmation: do it
void saveScreenYes(App& app) {
    App::SaveScreen& ss = app.saveScreen;
    if (ss.toTitle && ss.askLeave) { saveScreenToTitle(app); return; } // leaving unsaved
    if (ss.pending == 1) {
        if (saveGame(app)) {
            ss.data = captureSave(app);
            ss.haveSave = true;
            ss.message = 57082; // Save complete.
            menuSound(app, se::kSaved);
        } else {
            ss.message = -1; // couldn't write
            menuSound(app, se::kBuzzer);
        }
    } else if (ss.pending == 2) {
        const cr::SaveData s = ss.data;
        ss.open = false;
        const bool loaded = applySave(app, s);
        // applySave tears down the old session, including this menu. Rebuild only
        // its result panel, without reviving the pending load confirmation.
        ss.load = true;
        ss.haveSave = true;
        ss.data = s;
        ss.kind = 4;
        if (loaded) {
            ss.open = true;
            ss.message = 57083; // Load complete.
            menuSound(app, se::kLoaded);
        } else {
            ss.open = true;
            ss.message = -1;
            menuSound(app, se::kBuzzer);
        }
    } else if (ss.pending == 3) { // New: the boot script carries on with var 1 = the pair
        menuSound(app, se::kLoaded); // SaveLoadPrc plays it for any Yes in load mode
        ss.open = false;
        if (ss.kind == 1) app.chosenPair = 1; // Set_Prtyno(0): the last battle is the first pair's
        if (app.booting && app.runner.running()) {
            app.game.vars[1] = ss.kind; // SaveLoadClr: SetVar(1, the choice) - the boot script branches on it
        } else {
            app.titleOpen = false;
            app.runner.stop();
            startNewGame(app);
        }
        return;
    }
    ss.phase = 2;
    ss.timer = 0;
}

void saveScreenChoose(App& app, int i) { // the title's plates (savePlates): the last battle, Load, New
    App::SaveScreen& ss = app.saveScreen;
    const std::vector<int> plates = savePlates(app);
    if (i < 0 || i >= int(plates.size())) return;
    ss.cursor = i;
    ss.phase = 1;
    ss.confirm = 0;
    ss.lines[0] = ss.lines[1] = ss.lines[2] = 0;
    switch (plates[size_t(i)]) {
    case 0: ss.kind = 1, ss.pending = 3, ss.message = 57056; break; // Proceed to the last battle?
    case 1: ss.kind = 4, ss.pending = 2, ss.message = 57013; break; // Load data?
    default:
        ss.pending = 3;
        if (ss.cleared) ss.kind = 3, ss.message = 57086, ss.lines[0] = 57087, ss.lines[1] = 57088; // with your clear data?
        else ss.kind = 2, ss.message = 57057; // Begin new game?
        break;
    }
    menuSound(app, se::kChoose);
}

// ComUty_GameTimeAndPlayTimeDraw (the same in 1.0.0 and 3.1.0): the save's date on a dark board at
// (324, 12) - picture 0x81 (DATE) at (372, 12) over a line, the month 0x82 + m (1..12) at (336, 38),
// the day at 378, 0x8f at (400, 38), the year at 410; the time at 368 + (0, 12, 36, 48)
// (_ganRealTimeTbl, no leading zero) with the colon (0x4b) at (392, 58) - and under it
// __ComUty_PlayingTimeDraw at (324, 84): picture 0x82 at (344, 88) over a line, the hours (to 999)
// and minutes at (+32 / 44 / 56 / 80 / 92, +22) (_ganTimeTbl), the colon at (+68, +22). `dx`: the
// window's slide (TitlePut's window 5 comes in from +360)
void drawSaveDate(App& app, cr::MenuPainter& m, const cr::SaveData& d, float a, float dx) {
    (void)app;
    const time_t when = time_t(d.savedAt);
    struct tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &when);
#else
    localtime_r(&when, &tm);
#endif
    const float dark[4] = {0, 0, 0, 0.5f * a}, white[4] = {1, 1, 1, a};
    m.quad(324 + dx, 12, 148, 64, dark, dark);
    m.picture(0x81, 372 + dx, 12, a);
    m.quad(325 + dx, 28, 146, 1, white, white);
    m.picture(0x82 + std::clamp(tm.tm_mon + 1, 1, 12), 336 + dx, 38, a);
    drawShopNumber(m, tm.tm_mday, 378 + dx, 38, 2, 12, a);
    m.picture(0x8f, 400 + dx, 38, a);
    drawShopNumber(m, tm.tm_year + 1900, 410 + dx, 38, 4, 12, a);
    const int digit[4] = {tm.tm_hour / 10, tm.tm_hour % 10, tm.tm_min / 10, tm.tm_min % 10};
    static const float kAt[4] = {0, 12, 36, 48};
    for (int i = 0; i < 4; ++i)
        if (i || tm.tm_hour >= 10) drawShopNumber(m, digit[i], 368 + dx + kAt[i], 58, 1, 12, a);
    m.picture(0x4b, 392 + dx, 58, a);
    const int secs = int(std::max(0.0, d.playSeconds));
    const int hours = std::min(999, secs / 3600), mins = std::min(59, secs / 60 % 60);
    m.quad(324 + dx, 84, 148, 40, dark, dark);
    m.picture(0x82, 344 + dx, 88, a);
    m.quad(325 + dx, 100, 146, 1, white, white);
    const int pd[5] = {hours / 100, hours % 100 / 10, hours % 10, mins / 10, mins % 10};
    static const float kPlay[5] = {32, 44, 56, 80, 92};
    for (int i = 0; i < 5; ++i)
        if (i || pd[i]) drawShopNumber(m, pd[i], 324 + dx + kPlay[i], 84 + 22, 1, 12, a);
    m.picture(0x4b, 324 + dx + 68, 84 + 22, a);
}

void drawSaveScreen(App& app) {
    App::SaveScreen& ss = app.saveScreen;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu phoneMenu(app, m); // the iPhone layout: its screen's menu coordinates
    r.setOrigin(0);
    const bool art = ss.load && app.charSelectArt.id;
    if (art) r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 1);
    r.setOrigin(r.uiX());
    // On the title (__menuu_SaveLoadPut, mode 1) the scenario's Select Scenario art behind it all:
    // picture 24 (iPhone 22) of slot 0x10 (0x12, chara3, for the second scenario) at (-32, -64), z -10,
    // in white - under the menu mask (SaveLoadSet's ChgBackMaskDrawOrder / MuCom_MenuOneFrSetMask:
    // black at 0xa0), so 0x5f of it shows (Part 1 21.3 s against 18 s: the art at 0.33..0.43 of its
    // Character Select brightness; the port had 0.65)
    if (art) drawCharArt(app, 1, false, 1 - 0xA0 / 255.0f); // (else the menu mask, drawFrame's)
    r.setOrigin(r.uiX());
    const float a = std::min(1.0f, ss.anim / 8.0f);
    // Widescreen: the heading, face boards, emblems to the screen's left; the date, the plates to
    // its right; Back and the message centred (as the shop)
    r.setOrigin(wideLeft(app));
    // (no heading: SaveLoadPut draws none - Part 6 1130.3 s, the field's "Save data?" - the port had
    // the Camp's)
    // The slot (__menuu_SaveLoadPut): the pair's face boards (ComUty_FaceBoardDraw) at (0, 24) and
    // (0, 112) - the save's on the title, the party's when saving -, on the title the save's date
    // and play time (ComUty_GameTimeAndPlayTimeDraw) top right and the pair's emblems (pictures
    // 111..113 at (16 + 72k, 224))
    // (no save: still the boards - ComUty_CharInfoInit reads the party as the title holds it, after
    // SaveLoadSet's __prty_chg to the scenario: Part 9 14.5 s, the second scenario's New alone with
    // "Olgar" LV 1 on Vieg's face; the port wrote "No save data")
    const cr::GameState* shown = ss.load ? &ss.data.game : &app.game;
    // (ComUty_CharInfoInit: the boards are the shown game's scenario pair - Vieg and Olgar, or Vieg and
    // Dante - one while flag 0xfed + scenario holds the second back)
    const int scn = shown ? std::clamp(shown->scenario, 0, cr::kScenarios - 1) : 0;
    if (shown)
        for (int k = 0; k < (shown->flag(0xfed + scn) ? 1 : 2); ++k) {
            const int pc = shown->pairs[size_t(scn)][size_t(k)];
            const int ch = pc >= 0 && pc < cr::kCharacters ? pc : 0;
            int hp, hpMax, mp, mpMax;
            memberHpMp(app, ch, hp, hpMax, mp, mpMax, shown);
            drawFaceBoard(app, m, 0, 24 + 88.0f * float(k), ch, hp, hpMax, mp, mpMax, a, shown);
        }
    // (CR1's pair emblems, pictures 111..113: Omega's SaveLoadPut has none)
    r.setOrigin(wideRight(app));
    if (ss.load && ss.haveSave) drawSaveDate(app, m, ss.data, a, 0);
    if (ss.phase == 0) { // the plates at (328, 136 + 48 x which): the last battle, Load, New
        // (the Vita's SaveLoadPut: the plates at 0x173 and Back at 0xeb = 235 - 43 to the right; vita decomp 79864-79870)
        const std::vector<int> plates = savePlates(app);
        static const int kWord[3] = {4, 7, 6};
        r.setOrigin(vitaRight(app));
        for (size_t i = 0; i < plates.size(); ++i)
            m.plate(328, 136 + 48.0f * float(plates[i]), plateIcon(kWord[plates[i]]), kWord[plates[i]],
                    (ss.cursor == int(i) && !app.pointerLast) || pressIn(app, kPressSave, vitaRight(app), 328, 136 + 48.0f * float(plates[i]), 136, 40),
                    a, false); // (window 3; the icon is the word's: ComUty_ChoicePlateDraw; lit while held: DAT_000ecca0)
        const float bo = app.renderer.uiX() + 2 * vitaEdge43(app);
        r.setOrigin(bo);
        m.backBar(192, 288, pressIn(app, kPressSave, bo, 192, 288, 232, 24), a); // (Omega's ComUty_BackButtonDraw mode 1: CmdBack at (192, 288), no arrows; lit while held)
        r.setOrigin(r.uiX());
    } else { // SaveLoadPut, in every layout (Omega's iPad screen is the iPhone's doubled): the message
        // board (ComUty_MsgBoardDraw, picture 12) at (0, 220), lines from (24, 234); Yes / No the
        // plates at window 6 + (336, 104) / (336, 160) (ComUty_YesNoChoiceDraw)
        // (ComUty_MsgBoardDraw: its mark going out and back, the lines in the menu font - Part 1 21.3 s
        // "Begin new game?", Part 6 1130.3 s "Save data?")
        m.picture(12, 0, 220, a);
        const short* mark = cr::kMenuPicture[13];
        m.piece(458, 298, float(mark[0] + 20 * pictAnimFrame(ss.anim, 1, 4)), mark[1], mark[2], mark[3], a);
        const std::string msg = ss.message > 0 ? sysText(app, ss.message) : std::string("The save couldn't be written.");
        m.text(msg.empty() ? "?" : msg, 24, 234, false, a, 13);
        int extra = 0;
        for (int l : ss.lines)
            if (l > 0) m.text(sysText(app, l), 24, 234 + 16.0f * float(++extra), false, a, 13);
        if (ss.phase == 1) {
            const bool hy = pressIn(app, kPressSave, wideRight(app), 336, 104, 136, 40), hn = pressIn(app, kPressSave, wideRight(app), 336, 160, 136, 40);
            m.plate(336, 104, 0, 0, hy || (!hn && ss.confirm == 0), a, false); // (a held plate takes the cursor)
            m.plate(336, 160, 1, 1, hn || (!hy && ss.confirm == 1), a, false);
        }
    }
    r.setOrigin(wideLeft(app));
    if (keyHints(app)) // (our key hints)
        m.text(ss.phase == 1 ? "Up/Down  Enter  Esc: no" : ss.phase == 0 ? "Up/Down  Enter  Esc: back" : "", 20, 372, false,
           a * 0.8f, 9);
    r.setOrigin(r.uiX());
}

// One 20 Hz step: the fade-in, and the "done" message closing itself
void stepSaveScreen(App& app) {
    App::SaveScreen& ss = app.saveScreen;
    if (!ss.open) return;
    ++ss.anim;
    if (ss.phase == 2 && ++ss.timer >= (ss.toTitle || ss.load ? 20 : 40)) { // (SaveLoadPrc 5 / 10: "Load / Save complete." 20 frames at the title)
        ss.open = false;
        if (ss.load && ss.message > 0) app.titleOpen = false; // loaded: on with the game
        if (ss.toTitle) saveScreenToTitle(app);
    }
}

void saveScreenKey(App& app, SDL_Keycode key) {
    App::SaveScreen& ss = app.saveScreen;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    if (ss.phase == 2) {
        if (enter || back) ss.timer = 1000;
        return;
    }
    if (ss.phase == 1) {
        if (key == SDLK_UP || key == SDLK_W || key == SDLK_DOWN || key == SDLK_S || key == SDLK_LEFT || key == SDLK_A ||
            key == SDLK_RIGHT || key == SDLK_D) { ss.confirm ^= 1; menuSound(app, se::kCursor); } // (Yes over No)
        else if (enter && ss.confirm == 0) saveScreenYes(app);
        else if (enter || back) { // No
            menuSound(app, back ? se::kBack : se::kChoose); // (the Vita's × for No: 0x94)
            if (ss.toTitle) saveScreenNoToTitle(app);
            else if (ss.load && ss.kind == 3) { // then a plain new game? (57089..57091)
                ss.kind = 2;
                ss.message = 57089, ss.lines[0] = 57090, ss.lines[1] = 57091;
                ss.confirm = 0;
            } else if (ss.load) ss.phase = 0;
            else closeSaveScreen(app);
        }
        return;
    }
    const int n = int(savePlates(app).size());
    if (key == SDLK_UP || key == SDLK_W) { ss.cursor = (ss.cursor + n - 1) % n; menuSound(app, se::kCursor); }
    else if (key == SDLK_DOWN || key == SDLK_S) { ss.cursor = (ss.cursor + 1) % n; menuSound(app, se::kCursor); }
    else if (enter) saveScreenChoose(app, ss.cursor);
    else if (back) {
        if (ss.load && app.booting) {
            app.game.vars[1] = 0; // back to Character Select
            if (app.game.vars[0] == 2) playBgm(app, 0x1d); // (SaveLoadClr: the second scenario's screen gives its page back its tune)
        }
        closeSaveScreen(app);
        menuSound(app, se::kBack);
    }
}

void saveScreenClick(App& app, float px, float py) {
    App::SaveScreen& ss = app.saveScreen;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my, rx;
    menuPoint(app, lx, ly, mx, my);
    menuPointAt(app, lx, ly, wideRight(app), rx, my); // (widescreen: the plates on the right)
    const float v43 = vitaEdge43(app); // (the Vita's plates and Back 43 to the right, its Yes / No not)
    if (ss.phase == 2) { ss.timer = 1000; return; }
    if (ss.phase == 1) {
        // the plates (0x88 x 0x28) where drawSaveScreen puts them in every layout: (336, 104) / (336, 160)
        // from the right-hand origin (ComUty_YesNoChoice*)
        if (rx >= 336 && rx < 472 && my >= 104 && my < 144) { ss.confirm = 0; saveScreenYes(app); }
        else if (rx >= 336 && rx < 472 && my >= 160 && my < 200) saveScreenKey(app, SDLK_ESCAPE);
        return;
    }
    const std::vector<int> plates = savePlates(app);
    rx -= v43, mx -= v43;
    for (size_t i = 0; i < plates.size(); ++i) { // _pad_chk_updw_box(328, 136 + 48k, 136, 40)
        const float py0 = 136 + 48.0f * float(plates[i]);

        if (rx >= 328 && rx < 464 && my >= py0 && my < py0 + 40) { saveScreenChoose(app, int(i)); return; }
    }
    const float backY = 288; // Omega's ComUty_BackButtonDraw: (192, 288)
    if (my >= backY && my < backY + 24 && mx >= 192 && mx < 424) saveScreenKey(app, SDLK_ESCAPE);
}


int shopRoute(const App& app) { return std::clamp(app.game.scenario, 0, cr::kScenarios - 1); }

// The record the board shows: the scenario pair's first or second
int shopCharacter(const App& app) { return campCharacter(app, app.shop.member); }
bool shopEquipment(const App& app) { return app.shop.category >= 1 && app.shop.category <= 3; }

int ownedCount(const App& app, int item) {
    if (cr::otherScenarioPlate(app.game, item)) return 0; // (_GetItmCnt)
    auto it = app.game.items.find(item);
    return it == app.game.items.end() ? 0 : it->second;
}

// The price of one in the current list (half when selling)
void plateText(cr::MenuPainter& m, const std::string& s, float x, float y, int style, float a);
int shopUnitPrice(const App& app, int item) {
    const cr::ItemParam* p = app.itemTable.item(item);
    if (!p) return 0;
    if (app.shop.selling) return cr::sellPrice(*p);
    // 173 Sexy Pose on either member of the pair (MuCom_IsSkillEquip(0 / 1): the records Uty_SetPrtyNo named): 10% off
    const auto& pair = app.game.pairs[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))];
    const bool pose = cr::hasSkill(app.game, pair[0], 173) || cr::hasSkill(app.game, pair[1], 173);
    return pose ? cr::buyPrice(*p) - cr::buyPrice(*p) / 10 : cr::buyPrice(*p); // (ShopPrc: price - price / 10)
}

int shopMaxCount(const App& app, int item) {
    if (app.shop.selling) { // (MuScrList_GetChoiceListNo 9: refused with none to sell or no price - jewel 387 & co)
        const cr::ItemParam* sp = app.itemTable.item(item);
        if (!sp || sp->price == 0) return 0;
        return std::clamp(ownedCount(app, item) - cr::wornCount(app.game, item), 0, cr::kMaxItemCount);
    }
    const int n = cr::maxPurchase(shopUnitPrice(app, item), app.game.oz, ownedCount(app, item));
    // ShopPrc: a gene, skill or OMU item (kinds 4 / 5 / 11) already owned is greyed and refused; the
    // genes' and skills' stock (81..198) and the OMU items (521..540) are bought one at a time
    const cr::ItemParam* p = app.itemTable.item(item);
    if (p && (p->kind == 4 || p->kind == 5 || p->kind == 11) && ownedCount(app, item) > 0) return 0;
    return (item >= 81 && item <= 198) || (item >= 521 && item <= 540) ? std::min(n, 1) : n;
}

// MuScrList (mode 0): the rows move under a fixed line at y 150 and the focus row is the one on it
// (Part 13 850-852 s: Emergency Exit, Serum, White Chocolate each lit on the line in turn, as the
// Camp's lists); MuScrList_Proc moves them 8 a frame toward it (eb6c0 to eb6c8), a jump sets both
void shopFollowCursor(App::Shop& s) { s.listTop = s.listCursor; }
void stepShopScroll(App::Shop& s) {
    const float to = shopui::kRowH * float(s.listCursor);
    if (std::fabs(to - s.scroll) > shopui::kRowH) s.scroll = to;
    else if (s.scroll < to) s.scroll = std::min(to, s.scroll + 8);
    else s.scroll = std::max(to, s.scroll - 8);
}

void refreshShopList(App& app) {
    App::Shop& s = app.shop;
    s.list = s.selling ? cr::sellableItems(app.game, app.itemTable, shopCharacter(app), s.category)
                       : cr::shopStock(app.game, app.itemTable, shopCharacter(app), s.category, s.level);
    s.listCursor = std::clamp(s.listCursor, 0, std::max(0, int(s.list.size()) - 1));
    shopFollowCursor(s);
    s.scroll = shopui::kRowH * float(s.listCursor); // (a new list starts settled)
}

void openShop(App& app) {
    App::Shop& s = app.shop;
    cr::Texture keeper = s.keeper;
    s = App::Shop();
    s.keeper = keeper;
    s.open = true;
    s.level = cr::shopLevel(app.game, shopRoute(app));
    s.member = campMembers(app) > 1 && campCharacter(app, 1) == app.playerChar ? 1 : 0;
    s.bgmBack = app.game.vars[0]; // ShopSet: var 0 kept, the shop's music (_bgmgo(0x13))
    playBgm(app, 0x13);
    if (!s.keeper.id) {
        cr::Image img;
        if (loadImage(app, "c101_L01_hd.mct", img)) s.keeper = app.renderer.upload(img);
    }
}

void closeShop(App& app) {
    if (app.shop.open && !app.shop.noBgmRecovery) playBgm(app, uint32_t(app.shop.bgmBack)); // ShopClr
    if (app.shop.open && app.game.scenario == 0) { // ShopClr: the room's sounds back, looping, fading in over 20
        if (app.game.flag(0x472)) app.sound.play(0x800113), app.sound.play(0x80113);
        if (app.game.flag(0x473)) app.sound.play(0x800003), app.sound.play(0x80003);
    }
    app.shop.open = false;
    app.renderer.destroy(app.shop.keeper);
}

// Talk: the windows go, the area's EvrStart 4 plays (it clears flag 4090 when done) while the
// script that opened the shop waits parked; then back to the plates
void startShopTalk(App& app) {
    const uint32_t at = app.events.returnEntry(4);
    if (!at) { menuSound(app, se::kBuzzer); return; }
    app.shop.phase = App::Shop::Talk;
    app.shop.timer = 0;
}

void stepShop(App& app) {
    App::Shop& s = app.shop;
    if (!s.open) return;
    ++s.anim;
    ++s.mark;
    s.bob += s.bobDir; // (ComUty_CmdBack's DAT_eb29c: up to 5, back down)
    if (s.bob >= 5 || s.bob <= 0) s.bobDir = -s.bobDir;
    stepShopScroll(s);
    if (s.phase == App::Shop::Talk) {
        if (s.timer >= 0 && ++s.timer > 4) { // ShopPrc waits 4 frames, then starts it
            s.timer = -1;
            app.game.setFlag(4090, true);
            app.runner.swap(app.parkedRunner);
            if (!app.runner.startAt(app.events, app.events.returnEntry(4), 4, app.game, app.area, app.mapNumber)) {
                app.runner.swap(app.parkedRunner);
                s.phase = App::Shop::Main;
            } else {
                s.talkRunning = true;
                app.runnerWhat = "shop talk";
            }
        } else if (s.timer < 0 && !app.runner.running()) { // done: the shop's script again
            app.runner.swap(app.parkedRunner);
            s.talkRunning = false;
            app.game.setFlag(4090, false);
            s.phase = App::Shop::Main;
            s.anim = 0;
        }
    }
}

void shopChooseMain(App& app, int i) {
    App::Shop& s = app.shop;
    s.cursor = i;
    menuSound(app, se::kChoose);
    if (i == 0) { startShopTalk(app); return; }
    s.selling = i == 2;
    s.phase = App::Shop::Category;
    s.anim = 0;
}

// ShopPrc 4 / 5: Buy's Weapons, Armor and Jewel ((c | 4) != 4) and Sell's all but Items (c != 0)
// ask whose first (__Buy_CharChoiceReady: window 9, the pair's plates)
bool shopAsksMember(const App& app) {
    const App::Shop& s = app.shop;
    return s.selling ? s.category != 0 : (s.category | 4) != 4;
}

void shopChooseCategory(App& app, int c) {
    App::Shop& s = app.shop;
    s.category = c;
    s.listCursor = s.listTop = 0;
    menuSound(app, se::kChoose);
    s.anim = 0;
    if (shopAsksMember(app)) {
        s.phase = App::Shop::Member;
        return;
    }
    refreshShopList(app);
    s.phase = App::Shop::List;
}

// __Buy_CharChoiceProc: a plate of the pair (328, 176 + 56k) - that character's list
void shopChooseMember(App& app, int k) {
    App::Shop& s = app.shop;
    if (k < 0 || k >= campMembers(app)) return;
    s.member = k;
    s.listCursor = s.listTop = 0;
    refreshShopList(app);
    menuSound(app, se::kChoose);
    s.phase = App::Shop::List;
    s.anim = 0;
}

void shopChooseItem(App& app, int index) {
    App::Shop& s = app.shop;
    if (index < 0 || index >= int(s.list.size())) return;
    s.listCursor = index;
    const int item = s.list[size_t(index)];
    if (shopMaxCount(app, item) < 1) { menuSound(app, se::kBuzzer); return; } // can't afford / 99 already
    menuSound(app, se::kChoose);
    s.count = 1;
    s.confirm = 0;
    s.phase = App::Shop::Count;
    s.anim = 0;
    s.message = s.selling ? 57018 : 57017; // How many [item]s would you like to buy / sell?
}

// ◀ / ▶ in an equipment list: the pair's other character, and their list
void shopSwitchMember(App& app) {
    App::Shop& s = app.shop;
    if (campMembers(app) > 1) s.member ^= 1;
    s.listCursor = s.listTop = 0;
    refreshShopList(app);
    menuSound(app, se::kPage); // the Back bar's < > arrows
}

void shopBackToList(App& app) {
    App::Shop& s = app.shop;
    s.phase = App::Shop::List;
    s.message = 0;
    s.anim = 0;
    refreshShopList(app);
}

// Yes: pay, take the items (or the reverse); bought equipment the character can wear: "Equip [item]?"
void shopPay(App& app) {
    App::Shop& s = app.shop;
    const int item = s.list[size_t(s.listCursor)];
    const int total = shopUnitPrice(app, item) * s.count;
    if (s.selling) {
        int& have = app.game.items[item];
        have = std::max(0, have - s.count);
        cr::addOz(app.game, total);
    } else {
        app.game.items[item] = std::min(cr::kMaxItemCount, ownedCount(app, item) + s.count);
        cr::addOz(app.game, -total);
    }
    menuSound(app, se::kPaid);
    if (!s.selling) { // __menuu_ShopPrc 7 (3.1.0): SetSystemConfFlag(0x2b), _fld_checkAchievements (Android 1.1.6 checks after a sale too)
        setSystemConfFlag(app, cr::achievements::kBought);
        fieldCheckAchievements(app);
    }
    const cr::ItemParam* p = app.itemTable.item(item);
    if (!s.selling && p && cr::equipSlot(p->kind) >= 0 && (p->owners >> shopCharacter(app) & 1) &&
        app.game.party[size_t(shopCharacter(app))].equip[cr::equipSlot(p->kind)] != item) {
        s.phase = App::Shop::EquipAsk;
        s.message = 57076; // Equip [item]?
        s.confirm = 0;
        return;
    }
    shopBackToList(app);
}

// Yes to "Equip it?": worn by the board's character (both stay in the bag: __ChangeEquipItem)
void shopEquip(App& app) {
    App::Shop& s = app.shop;
    const int item = s.list[size_t(s.listCursor)];
    const cr::ItemParam* p = app.itemTable.item(item);
    if (p && cr::equipSlot(p->kind) >= 0 && ownedCount(app, item) > 0) {
        app.game.party[size_t(shopCharacter(app))].equip[cr::equipSlot(p->kind)] = item;
        menuSound(app, shopui::kSoundEquip);
    }
    shopBackToList(app);
}

void shopBack(App& app) {
    App::Shop& s = app.shop;
    menuSound(app, s.selling && s.phase == App::Shop::Count ? se::kChoose : se::kBack); // (ShopPrc: a sale's No is 0x93, a purchase's 0x94)
    switch (s.phase) {
    case App::Shop::Main: closeShop(app); break;
    case App::Shop::Category: s.phase = App::Shop::Main; s.anim = 0; break;
    case App::Shop::Member: s.phase = App::Shop::Category; s.anim = 0; break;
    case App::Shop::List: s.phase = shopAsksMember(app) ? App::Shop::Member : App::Shop::Category; s.anim = 0; break; // (ShopPrc 4 / 5's case 5)
    case App::Shop::Count: case App::Shop::EquipAsk: shopBackToList(app); break;
    default: break;
    }
}

std::string shopMessage(const App& app) {
    const App::Shop& s = app.shop;
    if (s.message <= 0 || s.list.empty()) return std::string();
    const int item = s.list[size_t(s.listCursor)];
    std::vector<std::vector<std::string>> pages;
    if (!app.systemText.message(
            s.message, textLanguage(app), nullptr, pages, nullptr,
            [&](int n) { return n == 9 ? itemName(app, item) : std::string(); }) ||
        pages.empty() || pages[0].empty())
        return std::string();
    return pages[0][0];
}

// ComUty_PictNumDraw: `digits` places `step` apart, right-aligned (left-aligned with `left`), in
// digit style `style` (picture 20 + style: 0 white, 2 outlined, 4 red / 5 cyan small)
void drawShopNumber(cr::MenuPainter& m, int value, float x, float y, int digits, float step, float alpha, int style,
                    bool left) {
    const std::string t = std::to_string(std::abs(value));
    const int n = int(t.size());
    const short* p = cr::kMenuPicture[20 + style];
    for (int k = 0; k < n; ++k)
        m.piece(x + float((left ? 0 : digits - n) + k) * step, y, p[0] + float(t[size_t(k)] - '0') * p[2], p[1], p[2], p[3],
                alpha);
}

// __ComUty_SubColorBar: a 140-wide bar, `type` 0 the black track, 1 HP red -> orange, 2 MP green
// -> orange, 3 experience blue -> lilac (0xc8800), filled to value / most
void drawShopBar(cr::MenuPainter& m, float x, float y, float value, float most, int type, float alpha) {
    static const float kColour[4][2][3] = {{{0, 0, 0}, {0, 0, 0}},
                                           {{0xdd / 255.f, 0x02 / 255.f, 0}, {1, 0xc3 / 255.f, 0x73 / 255.f}},
                                           {{0x07 / 255.f, 0xaf / 255.f, 0x73 / 255.f}, {1, 0xc3 / 255.f, 0x73 / 255.f}},
                                           {{0x02 / 255.f, 0x84 / 255.f, 0xf4 / 255.f}, {0xdc / 255.f, 0xcb / 255.f, 1}}};
    const float f = most > 0 ? std::clamp(value / most, 0.0f, 1.0f) : 0.0f;
    if (f <= 0) return;
    const float* a = kColour[type][0];
    const float* b = kColour[type][1];
    const float l[4] = {a[0], a[1], a[2], alpha};
    const float rr[4] = {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, alpha};
    m.quad(x + 4, y + 1, 140 * f, 3, l, rr);
}

// ComUty_HelpWindowDraw: picture 0x11 (the 336 x 68 striped panel) at (144, 8), the "HELP" tag (0x12) at
// (144, -4), up to three lines at x 156 from y 32 / 26 / 18 (one / two / three lines: DAT_d1698 + 8),
// 16 apart (Part 3 403 s: the panel at 450..1117 x 58 on the 1280 x 720 phone)
// (dx, dy: a group drawn in units shifted from tex2d's - the battle's right-hand group, +16 / +32)
void drawHelpWindow(App& app, cr::MenuPainter& m, int item, float a, float dx, float dy) {
    if (item <= 0) return;
    m.picture(17, 144 + dx, 8 + dy, a);
    m.picture(18, 144 + dx, -4 + dy, a);
    std::vector<std::vector<std::string>> help;
    if (!app.systemText.message(cr::itemHelpId(item), textLanguage(app), nullptr, help) || help.empty()) return;
    const int n = std::min(3, int(help[0].size()));
    static const int kTop[4] = {0, 24, 18, 10};
    // (MuComFontDisp style 1: black, edged white - Part 3 403 s, a dark 2 px core with light either side)
    for (int i = 0; i < n; ++i) m.text(help[0][size_t(i)], 156 + dx, float(kTop[n] + 8 + 16 * i) + dy, true, a, 13);
}

// ComUty_StateBoardDraw at (bx, by) for character c; `item` (if > 0) shows what wearing it changes;
// `record` draws the small record picture over the name (param_11: the Status page leaves it out);
// a seed `item` (3..6) marks its stat with `seeds` x 10 (param_9: the Camp's board after use)
void drawShopBoard(App& app, float bx, float by, int c, int item, float a, bool record = true, int seeds = 0) {
    using namespace shopui;
    cr::MenuPainter& m = app.menu;
    const cr::PartyMember& pm = app.game.party[size_t(c)];
    const float white[4] = {1, 1, 1, a};
    m.quad(bx + 12, by, 143, 16, white, white); // the name
    const std::string name = sysText(app, 58000 + charNameId(app, c)); // (ComUty_StateBoardChrName)
    m.textCentred(name, bx + 12 + (143 - m.textWidth(name, 14)) / 2, by + 8, true, a, 14);
    if (record) m.picture(148 + std::clamp(c, 0, cr::kCharacters - 1), bx - 8, by - 32, a); // ComUty_StateBoardDraw: 0x94 + record
    m.quad(bx + 12, by + 32, 143, 15, white, white); // LV
    m.picture(82, bx + 50, by + 36, a);
    drawShopNumber(m, pm.level, bx + 80, by + 33, 3, 12, a, 2, true);
    const int hpMax = cr::memberStat(app.game, c, cr::kHp, app.itemTable), mpMax = cr::memberStat(app.game, c, cr::kMp, app.itemTable);
    const int hp = pm.hp < 0 ? hpMax : pm.hp, mp = pm.mp < 0 ? mpMax : pm.mp;
    for (int k = 0; k < 2; ++k) { // __ComUty_FaceBoardHpMp
        const float x = bx - 1, y = by + 56 + 20 * k;
        m.picture(k ? 63 : 62, x, y, a);
        drawShopNumber(m, k ? mp : hp, x + 22, y - 4, 5, 12, a);
        m.picture(k ? 65 : 66, x + 82, y - 4, a);
        drawShopNumber(m, k ? mpMax : hpMax, x + 94, y - 4, 5, 12, a);
        drawShopBar(m, x + 16, y + 9, 1, 1, 0, a);
        drawShopBar(m, x + 15, y + 8, float(k ? mp : hp), float(k ? mpMax : hpMax), k ? 2 : 1, a);
    }
    { // __ComUty_FaceBoardNextExp
        const float x = bx - 2, y = by + 96;
        const int from = cr::expForLevel(pm.level), to = cr::expForLevel(pm.level + 1);
        m.picture(64, x, y, a);
        drawShopNumber(m, pm.level >= cr::levelCap(app.game.scenario) ? 0 : std::max(0, to - pm.exp), x + 82, y - 4, 6, 12, a);
        drawShopBar(m, x + 16, y + 9, 1, 1, 0, a);
        drawShopBar(m, x + 15, y + 8, float(pm.exp - from), float(std::max(1, to - from)), 3, a);
    }
    // EXP and the four stats: label boxes (83..87) at x + 12, their underline (88) at x + 64
    const int values[5] = {pm.exp, cr::memberStat(app.game, c, cr::kAttack, app.itemTable), cr::memberStat(app.game, c, cr::kDefense, app.itemTable),
                           cr::memberStat(app.game, c, cr::kSpeed, app.itemTable), cr::memberStat(app.game, c, cr::kLuck, app.itemTable)};
    for (int k = 0; k < 5; ++k) {
        const float y = by + 116 + 20 * k;
        m.picture(83 + k, bx + 12, y, a);
        m.picture(88, bx + 64, y + 11, a);
        if (k == 0) drawShopNumber(m, values[0], bx + 68, y - 2, 7, 12, a);
        else drawShopNumber(m, values[k], bx + 80, y - 2, 4, 12, a);
    }
    if (item <= 0) return;
    int delta[6] = {0, 0, 0, 0, 0, 0};
    if (item >= 3 && item <= 6) // a seed (kind 1, param_8 3..6): its stat by param_9 x 10 - the Camp's board after use
        delta[cr::kAttack + item - 3] = seeds * cr::kSeedPoints;
    else {
        const cr::ItemParam* p = app.itemTable.item(item);
        if (!p || cr::equipSlot(p->kind) < 0) return;
        for (int st = 0; st < 6; ++st)
            delta[st] = cr::memberStat(app.game, c, cr::Stat(st), app.itemTable, item) - cr::memberStat(app.game, c, cr::Stat(st), app.itemTable);
    }
    for (int st = 0; st < 6; ++st) { // the change: ▲ / ▼ at x + 158, sign and amount from x + 128 for the stats
        const int d = delta[st];
        if (d == 0) continue;
        const float y = by + kStatRowY[st];
        m.picture(d > 0 ? 93 : 94, bx + 158, y, a);
        if (st < 2) continue;
        const int ad = std::abs(d);
        m.picture(d > 0 ? 89 : 92, bx + 128 + (ad >= 100 ? 0 : ad >= 10 ? 7 : 14), y, a);
        drawShopNumber(m, ad, bx + 135, y, 3, 7, a, d > 0 ? 5 : 4);
    }
}

// ComUty_NumCountDraw at (348, 68): white < and > boxes fading out at the ends (alpha 0xb3; a
// tapped one cyan) around a dark box (black, 0xb3) with the count in big digits (picture 26 on:
// 28 x 28) - always two, the tens at 376 and the ones at 404 (Part 13 854 s: "06")
void drawNumCount(cr::MenuPainter& m, int count, int = 99, float dx = 0, float a = 1, bool minusHeld = false, bool plusHeld = false) {
    // (no dimming at the ends: both arrows always lit; _ComUty_NumCountDraw: a side held - +5 / +4 - turns
    // its box's gradient 0xEBFF4B, cyan)
    const float on[4] = {1, 1, 1, a * 0xB3 / 255.0f}, off[4] = {1, 1, 1, 0}, dark[4] = {0, 0, 0, a * 0xB3 / 255.0f};
    const float cyan[4] = {0x4B / 255.0f, 1, 0xEB / 255.0f, a * 0xB3 / 255.0f}, cyanOff[4] = {0x4B / 255.0f, 1, 0xEB / 255.0f, 0};
    const float* mOn = minusHeld ? cyan : on;
    const float* mOff = minusHeld ? cyanOff : off;
    const float* pOn = plusHeld ? cyan : on;
    const float* pOff = plusHeld ? cyanOff : off;
    m.quad(336 + dx, 60, 15, 31, mOff, mOn);
    m.quad(351 + dx, 60, 20, 31, mOn, mOn);
    m.quad(372 + dx, 60, 63, 31, dark, dark);
    m.quad(436 + dx, 60, 19, 31, pOn, pOn);
    m.quad(455 + dx, 60, 16, 31, pOn, pOff);
    m.picture(99, 348 + dx, 68, a);
    m.picture(100, 448 + dx, 68, a);
    const short* big = cr::kMenuPicture[26];
    const int n = std::clamp(count, 1, 99);
    m.piece(376 + dx, 62, big[0] + float(n / 10) * 28, big[1], 28, 28, a);
    m.piece(404 + dx, 62, big[0] + float(n % 10) * 28, big[1], 28, 28, a);
}

// ComUty_GuageBarDraw at (x, y), len long, for n rows scrolled `scroll` (<= 0) of `step` each: the
// track (window pieces (144, 184) top, (144, 200) 8-high middles, (144, 208) bottom at y + len - 8),
// ▲ / ▼ (pictures 0x4c / 0x4d, ComUty_PictMoveDraw: `bob` 0..4 outward) at y and y + len - 8, and
// the knob from y + 12: the whole track below three rows, else (len - 24) x 2 / n long (24 at
// least) at ((knob - len + 12) x scroll) / ((n - 2) x step), its caps (160, 196) / (160, 212)
void drawScrGauge(cr::MenuPainter& m, float x, float y, float len, int n, float scroll, float step, int bob, float a) {
    m.piece(x, y, 144, 184, 16, 16, a);
    for (float k = 16; k < len - 8; k += 8) m.piece(x, y + k, 144, 200, 16, std::min(8.0f, len - 8 - k), a); // (to the bottom piece)
    m.piece(x, y + len - 8, 144, 208, 16, 16, a);
    m.picture(76, x, y - float(bob), a);
    m.picture(77, x, y + len - 8 + float(bob), a);
    float knob = len - 16, pos = 0; // (below three rows: y + 12 .. y + 0xc4 + 8)
    if (n >= 3) {
        const float inner = len - 24;
        knob = std::max(24.0f, std::floor(inner * 2 / float(n)));
        pos = std::floor((knob - len + 12) * scroll / (float(n - 2) * step));
        pos = std::clamp(pos, 0.0f, inner - knob + 10);
    }
    const float top = y + 12 + pos;
    m.piece(x, top, 160, 196, 16, 12, a);
    for (float k = 12; k < knob - 8; k += 4) m.piece(x, top + k, 160, 208, 16, std::min(4.0f, knob - 8 - k), a);
    m.piece(x, top + knob - 8, 160, 212, 16, 8, a);
}

// A touch held on the shop (shopClick's units: cx the centre group, rx the right-hand one) inside a box
// (shift: the Vita's - its plates, list and Back 43 further right, vitaEdge43; the count and Yes / No not)
bool shopPressIn(const App& app, bool right, float x, float y, float w, float h, float shift) {
    if (!app.press.down || app.press.screen != kPressShop) return false;
    float lx, ly;
    app.renderer.toLogical(app.press.px, app.press.py, lx, ly);
    const float uiX = app.renderer.uiX(), rightX = app.renderer.phoneLayout() ? uiX : 2 * uiX;
    float cx = (lx - uiX) / 2, rx = (lx - rightX) / 2, my = ly / 2;
    phoneMenuPoint(app, cx, my);
    if (app.renderer.phoneLayout()) rx -= 16;
    const float px = (right ? rx : cx) - shift;
    return px >= x && px < x + w && my >= y && my < y + h;
}

void drawShop(App& app) {
    using namespace shopui;
    App::Shop& s = app.shop;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu phoneMenu(app, m); // the iPhone layout: its screen's menu coordinates
    if (s.phase == App::Shop::Talk) { // the shopkeeper's scene: ShopPrc 2 takes windows 1 - 5 off, "Shop" (window 0)
        r.setOrigin(vitaLeft(app));      // stays over it (Part 12 2142-2184 s: "Shop" over Piu-Piu's talk)
        m.picture(97, 8, 4, 1);
        r.setOrigin(r.uiX());
        return;
    }
    const float a = std::min(1.0f, float(s.anim + 1) / 6.0f), slide = (1 - a) * (1 - a);
    const float centre = r.uiX(), rightEdge = wideRight(app); // origins (the screen's edges inside the safe area)
    // The Vita's ShopPut (FUN_81069566): the left group - "Shop" (-35, 4), the labels at -31 / -3, pictures 15 / 16 at
    // (-43, 56) / (-19, 72), the shopkeeper at (-43, 64), the state board at -31 - 43 to the left; the plates at
    // 0x173, the list (its table: x 235, the gauge 499), HELP and Back 43 to the right; the Oz bar, the count and
    // Yes / No where 3.1.0 has them
    const float v = vitaEdge43(app), vitaL = vitaLeft(app), vitaR = vitaRight(app);
    const bool list = s.phase == App::Shop::List;
    const bool asking = s.phase == App::Shop::Count || s.phase == App::Shop::EquipAsk;
    const bool board = list && shopEquipment(app);
    const int item = (list || asking) && !s.list.empty() ? s.list[size_t(s.listCursor)] : 0;
    // The menu font (MuComFontFromIdRegist: 13 where MuComFontSet says so - Part 8 3474 s, Part 13
    // 854 s: "Buy", ">Items" and the board's lines 13 / 14 of the list's names)
    constexpr float kFont = 13;

    // Left: the shopkeeper (or the character's board), the heading and the section labels
    r.setOrigin(vitaL);
    if (s.keeper.id && !list && !board) // window 3: tex 10's (1, 3), 256 x 256 at (0, 64) (Part 4 1173.5 s)
        r.drawQuad(s.keeper, (16 + 0 - 240 * slide) * 2, (32 + 64) * 2, 256 * 2, 256 * 2, 1, 1, 1, 1);
    if (board) drawShopBoard(app, 12 - 200 * slide, 92, shopCharacter(app), s.selling ? 0 : item, a);
    m.picture(97, 8, 4, 1); // "Shop"
    if (s.phase == App::Shop::Main) { // window 2: font 0 (7, 0x3a) "Shopping" at (12, 40) over picture 15
        m.textCentred(sysText(app, 58058), 12 - 140 * slide, 47, false, a, kFont);
        m.picture(15, -140 * slide, 56, a);
    } else { // window 6: "Buy" / "Sell" (7, 0x5a / 0x5b); window 7: the category (7, 0xd39d0's)
        m.textCentred(sysText(app, s.selling ? 58091 : 58090), 12, 47, false, 1, kFont);
        m.picture(15, 0, 56, 1);
        if (list || asking) {
            static const int kCategory[5] = {58099, 58073, 58074, 58075, 58124}; // >Items, >Weapons, >Armor, >Jewel, Baby-Care Set
            m.textCentred(sysText(app, kCategory[std::clamp(s.category, 0, 4)]), 40, 65, false, 1, kFont);
            m.picture(16, 24, 72, 1);
        }
    }

    // Right: Oz, the plates, the list, the count and Yes / No
    r.setOrigin(vitaR);
    if (list && !s.list.empty()) drawHelpWindow(app, m, item, a); // HELP: the cursor's item
    r.setOrigin(rightEdge);
    m.picture(156, 316, 0, 1); // the Oz bar (ComUty_OzDraw: 0x9c)
    drawShopNumber(m, app.game.oz, 362, 0, 9, 12, 1);
    r.setOrigin(vitaR);
    if (s.phase == App::Shop::Main)
        for (int i = 0; i < 3; ++i)
            m.plate(328 + 240 * slide, 144 + 48.0f * float(i), kMainIcons[i], kMainWords[i],
                    (s.cursor == i && !app.pointerLast) || shopPressIn(app, true, 328, 144 + 48.0f * float(i), 136, 40, v), a, false);
    if (s.phase == App::Shop::Category)
        for (int i = 0; i < categories(app); ++i) {
            const float cy = (categories(app) == 5 ? 48 : 96) + 48.0f * float(i);
            m.plate(328 + 240 * slide, cy, kCategoryIcons[i], kCategoryWords[i],
                    (s.category == i && !app.pointerLast) || shopPressIn(app, true, 328, cy, 136, 40, v), a, false);
        }
    if (s.phase == App::Shop::Member) // __CharChoiceDraw: window 9, the pair at (328, 176 + 56k) (Part 8 3475 s)
        for (int k = 0; k < campMembers(app); ++k) {
            const int word = memberWord(app, campCharacter(app, k));
            m.plate(328 + 240 * slide, 176 + 56.0f * float(k), plateIcon(word), word,
                    (s.member == k && !app.pointerLast) || shopPressIn(app, true, 328, 176 + 56.0f * float(k), 136, 40, v), a, false);
        }
    if (list) {
        const int n = int(s.list.size());
        const float scroll = -s.scroll;
        if (s.list.empty())
            m.text(s.selling ? "Nothing to sell." : "Nothing for sale.", kRowX + 56, kRowY + 16, false, a, 15);
        m.setClip(kClipTop, kClipBottom);
        for (int index = 0; index < n; ++index) {
            const int it = s.list[size_t(index)];
            const float x = kRowX + 160 * slide, y = kRowY + scroll + kRowH * float(index);
            if (y + kRowH <= kClipTop || y >= kClipBottom) continue;
            // __menuu_ShopPrc's list: only the name's colour tells (MuComFontPlateDisp: 0 white, 2 grey
            // 0x808080, 3 0x00ffff - A B G R, yellow). Buying: 3 when the chosen member wears it
            // (MuScrList_IsNowEquip: gear and plates), then 2 over the purse, past 98, or a gene, skill
            // or OMU item (kinds 4 / 5 / 11) owned. Selling: 2 at a sell price of 0, 3 worn by the pair
            // (or scenario 1's third record) - Part 14 510.5 s: grey names, white prices
            const cr::ItemParam* ip = app.itemTable.item(it);
            const int owned = ownedCount(app, it);
            auto wears = [&](int r, bool plates) {
                if (r < 0 || r >= cr::kCharacters) return false;
                const cr::PartyMember& pm = app.game.party[size_t(r)];
                for (int e : pm.equip) if (e == it) return true;
                if (plates) for (int g : pm.genes) if (g == it) return true;
                return false;
            };
            const bool collected = ip && (ip->kind == 4 || ip->kind == 5 || ip->kind == 11) && owned > 0;
            int state = 0;
            if (!s.selling) {
                if (wears(campCharacter(app, s.member), true)) state = 3;
                if (shopUnitPrice(app, it) > app.game.oz || owned > 98 || collected) state = 2;
            } else {
                if (ip && cr::sellPrice(*ip) == 0) state = 2;
                if (cr::wornCount(app.game, it) > 0) state = 3;
            }
            m.picture(index == s.listCursor ? 9 : 8, x, y, a);
            if (ip && ip->icon >= 0)
                m.piece(x + 12, y + 8, float(ip->icon % 16) * 32, 248 + float(ip->icon / 16) * 32, 32, 32, a);
            plateText(m, itemName(app, it), x + 56, y + 12, state, a);
            drawShopNumber(m, shopUnitPrice(app, it), x + 158, y + 30, 6, 10, a);
            // (ComUty_ShopItemPlate: such a gene, skill or OMU item owned gets picture 0x66, "Sold Out!",
            // over its icon; its NEW (0x74) needs flag 0xffb, which only ever gets cleared)
            if (collected) m.picture(102, x + 8, y + 2, a);
            m.picture(101, x + 226, y + 34, a);
        }
        if (n > 2) { // the jump plates: ⬇ over the first rows, ⬆ under the last (MuScrList_Draw, type 0)
            if (s.listCursor < 2) m.picture(144, 280 + 160 * slide, 112 + scroll, a);
            if (s.listCursor >= n - 2) m.picture(145, 280 + 160 * slide, 208 + kRowH * float(n - 1) + scroll, a);
        }
        m.clearClip();
        drawScrGauge(m, kGaugeX, kGaugeY, kGaugeLen, n, scroll, kRowH, std::min(s.bob, 4), a);
    }
    r.setOrigin(rightEdge);
    if (asking && item > 0) {
        if (s.phase == App::Shop::Count) {
            m.picture(98, 312, 40, 1); // PRICE and the total
            drawShopNumber(m, shopUnitPrice(app, item) * s.count, 362, 40, 9, 12, 1);
            m.picture(73, 362, 49, 1);
            drawNumCount(m, s.count, 99, 0, 1, shopPressIn(app, true, 336, 60, 36, 31), shopPressIn(app, true, 436, 60, 36, 31));
        }
        // (a held plate takes the cursor: __ComUty_YesNoChoiceProc)
        const bool hy = shopPressIn(app, true, 336, 104, 136, 40), hn = shopPressIn(app, true, 336, 160, 136, 40);
        m.plate(336, 104, 0, 0, hy || (!hn && s.confirm == 0), 1, false); // Yes
        m.plate(336, 160, 1, 1, hn || (!hy && s.confirm == 1), 1, false); // No
    }

    // Centre: Back (with ◀ ▶ for equipment lists) and the message board
    r.setOrigin(centre);
    if (asking && item > 0) { // ComUty_MsgBoardDraw (window 0x10, (0, 220)): the lines at (24, 234), 16 apart, one style
        m.picture(12, 0, 220, 1);
        const short* mark = cr::kMenuPicture[13];
        m.piece(458, 298, float(mark[0] + 20 * pictAnimFrame(s.mark, 1, 4)), mark[1], mark[2], mark[3], 1);
        m.text(shopMessage(app), 24, 234, false, 1, kFont);
        std::vector<std::vector<std::string>> have;
        if (s.phase == App::Shop::Count &&
            app.systemText.message(57046, textLanguage(app), [&](int) { return ownedCount(app, item); }, have) && !have.empty() &&
            !have[0].empty())
            m.text(have[0][0], 24, 250, false, 1, kFont);
    } else if (list && shopEquipment(app)) { // (the Vita's at 235: 43 right)
        m.backButton(kBackX + v, kBackY, shopPressIn(app, false, kBackX + 51, kBackY, 130, 24, v) ? 1 : shopPressIn(app, false, kBackX + 181, kBackY, 51, 24, v) ? 2
                                     : shopPressIn(app, false, kBackX, kBackY, 51, 24, v) ? 3 : 0, s.bob, a); // ComUty_CmdBack 2: < Back >
    } else {
        m.backBar(kBackX + v, kBackY, shopPressIn(app, false, kBackX, kBackY, 232, 24, v), a); // (lit while held: DAT_000ec80c)
    }
    const char* help = s.phase == App::Shop::Count     ? "Left/Right: 1  Shift: 10  Up/Down: Yes / No  Enter  Esc"
                       : s.phase == App::Shop::EquipAsk ? "Up/Down  Enter  Esc: no"
                       : list && shopEquipment(app)     ? "Up/Down  Left/Right: character  Enter  Esc: back"
                                                        : "Up/Down  Enter  Esc: back";
    if (keyHints(app)) // (our key hints)
        m.text(help, 20, 372, false, a * 0.8f, 9);
    r.setOrigin(r.uiX());
}

void shopKey(App& app, SDL_Keycode key) {
    App::Shop& s = app.shop;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    const bool up = key == SDLK_UP || key == SDLK_W, down = key == SDLK_DOWN || key == SDLK_S;
    // (the Vita: L / R - PgUp / PgDn - switch the list's member and change the count: __menuu_ShopPrc
    // 4 -> 8..0xb, FUN_8105bea8; left / right too)
    const bool left = key == SDLK_LEFT || key == SDLK_A || key == SDLK_PAGEUP, right = key == SDLK_RIGHT || key == SDLK_D || key == SDLK_PAGEDOWN;
    switch (s.phase) {
    case App::Shop::Main:
        if (up || down) { s.cursor = (s.cursor + (up ? 2 : 1)) % 3; menuSound(app, se::kCursor); }
        else if (enter) shopChooseMain(app, s.cursor);
        else if (back) shopBack(app);
        break;
    case App::Shop::Category:
        if (up || down) { s.category = (s.category + (up ? shopui::categories(app) - 1 : 1)) % shopui::categories(app); menuSound(app, se::kCursor); }
        else if (enter) shopChooseCategory(app, s.category);
        else if (back) shopBack(app);
        break;
    case App::Shop::Member:
        if ((up || down) && campMembers(app) > 1) { s.member ^= 1; menuSound(app, se::kCursor); }
        else if (enter) shopChooseMember(app, s.member);
        else if (back) shopBack(app);
        break;
    case App::Shop::List: {
        const int n = int(s.list.size());
        if ((up || down) && n > 0) {
            s.listCursor = (s.listCursor + (up ? n - 1 : 1)) % n;
            shopFollowCursor(s);
            menuSound(app, se::kCursor);
        } else if ((left || right) && shopEquipment(app)) shopSwitchMember(app);
        else if (enter) shopChooseItem(app, s.listCursor);
        else if (back) shopBack(app);
        break;
    }
    case App::Shop::Count: {
        const int most = shopMaxCount(app, s.list[size_t(s.listCursor)]);
        const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 10 : 1;
        if (left || right) {
            const int was = s.count;
            s.count = std::clamp(s.count + (right ? step : -step), 1, std::max(1, most));
            menuSound(app, s.count != was ? se::kChoose : se::kBuzzer); // ComUty_NumCountProc
        } else if (up || down) { s.confirm ^= 1; menuSound(app, se::kCursor); }
        else if (enter && s.confirm == 0) shopPay(app);
        else if (enter || back) shopBack(app);
        break;
    }
    case App::Shop::EquipAsk:
        if (up || down || left || right) { s.confirm ^= 1; menuSound(app, se::kCursor); }
        else if (enter && s.confirm == 0) shopEquip(app);
        else if (enter || back) shopBack(app);
        break;
    default: break;
    }
}

void shopClick(App& app, float px, float py) {
    App::Shop& s = app.shop;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    const float uiX = app.renderer.uiX(), rightX = app.renderer.phoneLayout() ? uiX : 2 * uiX;
    float cx = (lx - uiX) / 2, rx = (lx - rightX) / 2, my = ly / 2; // centre / right-hand groups
    phoneMenuPoint(app, cx, my);
    if (app.renderer.phoneLayout()) rx -= 16;
    using shopui::kBackX;
    using shopui::kBackY;
    // (the Vita: the plates, the list and Back 43 to the right - FUN_81069566's 0x173, its list table's 235, Back 235;
    // the count and Yes / No not: rxYes)
    const float rxYes = rx;
    cx -= vitaEdge43(app), rx -= vitaEdge43(app);
    const bool onBack = my >= kBackY && my < kBackY + 24 && cx >= kBackX && cx < kBackX + 232;
    auto plateAt = [&](float x0, float y0, float step, int n) {
        for (int i = 0; i < n; ++i)
            if (rx >= x0 && rx < x0 + 136 && my >= y0 + step * i && my < y0 + 40 + step * i) return i;
        return -1;
    };
    auto yesNoAt = [&](float x0, float y0, float step, int n) {
        for (int i = 0; i < n; ++i)
            if (rxYes >= x0 && rxYes < x0 + 136 && my >= y0 + step * i && my < y0 + 40 + step * i) return i;
        return -1;
    };
    switch (s.phase) {
    case App::Shop::Main:
        if (const int i = plateAt(328, 144, 48, 3); i >= 0) shopChooseMain(app, i);
        else if (onBack) shopBack(app);
        break;
    case App::Shop::Category: {
        const int n = shopui::categories(app);
        if (const int i = plateAt(328, n == 5 ? 48 : 96, 48, n); i >= 0) shopChooseCategory(app, i);
        else if (onBack) shopBack(app);
        break;
    }
    case App::Shop::Member: // __Buy_CharChoiceProc: (328, 176 + 56k, 136, 40)
        if (const int i = plateAt(328, 176, 56, campMembers(app)); i >= 0) shopChooseMember(app, i);
        else if (onBack) shopBack(app);
        break;
    case App::Shop::List: {
        using namespace shopui;
        const int n = int(s.list.size());
        const float scroll = -s.scroll;
        const bool arrows = shopEquipment(app) && my >= kBackY && my < kBackY + 24;
        auto focus = [&](int to, bool jump) { // (a row brought to the line; the pills jump: SetListPosFromNo)
            to = std::clamp(to, 0, std::max(0, n - 1));
            if (to != s.listCursor) menuSound(app, se::kCursor);
            s.listCursor = to;
            shopFollowCursor(s);
            if (jump) s.scroll = kRowH * float(to);
        };
        if (n > 2 && s.listCursor < 2 && rx >= 280 && rx < 362 && my >= 112 + scroll && my < 140 + scroll &&
            my >= kClipTop && my < kClipBottom) // ⬇: to the list's end
            focus(n - 1, true);
        else if (n > 2 && s.listCursor >= n - 2 && rx >= 280 && rx < 362 && my >= 208 + kRowH * float(n - 1) + scroll &&
                 my < 236 + kRowH * float(n - 1) + scroll && my >= kClipTop && my < kClipBottom) // ⬆: to its top
            focus(0, true);
        else if (rx >= kRowX && rx < kRowX + 256 && my >= kClipTop && my < kClipBottom && my >= kRowY + scroll) {
            const int index = int((my - kRowY - scroll) / kRowH); // the row on the line is chosen, another comes to it
            if (index == s.listCursor) shopChooseItem(app, index);
            else if (index < n) focus(index, false);
        } else if (rx >= kGaugeX && rx < kGaugeX + 16 && my >= kGaugeY - 8 && my < kGaugeY + 16) focus(s.listCursor - 1, false);
        else if (rx >= kGaugeX && rx < kGaugeX + 16 && my >= kGaugeY + kGaugeLen - 16 && my < kGaugeY + kGaugeLen + 16)
            focus(s.listCursor + 1, false);
        else if (arrows && ((cx >= kBackX && cx < kBackX + 51) || (cx >= kBackX + 181 && cx < kBackX + 232)))
            shopSwitchMember(app);
        else if (onBack) shopBack(app);
        break;
    }
    case App::Shop::Count:
        // (the count's < >: stepped while held - pressCountStep -, not on the lift)
        if (my >= 60 && my < 91 && ((rxYes >= 336 && rxYes < 372) || (rxYes >= 436 && rxYes < 472))) {}
        else if (const int i = yesNoAt(336, 104, 56, 2); i == 0) shopPay(app);
        else if (i == 1) shopBack(app);
        break;
    case App::Shop::EquipAsk:
        if (const int i = yesNoAt(336, 104, 56, 2); i == 0) shopEquip(app);

        else if (i == 1) shopBack(app);
        break;
    default: break;
    }
}


bool keyHints(const App& app) { return app.showHud && !app.renderer.phoneLayout(); }
int plateIcon(int word) { return word >= 0 && word < cr::kNumPlateWords ? cr::kWordIcon[word] - 1 : -1; } // (kanIconNoTbl)
int campRoute(const App& app) { return std::clamp(app.game.scenario, 0, cr::kScenarios - 1); }
// ComUty_CharInfoInit: the boards are the scenario's pair; MuCom_ChkPartyNum: one while flag
// 0xfed + scenario holds the second back
int campCharacter(const App& app, int member) {
    const int c = app.game.pairs[size_t(campRoute(app))][size_t(std::clamp(member, 0, 1))];
    return c >= 0 && c < cr::kCharacters ? c : 0;
}
int campMembers(const App& app) { return app.game.flag(0xfed + campRoute(app)) ? 1 : 2; }

int memberWord(const App& app, int c) {
    static const int kEquipName[11] = {12, 13, 14, 15, 16, 17, 18, 19, 12, 13, 14};
    if (c == 0) return viegIsOlgar(app) ? 13 : 12;
    if (c == 1) return olgarIsDante(app) ? 15 : 13;
    return kEquipName[std::clamp(c, 0, 10)];
}

// ComUty_BattryAndRealTimeAndOzDraw: the clock (TIME, 71, at (363, 8); the digits at +47 / +59 /
// +83 / +95, the colon (75) blinking at +71; the line (74) under it) and the purse (OZ, 72, at
// (326, 24); nine digits from (362, 26); the line at (362, 36)) and the battery (__ComUty_BattryDraw at
// (335, 9): picture 0x76 full, +1 under 51%, +2 under 18% - the lower kept from 18 up to 23 when it came from
// under 18 -, +3 / +4 / +5 the same charging, +6 charged; SDL's power info, looked at once a second; a
// machine without a battery as charged)
int batteryPicture() {
    static Uint64 at = 0;
    static int picture = 0x76 + 6;
    static bool low = false; // (_gbMiddleFlg)
    if (at && SDL_GetTicks() - at < 1000) return picture;
    at = SDL_GetTicks();
    int pct = -1;
    const SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
    const int rest = pct < 0 ? 100 : pct;
    const bool charging = st == SDL_POWERSTATE_CHARGING;
    if (st == SDL_POWERSTATE_NO_BATTERY || st == SDL_POWERSTATE_UNKNOWN || st == SDL_POWERSTATE_ERROR) return picture = 0x76 + 6;
    if (rest >= 51) picture = 0x76 + (st == SDL_POWERSTATE_CHARGED ? 6 : charging ? 3 : 0);
    else if (rest >= 23) picture = 0x76 + (charging ? 4 : 1), low = false;
    else if (rest < 18) picture = 0x76 + (charging ? 5 : 2), low = true;
    else picture = 0x76 + (low ? (charging ? 5 : 2) : (charging ? 4 : 1));
    return picture;
}

void drawClockAndOz(App& app, cr::MenuPainter& m, float a) {
    // The Vita's (vita decomp 75033): no battery; the clock at (0x196, 8), OZ (picture 0x48) at (0x171, 0x18),
    // its digits at (0x195, 0x1a) and line (0x4a) at (0x195, 0x24) - 3.1.0's places 43 to the right
    const float vx = vitaEdge43(app);
    if (!app.renderer.vitaScreen()) m.picture(batteryPicture(), 335, 9, a);

    const time_t now = time(nullptr);
    struct tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    m.picture(71, (363 + vx), 8, a);
    const int digit[4] = {tm.tm_hour / 10, tm.tm_hour % 10, tm.tm_min / 10, tm.tm_min % 10};
    static const float kAt[4] = {0, 12, 36, 48};
    for (int i = 0; i < 4; ++i)
        if (i || digit[0]) drawShopNumber(m, digit[i], (363 + vx) + 47 + kAt[i], 6, 1, 12, a);
    // (__ComUty_RealTimeDraw: the colon while its own count, one a frame, is under 20 of 40 - a second on,
    // a second off at 20 Hz; Part 5 207-215 s: on 208.46-209.42, off to 210.44, on to 211.52, ...)
    if (SDL_GetTicks() / 1000 % 2 == 0) m.picture(75, (363 + vx) + 71, 6, a);
    m.picture(74, (363 + vx) + 36, 16, a);
    m.picture(72, 326 + vx, 24, a);
    drawShopNumber(m, app.game.oz, 362 + vx, 26, 9, 12, a);
    m.picture(74, 362 + vx, 36, a);
}

// ComUty_FaceBoardDraw at (x, y): the face (29 + n), the board (28) and the name (37 + n and the
// word 53 + n, English) at y + 24, LV (61) and the level at (24 / 48, 86), HP and MP
// (__ComUty_FaceBoardHpMp, style 2) at (96, 50 / 70), NEXT at (96, 90)
void drawFaceBoard(App& app, cr::MenuPainter& m, float x, float y, int c, int hp, int hpMax, int mp, int mpMax, float a,
                   const cr::GameState* gs) {
    c = std::clamp(c, 0, cr::kCharacters - 1);
    // (the Vita's four callers - TitlePut, CampMainPut, SaveLoadPut, MbResultPut - put the boards at y 0x19 +
    // 0x58k, 3.1.0's at 0x18 + 0x58k: vita decomp 89339, 68524, 79839, 63332)
    if (app.renderer.vitaScreen()) y += 1;
    const cr::PartyMember& pm = (gs ? *gs : app.game).party[size_t(c)];
    m.picture(28, x + 8, y + 24, a);
    m.picture(37 + c, x + 136, y + 24, a);
    if (languageSheet(app)) // 3.1.0 (FUN_000509fc): window_char's name, row c (Olgar's / Dante's for 0 / 1 when
        // the story has made them so) in the language's column - MuComPictTexNoDraw(0x30, table 8, c + 9 x language)
        m.imagePieceTo(app.charSheet, x + 96, y + 24, 92, 20, 92.0f * float(std::clamp(app.language, 0, 7)),
                       32.0f + 20.0f * float(boardNameIndex(app, c)), 92, 20, a);
    else m.picture((app.language == 0 ? 45 : 53) + boardNameIndex(app, c), x + 96, y + 24, a);
    m.picture(facePicture(app, c), x, y, a);
    const cr::GameState& g = gs ? *gs : app.game;
    if (g.scenario == 1 && c == g.leaders[1]) m.picture(164, x + 238, y + 26, a); // the leader's mark
    m.picture(61, x + 24, y + 86, a);
    // (__ComUty_FaceBoardFaceDraw: PictNumDraw(x + 0x30, y + 0x56, level, 3 places, 12, style 1) right-aligned
    // - Part 3 394 s: "LV  10", the digits at the field's right)
    drawShopNumber(m, pm.level, x + 48, y + 86, 3, 12, a, 1);
    for (int k = 0; k < 2; ++k) {
        const float hx = x + 96, hy = y + 50 + 20.0f * float(k);
        m.picture(k ? 63 : 62, hx, hy, a);
        drawShopNumber(m, k ? mp : hp, hx + 22, hy - 4, 5, 12, a, 2);
        m.picture(65, hx + 82, hy - 4, a);
        drawShopNumber(m, k ? mpMax : hpMax, hx + 94, hy - 4, 5, 12, a, 2);
        drawShopBar(m, hx + 16, hy + 9, 1, 1, 0, a);
        drawShopBar(m, hx + 15, hy + 8, float(k ? mp : hp), float(k ? mpMax : hpMax), k ? 2 : 1, a);
    }
    const float nx = x + 96, ny = y + 90;
    const int from = cr::expForLevel(pm.level), to = cr::expForLevel(pm.level + 1);
    m.picture(64, nx, ny, a);
    drawShopNumber(m, pm.level >= cr::levelCap(app.game.scenario) ? 0 : std::max(0, to - pm.exp), nx + 82, ny - 4, 6, 12, a, 2);
    drawShopBar(m, nx + 16, ny + 9, 1, 1, 0, a);
    drawShopBar(m, nx + 15, ny + 8, float(pm.exp - from), float(std::max(1, to - from)), 3, a);
}

// The member's HP (full outside battles) and MP as the boards show them
void memberHpMp(const App& app, int c, int& hp, int& hpMax, int& mp, int& mpMax, const cr::GameState* gs) {
    const cr::GameState& g = gs ? *gs : app.game;
    const cr::PartyMember& pm = g.party[size_t(c)];
    hpMax = cr::memberStat(g, c, cr::kHp, app.itemTable);
    mpMax = cr::memberStat(g, c, cr::kMp, app.itemTable);
    hp = hpMax;
    mp = pm.mp < 0 ? mpMax : pm.mp; // (stored, unclamped - above the most after gear came off)
}

// MuComMakeItemHaveData: what the bag holds of a category (the pair's own weapons and armor)
// MuComMakeItemHaveData: when the second member is held back (flag 0xfed + scenario), the one
// jewel they wear stays out of the bag's list (a second copy of it is listed)
int campAbsentJewel(const App& app) {
    return campMembers(app) > 1 ? 0 : app.game.party[size_t(campCharacter(app, 1))].equip[cr::kJewel];
}

std::vector<int> campItems(const App& app, int category) {
    std::vector<int> out;
    // make_item_list's ranges (Omega); MuComMakeItemHaveData keeps weapons / armor of their kind
    int from = 1, to = 60;
    const int kind = campui::kCategoryKind[std::clamp(category, 0, campui::kCategories - 1)];
    switch (kind) {
    case 3: from = 61, to = 80; break;
    case 6: case 7: from = 201, to = 368; break;
    case 8: from = 369, to = 418; break;
    case 10: from = 474, to = 520; break;
    case 11: from = 521, to = 550; break;
    case 12: from = 551, to = 630; break;
    default: break;
    }
    const int absentJewel = kind == 8 ? campAbsentJewel(app) : 0;
    for (int n = from; n <= to; ++n) {
        const int count = ownedCount(app, n);
        if (count <= 0) continue;
        const cr::ItemParam* p = app.itemTable.item(n);
        if (category == 2 && (!p || p->kind != cr::kKindWeapon)) continue;
        if (category == 3 && (!p || p->kind != cr::kKindArmor)) continue;
        if (n == absentJewel && count == 1) continue;
        out.push_back(n);
    }
    return out;
}

// The plates in the bag, 474.. in id order: the Genes page's list (MuComMakeGenePlate(10, 1), no
// Remove) - a plate without a known entry is listed but refused (MuScrList_GetChoiceListNo 0xb)
std::vector<int> campGeneList(const App& app) {
    std::vector<int> out;
    for (int p = cr::kFirstPlate; p < cr::kFirstPlate + cr::kPlates; ++p)
        if (ownedCount(app, p) > 0) out.push_back(p);
    return out;
}
// The plate's known entries (make_item_list 0xf: its table's entries the bag counts)
std::vector<int> campKnownEntries(const App& app, int plate) {
    std::vector<int> out;
    if (!cr::isPlate(plate)) return out;
    for (int e : cr::plateContents(plate))
        if (e > 0 && ownedCount(app, e) > 0) out.push_back(e);
    return out;
}
// MuCom_GetCommonSkill: a known entry 89..113 / 139..186 of a plate the bag holds
// (GetPlateNumFromSkill) - until there is one the empty plate slots 2-4 stay shut
bool campCommonSkill(const App& app) {
    for (int p = cr::kFirstPlate; p < cr::kFirstPlate + cr::kPlates; ++p) {
        if (ownedCount(app, p) <= 0) continue;
        for (int e : cr::plateContents(p))
            if (((e >= 0x59 && e <= 0x71) || (e >= 0x8b && e <= 0xba)) && ownedCount(app, e) > 0) return true;
    }
    return false;
}
bool campWears(const App& app, int character, int plate) { // MuScrList_IsNowEquip (a plate)
    const cr::PartyMember& pm = app.game.party[size_t(character)];
    return plate > 0 && std::find(std::begin(pm.genes), std::end(pm.genes), plate) != std::end(pm.genes);
}
// MuScrList_GetChoiceListNo -4 for the slot list (SetIdInfo kind 4): an empty plate slot 2-4
// (0x1c8..0x1ca) before a common skill is known, the Human Genes 474..476 in the first scenario,
// and the empty jewel slot (0x1c6) while no jewel is free (__IsJewelChoiceMainCheck)
bool campSlotLocked(const App& app, int member, int slot) {
    const cr::PartyMember& pm = app.game.party[size_t(campCharacter(app, member))];
    if (slot >= 4 && pm.genes[slot - 3] <= 0) return !campCommonSkill(app);
    if (slot == 3 && campRoute(app) == 0 && pm.genes[0] >= 474 && pm.genes[0] <= 476) return true;
    if (slot == cr::kJewel && pm.equip[cr::kJewel] <= 0) {
        int jewels = 0;
        for (int n = 369; n <= 418; ++n) jewels += ownedCount(app, n);
        if (jewels > 1) return false;
        return jewels == 0 || app.game.party[size_t(campCharacter(app, member ^ 1))].equip[cr::kJewel] > 0;
    }
    return false;
}
// ... -4 for a plate row (kinds 6 / 0xb): none of its entries known
bool campPlateLocked(const App& app, int item) { return cr::isPlate(item) && cr::plateLearned(app.game, item) == 0; }

// What can go in member c's slot. __ChangeEquipItem's Remove (item 200) comes last: for a jewel
// or a plate 2-4 while the slot is worn (MuComMakeItemData / MuComMakeGenePlate's flag), for the
// Human Genes always (__MuCampScrListComInit(0xd, 0))
std::vector<int> campEquipList(const App& app, int c, int slot) {
    std::vector<int> out;
    const cr::PartyMember& pm = app.game.party[size_t(c)];
    if (slot >= 3) { // MuComMakeGenePlate: the Human Genes slot lists the bag's 474..480 (0xd), the
        // others 481.. with a known entry (0xe); a plate the other member wears stays listed
        const int from = slot == 3 ? cr::kFirstPlate : cr::kFirstPlate + 7, to = slot == 3 ? cr::kFirstPlate + 7 : cr::kFirstPlate + cr::kPlates;
        for (int p = from; p < to; ++p) {
            if (ownedCount(app, p) <= 0) continue;
            if (slot > 3 && cr::plateLearned(app.game, p) == 0) continue;
            out.push_back(p);
        }
        if (slot == 3 || pm.genes[slot - 3] > 0) out.push_back(campui::kRemove);
        return out;
    }
    // _MuComMakeItemData (6, 7, 8) on make_item_list: what the bag holds of the kind - a weapon or armor of the
    // member's (+0x13's bit), any jewel - whoever wears it (a jewel with no copy free is listed grey and
    // refused: campJewelFree); only the held-back member's jewel stays out while it is its one copy
    const int kind = slot == cr::kWeapon ? cr::kKindWeapon : slot == cr::kArmor ? cr::kKindArmor : cr::kKindJewel;
    const int absentJewel = slot == cr::kJewel ? campAbsentJewel(app) : 0;
    for (int n = 201; n <= 418; ++n) { // (Omega: weapons and armor 201..368, jewels 369..418)
        const cr::ItemParam* p = app.itemTable.item(n);
        if (!p || p->kind != kind || ownedCount(app, n) <= 0) continue;
        if (kind != cr::kKindJewel && !(p->owners & (1 << c))) continue;
        if (n == absentJewel && ownedCount(app, n) == 1) continue;
        out.push_back(n);
    }
    if (slot == cr::kJewel && pm.equip[cr::kJewel] > 0) out.push_back(campui::kRemove);
    return out;
}

// __IsJewelChoiceSubCheck: a jewel some copy of which is free of the others - the pair's other member (held back
// too) and, in the second scenario, the record its partner choice left out (MuCom_OtherChar_IsNowEquip: the pair
// table's +0x84719, its second, 1 -> record 2, 2 -> 1; its weapon, armor and jewel) -; worn equipment stays in the
// bag. Else MuScrList_Draw type 5 greys it and the choice is refused. Weapons and armor aren't checked
bool campJewelFree(const App& app, int member, int item) {
    const cr::ItemParam* p = app.itemTable.item(item);
    if (!p || p->kind != cr::kKindJewel) return true;
    int taken = app.game.party[size_t(campCharacter(app, member ^ 1))].equip[cr::kJewel] == item;
    if (campRoute(app) == 1) {
        const int second = app.game.pairs[1][1], left = second == 1 ? 2 : second == 2 ? 1 : -1;
        if (left >= 0) {
            const cr::PartyMember& lm = app.game.party[size_t(left)];
            taken += std::find(std::begin(lm.equip), std::end(lm.equip), item) != std::end(lm.equip);
        }
    }
    return ownedCount(app, item) - taken > 0;
}

// MuCom_CheckExitItem = use_item(0, 1): the Emergency Exit works while the scenario's exit flag
// (0x43e, the second scenario 0x632) is clear and the area (gac+0x8a972) isn't 1, 9 or 10
bool campExitUsable(const App& app) {
    if (app.game.flag(app.game.scenario == 1 ? 0x632 : 0x43e)) return false;
    return app.area != 1 && app.area != 9 && app.area != 10;
}
// __ChkCampItem (MuScrList_GetChoiceListNo kind 10; table 0xd2b90): the Emergency Exit (1) while it
// works, the seeds 3..6 and the MP items 24..27. (It passes 419..473 as well - CampMainPrc 2's
// event path, EvrStart 3 with var 0 = item - 418 - but those are the manual's (make_item_list 9),
// which none of the Camp's eight categories lists)
bool campUsable(const App& app, int item) {
    if (item == 1) return campExitUsable(app);
    return (item >= 3 && item <= 6) || (item >= 24 && item <= 27);
}

[[maybe_unused]] void campMessage(App& app, const std::string& text) {
    app.camp.message = text;
    app.camp.messageFrames = text.empty() ? 0 : 90;
}

// A page's windows (0 out .. 1 in). The windows' moves - ComUty_WinDispOn / Off - are linear over four frames
// (Part 3 393.52-393.68 s, the face boards 180, 120, 60, 0 units short of their place a frame apart, fading in
// with them). A page's windows come after the main page's have gone: CampMain 6 takes the main page's off, 7
// and 8 set the page up, and the page's first state puts its own on - the Items list (CampMainPrc 0's 1) three
// frames after the main page's went, the Genes list (2's 2, after its pictures load) four; Equip's and Status's
// member plates (window 8) come in with CampMain 6 itself. Part 13 1171.82-1172.10 s, a frame at a time: the
// boards and plates a quarter, half, three quarters out, then the faint last of them with the Items windows a
// quarter in, half, three quarters, in. Back: the page's windows go first (campPageOutFrames)
float campEase(const App& app) {
    const App::Camp& c = app.camp;
    if (c.pageOut) return 1 - std::min(1.0f, float(c.pageOut - 1) / 4.0f);
    const int on = c.page == App::Camp::Items ? 3 : c.page == App::Camp::Genes ? 4 : 0;
    return std::clamp(float(c.anim - on) / 4.0f, 0.0f, 1.0f);
}
// Back from a page: the frame the main page's windows go on, counted from the page's going off - the Items
// list's 0xf - 0x11 (WinDispOff, five frames' wait, done), Equip's 0x20 - 0x22 and Status's 0xd - 0xf the same,
// then CampMain 9 a frame on; the Genes list's 7 - 9 (no wait). Part 13 1182.02-1182.62 s (Items): the list
// out over four frames, three or four with nothing, then the main page in
int campPageOutFrames(const App& app) { return app.camp.page == App::Camp::Genes ? 3 : 7; }
// The Items page between its states (CampMainPrc 0): the state left takes its windows off (from its frame, or the
// next where it is the Yes / No's 7 or the board's 10), the next puts its own on a frame after - the Yes / No
// (5) after the list (2) or the member choice (4), the list (1) after the Yes / No's No (7) or the board (10),
// the board (8) on the same frame as the Yes / No go. Part 13 2945.5-2945.8 s: the HELP gone at once, the list
// out to the right over four frames, the Yes / No in from the right and the question's board up from below
float campPhaseEase(const App& app, App::Camp::Phase ph) {
    const App::Camp& c = app.camp;
    const bool fromAsk = c.prevPhase == App::Camp::Ask || c.prevPhase == App::Camp::Result;
    const int offAt = fromAsk ? 1 : 0;
    const int onAt = c.prevPhase == App::Camp::Ask && c.phase == App::Camp::Result ? 1 : offAt + 1;
    if (ph == c.phase) return c.phaseT >= 100 ? 1.0f : std::clamp(float(c.phaseT - onAt) / 4.0f, 0.0f, 1.0f);
    if (ph == c.prevPhase && c.phaseT < 100) return 1 - std::clamp(float(c.phaseT - offAt) / 4.0f, 0.0f, 1.0f);
    return 0;
}
// Equip's windows between its states (__menuu_CampMainPrc 1): each goes off (ComUty_WinDispOff / MuScrList_DispOff:
// whole, then out over four frames to its side) or on (in from its side over four) on the state's frame - t counts
// frames since the input that changed the state. The face boards (window 2) to -240, the member plates (8) to
// +360, the title (9) and the section line (0x10) to -140, the state board (10) and the gene board (0xb) to -240,
// the slot list (0xe), a gear / plate list (0xf) and a plate's entries (0x13) to +-480:
//   a member chosen (1 -> 3): 2, 8 off; five frames on (3's count) CategoryChoiceInit puts 9, 10, 0xe on.
//     Back (4 -> 5 -> 6): 9, 10 and 0xe off a frame on (0xe to +480), 2 and 8 on five frames after that
//   a slot chosen (4): 0xe off to -480; a frame on 0x10 and the list (from +480) on (0xb / 0xf / 0x13), and for a
//     gene slot (0x17) 10 off, the gene board on a frame later (0x18, once its pictures are read)
//   a gear chosen (0xc -> 0xd): the list off to -480, a frame on 0x10 off and 0xe on from +480; Back (-> 0xe): the
//     list off to +480 and 0xe on from -480 at once, 0x10 off a frame on
//   a plate list's Remove (0x19, 200): all at once - the list off to +480, 0x10 off, 0xe on from +480, 10 on, 0xb
//     off; its Back (-> 0x1b): the list to +480, 0xe from -480, then 0x10, 10, 0xb a frame on
//   a plate chosen (-> 0x1a): the list off to -480; a frame on 0x10 off and the entries on from +480; 0x10 on
//     again (">Gene Details") the frame after (0x1c). The entries' Equip (0x1f): 0x10, 0xb off and the entries off
//     to -480 a frame on, then five frames (0x1e) and 10 on, 0xe from +480. Their Back: 0x10 again, the entries
//     off to +480 and the plate list on from -480 at once
//   L / R (7 -> 10): 9, 10 (+-240) and 0xe (+-480) off, the other member's on from the other side five frames on.
// Part 3 398.2-398.4 s (a gene slot chosen): the slot list and the state board going left, the plate list and
// HELP in from the right, the gene board from the left
struct CampWin { float e = 0, dx = 0; };
struct EquipWins {
    CampWin faces, plates, title, board, slots, list, gene, entries, section;
    bool sectionPrev = false; // (the line going: the state left's words)
    bool swapOld = false;     // (L / R: the member left's board and slots going)
};
EquipWins equipWins(const App& app) {
    using P = App::Camp::Phase;
    const App::Camp& c = app.camp;
    EquipWins w;
    int t = c.phaseT;
    auto shown = [](CampWin& x) { x.e = 1, x.dx = 0; };
    auto on = [&t](CampWin& x, int f, float from) {
        const float e = std::clamp(float(t - f) / 4.0f, 0.0f, 1.0f);
        x.e = e, x.dx = from * (1 - e);
    };
    auto off = [&t](CampWin& x, int f, float to) {
        const float e = 1 - std::clamp(float(t - f) / 4.0f, 0.0f, 1.0f);
        x.e = e, x.dx = to * (1 - e);
    };
    const bool gene = c.slot >= 3;
    auto settled = [&](P ph) {
        if (ph == P::Member) { shown(w.faces), shown(w.plates); return; }
        shown(w.title);
        if (ph == P::Slot) { shown(w.board), shown(w.slots); return; }
        shown(w.section);
        if (ph == P::List) { shown(w.list), shown(gene ? w.gene : w.board); return; }
        shown(w.gene), shown(w.entries);
    };
    const P from = c.prevPhase, to = c.phase;
    if (t >= 100 || from == to) settled(to);
    else if (from == P::Member && to == P::Slot) {
        off(w.faces, 0, -240), off(w.plates, 0, 360);
        on(w.title, 5, -140), on(w.board, 5, -240), on(w.slots, 5, 480);
    } else if (from == P::Slot && to == P::Member) {
        off(w.title, 1, -140), off(w.board, 1, -240), off(w.slots, 1, 480);
        on(w.faces, 6, -240), on(w.plates, 6, 360);
    } else if (from == P::Slot && to == P::List) {
        shown(w.title);
        off(w.slots, 0, -480), on(w.section, 1, -140), on(w.list, 1, 480);
        if (gene) off(w.board, 1, -240), on(w.gene, 2, -240);
        else shown(w.board);
    } else if (from == P::List && to == P::Slot) {
        shown(w.title);
        w.sectionPrev = true;
        if (!gene) {
            shown(w.board);
            if (c.equipHow == 1) off(w.list, 0, 480), on(w.slots, 0, -480), off(w.section, 1, -140);
            else off(w.list, 0, -480), off(w.section, 1, -140), on(w.slots, 1, 480);
        } else if (c.equipHow == 2) {
            off(w.list, 0, 480), off(w.section, 0, -140), on(w.slots, 0, 480), on(w.board, 0, -240), off(w.gene, 0, -240);
        } else {
            off(w.list, 0, 480), on(w.slots, 0, -480);
            off(w.section, 1, -140), on(w.board, 1, -240), off(w.gene, 1, -240);
        }
    } else if (from == P::List && to == P::Entries) {
        shown(w.title), shown(w.gene);
        off(w.list, 0, -480), on(w.entries, 1, 480);
        if (t < 2) off(w.section, 1, -140), w.sectionPrev = true;
        else on(w.section, 2, -140);
    } else if (from == P::Entries && to == P::Slot) {
        shown(w.title);
        w.sectionPrev = true;
        off(w.section, 1, -140), off(w.gene, 1, -240), off(w.entries, 1, -480);
        on(w.board, 6, -240), on(w.slots, 6, 480);
    } else if (from == P::Entries && to == P::List) {
        shown(w.title), shown(w.gene);
        on(w.section, 0, -140), off(w.entries, 0, 480), on(w.list, 0, -480);
    } else settled(to);
    if (to == P::Slot && c.swapT < 100 && (t >= 100 || from == to)) { // L / R
        t = c.swapT;
        const float d = float(c.swapDir);
        w.swapOld = t < 5;
        if (w.swapOld) off(w.title, 0, -140), off(w.board, 0, 240 * d), off(w.slots, 0, 480 * d);
        else on(w.title, 5, -140), on(w.board, 5, -240 * d), on(w.slots, 5, -480 * d);
    }
    return w;
}
void campTrackPhase(App::Camp& c) {
    if (c.page != c.shownPage) c.shownPage = c.page, c.shownPhase = c.prevPhase = c.phase, c.phaseT = 100;
    else if (c.phase != c.shownPhase) c.prevPhase = c.shownPhase, c.shownPhase = c.phase, c.phaseT = 0;
}
float campListX(const App& app) {
    // (the Items list's window comes in from 480 to the right - MuScrList_Start(.., 0): WinDispOn(.., 0x1e0);
    // Part 13 1172.02-1172.10 s: 237, 121, 0 units short on successive frames)
    return campui::kRowX + (app.camp.page == App::Camp::Items ? 480 * (1 - campEase(app)) : 0);
}
bool campSlotList(const App& app) { return app.camp.page == App::Camp::Equip && app.camp.phase == App::Camp::Slot; }
bool campStatusList(const App& app) { // the Status details' equipment or skill list
    return app.camp.page == App::Camp::Status && app.camp.phase == App::Camp::Slot && app.camp.statusView != 0;
}
bool campScrollable(const App& app) {
    return campSlotList(app) || campStatusList(app) ||
           ((app.camp.page == App::Camp::Items || app.camp.page == App::Camp::Equip || app.camp.page == App::Camp::Genes) &&
            (app.camp.phase == App::Camp::List || app.camp.phase == App::Camp::Entries));
}
int campScrollCount(const App& app) { return campSlotList(app) ? campui::kEquipSlots : int(app.camp.list.size()); }
int campScrollTop(const App& app) { return campSlotList(app) ? app.camp.slotTop : app.camp.listTop; }
// MuScrList (mode 0): the rows move under a fixed line - the focus row is the one on it - so a scroll
// is a move of the focus (keys, the wheel, a drag, the gauge's arrows)
void campScrollBy(App& app, int rows) {
    if (!campScrollable(app)) return;
    auto& c = app.camp;
    int& cursor = campSlotList(app) ? c.slot : c.listCursor;
    const int n = campScrollCount(app), before = cursor;
    cursor = std::clamp(cursor + rows, 0, std::max(0, n - 1));
    int& top = campSlotList(app) ? c.slotTop : c.listTop; // (kept for the Status lists' memory)
    top = cursor;
    if (before != cursor) menuSound(app, se::kCursor);
}
int campFocus(const App& app) { return campSlotList(app) ? app.camp.slot : app.camp.listCursor; }
// Which list is up: a new one (a page, a phase, a category, a member, a Status view) starts settled
int campScrollKey(const App& app) {
    const App::Camp& c = app.camp;
    return int(c.page) * 4096 + int(c.phase) * 256 + c.statusView * 64 + c.category * 8 + c.member;
}
// The rows' offset now: 48 x the focus row, or on the way to it (MuScrList_Proc: eb6c0 steps d2a74 = 8 a
// frame toward eb6c8; a jump - the ⬇ / ⬆ pills, SetListPosFromNo - sets both at once)
float campScrollPx(const App& app) {
    return app.camp.scrollKey == campScrollKey(app) ? app.camp.scroll : campui::kRowH * float(campFocus(app));
}
void stepCampScroll(App& app) {
    App::Camp& c = app.camp;
    const int key = campScrollKey(app);
    const float to = campui::kRowH * float(campFocus(app));
    if (c.scrollKey != key || std::fabs(to - c.scroll) > campui::kRowH) c.scrollKey = key, c.scroll = to;
    else if (c.scroll < to) c.scroll = std::min(to, c.scroll + 8);
    else c.scroll = std::max(to, c.scroll - 8);
}

int campEntries(const App&) { return campui::kEntries; }
float campBackY(const App&) { return campui::kPhoneBackY; } // (Omega's iPad: the iPhone layout doubled)


void openCamp(App& app) {
    App::Camp& c = app.camp;
    app.campOpen = true;
    app.campPointer = App::CampPointer();
    auto art = std::move(c.plateArt); // the plates' pictures stay loaded
    auto portraits = std::move(c.statusArt);
    c = App::Camp();
    c.plateArt = std::move(art);
    c.statusArt = std::move(portraits);
    c.cursor = 0; // (Omega: none lit until touched - the keys start on Items)
    // CampMainSet: the first time a gene plate 0x1e1..0x1f8 is in the bag (flag 0x38), message
    // 56010 comes before the Camp's windows (CampMain 0-2, then 3)
    if (!app.game.flag(0x38))
        for (int p = 0x1e1; p < 0x1f9; ++p)
            if (ownedCount(app, p) > 0) {
                app.game.setFlag(0x38, true);
                campNotice(app, 56010);
                break;
            }
}

// The Camp's once-only messages (MsgUty_WinOpen / SetMsg, block 5): the field's message window
// over the Camp, its windows and the mask down until the message is read
void campNotice(App& app, int id) {
    app.camp.notice = id;
    showMessages(app, {id});
}

// The Status details' lists (CampMainPrc 3's state 2): the seven the member wears
// (__MuCampScrListEquipInit(.., 0xc, 0): an empty jewel slot 0x1c6, empty plate slots 0x1c7..),
// and every known entry of the four plates worn, each once
std::vector<int> campStatusItems(const App& app, int view) {
    std::vector<int> out;
    const cr::PartyMember& pm = app.game.party[size_t(campCharacter(app, app.camp.member))];
    if (view == 1) {
        for (int slot = 0; slot < campui::kEquipSlots; ++slot) {
            const int worn = slot < 3 ? pm.equip[slot] : pm.genes[slot - 3];
            out.push_back(worn > 0 ? worn : slot == 2 ? 0x1c6 : slot >= 3 ? 0x1c7 + (slot - 3) : 0);
        }
    } else if (view == 2) {
        for (int g : pm.genes) {
            if (g <= 0) continue;
            for (int e : cr::plateContents(g))
                if (e > 0 && ownedCount(app, e) > 0 && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        }
    }
    return out;
}
// -2 (the right arrow, a swipe to the left) board -> equipment -> skills -> board; -3 the other way
// (0x96 either way; the lists keep where they were)
void campStatusTurn(App& app, int dir) {
    App::Camp& c = app.camp;
    menuSound(app, se::kPage);
    c.viewTop[c.statusView] = c.listCursor;
    c.statusView = (c.statusView + dir + 3) % 3;
    c.list = campStatusItems(app, c.statusView);
    c.listCursor = c.listTop = std::clamp(c.viewTop[c.statusView], 0, std::max(0, int(c.list.size()) - 1));
}

void campRefreshList(App& app) {
    App::Camp& c = app.camp;
    if (c.phase == App::Camp::Entries) c.list = campKnownEntries(app, c.plate);
    else if (c.page == App::Camp::Items) c.list = campItems(app, c.category);
    else if (c.page == App::Camp::Equip) c.list = campEquipList(app, campCharacter(app, c.member), c.slot);
    else if (c.page == App::Camp::Genes) c.list = campGeneList(app);
    c.listCursor = std::clamp(c.listCursor, 0, std::max(0, int(c.list.size()) - 1));
    c.listTop = std::clamp(c.listTop, std::max(0, c.listCursor - campui::kRows + 1), c.listCursor);
}

// Items: the next category that holds anything, `dir` from the current (the current one too if 0)
void campNextCategory(App& app, int dir) {
    App::Camp& c = app.camp;
    const int n = campui::kCategories;
    for (int k = dir ? 1 : 0; k <= n; ++k) {
        const int cat = ((c.category + dir * k) % n + n) % n;
        if (!campItems(app, cat).empty() || k == n) {
            c.category = cat;
            break;
        }
    }
    c.listCursor = c.listTop = 0;
    campRefreshList(app);
}

void campOpenEntries(App& app, int plate) {
    App::Camp& c = app.camp;
    c.plate = plate;
    c.plateCursor = c.listCursor, c.plateTop = c.listTop;
    c.goneList = c.list, c.goneCursor = c.listCursor; // (the plate list, going as the entries come)
    c.phase = App::Camp::Entries;
    c.listCursor = c.listTop = 0;
    campRefreshList(app);
}
void campCloseEntries(App& app) {
    App::Camp& c = app.camp;
    c.plate = 0;
    c.goneList = c.list, c.goneCursor = c.listCursor; // (the entries, going as the plate list comes)
    c.phase = App::Camp::List;
    c.listCursor = c.plateCursor, c.listTop = c.plateTop;
    campRefreshList(app);
}

void campOpenPage(App& app, int i, bool sound) {
    App::Camp& c = app.camp;
    c.cursor = i;
    if (i < 5 && !campui::kReady[i]) {
        menuSound(app, se::kBuzzer);
        return;
    }
    if (sound) menuSound(app, se::kChoose);
    c.anim = 0;
    c.member = 0;
    c.statusView = 0;
    c.message.clear();
    switch (i) {
    case 0:
        c.page = App::Camp::Items;
        c.phase = App::Camp::List;
        c.category = 0;
        campNextCategory(app, 0);
        break;
    case 1: c.page = App::Camp::Equip; c.phase = App::Camp::Member; break;
    case 2: // Genes: the plates in the bag, the board showing the cursor's
        c.page = App::Camp::Genes;
        c.phase = App::Camp::List;
        c.listCursor = c.listTop = 0;
        campRefreshList(app);
        break;
    case 3: c.page = App::Camp::Status; c.phase = App::Camp::Member; break;
    case 4: openOptions(app, 0); break;
    default: app.campOpen = false; openSaveScreen(app, false); break; // Save
    }
}


// The Camp busy: a once-only message up, the Emergency Exit under way
bool campBusy(const App& app) { return app.camp.notice != 0 || app.camp.phase == App::Camp::Exit || app.camp.pageOut != 0; }

void campBack(App& app) {
    App::Camp& c = app.camp;
    if (campBusy(app)) return;
    // (no Back bar during the Yes / No - window 4 is off, CampMainPrc 4 -> 5 -, but the Vita's × answers
    // No there: __ComUty_YesNoChoiceProc, 0x93 and the list again)
    if (c.page == App::Camp::Items && c.phase == App::Camp::Ask) { c.confirm = 1; campChoose(app); return; }
    if (c.page == App::Camp::Items && c.phase == App::Camp::Result) { campChoose(app); return; } // (any tap: CampMainPrc 9)
    if (c.page != App::Camp::Main) menuSound(app, se::kBack); // (the main page's: 0x9a alone - FUN_81053c6c 5)
    c.message.clear();
    switch (c.page) {
    case App::Camp::Main: // (CampMain 5 -> 0xd: every window off, the Camp closed four frames on - 0xe, 0x10;
        // Part 3 437.32-437.40 s: the boards 60, 120, 180 units out, then gone)
        if (!c.leaving) c.leaving = 1, c.closing = true, menuSound(app, se::kCampClose);
        return;
    case App::Camp::Items:
        if (c.phase == App::Camp::Member) c.phase = App::Camp::List; // (CampMainPrc 4's -1: the list again)
        else c.pageOut = 1;
        return;
    case App::Camp::Equip:
        if (c.phase == App::Camp::Entries) { campCloseEntries(app); return; } // (0x1d's -1: the plate list again)
        if (c.phase == App::Camp::List) { c.phase = App::Camp::Slot, c.equipHow = 1; return; }
        if (c.phase == App::Camp::Slot) { c.phase = App::Camp::Member; return; }
        c.pageOut = 1;
        return;
    case App::Camp::Status:
        if (c.phase == App::Camp::Slot) { // Slot = the details: any view's -1 is the member choice (__MuState_ReturnCharChoice)
            c.phase = App::Camp::Member;
            c.statusView = 0;
            return;
        }
        c.pageOut = 1;
        return;
    case App::Camp::Genes:
        if (c.phase == App::Camp::Entries) { campCloseEntries(app); return; } // (Genes 5 -> 6: the plate list again)
        c.pageOut = 1;
        return;
    }
}

// Items, Yes (CampMainPrc 6): the chosen item on member c, del_item for each one used; false (0x95,
// the Yes / No staying up) when it does nothing: a seed's stat at 9999 already - each seed is
// use_item'd in turn, so a stat that fills up part way stops it there, the seeds before used -, an
// MP item on full MP
bool campUseItem(App& app, int character, int item, int count) {
    cr::PartyMember& pm = app.game.party[size_t(character)];
    if (count < 1) return false;
    auto used = [&] { if (--app.game.items[item] <= 0) app.game.items.erase(item); };
    if (item >= 3 && item <= 6) {
        const cr::Stat stat = cr::Stat(cr::kAttack + item - 3);
        for (int k = 0; k < count; ++k) {
            if (ownedCount(app, item) <= 0 || cr::statBonus(pm, stat) >= cr::kMaxStatBonus) return false;
            pm.seeds[item - 3] += cr::kSeedPoints; // use_item: 10 a seed (statBonus caps the bonus at 9999)
            used();
        }
        return true;
    }
    if (item >= 24 && item <= 27) {
        const int most = cr::memberStat(app.game, character, cr::kMp, app.itemTable);
        const int mp = pm.mp < 0 ? most : pm.mp; // (FUN_00066428: refused at exactly the most; above it -
        if (mp == most || ownedCount(app, item) <= 0) return false; // gear taken off - used, and clamped)
        static const int kGain[3] = {100, 250, 500};
        pm.mp = item == 27 ? most : std::min(most, mp + kGain[item - 24]);
        used();
        return true;
    }
    return false;
}

// CampMainPrc 5: how many seeds the count goes to - MuCom_GetUsedItemLimitNum ((9999 - the bonus)
// / 10 + 1, none at 9999) or the bag's, the fewer, kept within 1..999
int campSeedLimit(const App& app, int character, int item) {
    const cr::PartyMember& pm = app.game.party[size_t(character)];
    return std::clamp(std::min(cr::seedRoom(pm, cr::Stat(cr::kAttack + item - 3)), ownedCount(app, item)), 1, 999);
}

// CampMainSet: on the Ark's maps 0xa9..0xd6 (DAT_000ec5ac) whatever __ChangeEquipItem wears (not
// Remove) refills the pair's HP and MP (pcparam_hpmp_recovery)
void campEquipRefill(App& app) {
    if (app.mapNumber >= 0xa9 && app.mapNumber <= 0xd6) ViewerHost(app).recoverPair();
}

void campChoose(App& app) {
    App::Camp& c = app.camp;
    if (campBusy(app)) return;
    const int item = c.list.empty() ? -1 : c.list[size_t(c.listCursor)];
    switch (c.page) {
    case App::Camp::Main:
        if (c.cursor == 0 && !app.game.flag(0x39)) { // CampMain 5: the first Items tap (flag 0x39) - the windows
            menuSound(app, se::kChoose);              // go, message 56016, then the Items page (CampMain 2 -> 6)
            app.game.setFlag(0x39, true);
            campNotice(app, 56016);
            return;
        }
        campOpenPage(app, c.cursor);
        return;
    case App::Camp::Items:
        if (c.phase == App::Camp::List) {
            if (item <= 0 || !campUsable(app, item)) { menuSound(app, se::kBuzzer); return; }
            menuSound(app, se::kChoose);
            if (item == 1) { // the Emergency Exit: straight to its Yes / No (CampMainPrc 2 -> 5, text 57014)
                c.phase = App::Camp::Ask;
                c.count = 1, c.confirm = 0;
            } else c.phase = App::Camp::Member;
        } else if (c.phase == App::Camp::Member) { // (CampMainPrc 4 -> 5: no member refused here)
            menuSound(app, se::kChoose);
            c.phase = App::Camp::Ask;
            c.count = 1;
            c.confirm = 0;
        } else if (c.phase == App::Camp::Ask) {
            if (c.confirm != 0) { // No: back to the list (CampMainPrc 7 -> 1), 0x93
                menuSound(app, se::kChoose);
                c.phase = App::Camp::List;
                return;
            }
            if (item == 1) { // Yes to the Emergency Exit: DAT_000ec698 / ec5ae, del_item, 0x93, state 7 -> 0xf
                if (!campExitUsable(app) || ownedCount(app, 1) <= 0) { menuSound(app, se::kBuzzer); return; }
                if (--app.game.items[1] <= 0) app.game.items.erase(1);
                menuSound(app, se::kChoose);
                c.phase = App::Camp::Exit;
                c.exitT = 0;
                return;
            }
            if (!campUseItem(app, campCharacter(app, c.member), item, c.count)) { menuSound(app, se::kBuzzer); return; }
            menuSound(app, campui::kSoundUse); // 0xa5, then the board (window 0x12) until a tap (CampMainPrc 8, 9)
            c.resultItem = item;
            c.resultCount = item >= 3 && item <= 6 ? c.count : 0;
            c.phase = App::Camp::Result;
        } else if (c.phase == App::Camp::Result) { // the tap (0x93): the list again (10 -> 1)
            menuSound(app, se::kChoose);
            c.phase = App::Camp::List;
            campRefreshList(app);
            if (c.list.empty()) campNextCategory(app, 0);
        }
        return;
    case App::Camp::Equip: {
        cr::PartyMember& pm = app.game.party[size_t(campCharacter(app, c.member))];
        if (c.phase == App::Camp::Member) { menuSound(app, se::kChoose); c.phase = App::Camp::Slot; c.slot = 0, c.slotTop = 0; }
        else if (c.phase == App::Camp::Slot) {
            if (campSlotLocked(app, c.member, c.slot)) { menuSound(app, se::kBuzzer); return; } // (MuScrList_GetChoiceListNo -4)
            c.listCursor = c.listTop = 0;
            campRefreshList(app);
            if (c.list.empty()) { menuSound(app, se::kBuzzer); return; } // nothing to wear
            // __MuCampScrListComInit: with the slot worn, MuScrList_SetListPosFromItemNo(worn) - the list
            // opens settled with what is worn on the focus line (3.1.0 0x6ec40; Part 3 398.3 s and Part 10 201 s: Gene
            // Plate 1's list comes in with the worn "Deer Genes" on the line, the Jewel list with "Pebbles")
            if (const int worn = c.slot < 3 ? pm.equip[c.slot] : pm.genes[c.slot - 3]; worn > 0)
                if (const auto at = std::find(c.list.begin(), c.list.end(), worn); at != c.list.end())
                    c.listCursor = c.listTop = int(at - c.list.begin());
            menuSound(app, se::kChoose);
            c.phase = App::Camp::List, c.equipHow = 0;
        } else if (c.phase == App::Camp::List && !c.list.empty() && c.slot >= 3) { // a gene plate
            if (item == campui::kRemove) { // CampMainPrc 0x19: Remove acts at once (__ChangeEquipItem 200)
                pm.genes[c.slot - 3] = 0;
                menuSound(app, campui::kSoundEquip);
                c.phase = App::Camp::Slot, c.equipHow = 2;
            } else if (campPlateLocked(app, item)) menuSound(app, se::kBuzzer);
            else { // 0x93, then the plate's known entries (0x1a) with the board in picture mode
                menuSound(app, se::kChoose);
                campOpenEntries(app, item);
            }
        } else if (c.phase == App::Camp::Entries) { // 0x1d: the list's Equip row (-5) - none while the member wears it
            if (campWears(app, campCharacter(app, c.member), c.plate)) { menuSound(app, se::kBuzzer); return; }
            pm.genes[c.slot - 3] = c.plate;
            menuSound(app, campui::kSoundEquip);
            campEquipRefill(app);
            c.plate = 0;
            c.phase = App::Camp::Slot;
        } else if (c.phase == App::Camp::List && !c.list.empty()) {
            // (kind 5: what the member wears already is refused, MuScrList_IsNowEquip -4)
            if (item != campui::kRemove && pm.equip[c.slot] == item) { menuSound(app, se::kBuzzer); return; }
            if (item != campui::kRemove && !campJewelFree(app, c.member, item)) { menuSound(app, se::kBuzzer); return; } // (grey: -4)
            pm.equip[c.slot] = item == campui::kRemove ? 0 : item; // (the bag as it was: __ChangeEquipItem)
            menuSound(app, campui::kSoundEquip);
            if (item != campui::kRemove) campEquipRefill(app);
            c.phase = App::Camp::Slot, c.equipHow = 0;
        }
        return;
    }
    case App::Camp::Status:
        if (c.phase == App::Camp::Member) { // CampMainPrc 3's 2: the board first, the lists made
            menuSound(app, se::kChoose);
            c.phase = App::Camp::Slot;
            c.statusView = 0;
            c.viewTop[0] = c.viewTop[1] = c.viewTop[2] = 0;
            c.list.clear();
            c.listCursor = c.listTop = 0;
        }
        return;
    case App::Camp::Genes: // browsing: a plate opens its known entries (Genes 3 -> 4), a plate with none is refused (kind 0xb)
        if (c.phase == App::Camp::List && !c.list.empty()) {
            if (campPlateLocked(app, item)) { menuSound(app, se::kBuzzer); return; }
            menuSound(app, se::kChoose);
            campOpenEntries(app, item);
        } else if (c.phase == App::Camp::Entries) menuSound(app, se::kChoose); // (the list's own SE; Back alone leaves it)
        return;
    }
}

// Scenarios from the Camp (CampMainPrc 5 -> 6: SE 0x93, the main page's windows off; 7: the menu set)
void campToScenarios(App& app) {
    if (app.camp.leaving) return;
    menuSound(app, se::kChoose);
    app.camp.leaving = 1;
}

// A frame of that (20 Hz): 4 frames of the windows going, then Scenarios - the Camp's windows slide
// back in from their sides when it closes (state 9)
void stepCampLeaving(App& app) {
    App::Camp& c = app.camp;
    if (!c.leaving || ++c.leaving <= 4) return;
    c.leaving = 0;
    c.anim = 0;
    if (c.closing) { c.closing = false; app.campOpen = false; return; } // (Back: the Camp closed)
    openScenarios(app, 1);
}


void campExitWarp(App& app) {
    App::Camp& c = app.camp;
    app.campOpen = false;
    app.campPointer = App::CampPointer();
    c.phase = App::Camp::List;
    c.page = App::Camp::Main;
    c.exitT = 0;
    ViewerHost(app).recoverPair();
    const bool second = app.game.scenario == 1;
    cr::WarpRequest w;
    w.valid = true;
    w.area = second ? 10 : 9;
    w.map = second ? 0xc0 : app.game.flag(0x4f4) ? 0xbc : 0xa9;
    w.pos[0] = 0, w.pos[1] = 7.5f, w.pos[2] = 15;
    w.angleDegrees = 0;
    performWarp(app, w, -1); // (its field fades in from the white: SetMask2 keeps the colour)
    app.lastEvent = "Emergency Exit: back to the Ark";
}

// A Camp frame (20 a second): a once-only message read, the Emergency Exit's frames
void stepCamp(App& app) {
    App::Camp& c = app.camp;
    if (!app.campOpen) return;
    ++c.anim; // (a game frame: the windows' moves)
    campTrackPhase(c);
    if (c.phaseT < 100) ++c.phaseT;
    if (c.swapT < 100) ++c.swapT;
    if (c.leaving) { stepCampLeaving(app); return; } // (Scenarios chosen: the windows going)
    if (c.pageOut && ++c.pageOut - 1 >= campPageOutFrames(app)) { // the page's windows gone: the main page's on (CampMain 9)
        c.pageOut = 0;
        c.barIn = c.page == App::Camp::Items;
        c.page = App::Camp::Main;
        c.phase = App::Camp::List;
        c.anim = 0;
        campTrackPhase(c);
    }
    stepCampScroll(app);
    ++c.mark;
    if (c.notice && app.dialogue.empty()) { // the message closed: on to what was asked (CampMain 2)
        const int id = c.notice;
        c.notice = 0;
        c.anim = 0;
        if (id == 56016) campOpenPage(app, 0, false);
    }
    if (c.phase != App::Camp::Exit) return;
    ++c.exitT;
    if (c.exitT == kCampExitFade) {
        app.fadeGrey = 1, app.fadeFrom = 0, app.fadeTo = 1, app.fadeFrames = 10, app.fadeT = 0; // SetFade(white, 0, 255, 500 ms)
        menuSound(app, 0x128);
        playBgm(app, 0x8000000u);
    }
    if (c.exitT >= kCampExitWarp) campExitWarp(app);
}

void campKey(App& app, SDL_Keycode key) {
    App::Camp& c = app.camp;
    if (c.leaving || c.pageOut) return; // (the windows going to Scenarios or back to the main page)
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK || (key == SDLK_K && c.page == App::Camp::Main);
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    const bool up = key == SDLK_UP || key == SDLK_W, down = key == SDLK_DOWN || key == SDLK_S;
    // (the Vita turns the Items categories, the Equip member and the Status views with L / R - the Back
    // bar's arrows, FUN_81052fd0 / 81051eec / 81050cde -: PgUp / PgDn, L / R, as left / right off the
    // main page)
    const bool lr = c.page != App::Camp::Main;
    const bool left = key == SDLK_LEFT || key == SDLK_A || (lr && key == SDLK_PAGEUP),
               right = key == SDLK_RIGHT || key == SDLK_D || (lr && key == SDLK_PAGEDOWN);
    if (c.notice) { if (enter || back) tapDialogue(app); return; } // (the message's page turned, as a tap)
    if (c.phase == App::Camp::Exit) return;
    if (c.phase == App::Camp::Result) { if (enter || back) campChoose(app); return; } // (pad_chk_updw_box: the whole screen)
    if (back) { campBack(app); return; }
    if (enter && c.page == App::Camp::Main && c.btnRow >= 0) { // a button below the boards (0x93)
        if (c.btnRow == 0) campToScenarios(app);
        else if (app.game.scenario != 0) { menuSound(app, se::kChoose); openCollection(app); }
        return;
    }
    if (enter) { campChoose(app); return; }
    const int step = up ? -1 : down ? 1 : 0;
    if (c.page == App::Camp::Status && c.phase == App::Camp::Slot) { // the details: the arrows turn, the lists scroll
        if (left || right) campStatusTurn(app, right ? 1 : -1);
        else if (step && c.statusView != 0) campScrollBy(app, step);
        return;
    }
    if (c.page == App::Camp::Equip && c.phase == App::Camp::Slot && (left || right)) { // CampMainPrc 1's 4 -> 7..10: the
        if (campMembers(app) > 1 && c.swapT >= 9) { // (__MuEquip_CategoryChoiceEnd: -3 - left, a swipe right - the windows
            // go right, -2 to the left; the other member's come from the other side once they have gone)
            c.swapFrom = c.member, c.swapDir = left ? 1 : -1, c.swapT = 0;
            c.member ^= 1;
            menuSound(app, se::kPage);
        }
        return;
    }
    switch (c.page) {
    case App::Camp::Main: {
        // (the Vita's FUN_81053c6c 5: left / right from the plates to the buttons below the boards -
        // right the first, left the last -, along them, off either end back to the plates; ○ opens
        // the one lit; up / down do nothing there)
        const int buttons = app.game.scenario != 0 ? 2 : 1;
        if (left || right) {
            if (c.btnRow < 0) c.btnRow = right ? 0 : buttons - 1;
            else if ((c.btnRow += right ? 1 : -1) < 0 || c.btnRow >= buttons) c.btnRow = -1;
            menuSound(app, se::kCursor);
            return;
        }
        if (c.btnRow >= 0) return; // (○ there: before the switch)
        if (step) { c.cursor = (c.cursor + step + campEntries(app)) % campEntries(app); menuSound(app, se::kCursor); }
        else if (key == SDLK_Q || key == SDLK_PAGEUP) campToScenarios(app); // (keys, L: the Scenarios picture)
        else if ((key == SDLK_C || key == SDLK_PAGEDOWN) && app.game.scenario != 0) { menuSound(app, se::kChoose); openCollection(app); } // (R: Collection)
        return;
    }
    case App::Camp::Items:
    case App::Camp::Equip:
    case App::Camp::Genes:
        if (c.phase == App::Camp::List || c.phase == App::Camp::Entries) {
            const int n = int(c.list.size());
            if (step && n > 0) {
                c.listCursor = (c.listCursor + step + n) % n;
                c.listTop = std::clamp(c.listTop, std::max(0, c.listCursor - campui::kRows + 1), c.listCursor);
                menuSound(app, se::kCursor);
            } else if ((left || right) && c.page == App::Camp::Items) {
                campNextCategory(app, left ? -1 : 1);
                menuSound(app, se::kPage);
            }
            return;
        }
        if (c.phase == App::Camp::Ask) {
            const int item = c.list.empty() ? 0 : c.list[size_t(c.listCursor)];
            if ((left || right) && item >= 3 && item <= 6) {
                const int was = c.count;
                c.count = std::clamp(c.count + (right ? 1 : -1), 1, std::max(1, campSeedLimit(app, campCharacter(app, c.member), item)));
                menuSound(app, c.count != was ? se::kChoose : se::kBuzzer);
            } else if (step) { c.confirm ^= 1; menuSound(app, se::kCursor); }
            return;
        }
        if (c.phase == App::Camp::Slot && c.page == App::Camp::Equip) {
            if (step) {
                c.slot = (c.slot + step + campui::kEquipSlots) % campui::kEquipSlots;
                if (c.slot < c.slotTop) c.slotTop = c.slot;
                if (c.slot >= c.slotTop + campui::kRows) c.slotTop = c.slot - campui::kRows + 1;
                menuSound(app, se::kCursor);
            }
            return;
        }
        [[fallthrough]];
    case App::Camp::Status:
        if (c.phase == App::Camp::Member && (step || left || right)) { if (campMembers(app) > 1) c.member ^= 1; menuSound(app, se::kCursor); }
        return;
    }
}

// ComUty_BackButtonProc's mode for the Back bar: 2 (the < > arrows: -3 at 0xc0, Back at 0xf8, -2 at
// 0x175, each 0x22 high at 0x11e) on the Items list (MuScrList's, SetIdInfo(10, .., 2)), the
// equipment slots with two members (__MuCampScrListEquipInit(.., 4, ..)) and the Status details
// (DAT_000ec58c = 2); none during the Items Yes / No, its board and the Emergency Exit; else 1
bool campArrows(const App& app) {
    const App::Camp& c = app.camp;
    return (c.page == App::Camp::Items && c.phase == App::Camp::List) ||
           (c.page == App::Camp::Equip && c.phase == App::Camp::Slot && campMembers(app) > 1) ||
           (c.page == App::Camp::Status && c.phase == App::Camp::Slot);
}
// ComUty_BackButtonProc mode 3 (Back and OK, OK -5): a plate's entries in Equip while the member doesn't
// wear it (CampMainPrc 0x1a: SetIdInfo(7, 0x13, .., 1 if worn else 3); Part 3 406 s "Back | OK")
bool campOkBar(const App& app) {
    const App::Camp& c = app.camp;
    return c.page == App::Camp::Equip && c.phase == App::Camp::Entries && !campWears(app, campCharacter(app, c.member), c.plate);
}
bool campBackShown(const App& app) {
    const App::Camp& c = app.camp;
    return !(c.page == App::Camp::Items && (c.phase == App::Camp::Ask || c.phase == App::Camp::Result || c.phase == App::Camp::Exit));
}

void campClick(App& app, float px, float py) {
    using namespace campui;
    App::Camp& c = app.camp;
    c.btnRow = -1; // (a touch: the pad's place on the buttons goes)
    if (c.notice) { tapDialogue(app); return; }
    if (c.phase == App::Camp::Exit) return;
    if (c.phase == App::Camp::Result) { campChoose(app); return; } // (a tap anywhere)
    if (c.leaving || c.pageOut) return;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    // (widescreen: Back centred, the section's box on the left, the rest on the right - drawCamp)
    float cx, my, lx0, rx;
    menuPointAt(app, lx, ly, app.renderer.uiX(), cx, my);
    menuPointAt(app, lx, ly, wideLeft(app), lx0, my);
    menuPointAt(app, lx, ly, wideRight(app), rx, my);
    // (the Vita's CampMainPut / Prc: the plates at 0x173, the list (its table's 235) and Back (235) 43 to the right;
    // the count and Yes / No where 3.1.0 has them: rxYes)
    const float rxYes = rx;
    rx -= vitaEdge43(app), cx -= vitaEdge43(app);
    float mx = rx;
    auto in = [&](float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    auto inAt = [&](float at, float x, float y, float w, float h) { return at >= x && at < x + w && my >= y && my < y + h; };
    if (campBackShown(app)) {
        if (campOkBar(app)) {
            if (inAt(cx, 0xc0, campBackY(app), 0x72, 34)) campBack(app);
            else if (inAt(cx, 0x137, campBackY(app), 0x72, 34)) campChoose(app); // (-5: __ChangeEquipItem)
            if (inAt(cx, 0xc0, campBackY(app), 0xe8, 34)) return;
        } else if (!campArrows(app) && inAt(cx, kBackX, campBackY(app), 256, 34)) { campBack(app); return; }
        if (campArrows(app)) {
            if (inAt(cx, 0xc0, campBackY(app), 0x33, 34)) { campKey(app, SDLK_LEFT); return; }
            if (inAt(cx, 0xf8, campBackY(app), 0x78, 34)) { campBack(app); return; }
            if (inAt(cx, 0x175, campBackY(app), 0x33, 34)) { campKey(app, SDLK_RIGHT); return; }
        }
    }
    switch (c.page) {
    case App::Camp::Main: {
        for (int i = 0; i < campEntries(app); ++i)
            if (in(kPlateX + 360 * (1 - campEase(app)), kPlateY + kPlateStep * float(i), 136, 40)) { c.cursor = i; campChoose(app); return; }
        const short* sp = cr::kMenuPicture[162]; // __menuu_CampMainPut's Scenarios picture (CampMainPrc case 5)
        if (inAt(lx0, 12 - 120 * (1 - campEase(app)), 236, sp[2], sp[3])) { campToScenarios(app); return; }
        const short* cp = cr::kMenuPicture[161]; // ... and Collection (case 6; the second scenario's only)
        if (app.game.scenario != 0 && inAt(lx0, 84 - 120 * (1 - campEase(app)), 236, cp[2], cp[3])) { menuSound(app, se::kChoose); openCollection(app); return; }
        // CampMain 5: in the second scenario with flag 0x636 or 0x637 a face board's box (0, 0x30 + 0x58k,
        // 0x104 x 0x50) makes that member the leader (0x93, Set_Prtyno) unless it leads already
        if (app.game.scenario == 1 && (app.game.flag(0x636) || app.game.flag(0x637)))
            for (int k = 0; k < campMembers(app); ++k)
                if (inAt(lx0, 0, 48 + 88.0f * float(k), 260, 80)) {
                    const int ch = campCharacter(app, k);
                    if (app.game.leaders[1] != ch) { menuSound(app, se::kChoose); ViewerHost(app).setLeader(ch); }
                    return;
                }
        return;
    }
    default: break;
    }
    if (c.phase == App::Camp::Member) { // __MuCommon_CharChoiceProc: a member's plate (0x148, 0xb0 + 0x38k) or face board (8, 0x30 + 0x58k)
        for (int k = 0; k < campMembers(app); ++k)
            if (in(kPlateX + 360 * (1 - campEase(app)), kMemberY + kMemberStep * float(k), 136, 40) ||
                inAt(lx0, 8, 48 + 88.0f * float(k), 252, 80)) { c.member = k; campChoose(app); return; }
        return;
    }
    if (c.page == App::Camp::Status && c.phase == App::Camp::Slot && c.statusView == 0) return; // (the board: arrows and swipes)
    if (campScrollable(app)) {
        // The gauge's arrows (0x4c / 0x4d at (456, 72) / (456, 272)) step the list
        if (in(kGaugeX - 4, kGaugeY - 4, 24, 24)) { campScrollBy(app, -1); return; }
        if (in(kGaugeX - 4, kGaugeY + kGaugeH - 12, 24, 28)) { campScrollBy(app, 1); return; }
        // A row (MuScrList_Proc): the one on the line is chosen (pad_chk_updw_box(192, 150, 256, 48)); another
        // is brought to the line (the original scrolls it there by a drag). Above the first row is the down
        // pill's box (192, 76, 256, 74: SetListPosFromNo(n) - the end), under the last the up pill's (the top)
        if (in(campListX(app), kListTop, 256, kListBottom - kListTop)) {
            const int n = campScrollCount(app);
            int& cursor = campSlotList(app) ? c.slot : c.listCursor;
            const int row = int(std::floor((my - kFocusY + campScrollPx(app)) / kRowH));
            if (row >= 0 && row < n) {
                if (row == cursor) {
                    if (c.page != App::Camp::Status && !(c.page == App::Camp::Equip && c.phase == App::Camp::Entries)) campChoose(app);
                } else { cursor = row; menuSound(app, se::kCursor); }
            } else if (n > 2 && row < 0 && cursor < 2) { cursor = n - 1; c.scrollKey = -1; menuSound(app, se::kCursor); }
            else if (n > 2 && row >= n && cursor >= n - 2) { cursor = 0; c.scrollKey = -1; menuSound(app, se::kCursor); }
            return;
        }
    }
    if (c.phase == App::Camp::List && c.page == App::Camp::Items && inAt(lx0, 0, 40, 180, 40)) {
        campNextCategory(app, 1), menuSound(app, se::kPage);
        return;
    }
    if (c.phase == App::Camp::Ask) {
        const int item = c.list.empty() ? 0 : c.list[size_t(c.listCursor)];
        mx = rxYes;
        if ((in(336, 60, 36, 31) || in(436, 60, 36, 31)) && item >= 3 && item <= 6) {} // (stepped while held: pressCountStep)
        else if (in(336, 104, 136, 40)) { c.confirm = 0; campChoose(app); }
        else if (in(336, 160, 136, 40)) { c.confirm = 1; campChoose(app); }
    }
}

void campPointerPoint(const App& app, float px, float py, float& mx, float& my) {
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    menuPointAt(app, lx, ly, vitaRight(app), mx, my); // (the Vita's lists 43 to the right)
}
// A touch held inside a Camp box (menu units of the group drawn from `origin`): 3.1.0 lights the control
// while _pad_chk_updw_box finds the finger on it (not once it drags a list)
bool campPressIn(const App& app, float origin, float x, float y, float w, float h) {
    const auto& p = app.campPointer;
    if (!p.down || p.dragging) return false;
    float lx, ly, mx, my;
    app.renderer.toLogical(p.curPx, p.curPy, lx, ly);
    menuPointAt(app, lx, ly, origin, mx, my);
    return mx >= x && mx < x + w && my >= y && my < y + h;
}
// ... on a button below the boards (0 Scenarios, 1 Collection): picture 163 under it
bool campButtonHeld(const App& app, int k) {
    if (app.camp.page != App::Camp::Main) return false;
    const short* sp = cr::kMenuPicture[k == 0 ? 162 : 161];
    return campPressIn(app, wideLeft(app), k == 0 ? 12.0f : 84.0f, 236, sp[2], sp[3]);
}
void campPointerDown(App& app, float px, float py) {
    auto& p = app.campPointer;
    p = App::CampPointer();
    p.down = true, p.page = app.camp.page, p.phase = app.camp.phase;
    campPointerPoint(app, px, py, p.startX, p.startY);
    p.lastY = p.startY;
    p.curPx = px, p.curPy = py;
    const bool list = campScrollable(app);
    p.bar = list && campScrollCount(app) > 1 && p.startX >= campui::kGaugeX - 2 && p.startX < campui::kGaugeX + 22 &&
        p.startY >= campui::kGaugeY + 12 && p.startY < campui::kGaugeY + campui::kGaugeH - 12;
}
void campPointerMove(App& app, float px, float py) {
    auto& p = app.campPointer;
    if (!p.down) return;
    if (!app.campOpen || app.camp.page != p.page || app.camp.phase != p.phase) { p = App::CampPointer(); return; }
    float mx, my;
    campPointerPoint(app, px, py, mx, my);
    p.curPx = px, p.curPy = py;
    const bool list = campScrollable(app);
    const bool inList = p.startX >= campListX(app) && p.startX < campListX(app) + 256 &&
        p.startY >= campui::kListTop && p.startY < campui::kListBottom;
    if (std::fabs(mx - p.startX) > 6 || std::fabs(my - p.startY) > 6) p.dragging = true;
    if (list && p.bar && p.dragging) {
        const int most = std::max(0, campScrollCount(app) - 1);
        const int to = int(std::lround(std::clamp((my - campui::kGaugeY - 24) / (campui::kGaugeH - 48), 0.0f, 1.0f) * float(most)));
        campScrollBy(app, to - campFocus(app));
    } else if (list && inList && p.dragging) {
        p.remainder += p.lastY - my;
        const int rows = int(p.remainder / campui::kRowH);
        if (rows) { campScrollBy(app, rows); p.remainder -= rows * campui::kRowH; }
    }
    p.lastY = my;
}
void campPointerUp(App& app, float px, float py) {
    if (!app.campPointer.down) return;
    campPointerMove(app, px, py);
    const auto p = app.campPointer;
    app.campPointer = App::CampPointer();
    if (p.down && p.dragging && app.campOpen && app.camp.page == App::Camp::Status && app.camp.phase == App::Camp::Slot &&
        p.page == App::Camp::Status && p.phase == App::Camp::Slot && !p.bar) {
        // The details flicked sideways (ComUty_GetSwipeX over (0, 0x4c, 0x1c8, 0xd0), or the list's
        // own swipe): more than 30 to the left is -2, to the right -3
        float mx, my;
        campPointerPoint(app, px, py, mx, my);
        const float dx = p.startX - mx;
        if (std::fabs(dx) > 30 && std::fabs(dx) > std::fabs(my - p.startY) && p.startY >= 0x4c && p.startY < 0x4c + 0xd0)
            campStatusTurn(app, dx > 0 ? 1 : -1);
        return;
    }
    if (p.down && !p.dragging && app.campOpen && app.camp.page == p.page && app.camp.phase == p.phase) {
        if (p.bar) {
            const int most = std::max(0, campScrollCount(app) - 1);
            const int to = int(std::lround(std::clamp((p.startY - campui::kGaugeY - 24) / (campui::kGaugeH - 48), 0.0f, 1.0f) * float(most)));
            campScrollBy(app, to - campFocus(app));
        } else campClick(app, px, py);
    }
}

// MuComFontPlateDisp's colours: 0 white, 2 grey (0x808080), 3 yellow (0x00ffff, its BGR) - the plate and the
// icon stay as they are (Part 13 1174 s: an item that can't be used, grey "White Chocolate" on a lit plate)
void plateText(cr::MenuPainter& m, const std::string& s, float x, float y, int style, float a) {
    if (style == 2) m.textTint(s, x, y, 0x80 / 255.0f, 0x80 / 255.0f, 0x80 / 255.0f, a, 14);
    else if (style == 3) m.textTint(s, x, y, 1, 1, 0, a, 14);
    else m.text(s, x, y, false, a, 14);
}

// A list row (ComUty_ComPlate_Base): plate 8 / 9, the icon at (12, 8), the name at (56, 12) in the style's
// colour; an item count (FUN_00061610 / 000618dc: ComUty_PictNumDraw(x + 0xde, y + 0x1e, 2 places, 10,
// style 0 - white; Part 13 1174 s "49", "20", "4")
void drawCampRow(App& app, cr::MenuPainter& m, float x, float y, int item, int count, bool lit, float a, int style,
                 const std::string& label) {
    m.picture(lit ? 9 : 8, x, y, a);
    if (const cr::ItemParam* p = app.itemTable.item(item); p && p->icon >= 0)
        m.piece(x + 12, y + 8, float(p->icon % 16) * 32, 248 + float(p->icon / 16) * 32, 32, 32, a);
    plateText(m, item > 0 ? itemName(app, item) : std::string("-"), x + 56, y + 12, style, a);
    if (!label.empty()) m.text(label, x + 56, y + 30, false, a * 0.8f, 11);
    if (count > 0) drawShopNumber(m, count, x + 222, y + 30, 2, 10, a, 0);
}

// A plate's entry in the Camp (MuScrList type 7: ComUty_ComPlate_Equip_GeneAbility): the base plate,
// then for a gene (0x51..0x82) "MP/" (picture 0x1b at (188, 34)) and its cost in three white digits from
// (218, 30) - halved while the member has skill 0x84; a skill shows no cost (Part 3 406 s:
// "Healing (HP) MP/ 20", "Analyze MP/ 4", "Beast Wavelength")
void drawGeneAbilityRow(App& app, cr::MenuPainter& m, float x, float y, int it, int ch, bool lit, float a) {
    drawCampRow(app, m, x, y, it, 0, lit, a);
    if (cr::isSkill(it)) return;
    const cr::BattleActionDef* act = app.battleActions.get(cr::geneAction(it));
    if (!act) return;
    const int cost = ch >= 0 && ch < cr::kCharacters && cr::hasSkill(app.game, ch, 132) ? act->mp / 2 : act->mp;
    m.picture(27, x + 188, y + 34, a);
    drawShopNumber(m, cost, x + 218, y + 30, 3, 10, a, 0);
}

// A gene plate's row (ComUty_ComPlate_Equip_GenePlate): plate 8 / 9, its icon and name - yellow (3)
// for one worn, grey (2) while none of it is known - then six pips at (167 + 14k, 30), the unknown ones
// first and dim (10, known 11), or "MASTER" (picture 96 at (176, 24)) once all six are known
// (MuScrList_Draw types 6 / 0xb pass 6; Part 3 403 s: three dark, three gold)
void drawGeneRow(App& app, cr::MenuPainter& m, float x, float y, int plate, bool lit, bool worn, float a) {
    m.picture(lit ? 9 : 8, x, y, a);
    if (const cr::ItemParam* p = app.itemTable.item(plate); p && p->icon >= 0)
        m.piece(x + 12, y + 8, float(p->icon % 16) * 32, 248 + float(p->icon / 16) * 32, 32, 32, a);
    const int known = cr::plateLearned(app.game, plate);
    constexpr int total = 6;
    plateText(m, itemName(app, plate), x + 56, y + 12, known == 0 ? 2 : worn ? 3 : 0, a);
    if (known >= total) m.picture(96, x + 176, y + 24, a);
    else
        for (int k = 0; k < total; ++k) m.picture(k < total - known ? 10 : 11, x + 251 - 14 * float(total) + 14 * float(k), y + 30, a);
}

// ComUty_GuageBarDraw(456, 72, offset, 2, n, 48, 208): the track (window sheet (144, 184) 16 x 16, (144, 200)
// 16 x 8 down to the end (144, 208) at +200), its arrows (0x4c at the top, 0x4d at +200) and the knob - top
// (160, 196) 16 x 12, middle (160, 208) 16 x 4, end (160, 212) 16 x 8 - max(24, 368 / n) long (the whole 192
// under three rows), moved (196 - L) x offset / ((n - 2) x 48) down from +12. Drawn for any list
void drawCampScrollBar(cr::MenuPainter& m, float scroll, int n, float a) {
    using namespace campui;
    const float x = kGaugeX, y = kGaugeY, h = kGaugeH;
    m.piece(x, y, 144, 184, 16, 16, a);
    for (int i = 0; i < int(h - 24) / 8; ++i) m.piece(x, y + 16 + 8 * float(i), 144, 200, 16, 8, a);
    m.piece(x, y + h - 8, 144, 208, 16, 16, a);
    m.picture(0x4c, x, y, a);
    m.picture(0x4d, x, y + h - 8, a);
    float len = 192, at = 0;
    if (n >= 3) {
        len = std::max(24.0f, std::floor((h - 24) * 2 / float(n)));
        at = std::clamp(std::floor((196 - len) * scroll / (float(n - 2) * kRowH)), 0.0f, h - 24 - len + 10);
    }
    const float top = y + 12 + at;
    m.piece(x, top, 160, 196, 16, 12, a);
    if (len > 20) m.pieceTo(x, top + 12, 16, len - 20, 160, 208, 16, 4, a);
    m.piece(x, top + len - 8, 160, 212, 16, 8, a);
}

// ComUty_GeneBoardDraw at (x, y): the plate's name and "n / 6" known, then EITHER (mode 1, while the
// plate list is browsed) the names of its known entries - ComUty_GeneAbilityStrChange's, up to six,
// each centred in 128 px from (x + 12, y + 75) 18 apart - OR (mode 2, a plate chosen) its picture
// (gene_m*c.mct) at (x + 12, y + 68)
void drawGeneBoard(App& app, cr::MenuPainter& m, float x, float y, int plate, float a, int mode = 1) {
    if (!cr::isPlate(plate)) return;
    // ComUty_StateBoardChrName (x, y, 163): a white 163 x 16 bar, the name centred on it in the dark
    // style (MuComFontDisp style 1); then MuComRectDraw's white 163 x 16 under "SKILL n / 6" - both
    // solid, no panel behind the board (Part 3 403 s: the Equip > Genes page)
    const float white[4] = {1, 1, 1, a};
    m.quad(x, y, 163, 16, white, white);
    const std::string name = itemName(app, plate);
    m.textCentred(name, x + (163 - m.textWidth(name, 14)) / 2, y + 8, true, a, 14);
    m.quad(x, y + 24, 163, 16, white, white);
    m.picture(95, x + 24, y + 27, a);
    drawShopNumber(m, cr::plateLearned(app.game, plate), x + 90, y + 24, 1, 12, a, 2);
    m.picture(65, x + 102, y + 24, a);
    drawShopNumber(m, 6, x + 114, y + 24, 1, 12, a, 2);
    // __MuCamp_GeneTexRead (_gkpachGenePlateImage): gene_m001c..m007c, then m201c..m224c
    static const std::array<std::string, cr::kPlates> kArt = [] {
        std::array<std::string, cr::kPlates> a;
        char name[32];
        for (int i = 0; i < cr::kPlates; ++i) {
            std::snprintf(name, sizeof name, "gene_m%03dc.mct", i < 7 ? i + 1 : 201 + i - 7);
            a[size_t(i)] = name;
        }
        return a;
    }();
    cr::Texture& art = app.camp.plateArt[size_t(plate - cr::kFirstPlate)];
    if (!art.id) {
        cr::Image img;
        if (loadImage(app, kArt[size_t(plate - cr::kFirstPlate)].c_str(), img)) art = app.renderer.upload(img);
    }
    if (mode == 2) {
        if (art.id) { // the picture, within 128 x 128 (the iPhone sheet's size)
            const float w = float(art.width), h = float(art.height), k = std::min(1.0f, std::min(128 / std::max(w, 1.0f), 128 / std::max(h, 1.0f)));
            // (the Vita: gene_nn read by _MuCom_MctTexRead - kind 3, full size: the pictures' layer)
            cr::Renderer& rr = app.renderer;
            const int keep = rr.layer();
            if (rr.vitaScreen()) rr.setLayer(3);
            m.image(art, x + 12, y + 68, w * k, h * k, a);
            rr.setLayer(keep);
        }

        return;
    }
    float ly = y + 75;
    int shown = 0;
    for (int e : cr::plateContents(plate)) {
        if (e <= 0 || ownedCount(app, e) <= 0 || shown >= 6) continue;
        const std::string name = itemName(app, e);
        m.text(name, x + 12 + (128 - m.textWidth(name, 12)) / 2, ly, false, a, 12);
        ly += 18;
        ++shown;
    }
}

// ---- The staff roll (Util 7: ModeStaffRollMenu(var 0), __menuu_StaffRollSet / Prc / Put) ----
// Omega's: the picture (sheet 1 picture 12, 512 x 1024, of EndBack01 / EndBack02 / EndBack01_end
// by mode, _gkpachEndBack) at tex2d (-16, y; the iPad - 32) under 17 lines of system text block 9
// (x 16, 20 apart from y 352 - the iPhone 320 -, each past -52 / -20 taking the next line of 316;
// a line of < 3 characters is a gap), over a white fade (the first scenario; black the second)
// clearing over 60 frames. The lines rise 2 a frame (the second scenario 1.413), the picture 0.2
// ((1024 - 384) / 4600, the iPhone 320) to -(1024 - 384 / 320); the corner button (window picture
// 115 at (460, -32) / (444, 0)) held runs both 10 times over. Once the lines are through and the
// picture down: 100 frames (the text and the button go at 5), 120 more, then (mode 0) the end
// picture (EndBack01_end's picture 14 at (176, 128)) fades in, 6 a frame over 80 frames, the
// screen fades to black over 120 and 130 frames on the windows go and the script goes on - no tap
namespace staffroll {
constexpr int kShown = 17, kLines = 316;
// (Android 1.1.6's InitStaffRollMenu / Prc: the phone's screen - the lines from 320 + oy to -20 - oy,
// the picture's height 320 + 2 oy and drawn oy higher - so it ends on the phone's bottom edge)
float lineStart(const App::StaffRoll& st) { return st.phone ? 320.0f + st.edgeY : 352.0f; }
float lineEnd(const App::StaffRoll& st) { return st.phone ? -20.0f - st.edgeY : -52.0f; }
float screenH(const App::StaffRoll& st) { return st.phone ? 320.0f + 2 * st.edgeY : 384.0f; }
}
int staffLines(const App&) { return staffroll::kLines; }
float staffPicEnd(const App::StaffRoll& st) { return -(1024 - staffroll::screenH(st)); }

void staffWin(App& app, int w, bool on) { app.staffRoll.winTo[w] = on ? 1.0f : 0.0f; }
void staffFade(App::StaffRoll& st, bool white, float from, float to, int ms) { // SetFade(colour, from, to, ms): 50 ms a frame
    st.fadeWhite = white, st.fadeFrom = from, st.fadeTo = to, st.fadeAt = 0, st.fadeFrames = std::max(1, ms / 50);
    st.fade = from;
}

std::string staffLine(const App& app, int entry) { return sysText(app, 60000 + entry); } // font block 9

void openStaffRoll(App& app, int mode) {
    using namespace staffroll;
    App::StaffRoll& st = app.staffRoll;
    app.renderer.destroy(st.tex);
    app.renderer.destroy(st.endTex);
    st = App::StaffRoll();
    st.open = true;
    st.mode = std::clamp(mode, 0, 2);
    st.phone = app.renderer.phoneLayout();
    // (the Vita's fast button (picture 115) at (487, 0) and its hold box at 0x1e7 - the iPhone's 444 + 43:
    // vita decomp 8110d828, 87245)
    st.edgeX = androidEdgeX(app) + vitaEdge43(app), st.edgeY = androidEdgeY(app);
    staffFade(st, app.game.scenario == 0, 1, 1, 1); // SetFade(colour, 0, 255, 1): covered
    for (int i = 0; i < kShown; ++i)
        st.lines[i] = App::StaffRoll::Line{staffLine(app, i).size() >= 3, lineStart(st) + 20.0f * float(i), i};
    st.next = kShown;
    cr::Image img;
    static const char* const kBack[3] = {"EndBack01.mct", "EndBack02.mct", "EndBack01_end.mct"}; // _gkpachEndBack
    if (loadImage(app, kBack[st.mode], img)) st.tex = app.renderer.upload(img);
    if (st.mode == 0 && loadImage(app, "EndBack01_end.mct", img)) st.endTex = app.renderer.upload(img);
    staffWin(app, 1, true);
}

void stepStaffRoll(App& app) {
    using namespace staffroll;
    App::StaffRoll& st = app.staffRoll;
    if (!st.open) return;
    for (int w = 0; w < 5; ++w) // the windows fade 4 frames
        st.win[w] = st.win[w] < st.winTo[w] ? std::min(st.winTo[w], st.win[w] + 0.25f) : std::max(st.winTo[w], st.win[w] - 0.25f);
    if (st.fadeAt < st.fadeFrames) st.fade = st.fadeFrom + (st.fadeTo - st.fadeFrom) * float(++st.fadeAt) / float(st.fadeFrames);
    const bool first = app.game.scenario == 0;
    switch (st.state) {
    case 0: st.state = 1; break; // the textures read
    case 1:
        staffWin(app, 0, true), staffWin(app, 3, true), staffWin(app, 2, true);
        st.state = 2;
        break;
    case 2: staffFade(st, first, 1, 0, 3000), st.timer = 60, st.state = 3; break;
    case 3: if (--st.timer < 0) st.state = 4, --st.timer; break;
    case 4:
        if (st.countdown < 1) {
            if (st.next < kLines + 1) {
                const float speed = first ? 2.0f : 1.4130435f;
                const int steps = st.fast ? 10 : 1;
                for (auto& l : st.lines)
                    for (int k = 0; k < steps; ++k) {
                        l.y -= speed;
                        if (l.y > lineEnd(st)) continue;
                        const int e = st.next++;
                        l.y = lineStart(st);
                        l.on = e < kLines && staffLine(app, e).size() >= 3;
                        l.entry = e;
                    }
            }
            const float step = first ? 0.2f : (1024 - screenH(st)) / 4600.0f, end = staffPicEnd(st);
            st.picY = std::max(end, st.fast ? st.picY + step * -10 : st.picY - step);
            if (st.next > kLines && st.picY == end) st.countdown = 100;
        } else {
            if (st.countdown == 5) staffWin(app, 3, false), staffWin(app, 2, false);
            if (--st.countdown < 1) st.state = 6;
        }
        break;
    case 6: if (++st.hold >= 120) st.state = 7; break;
    case 7: staffWin(app, 4, true), st.timer = 80, st.state = 8; break;
    case 8:
        st.endAlpha = std::min(255, st.endAlpha + 6);
        if (--st.timer < 0) staffFade(st, false, 0, 1, 6000), st.timer = 130, st.state = 9;
        break;
    case 9: if (--st.timer < 0) st.state = 10, --st.timer; break;
    case 10: staffWin(app, 0, false), staffWin(app, 1, false), st.timer = 4, st.state = 11; break;
    case 11: if (st.timer-- < 0) st.state = 5; break;
    case 5: // __mc_SetMenuStat(0xc, 4): the script goes on
        app.renderer.destroy(st.tex);
        app.renderer.destroy(st.endTex);
        st = App::StaffRoll();
        break;
    default: break;
    }
}

void staffRollTap(App&) {} // (Omega's staff roll takes no tap; CR1's end card did)

void drawStaffRoll(App& app) {
    const App::StaffRoll& st = app.staffRoll;
    if (!st.open) return;
    cr::Renderer& r = app.renderer;
    const float W = r.screenW(), H = cr::Renderer::kLogicalH;
    r.setOrigin(0);
    r.fillRect(0, 0, W, H, 0, 0, 0, 1);
    r.setOrigin(r.uiX());
    cr::MenuPainter& m = app.menu;
    if (!app.windowTex.id) return;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    {
        const PhoneMenu phoneMenu(app, m);
        // (the Vita: EndBack read by _MuCom_MctTexRead - kind 3, full size: the pictures' layer)
        const int keepLayer = r.layer();
        if (r.vitaScreen()) r.setLayer(3);
        if (st.win[0] > 0) outPicture(m, st.tex, 12, -16, st.picY + (st.phone ? -st.edgeY : -32), st.win[0]);
        r.setLayer(keepLayer);
        if (st.win[3] > 0)
            for (const auto& l : st.lines)
                if (l.on) m.text(staffLine(app, l.entry), 16, float(int(l.y)), false, st.win[3], 13);
        if (st.win[2] > 0) m.picture(115, st.phone ? 444 + st.edgeX : 460, st.phone ? -st.edgeY : -32, st.win[2]); // the fast button (Android: + ex, - ey)
        if (r.vitaScreen()) r.setLayer(3);
        if (st.mode == 0 && st.win[4] > 0 && st.endAlpha > 0) outPicture(m, st.endTex, 14, 176, 128, st.win[4] * float(st.endAlpha) / 255);
        r.setLayer(keepLayer);

    }
    r.setOrigin(0);
    if (st.fade > 0) {
        const float c = st.fadeWhite ? 1.0f : 0.0f;
        r.fillRect(0, 0, W, H, c, c, c, st.fade);
    }
    r.setOrigin(r.uiX());
}

// ---- The last battle's pair choice (Util 8; __menuu_LastBtSet / Prc / Put / Clr) ----
// Flag 0 clear: StartLastBtMenu - the rounds start over (no menu). Else, round `count` (0..3):
// everyone's HP / MP restored (pcparam_hpmp_all_recovery), window 0 the header (sheet 0 picture
// 109 at (8, 6)), 1 the round's title (font block 7 entry 0x68 + count) over a rule (picture 15 at
// (0, 56)), 2 the guardian's picture (hi / mizu / kaze / mu.mct, sheet 1 picture 5: 256 x 256 at
// (0, 64)) - 1 and 2 slide in from 140 / 240 to the left - and the pairs not yet used as plates.
// The plates: the leaders' names (MuScrList_ItemRegist(0x1e + 2p) registers block 7 entry id - 1). One chosen: the windows go, that pair is the party
// (Set_Prtyno) and its camp menu opens; once it closes, the question (block 6 entry 0x13 + count,
// var 0 = the leader's name, MuStr_GetCharNameId(2p) = block 7 entry 2p + 0x1d) and Yes / No: No
// back to the plates, Yes closes - count + 1, the pair's bit set, var 0 = the pair; after the
// fourth, count and the bits clear and so does flag 1.
namespace lastbt {
// The pairs: MuScrList rows (list layout 0, as the World Select's and the shop's) of
// ComUty_ComPlate_LastBt - plate 8 / 9 (lit), icon 4 at (12, 8), the leader's name at (56, 12)
constexpr float kRowX = 192, kRowY = 92, kRowStep = 48, kRowW = 256, kRowH = 44; // menu units
}

// MuScrList_ItemRegist(0x1e + 2p) registers block 7 entry id - 1: the pair's leader
std::string lastBattlePlate(const App& app, int p) { return sysText(app, 58029 + 2 * p); }

void lbWin(App& app, int w, bool on) { app.lastBattle.winTo[w] = on ? 1.0f : 0.0f; }

void openLastBattle(App& app) {
    App::LastBattle& lb = app.lastBattle;
    app.renderer.destroy(lb.tex);
    lb.open = true, lb.state = 0, lb.timer = 0, lb.cursor = 0, lb.chosen = -1, lb.confirm = 0;
    for (float& w : lb.win) w = 0;
    for (float& w : lb.winTo) w = 0;
    cr::Image img;
    static const char* const kGuardian[4] = {"hi.mct", "mizu.mct", "kaze.mct", "mu.mct"};
    if (loadImage(app, kGuardian[std::clamp(lb.count, 0, 3)], img)) lb.tex = app.renderer.upload(img);
    for (int c = 0; c < cr::kCharacters; ++c) { // pcparam_hpmp_all_recovery (the most MP, a number)
        app.game.party[size_t(c)].hp = -1;
        app.game.party[size_t(c)].mp = cr::memberStat(app.game, c, cr::kMp, app.itemTable);
    }
}

void lastBattleList(App& app, bool windows) { // __LastBt_CharChoiceInit
    App::LastBattle& lb = app.lastBattle;
    lb.pairs.clear();
    for (int p = 0; p < 4; ++p)
        if (!(lb.mask & (1 << p))) lb.pairs.push_back(p);
    lb.cursor = 0;
    if (windows) lbWin(app, 0, true), lbWin(app, 1, true), lbWin(app, 2, true);
    lbWin(app, 3, true); // the plates
    lb.state = 3;
}

void lastBattleChoose(App& app, int i) {
    App::LastBattle& lb = app.lastBattle;
    if (lb.state != 3 || i < 0 || i >= int(lb.pairs.size())) return;
    menuSound(app, se::kChoose);
    lb.chosen = lb.pairs[size_t(i)];
    lb.state = 4;
}

void lastBattleAnswer(App& app, bool yes) {
    App::LastBattle& lb = app.lastBattle;
    if (lb.state != 10) return;
    menuSound(app, se::kChoose);
    if (yes) lb.state = 11;
    else lbWin(app, 5, false), lbWin(app, 6, false), lb.state = 2;
}

void stepLastBattle(App& app) {
    App::LastBattle& lb = app.lastBattle;
    if (!lb.open) return;
    for (int w = 0; w < 7; ++w)
        lb.win[w] = lb.win[w] < lb.winTo[w] ? std::min(lb.winTo[w], lb.win[w] + 0.25f) : std::max(lb.winTo[w], lb.win[w] - 0.25f);
    switch (lb.state) {
    case 0: lb.state = 1; break;
    case 1: lastBattleList(app, true); break;
    case 2: lastBattleList(app, false); break;
    case 3: break; // a plate (lastBattleChoose)
    case 4:
        for (int w = 0; w < 4; ++w) lbWin(app, w, false);
        lb.state = 5, lb.timer = 4;
        break;
    case 5: if (lb.timer-- < 0) lb.state = 6; break;
    case 6: // Set_Prtyno, ModeCampMenu(1), SetCampMainMenu
        app.playerChar = 2 * lb.chosen;
        openCamp(app);
        lb.state = 7;
        break;
    case 7: if (!app.campOpen) lb.state = 8; break;
    case 8: lb.state = 9; break;
    case 9:
        for (int w : {0, 1, 2, 5, 6}) lbWin(app, w, true);
        lb.confirm = 0;
        lb.state = 10;
        break;
    case 10: break; // Yes / No (lastBattleAnswer)
    case 11: case 12:
        for (int w = 0; w < 7; ++w) lbWin(app, w, false);
        lb.state = 13, lb.timer = 4;
        break;
    case 13: if (lb.timer-- < 0) lb.state = 14; break;
    case 14: { // __mc_SetMenuStat(9, 4) - __menuu_LastBtClr
        app.renderer.destroy(lb.tex);
        lb.open = false;
        ++lb.count;
        lb.mask |= 1 << lb.chosen;
        app.game.vars[0] = lb.chosen;
        if (lb.count >= 4) {
            lb.count = lb.mask = 0;
            app.game.setFlag(1, false);
        }
        break;
    }
    default: break;
    }
}

void drawLastBattle(App& app) {
    App::LastBattle& lb = app.lastBattle;
    if (!lb.open || app.campOpen || !app.windowTex.id) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    // __menuu_LastBtPut: its windows sit at (0, 0) (as the camp's), so these are menu units: the
    // header (picture 109) at (8, 6), the round's title (FontDisp (12, 40)) over the rule (15 at
    // (0, 56)), the guardian at (0, 64), the rows, the board (ComUty_MsgBoardDraw: picture 12 at
    // (0, 220)) and Yes / No. The iPhone layout: all of it at its screen's offset (16, 32)
    const PhoneMenu phoneMenu(app, m);
    // Widescreen: the heading, rule and picture to the screen's left, the rows and Yes / No to its
    // right, the question centred
    r.setOrigin(wideLeft(app));
    auto ease = [](float t) { return 1 - (1 - t) * (1 - t); };
    if (lb.win[0] > 0) m.picture(110, 8, 6, lb.win[0]); // (_stMuPictInfo_LastBt: picture 110 at (8, 6))
    if (lb.win[1] > 0) {
        const float dx = -140 * (1 - ease(lb.win[1]));
        m.picture(15, dx, 56, lb.win[1]);
        m.textCentred(sysText(app, 58000 + 0x68 + lb.count), dx + 12, 47, false, lb.win[1], 15);
    }
    if (lb.win[2] > 0 && lb.tex.id) { // sheet 1 picture 5: (0, 0, 256, 256) at (0, 64)
        const float dx = -240 * (1 - ease(lb.win[2]));
        // (the Vita: hi / mizu / kaze / mu read by _MuCom_MctTexRead - kind 3, full size: the pictures' layer)
        const int keep = r.layer();
        if (r.vitaScreen()) r.setLayer(3);
        m.imagePiece(lb.tex, dx, 64, 0, 0, 256, 256, lb.win[2]);
        r.setLayer(keep);

    }
    r.setOrigin(wideRight(app));
    if (lb.win[3] > 0)
        for (size_t i = 0; i < lb.pairs.size(); ++i) {
            using namespace lastbt;
            const float a = lb.win[3], x = kRowX + 200 * (1 - ease(a)), y = kRowY + kRowStep * float(i);
            m.picture(lb.cursor == int(i) ? 9 : 8, x, y, a);
            m.piece(x + 12, y + 8, float(4 % 16) * 32, 248 + float(4 / 16) * 32, 32, 32, a); // icon 4
            m.text(lastBattlePlate(app, lb.pairs[i]), x + 56, y + 12, false, a, 14);
        }
    r.setOrigin(r.uiX());
    if (lb.win[5] > 0) { // the question (ComUty_MsgBoardDraw at y 220, sliding up from 160 below)
        const float dy = 160 * (1 - ease(lb.win[5]));
        m.picture(12, 0, 220 + dy, lb.win[5]);
        m.text(sysText(app, 57000 + 0x13 + lb.count, sysText(app, 58029 + 2 * std::max(0, lb.chosen))), 24, 234 + dy,
               false, lb.win[5], 15);
    }
    if (lb.win[6] > 0) { // Yes / No (from 360 to the right)
        const float dx = 360 * (1 - ease(lb.win[6]));
        r.setOrigin(wideRight(app));
        const bool hy = pressIn(app, kPressLastBattle, wideRight(app), 336, 104, 136, 40), hn = pressIn(app, kPressLastBattle, wideRight(app), 336, 160, 136, 40);
        m.plate(336 + dx, 104, 0, 0, hy || (!hn && lb.confirm == 0), lb.win[6], false); // (a held plate takes the cursor)
        m.plate(336 + dx, 160, 1, 1, hn || (!hy && lb.confirm == 1), lb.win[6], false);
    }
    r.setOrigin(r.uiX());
}
void lastBattleKey(App& app, SDL_Keycode key) {
    App::LastBattle& lb = app.lastBattle;
    if (lb.state == 3) {
        const int n = int(lb.pairs.size());
        if (n <= 0) return;
        if (key == SDLK_UP || key == SDLK_W) lb.cursor = (lb.cursor + n - 1) % n, menuSound(app, se::kCursor);
        else if (key == SDLK_DOWN || key == SDLK_S) lb.cursor = (lb.cursor + 1) % n, menuSound(app, se::kCursor);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E) lastBattleChoose(app, lb.cursor);
    } else if (lb.state == 10) {
        if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_W || key == SDLK_S) lb.confirm ^= 1, menuSound(app, se::kCursor);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E) lastBattleAnswer(app, lb.confirm == 0);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) lastBattleAnswer(app, false);
    }
}

void lastBattleClick(App& app, float px, float py) {
    App::LastBattle& lb = app.lastBattle;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my;
    menuPointAt(app, lx, ly, wideRight(app), mx, my); // menu units (widescreen: the plates on the right)
    auto in = [&](float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    if (lb.state == 3) {
        for (size_t i = 0; i < lb.pairs.size(); ++i)
            if (in(lastbt::kRowX, lastbt::kRowY + lastbt::kRowStep * float(i), lastbt::kRowW, lastbt::kRowH)) {
                lb.cursor = int(i);
                lastBattleChoose(app, int(i));
            }
    } else if (lb.state == 10) { // the boxes (0x88 x 0x28)
        if (in(336, 104, 136, 40)) lastBattleAnswer(app, true);
        else if (in(336, 160, 136, 40)) lastBattleAnswer(app, false);
    }
}

// ---- The World Select (Util 1: __menuu_WldChoiceSet / Prc / Put / Clr) ----
//
// Windows (all at (0, 0), sliding in from an offset): 0 the title (picture 0 at (8, 4)), 1 the
// gate's picture (MAP_0n.mct, sheet 1 picture 1: 256 x 256 at (0, 64)), 2 the rule (picture 15
// at (0, 56)) under "World Select" (block 7 entry 0 at (12, 40)), 3 the rule (16 at (24, 72))
// under ">Level Select" (entry 1 at (40, 58)), 6 / 7 the lists, 8 Yes / No, 9 / 10 the message
// board at y 220. The gates (list 0, block 7 entries 2..7) follow the pair's order (0xca7d0),
// offered when their flags are set (0xca810 + k, the sixth 0xca820; none: the first alone); a
// plate (ComUty_ComPlate_Wld) is picture 8 (9 chosen), icon 0x3d at (12, 8), the name at (56,
// 12) and, for gates 0..4, four pips at (195 + 14k, 30) - picture 11 for each of the gate's
// flags set (0xc9bb0), else 10. A gate whose flag 0xca7f0 is set first asks "Skip the first
// half?" (block 6 entry 0x40); then the level bands (list 1: entries 8.., 2 + the pair's flags
// 0xca830 set); then "Is this okay?" (entry 0x12) with the gate and band. The sixth gate has no
// bands. Clr: Back sets flag 0; else var 0 = the gate, var 1 = the band, var 2 = the pair,
// flag 1 = the skip (not for gates 4 and 5).
namespace wldsel {
// Omega's (__menuu_WldChoiceSet / Prc / Clr): scenario 0's worlds 0..5 (_gkanWldSelectFlgTbl0),
// scenario 1's 6..12 (Tbl1), offered when their flags are set (none: the first alone), in
// _kachWorldNoTbl's order (as numbered); the last of each (5 / 12) has no level bands. Bands:
// scenario 0 one per flag of _gkanLvlSelectFlgTbl set, scenario 1 by the collection held. A
// scenario-0 world below 5 whose flag _gkashStgSkipTbl is set first asks "Skip the first half?".
// Clr: var 0 the world (within the scenario), var 1 the band, var 2 the scenario, flag 1 the skip;
// the band is also the scenario's level byte (+0x1544 + s: enemies, treasure)
constexpr short kWorldFlag[13] = {1076, 1077, 1078, 1079, 1081, 1082, 1576, 1577, 1578, 1579, 1580, 1581, 1582};
constexpr short kLevelFlag[5] = {1076, 1163, 1203, 1183, 1223};
constexpr short kSkipFlag[5] = {1162, 1202, 1182, 1222, 1274};
inline int firstWorld(int scenario) { return scenario ? 6 : 0; }
inline int lastWorld(int scenario) { return scenario ? 12 : 5; }
constexpr float kListX = 192, kListY = 92, kListStep = 48; // list layout 0, as the shop's rows
constexpr int kRows = 4;
constexpr float kBackX = 208, kBackY = 320;

void on(App& app, int w, float x = 0, float y = 0) {
    App::WorldSelect& ws = app.worldSelect;
    ws.winTo[w] = 1, ws.fromX[w] = x, ws.fromY[w] = y;
}
void off(App& app, int w, float x = 0, float y = 0) {
    App::WorldSelect& ws = app.worldSelect;
    ws.winTo[w] = 0, ws.fromX[w] = x, ws.fromY[w] = y;
}
// __MuWldChoice_LvlToYesNoChange: the lists' windows go, Yes / No and the board come
void toYesNo(App& app, bool ask) {
    App::WorldSelect& ws = app.worldSelect;
    if (!ask) off(app, 2, 0, -140), off(app, 3, 0, -140);
    off(app, 4), off(app, 5);
    on(app, 8, 140, 0), on(app, 9, 0, 160), on(app, 10, 0, 160);
    ws.asking = ask;
    ws.confirm = 0;
}
// MuScrList_Draw's scroll bar (ComUty_GuageBarDraw) beside the rows while some are out of sight,
// as the shop's: track, the arrows (76 / 77), the knob (106)
void scrollBar(cr::MenuPainter& m, float dx, int top, int n, float a) {
    if (n <= kRows) return;
    const float sx = 462 + dx, y = kListY, h = kListStep * kRows;
    const float dark[4] = {0.05f, 0.05f, 0.08f, 0.8f * a};
    m.quad(sx, y, 8, h, dark, dark);
    m.picture(76, sx - 4, y - 14, a);
    m.picture(77, sx - 4, y + h - 2, a);
    m.picture(106, sx, y + (h - 40) * float(top) / float(n - kRows), a);
}
void levelList(App& app, float from, bool fresh) {
    App::WorldSelect& ws = app.worldSelect;
    if (fresh) ws.levelCursor = 0, ws.levelTop = 0; // (__WldChoice_LvlChoiceListInit: a new list for each world)
    on(app, 3, -140, 0);
    on(app, 7, from, 0);
    ws.levelCursor = std::clamp(ws.levelCursor, 0, ws.levels - 1);
    ws.state = 6;
}
} // namespace wldsel

bool worldAsksSkip(const App& app, int world, int pair) { // (WldChoicePrc: a scenario-0 world below 5 once its flag is set)
    return pair == 0 && world >= 0 && world < 5 && app.game.flag(wldsel::kSkipFlag[world]);
}

void openWorldSelect(App& app) {
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    for (cr::Texture& t : ws.maps) app.renderer.destroy(t);
    ws = App::WorldSelect();
    ws.pair = std::clamp(app.game.scenario, 0, cr::kScenarios - 1);
    for (int w = firstWorld(ws.pair); w <= lastWorld(ws.pair); ++w)
        if (app.game.flag(kWorldFlag[w])) ws.worlds.push_back(w);
    if (ws.worlds.empty()) ws.worlds.push_back(firstWorld(ws.pair));
    ws.levels = 0;
    if (ws.pair == 0)
        for (short f : kLevelFlag) ws.levels += app.game.flag(f);
    else { // by the collection held (551..630): 1 under 4, then one more each 8 to 9 under 64; from
        // 64, 10 (11 with flag 0x65), then flags 0x66..0x69 12..15 and 0x6a 20
        int n = 0;
        for (int i = 551; i <= 630; ++i) n += ownedCount(app, i) > 0;
        if (n < 64) ws.levels = n < 4 ? 1 : n < 8 ? 2 : 2 + n / 8;
        else {
            ws.levels = app.game.flag(0x65) ? 11 : 10;
            for (int f = 0x66; f <= 0x69; ++f)
                if (app.game.flag(f)) ws.levels = 12 + (f - 0x66);
            if (app.game.flag(0x6a)) ws.levels = 20;
        }
    }
    ws.levels = std::max(1, ws.levels);
    ws.world = ws.worlds[0];
    // __menuu_WldChoiceSet: the gates' pictures by the table at e3bd0 (the first pair's six) / e3be8 (the
    // second's seven) - not in number order: the second and third gates swapped, the last two 06 and 13
    static const char* const kMapName[2][7] = {{"MAP_01.mct", "MAP_03.mct", "MAP_02.mct", "MAP_04.mct", "MAP_06.mct", "MAP_13.mct", nullptr},
                                               {"MAP_07.mct", "MAP_08.mct", "MAP_09.mct", "MAP_10.mct", "MAP_11.mct", "MAP_12.mct", "MAP_14.mct"}};
    for (int k = 0; k < 7; ++k) {
        cr::Image img;
        const char* name = kMapName[ws.pair ? 1 : 0][k];
        if (name && loadImage(app, name, img)) ws.maps[k] = app.renderer.upload(img);
    }
    on(app, 0), on(app, 1), on(app, 2, -140, 0), on(app, 6, -480, 0), on(app, 4), on(app, 5);
    ws.state = 1;
    app.worldChoiceOpen = true;
    app.worldCursor = 0;
}

void closeWorldSelect(App& app) { // __menuu_WldChoiceClr
    App::WorldSelect& ws = app.worldSelect;
    for (cr::Texture& t : ws.maps) app.renderer.destroy(t);
    app.worldChoiceOpen = false;
    app.game.setFlag(0, ws.cancelled);
    if (ws.cancelled) return;
    app.game.vars[0] = ws.world - wldsel::firstWorld(ws.pair);
    app.game.vars[1] = ws.level; // (DAT_000eb2a8: 0 from WldChoiceSet's memset, else the band list's last answer -
    // -1 after a Back out of it, kept for the last world, which has no list)
    app.game.vars[2] = ws.pair;
    if (ws.skip) app.game.setFlag(1, true);
    if (app.game.vars[0] == 5) app.game.setFlag(1, false);
    app.game.bands[size_t(ws.pair)] = app.game.vars[1]; // (+0x84340 + scenario)
    app.treasureReroll = true; // MuCom_BitSet(0x40000000): the next area loaded draws its boxes again
}

void worldSelectChoose(App& app, int pos) { // state 1: a gate, or Back (-1)
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    if (ws.state != 1) return;
    if (pos < 0) { // Back: everything goes
        menuSound(app, se::kBack);
        off(app, 6, 480, 0);
        for (int w = 0; w < 11; ++w)
            if (w != 6) off(app, w);
        ws.cancelled = true;
        ws.state = 9, ws.timer = 4;
        return;
    }
    if (pos >= int(ws.worlds.size())) return;
    menuSound(app, se::kChoose);
    ws.world = ws.worlds[size_t(pos)];
    off(app, 6, -480, 0);
    if (ws.world == lastWorld(ws.pair)) {
        toYesNo(app, false);
        ws.state = 7, ws.timer = 4;
    } else if (ws.pair == 0 && ws.world < 5 && app.game.flag(kSkipFlag[ws.world])) {
        toYesNo(app, true);
        ws.state = 3, ws.timer = 4;
    } else
        levelList(app, 480, true);
}

void worldSelectLevel(App& app, int n) { // state 6: a band, or Back (-1)
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    if (ws.state != 6) return;
    if (n < 0) {
        menuSound(app, se::kBack);
        off(app, 3, 0, -140), off(app, 7, 480, 0), on(app, 6, -480, 0);
        ws.skip = false;
        ws.level = -1; // (DAT_000eb2a8 = MuScrList_GetChoiceListNo(): -1, kept for a gate chosen next)
        ws.state = 1;
        return;
    }
    if (n >= ws.levels) return;
    menuSound(app, se::kChoose);
    ws.level = n;
    toYesNo(app, false);
    off(app, 7, -480, 0);
    ws.state = 7, ws.timer = 4;
}

void worldSelectAnswer(App& app, bool yes, bool cancelled) { // states 4 (skip?) and 8 (okay?)
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    if (ws.state != 4 && ws.state != 8) return;
    menuSound(app, cancelled ? se::kBack : se::kChoose); // (the Vita's × for No: 0x94)
    if (ws.state == 4) { // ComUty_YesNoChoiceProc: 2 the first box (Yes)
        off(app, 8, -360, 0), off(app, 9, 0, 160), off(app, 10, 0, 160);
        on(app, 4), on(app, 5);
        ws.skip = yes;
        levelList(app, 480, true);
        return;
    }
    if (!yes) {
        off(app, 8, 140, 0), off(app, 9, 0, 160), off(app, 10, 0, 160);
        on(app, 2, -140, 0), on(app, 4), on(app, 5);
        if (ws.world == lastWorld(ws.pair)) {
            on(app, 6, -480, 0);
            ws.state = 1;
        } else
            levelList(app, -480, false); // (state 8's No: the list as it was, MuScrList_DispOn)
        return;
    }
    off(app, 0), off(app, 1), off(app, 8), off(app, 9, 0, 160), off(app, 10, 0, 160);
    ws.state = 9, ws.timer = 4;
}

void stepWorldSelect(App& app) {
    App::WorldSelect& ws = app.worldSelect;
    if (!app.worldChoiceOpen) return;
    for (int w = 0; w < 11; ++w)
        ws.win[w] = ws.win[w] < ws.winTo[w] ? std::min(ws.winTo[w], ws.win[w] + 0.25f) : std::max(ws.winTo[w], ws.win[w] - 0.25f);
    { // MuScrList_Proc: the rows step 8 a frame toward the focus row's place (a jump sets it at once)
        const int list = ws.winTo[7] > 0 || ws.state == 6 ? 1 : 0;
        const float to = wldsel::kListStep * float(list ? ws.levelCursor : ws.cursor);
        if (ws.scrollList != list || std::fabs(to - ws.scroll) > wldsel::kListStep) ws.scrollList = list, ws.scroll = to;
        else ws.scroll = ws.scroll < to ? std::min(to, ws.scroll + 8) : std::max(to, ws.scroll - 8);
    }
    ++ws.mark;
    switch (ws.state) {
    case 3: if (--ws.timer < 0) ws.state = 4; break;
    case 7: if (--ws.timer < 0) ws.state = 8; break;
    case 9: if (--ws.timer < 0) ws.state = 10; break;
    case 10: closeWorldSelect(app); break;
    default: break;
    }
    if (ws.state == 1 && !ws.worlds.empty()) ws.world = ws.worlds[size_t(std::clamp(ws.cursor, 0, int(ws.worlds.size()) - 1))];
}

void drawWorldSelect(App& app) {
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    if (!app.worldChoiceOpen || !app.windowTex.id) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    // WldChoicePut's windows sit at (0, 0): menu units, 1.0.1's records - the title (picture 0) at
    // (8, 4), the gate's picture at (0, 64), the rules (15 / 16) at (0, 56) / (24, 72) under
    // FontDisp (12, 40) / (40, 58), the board (ComUty_MsgBoardDraw: picture 12) at (0, 220). The
    // iPhone layout: all of it at its screen's offset (16, 32) (o), the lists and Yes / No too;
    // Back is placed as tex2d + (16, 32) already
    const bool phone = phoneMenus(app); // (Omega: the iPhone layout in both)
    auto ownPlace = [&](bool own) { m.setOffset(own && phone ? 16.0f : 0.0f, own && phone ? 32.0f : 0.0f); };
    const float ox = phone ? 16.0f : 0.0f, oy = phone ? 32.0f : 0.0f;
    // Widescreen: the title, rules and the gate's picture to the screen's left, the lists (with
    // their Back) and Yes / No to its right, the board centred
    r.setOrigin(wideLeft(app));
    auto ease = [](float t) { return 1 - (1 - t) * (1 - t); };
    auto dx = [&](int w) { return ws.fromX[w] * (1 - ease(ws.win[w])); };
    auto dy = [&](int w) { return ws.fromY[w] * (1 - ease(ws.win[w])); };
    const int mapIx = std::clamp(ws.world - wldsel::firstWorld(ws.pair), 0, 6);
    if (ws.win[1] > 0 && ws.maps[mapIx].id) { // the world's picture
        // (the Vita: read by _MuCom_MctTexRead - kind 3, full size: the pictures' layer)
        const int keep = r.layer();
        if (r.vitaScreen()) r.setLayer(3);
        m.image(ws.maps[mapIx], dx(1) + ox, dy(1) + 64 + oy, 256, 256, ws.win[1]); // (the whole sheet: the _hd one is 512 square)
        r.setLayer(keep);
    }
    if (ws.win[0] > 0) m.picture(0, dx(0) + 8 + ox, dy(0) + 4 + oy, ws.win[0]);
    if (ws.win[2] > 0) { // (the labels as the camp's: FontDisp's y the line box's top)
        m.picture(15, dx(2) + ox, dy(2) + 56 + oy, ws.win[2]);
        m.textCentred(sysText(app, 58000), dx(2) + 12 + ox, dy(2) + 47 + oy, false, ws.win[2], 14); // (the menu font, as the Camp's lines)
    }
    if (ws.win[3] > 0) {
        m.picture(16, dx(3) + 24 + ox, dy(3) + 72 + oy, ws.win[3]);
        m.textCentred(sysText(app, 58001), dx(3) + 40 + ox, dy(3) + 65 + oy, false, ws.win[3], 14);
    }
    // MuScrList mode 0 (as the Camp's and the shop's lists - __menuu_WldChoicePrc 0: MuScrList_Init(4, 5),
    // SetIdInfo(0, 6, type 7) for the gates, __WldChoice_LvlChoiceListInit's (1, 7, type 7) for the bands): row
    // k at (192, 150 + 48k - offset), cut to 76..280, the focus row on the line lit; the down pill (0x90) over
    // the first row while the focus is one of the first two, the up pill (0x91) under the last while it is one
    // of the last two; the gauge (ComUty_GuageBarDraw at (456, 72)); the plates ComUty_ComPlate_Wld's (icon
    // 0x3d and the name). Part 8 (rU44ipMZ91g) 3.0 s: "Conglom Plant" lit on the line, the up pill under it
    auto list = [&](int w, int n, auto&& name) {
        using namespace campui;
        const float x = wldsel::kListX + dx(w), a = ws.win[w];
        const float scroll = ws.scrollList == (w == 7 ? 1 : 0) ? ws.scroll : 0.0f;
        const int focus = std::clamp(int((scroll + 28) / kRowH), 0, std::max(0, n - 1));
        ownPlace(true);
        m.setClip(kListTop, kListBottom);
        for (int i = 0; i < n; ++i) {
            const float y = kFocusY + kRowH * float(i) - scroll;
            if (y + kRowH <= kListTop || y >= kListBottom) continue;
            m.picture(i == focus ? 9 : 8, x, y, a);
            m.piece(x + 12, y + 8, float(0x3d % 16) * 32, 248 + float(0x3d / 16) * 32, 32, 32, a); // icon 0x3d
            plateText(m, name(i), x + 56, y + 12, 0, a);
        }
        if (n > 2) {
            if (focus < 2) m.picture(0x90, x + 88, 112 - scroll, a);
            if (focus >= n - 2) m.picture(0x91, x + 88, 208 + kRowH * float(n - 1) - scroll, a);
        }
        m.clearClip();
        drawCampScrollBar(m, scroll, n, a);
        ownPlace(false);
        const bool ph = app.renderer.phoneLayout(); // (lit while held: the box worldSelectClick tests)
        m.backBar(wldsel::kBackX + dx(w), wldsel::kBackY,
                  pressIn(app, kPressWorld, vitaRight(app), wldsel::kBackX - (ph ? 16 : 0), wldsel::kBackY - (ph ? 32 : 0), 232, 24), a);
    };
    r.setOrigin(vitaRight(app)); // (the Vita: its list table's x 235 and Back 235 - 43 to the right; Yes / No not)
    if (ws.win[6] > 0) // the gates
        list(6, int(ws.worlds.size()), [&](int i) { return sysText(app, 58002 + ws.worlds[size_t(i)]); });
    if (ws.win[7] > 0) // the level bands
        list(7, ws.levels, [&](int i) { return sysText(app, 58200 + i); });
    r.setOrigin(r.uiX());
    if (ws.win[9] > 0) { // the board (ComUty_MsgBoardDraw at y 220, from 160 below)
        const float a = ws.win[9], y = 220 + oy + dy(9);
        m.picture(12, ox, y, a);
        // (ComUty_MsgBoardDraw: the mark, picture 13 out and back a tick a frame, at (458, 298); the lines in the
        // menu font - Part 8 6.0 s: "Conglom Plant / LV41 - LV50 / Is this okay?", the mark at the lower right)
        const short* mk = cr::kMenuPicture[13];
        m.piece(458 + ox, y + 78, float(mk[0] + 20 * pictAnimFrame(ws.mark, 1, 4)), mk[1], mk[2], mk[3], a);
        std::vector<std::string> lines;
        if (ws.asking) lines = {sysText(app, 57000 + 0x40)};
        else if (ws.world == wldsel::lastWorld(ws.pair)) lines = {sysText(app, 58002 + ws.world), sysText(app, 58018)};
        else lines = {sysText(app, 58002 + ws.world), sysText(app, 58200 + ws.level), sysText(app, 58018)};
        for (size_t k = 0; k < lines.size(); ++k) m.text(lines[k], 24 + ox, y + 14 + 16 * float(k), false, a, 13);
    }
    r.setOrigin(wideRight(app));
    if (ws.win[8] > 0) { // Yes / No
        ownPlace(true);
        // (__WldChoice_YesNoChoiceProc: ComUty_YesNoChoiceProc lights only the box a finger is on - Part 8 6.0 s
        // both dark; keys and pads keep the cursor's lit)
        const bool lit = !app.pointerLast;
        const bool hy = pressIn(app, kPressWorld, wideRight(app), 336, 104, 136, 40), hn = pressIn(app, kPressWorld, wideRight(app), 336, 160, 136, 40);
        m.plate(336 + dx(8), 104, 0, 0, hy || (!hn && lit && ws.confirm == 0), ws.win[8], false); // (a held plate takes the cursor)
        m.plate(336 + dx(8), 160, 1, 1, hn || (!hy && lit && ws.confirm == 1), ws.win[8], false);
    }
    ownPlace(false);
    r.setOrigin(r.uiX());
}

void worldSelectKey(App& app, SDL_Keycode key) {
    App::WorldSelect& ws = app.worldSelect;
    const bool up = key == SDLK_UP || key == SDLK_W, down = key == SDLK_DOWN || key == SDLK_S;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK;
    if (ws.state == 1) {
        const int n = int(ws.worlds.size());
        if ((up || down) && n > 0) ws.cursor = (ws.cursor + (up ? n - 1 : 1)) % n, menuSound(app, se::kCursor);
        else if (enter) worldSelectChoose(app, ws.cursor);
        else if (back) worldSelectChoose(app, -1);
    } else if (ws.state == 6) {
        if (up || down) ws.levelCursor = (ws.levelCursor + (up ? ws.levels - 1 : 1)) % ws.levels, menuSound(app, se::kCursor);
        else if (enter) worldSelectLevel(app, ws.levelCursor);
        else if (back) worldSelectLevel(app, -1);
    } else if (ws.state == 4 || ws.state == 8) {
        if (up || down || key == SDLK_LEFT || key == SDLK_RIGHT) ws.confirm ^= 1, menuSound(app, se::kCursor);
        else if (enter) worldSelectAnswer(app, ws.confirm == 0);
        else if (back) worldSelectAnswer(app, false, true);
    }
}

void worldSelectClick(App& app, float px, float py) {
    using namespace wldsel;
    App::WorldSelect& ws = app.worldSelect;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my;
    menuPointAt(app, lx, ly, wideRight(app), mx, my); // (widescreen: the lists and Yes / No on the right)
    auto in = [&](float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    if (ws.state == 1 || ws.state == 6) {
        mx -= vitaEdge43(app); // (the Vita's list and Back 43 to the right)

        const bool phone = phoneMenus(app); // (Back: tex2d + (16, 32) already, see drawWorldSelect)
        if (in(kBackX - (phone ? 16 : 0), kBackY - (phone ? 32 : 0), 232, 24)) {
            if (ws.state == 1) worldSelectChoose(app, -1);
            else worldSelectLevel(app, -1);
            return;
        }
        // (MuScrList mode 0: the row on the line is chosen, another tapped comes to the line; the pills jump to
        // the other end)
        const int n = ws.state == 1 ? int(ws.worlds.size()) : ws.levels;
        int& cur = ws.state == 1 ? ws.cursor : ws.levelCursor;
        const float scroll = ws.scroll;
        if (n > 2 && cur < 2 && in(kListX + 88, 112 - scroll, 80, 24)) { cur = n - 1; menuSound(app, se::kCursor); return; }
        if (n > 2 && cur >= n - 2 && in(kListX + 88, 208 + kListStep * float(n - 1) - scroll, 80, 24)) { cur = 0; menuSound(app, se::kCursor); return; }
        if (my < campui::kListTop || my >= campui::kListBottom) return;
        for (int i = 0; i < n; ++i)
            if (in(kListX, campui::kFocusY + kListStep * float(i) - scroll, 256, 48)) {
                if (i != cur) { cur = i; menuSound(app, se::kCursor); return; }
                if (ws.state == 1) worldSelectChoose(app, i);
                else worldSelectLevel(app, i);
                return;
            }
    } else if (ws.state == 4 || ws.state == 8) {
        if (in(336, 104, 136, 40)) worldSelectAnswer(app, true);
        else if (in(336, 160, 136, 40)) worldSelectAnswer(app, false);
    }
}

void drawCamp(App& app) {
    using namespace campui;
    App::Camp& c = app.camp;
    // A once-only message (the field's window) or the Emergency Exit past CampMain 0xd: no windows
    if (c.notice || (c.phase == App::Camp::Exit && c.exitT >= kCampExitGone)) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline); // (the scene under the menu mask: drawFrame's)
    r.setOrigin(r.uiX());
    const PhoneMenu phoneMenu(app, m);
    // Widescreen: the heading, the boards and the section labels to the screen's left, the plates,
    // lists, Yes / No, the clock and Oz to its right (as the shop); Back and the message centred
    const auto left = [&] { r.setOrigin(wideLeft(app)); };
    const auto right = [&] { r.setOrigin(wideRight(app)); };
    // (the Vita's CampMainPut: the plates at 0x173, the lists - its table's x 235 -, HELP (187) and Back (235) 43 to
    // the right; Yes / No, the count, the boards and the labels where 3.1.0 has them)
    const auto rightV = [&] { r.setOrigin(vitaRight(app)); };
    const float v43 = vitaEdge43(app);
    const auto centre = [&] { r.setOrigin(r.uiX()); };
    left();
    campTrackPhase(c);
    const float ease = campEase(app);
    const bool main = c.page == App::Camp::Main;
    const bool items = c.page == App::Camp::Items;
    const float leave = c.leaving ? std::min(1.0f, float(c.leaving) / 4.0f) : 0.0f; // (off to Scenarios: all of it goes)
    // "Camp" (window 0): CampMain 0xd takes it off with the rest at the Emergency Exit's end (kCampExitHide), whole
    // the frame after, then 3/4 .. gone
    const float exitOff = c.phase == App::Camp::Exit ? std::clamp(float(c.exitT - kCampExitHide - 1) / 4.0f, 0.0f, 1.0f) : 0.0f;
    m.picture(81, 8, 4, (1 - leave) * (1 - exitOff)); // "Camp"
    // The main screen's pieces slide away on a page (CampMain 6), before the page's own come (campEase), and stay
    // away while the page's go on Back
    const float out = main ? std::max(1 - ease, leave) : c.pageOut ? 1.0f : std::min(1.0f, float(c.anim) / 4.0f);
    // (a window going off is still whole the frame after - ClrWin's state 5 starts its fade at 0xff0 - while its
    // place moves a quarter: Part 13 1171.84 s, the plates a quarter out at full strength, the clock whole three
    // frames)
    const float outA = main ? out : c.pageOut ? 1.0f : std::clamp(float(c.anim - 1) / 4.0f, 0.0f, 1.0f);
    // The face boards (window 2): the main screen's and every member choice's (CampMainPrc 0's 3,
    // the Equip and Status pages keep them from the main screen); off once a member is chosen
    const float faceOut = main ? std::max(1 - ease, leave)
                          : items ? std::min(out, 1 - std::min(ease, campPhaseEase(app, App::Camp::Member)))
                          : c.page == App::Camp::Equip ? 1 - equipWins(app).faces.e // (off as a member is chosen, on with Back)
                          : c.phase == App::Camp::Member ? 0.0f : out;
    for (int k = 0; k < campMembers(app); ++k) {
        const int ch = campCharacter(app, k);
        int hp, hpMax, mp, mpMax;
        memberHpMp(app, ch, hp, hpMax, mp, mpMax);
        drawFaceBoard(app, m, -240 * faceOut, 24 + 88.0f * float(k), ch, hp, hpMax, mp, mpMax, 1 - (faceOut == out ? outA : faceOut));
    }
    rightV();
    for (int i = 0; i < campEntries(app); ++i) {
        const bool save = i == 5;
        // (lit: the keys' cursor - touch play none -, or a touch holding it: FUN_0006c548's DAT_000ec590)
        const bool held = main && campPressIn(app, vitaRight(app), kPlateX, kPlateY + kPlateStep * float(i), 136, 40);
        m.plate(kPlateX + 360 * out, kPlateY + kPlateStep * float(i), save ? 6 : plateIcon(kWords[i]), save ? -1 : kWords[i],
                (main && c.cursor == i && c.btnRow < 0 && !app.pointerLast) || held, 1 - outA, !save && !kReady[i], save ? "Save" : "");
    }
    centre();
    left();
    if (main || outA < 1) { // __menuu_CampMainPut: Scenarios (162; 128 "new" over it while no cell is open) and, in the
        // second scenario only (window 0x15), Collection (161) under the boards
        // (the one lit - the pad's place on them; 3.1.0 a touch held - picture 163 under it: 0xd3770 / 0xd37d0)
        if (main && (c.btnRow == 0 || campButtonHeld(app, 0))) m.picture(163, 12 - 120 * out, 236, 1 - outA);
        m.picture(162, 12 - 120 * out, 236, 1 - outA); // (windows 0x14 / 0x15: WinDispOn / Off at x -120)
        if (campScenariosNew(app)) m.picture(128, 12 - 120 * out, 236, 1 - outA);
        if (app.game.scenario != 0 && main && (c.btnRow == 1 || campButtonHeld(app, 1))) m.picture(163, 84 - 120 * out, 236, 1 - outA);
        if (app.game.scenario != 0) m.picture(161, 84 - 120 * out, 236, 1 - outA);
    }
    const int ch = campCharacter(app, c.member);
    const int item = c.list.empty() ? 0 : c.list[size_t(c.listCursor)];
    auto memberPlates = [&](bool lit, float e) {
        rightV();
        for (int k = 0; k < campMembers(app); ++k) {
            const int word = memberWord(app, campCharacter(app, k));
            // (window 8: WinDispOn(8, 0x168) - in from 360 to the right, as the main plates go out)
            // (__MuCommon_CharChoiceProc: the plate - or the member's face board - held lights it)
            const bool held = lit && (campPressIn(app, vitaRight(app), kPlateX, kMemberY + kMemberStep * float(k), 136, 40) ||
                                      campPressIn(app, wideLeft(app), 8, 0x30 + 0x58 * float(k), 0xfc, 0x50));
            m.plate(kPlateX + 360 * (1 - e), kMemberY + kMemberStep * float(k), plateIcon(word), word,
                    (lit && c.member == k && !app.pointerLast) || held, e, false);
        }
        left();
    };
    auto helpBoard = [&](int it, float e) { // HELP: the item's text across the top (as the shop's)
        if (it <= 0) return;
        rightV();
        drawHelpWindow(app, m, it, e, 480 * (1 - e)); // (MuScrList's: in from 480 to the right with the list)
        left();
    };
    // MuScrList_Draw (mode 0): row k at (x, 150 + 48k - offset), cut to y 76..280 (its mask quads over 0..76
    // and 280..356), the row on the line lit; with more than two rows the down pill (0x90) at (280, 112 -
    // offset) while the focus is one of the first two and the up pill (0x91) under the last row while it is
    // one of the last two (taps there jump to the other end); then the gauge. Part 3 396 / 398 / 403 s,
    // Part 13 1172.5 / 1174 s: the focus row at y 340 of 720, the pill over the first row
    // (a list going - Equip's states - at its own place, settled, no gauge: the one coming has it)
    auto scrollList = [&](float x, int n, auto&& row, float e = -1, float at = -1, bool gauge = true) {
        if (e < 0) e = ease;
        const float scroll = at >= 0 ? at : campScrollPx(app);
        const int focus = std::clamp(int((scroll + 28) / kRowH), 0, std::max(0, n - 1)); // (__MuScrList_GetFocusPos)
        m.setClip(kListTop, kListBottom);
        for (int k = 0; k < n; ++k) {
            const float y = kFocusY + kRowH * float(k) - scroll;
            if (y + kRowH > kListTop && y < kListBottom) row(k, x, y, k == focus);
        }
        if (n > 2) {
            if (focus < 2) m.picture(0x90, x + 88, 112 - scroll, e);
            if (focus >= n - 2) m.picture(0x91, x + 88, 208 + kRowH * float(n - 1) - scroll, e);
        }
        m.clearClip();
        if (gauge) drawCampScrollBar(m, scroll, n, e);
    };
    switch (c.page) {
    case App::Camp::Items: {
        // A state's windows (campPhaseEase): the one left going, the current one coming
        auto itemsPhase = [&](App::Camp::Phase ph, float e, bool current) {
            if (e <= 0) return;
            if (ph == App::Camp::List) {
                // (MuScrList's HELP: gone at once when the list goes - Part 13 2945.52 s, 1182.04 s)
                if (current && !c.pageOut) helpBoard(item, e);
                rightV();

                // (MuScrList_Start / DispActive: WinDispOn / Off(.., 0x1e0) - in from 480 to the right and out to it)
                const float lx = kRowX + 480 * (1 - e);
                if (c.list.empty()) m.text("Nothing here.", lx + 56, kFocusY + 16, false, e, 15);
                // MuScrList_Draw type 10 (__ChkCampItem): white for what can be used here (its table: 1 while
                // MuCom_CheckExitItem, 3..6, 24..27), yellow for what either member wears, else grey; no count for a
                // plate (Part 13 1172.5 s: "Emergency Exit" white, "Key" and the Collection grey)
                scrollList(lx, int(c.list.size()), [&](int k, float x, float y, bool lit) {
                    const int it = c.list[size_t(k)];
                    bool worn = false;
                    for (int g = 0; g < campMembers(app); ++g) {
                        const cr::PartyMember& pm = app.game.party[size_t(campCharacter(app, g))];
                        worn |= std::find(std::begin(pm.equip), std::end(pm.equip), it) != std::end(pm.equip) || campWears(app, campCharacter(app, g), it);
                    }
                    drawCampRow(app, m, x, y, it, cr::isPlate(it) ? 0 : ownedCount(app, it), lit, e, worn ? 3 : campUsable(app, it) ? 0 : 2);
                }, e);
                left();
            } else if (ph == App::Camp::Member) memberPlates(current, e); // (the face boards above; no state board)
            else if (ph == App::Camp::Result) // window 0x12: StateBoardDraw(12, 92, .., item, count, 0, 1), in from -140
                drawShopBoard(app, 12 - 140 * (1 - e), 92, ch, c.resultItem, e, true, c.resultCount);
            else if (ph == App::Camp::Ask) { // (the Yes / No alone: windows 10 and 0xb, the count on 10)
                const bool seed = item >= 3 && item <= 6;
                right();
                // (window 10: WinDispOn(10, 0x168) - in from 360 to the right; 0xb: WinDispOnTypeY(0xb, 0xa0) - up
                // from 160 below)
                const float dx = 360 * (1 - e), dy = 160 * (1 - e);
                const float wr = wideRight(app);
                if (seed) drawNumCount(m, c.count, campSeedLimit(app, ch, item), dx, e, current && campPressIn(app, wr, 336, 60, 36, 31),
                                       current && campPressIn(app, wr, 436, 60, 36, 31));
                // (a held plate takes the cursor: __ComUty_YesNoChoiceProc)
                const bool hy = current && campPressIn(app, wr, 336, 104, 136, 40), hn = current && campPressIn(app, wr, 336, 160, 136, 40);
                m.plate(336 + dx, 104, plateIcon(0), 0, hy || (!hn && current && c.confirm == 0), e, false); // Yes
                m.plate(336 + dx, 160, plateIcon(1), 1, hn || (!hy && current && c.confirm == 1), e, false); // No
                centre();
                // ComUty_MsgBoardDraw(0, 0xdc, .., 0, font 3): picture 12, the mark (picture 13, PictAnimInit(1 tick, 4
                // frames, 20 px, out and back)) at (458, 298), the line at (24, 234) in the menu font (Part 13 2946 s:
                // "Use Emergency Exit?" at the menu text's size, the mark at the board's lower right)
                m.picture(12, 0, 220 + dy, e);
                const short* mark = cr::kMenuPicture[13];
                m.piece(458, 298 + dy, float(mark[0] + 20 * pictAnimFrame(c.mark, 1, 4)), mark[1], mark[2], mark[3], e);
                if (current) m.text(sysText(app, seed ? 57077 : 57014, itemName(app, item)), 24, 234 + dy, false, e, 14); // (its line goes at once - 2946.56 s)
                left();
            }
        };
        if (c.prevPhase != c.phase && c.phaseT < 100) itemsPhase(c.prevPhase, campPhaseEase(app, c.prevPhase), false);
        itemsPhase(c.phase, std::min(ease, campPhaseEase(app, c.phase)), true);
        break;
    }
    case App::Camp::Equip: { // (its states' windows: equipWins)
        const EquipWins w = equipWins(app);
        const int shownMember = w.swapOld ? c.swapFrom : c.member; // (L / R: the member left's, going)
        const int sch = campCharacter(app, shownMember);
        const cr::PartyMember& pm = app.game.party[size_t(sch)];
        const bool gene = c.slot >= 3;
        if (w.plates.e > 0) memberPlates(c.phase == App::Camp::Member, std::min(ease, w.plates.e)); // (the face boards above)
        // the state board (window 10): what the gear list's focus row would make of the member's numbers
        if (w.board.e > 0) drawShopBoard(app, 12 + w.board.dx, 92, sch, c.phase == App::Camp::List && !gene && item > 0 ? item : 0, w.board.e);
        if (w.gene.e > 0) { // the gene board (0xb): the plate list's focus row by its entries, a chosen plate by its picture
            const bool picture = c.phase == App::Camp::Entries || (c.phase == App::Camp::Slot && c.prevPhase == App::Camp::Entries);
            const int plate = c.phase == App::Camp::Entries ? c.plate : picture ? pm.genes[std::clamp(c.slot - 3, 0, 2)] : item;
            // (CampMainPut: the board for the focus row's plate only - none on Remove, 200 - 0x1da being out of
            // range; Part 3 401 s: "Remove equipment" with the left side empty)
            drawGeneBoard(app, m, 14 + w.gene.dx, 92, plate, w.gene.e, picture ? 2 : 1);
        }
        rightV();
        int helpItem = 0;
        float helpE = 0;
        if (w.slots.e > 0) {
            // __MuCampScrListEquipInit: a scrolling list of the seven - weapon, armor, jewel (else item
            // 0x1c6, "Jewel"), the Human Genes and three plates (else 0x1c7.., their names). Their text
            // (__MuScrList_ScrListEquip_Main): yellow for what is worn, white for an empty slot that can be
            // filled, grey for one that can't (MuScrList_GetChoiceListNo's -4) - Part 3 396 / 398 s: the three
            // worn yellow, "Berserker" grey
            const bool current = c.phase == App::Camp::Slot && !w.swapOld;
            scrollList(kRowX + w.slots.dx, kEquipSlots, [&](int slot, float x, float y, bool lit) {
                const int worn = slot < 3 ? pm.equip[slot] : pm.genes[slot - 3];
                const int shown = worn > 0 ? worn : slot == 2 ? 0x1c6 : slot >= 3 ? 0x1c7 + (slot - 3) : 0;
                drawCampRow(app, m, x, y, shown, 0, lit, w.slots.e, campSlotLocked(app, shownMember, slot) ? 2 : worn > 0 ? 3 : 0);
            }, w.slots.e, current ? -1 : kRowH * float(c.slot), false);
        }
        if (w.list.e > 0) { // a gear list or a gene slot's plates (window 0xf)
            const bool current = c.phase == App::Camp::List, gone = c.phase == App::Camp::Entries;
            const std::vector<int>& list = gone ? c.goneList : c.list;
            scrollList(kRowX + w.list.dx, int(list.size()), [&](int k, float x, float y, bool lit) {
                const int it = list[size_t(k)];
                if (gene) { // (MuScrList_Draw type 6: yellow for a plate this member wears - MuScrList_IsNowEquip(.., the
                    // member) - grey while none of it is known; Part 3 403 s: "Deer Genes" yellow, the others white)
                    if (cr::isPlate(it)) drawGeneRow(app, m, x, y, it, lit, campWears(app, sch, it), w.list.e);
                    else drawCampRow(app, m, x, y, it, 0, lit, w.list.e);
                } else { // (MuScrList_Draw type 5: yellow for what the member wears - MuScrList_IsNowEquip -, no count)
                    const bool worn = it > 0 && std::find(std::begin(pm.equip), std::end(pm.equip), it) != std::end(pm.equip);
                    drawCampRow(app, m, x, y, it, 0, lit, w.list.e, worn ? 3 : it > 0 && !campJewelFree(app, shownMember, it) ? 2 : 0);
                }
            }, w.list.e, current ? -1 : kRowH * float(gone ? c.goneCursor : c.listCursor), false);
            if (current) helpItem = item, helpE = w.list.e; // (Remove's own: "Remove equipment" - Part 3 401 s)
        }
        if (w.entries.e > 0) { // CampMainPrc 0x1c: the chosen plate's known entries (window 0x13), the Equip row (the
            // list's -5) unless the member wears it
            const bool current = c.phase == App::Camp::Entries, gone = c.phase == App::Camp::List;
            const std::vector<int>& list = gone ? c.goneList : c.list;
            scrollList(kRowX + w.entries.dx, int(list.size()), [&](int k, float x, float y, bool lit) {
                drawGeneAbilityRow(app, m, x, y, list[size_t(k)], sch, lit, w.entries.e);
            }, w.entries.e, current ? -1 : kRowH * float(gone ? c.goneCursor : c.listCursor), false);
            if (current) helpItem = item, helpE = w.entries.e;
        }
        // (the gauge stays where it is while the lists change - Part 3 398.2-398.4 s -: the current list's, whole
        // from the slots on; with the slots and the member choice it comes and goes with them)
        const bool withSlots = c.phase == App::Camp::Member || (c.prevPhase == App::Camp::Member && c.phaseT < 100) || c.swapT < 100;
        const float ge = withSlots ? w.slots.e : 1.0f;
        if (ge > 0) {
            const bool slots = c.phase == App::Camp::Slot || c.phase == App::Camp::Member;
            drawCampScrollBar(m, c.phase == App::Camp::Member ? kRowH * float(c.slot) : campScrollPx(app), slots ? kEquipSlots : int(c.list.size()), ge);
        }
        left();
        helpBoard(helpItem, helpE); // (MuScrList's HELP: with the list coming, gone at once with it going)
        break;
    }
    case App::Camp::Genes:
        if (c.phase == App::Camp::Entries) { // Genes 4-5: the chosen plate's picture and its known entries
            drawGeneBoard(app, m, 14, 92, c.plate, ease, 2);
            rightV();
            scrollList(kRowX, int(c.list.size()), [&](int k, float x, float y, bool lit) {
                drawGeneAbilityRow(app, m, x, y, c.list[size_t(k)], ch, lit, ease);
            });
            helpBoard(item, ease);
            left();
            break;
        }
        drawGeneBoard(app, m, 14, 92, item, ease, 1); // (browsing: the cursor's known entries by name)
        rightV();
        if (c.list.empty()) m.text("No gene plates.", kRowX + 56, kFocusY + 16, false, ease, 15);
        // (MuScrList_Draw type 0xb: yellow for a plate either member wears; its HELP the plate's own text -
        // SetIdInfo(0xb, 0xe, .., 1, ..) - Part 5 1132 s "Gene Plate used to record / Dragon abilities")
        scrollList(kRowX, int(c.list.size()), [&](int k, float x, float y, bool lit) {
            const int it = c.list[size_t(k)];
            bool worn = false;
            for (int g = 0; g < campMembers(app); ++g) worn |= campWears(app, campCharacter(app, g), it);
            drawGeneRow(app, m, x, y, it, lit, worn, ease);
        });
        helpBoard(item, ease);
        break;
    case App::Camp::Status:
        if (c.phase == App::Camp::Member) memberPlates(true, ease); // (the face boards; no state board)
        else { // CampMainPut case 3: the record's portrait (texture 8 / 9, group-1 picture 4) at (0, 64),
            // then its state board at (308, 68), the seven it wears (window 0xe) or its plates' known
            // entries (0xd); the face boards and the members' plates are off (CampMainPrc 3's 2)
            static const char* const kFaceImage[11] = {"j039_L01.mct", "j040_L01.mct", "j041_L01.mct", "j001_L01.mct",
                                                       "j002_L01.mct", "j003_L01.mct", "j004_L01.mct", "j005_L01.mct",
                                                       "j006_L01.mct", "j007_L01.mct", "j008_L01.mct"};
            cr::Texture& art = c.statusArt[size_t(std::clamp(ch, 0, 10))];
            if (!art.id) {
                cr::Image img;
                if (loadImage(app, kFaceImage[std::clamp(ch, 0, 10)], img)) art = app.renderer.upload(img);
            }
            left();
            { // (the Vita: j0nn_L01 read by _MuCom_MctTexRead - kind 3, full size: the pictures' layer)
                const int keep = r.layer();
                if (r.vitaScreen()) r.setLayer(3);
                outPicture(m, art, 4, 0 - 140 * (1 - ease), 64, ease);
                r.setLayer(keep);
            }
            right(); // (the Vita's board at 0x134 as 3.1.0's; its lists - the table's - 43 to the right)
            if (c.statusView == 0) drawShopBoard(app, 308 + 200 * (1 - ease), 68, ch, 0, ease, false); // (StateBoardDraw param_11 = 0)
            else { // (MuScrList mode 0 as the others - code reading only: no video shows the Status page)
                rightV();
                scrollList(kRowX, int(c.list.size()), [&](int k, float x, float y, bool lit) {
                    const int it = c.list[size_t(k)];
                    if (c.statusView == 2) drawGeneAbilityRow(app, m, x, y, it, ch, lit, ease);
                    else drawCampRow(app, m, x, y, it, 0, lit, ease);
                });
                if (c.list.empty()) m.text("-", kRowX + 120, kFocusY + 16, false, ease, 15);
            }
            left();
        }
        break;
    default: break;
    }
    centre();
    // ComUty_BackButtonDraw (after the page's own pieces: over the Status page's portrait, CampMainPut's end): the
    // arrows (mode 2) where the page turns (campArrows), else the plain
    // bar; none during the Items Yes / No, its board and the Emergency Exit
    // (window 4: CampMain 6 takes it off for Items - the list brings its own and takes it with it, Part 13
    // 1171.9-1172.1 s, 1182.1-1182.5 s -; Equip, Genes and Status keep the main page's)
    const float barA = main ? std::min(1 - leave, c.barIn ? ease : 1.0f) : items ? std::min(ease, campPhaseEase(app, App::Camp::List)) : 1.0f;
    r.setOrigin(r.uiX() + 2 * v43); // (the Vita's Back at 0xeb = 235: 43 to the right)
    if (campBackShown(app)) { // (lit while a touch holds it - the boxes campClick tests)
        const float ux = app.renderer.uiX() + 2 * v43, by = campBackY(app);
        if (campOkBar(app))
            m.backOkBar(kBackX, by, campPressIn(app, ux, 0xc0, by, 0x72, 34) ? 1 : campPressIn(app, ux, 0x137, by, 0x72, 34) ? 5 : 0, barA);
        else if (campArrows(app))
            m.backButton(kBackX, by, campPressIn(app, ux, 0xf8, by, 0x78, 34) ? 1 : campPressIn(app, ux, 0x175, by, 0x33, 34) ? 2
                                     : campPressIn(app, ux, 0xc0, by, 0x33, 34) ? 3 : 0, 0, barA);
        else m.backBar(kBackX, by, campPressIn(app, ux, kBackX, by, 256, 34), barA);
    }
    if (items && !c.pageOut && ease <= 0) m.backBar(kBackX, campBackY(app), false, 1 - outA); // (the main page's going)
    centre();

    // The title and section lines over everything else of the page: the section's line runs over the HELP
    // window's foot and its ">" over the state board's face (Part 13 1174 s, Part 10 202 s), both in the
    // menu font (MuComFontDisp of slots 0 / 2: "Equip", ">Jewel" a size under the port's old 15)
    left();
    // The Items page's title and section (windows 9, 0x10 / 0x11) are its list's: off from the member
    // choice to the list again (CampMainPrc 0's 2 -> 3 .. 10 -> 1). Equip's and Status's come with the member
    // (__MuEquip_CategoryChoiceInit, CampMainPrc 3's 2): none on their member choice (Part 3 395.5 s)
    const float itemsTitle = items ? std::min(ease, campPhaseEase(app, App::Camp::List)) : 0.0f;
    const bool equip = c.page == App::Camp::Equip;
    const EquipWins ew = equip ? equipWins(app) : EquipWins(); // (Equip's: windows 9 and 0x10 with its states)
    if (!main && (items ? itemsTitle > 0 : equip ? ew.title.e > 0 : c.phase != App::Camp::Member)) {
        static const int kTitle[5] = {0, 58070, 58059, 58087, 58077};
        const float pa = items ? itemsTitle : equip ? std::min(ease, ew.title.e) : ease;
        const App::Camp::Phase sp = equip && ew.sectionPrev ? c.prevPhase : c.phase; // (the line going: the state left's)
        const float sa = equip ? ew.section.e : pa;
        m.picture(15, 0 - 140 * (1 - pa), 56, pa);
        m.textCentred(sysText(app, kTitle[c.page]), 12 - 140 * (1 - pa), 47, false, pa, 14);
        std::string section;
        if (c.page == App::Camp::Items) section = sysText(app, kCategoryLabel[c.category]);
        // __menuu_CampMainPrc's window 0x10 (font slot 2): the Equip lists' ">Weapons" / ">Armor" / ">Jewel"
        // (cases 0xb / 0xf / 0x13: 0x49..0x4b) and a gene slot's ">Genes" (0x17: 0x4c), a plate's entries'
        // ">Gene Details" (0x1c, and the Genes page's 4: 0x4e); none on the member choice and the slot list
        // (__MuEquip_CategoryChoiceInit: the title and the name only) nor while the Genes page browses (its
        // 2: the title alone). Part 3 403 / 406 s: "Equip >Genes", "Equip >Gene Details"
        else if (c.page == App::Camp::Equip || c.page == App::Camp::Genes) {
            if (sp == App::Camp::Entries) section = sysText(app, 58078);
            else if (c.page == App::Camp::Equip && sp != App::Camp::Member && sp != App::Camp::Slot)
                section = sysText(app, c.slot < 3 ? 58073 + c.slot : 58076);
        } else if (c.phase == App::Camp::Slot && c.statusView) section = sysText(app, 58099 + c.statusView); // (windows 0x10 / 0xf: 0x64, 0x65)
        if (!section.empty() && section[0] != '>') section = ">" + section;
        if (!section.empty() && sa > 0) { // (the Status page's only on its lists)
            m.picture(16, 24 - 140 * (1 - sa), 72, sa);
            m.textCentred(section, 40 - 140 * (1 - sa), 65, false, sa, 14);
        }
        // (No arrows beside the Items section: CampMainPut 0 draws windows 0x10 / 0x11 - the category's
        // line and the one it changes from - and the page turns on the Back bar's arrows; Part 13 1172.5 s)
    }
    // Window 1 (BattryAndRealTimeAndOzDraw) is the main page's: CampMain's 6 takes it off for any page and
    // its 9 puts it back with the main page (Part 3 395.5-396 s: no clock on Equip's member choice or lists)
    right();
    if (main || (!c.pageOut && outA < 1)) drawClockAndOz(app, m, 1 - outA); // (CampMain 6 takes it off for any page)
    // (the Emergency Exit's end: CampMain 9 puts it on, 0xd at once takes it off - whole at 11, then fading)
    if (c.phase == App::Camp::Exit && c.exitT > kCampExitHide && c.exitT < kCampExitGone)
        drawClockAndOz(app, m, 1 - float(c.exitT - kCampExitHide - 1) / 4.0f);
    centre();
    if (c.messageFrames > 0) --c.messageFrames;
    if (c.messageFrames > 0 && !c.message.empty()) {
        m.picture(12, 0, 220, 1);
        m.text(c.message, 24, 234, false, 1, 14);
    }
    left();
    const char* help = main ? "Up/Down  Enter  Esc or K: back"
                       : c.page == App::Camp::Items && c.phase == App::Camp::List ? "Up/Down  Left/Right: category  Enter  Esc: back"
                       : c.phase == App::Camp::Ask ? "Left/Right: how many  Up/Down  Enter"
                       : c.phase == App::Camp::Result ? "Enter: on"
                       : c.page == App::Camp::Status && c.phase == App::Camp::Slot ? "Left/Right: status, equipment, skills  Esc: back"
                       : c.page == App::Camp::Equip && c.phase == App::Camp::Slot && campMembers(app) > 1
                           ? "Up/Down  Left/Right: the other member  Enter  Esc: back"
                           : "Up/Down  Enter  Esc: back";
    if (keyHints(app)) // (our key hints)
        m.text(help, 20, 372, false, 0.8f, 9);
    centre();
}

// ---- The Progress Tracker (ContData_Save): the field kept when the game is left ----
bool trackerField(const App& app) { // (G+0x388 bit 6 set, bit 3 clear: on the field)
    return !app.booting && !app.titleOpen && app.havePlayer && !app.battle.active && app.mode == Mode::Maps;
}
void writeResume(App& app) {
    if (!app.persist || !trackerField(app)) return;
    cr::writeSave(savePath(app, "resume.dat"), captureSave(app));
}
// The Vita's (FORMATS "The Vita's code"): saved as you go, not only when the game is left - every
// script's end on the field (__EvtReturn / __EvtAbort: _ContData_Save_set_fld, then the save
// opcode 0x45) but the first after a load or a new game, which arms it, and before an encounter
// (fldencount_encount: _ContData_Save_set)
void trackerSceneEnd(App& app) {
    if (!trackerField(app)) return;
    if (!app.trackerArmed) { app.trackerArmed = true; return; }
    writeResume(app);
}

// ---- The title's Progress Tracker offer (__menuu_TitlePrc 0x16..0x1f, TitlePut windows 4..7) ----
// Start with a resume point (TitlePrc 8: MuCom_ChackInterruptData, isContData): first, while flag
// 0x44 is clear and not yet this launch (_gbTutoFlg, which TitleSet's memset leaves alone), flags
// 0x44 and 3 set and "About Progress Tracker" (system message 56041, MsgUty_SetMsg(0x29)) read
// through in the message window (0x16..0x19); then (0x1a, ContData_memcpy: the interrupted game in
// play) its pair's face boards come in from x -240 at (0, 24 + 88k) (window 6), its date and play
// time from +360 (window 5, ComUty_GameTimeAndPlayTimeDraw), Yes / No from +360 at (336, 126) /
// (336, 176) (window 4, ComUty_YesNoChoicePosDraw / Proc: the upper box answers 2, Yes) and the
// message board up from +160 at y 220 with 57092 / 57093 (window 7, ComUty_MsgBoardDraw). An answer
// (SE 0x93 either way) sends the four windows back out (0x1c); Yes then stops the music and fades
// to black over 10 frames before the load (0x1d / 0x1e, SaveData_Load_set); No waits 4 and Start
// goes on (TitleClr: Load_SaveData, flag 0 clear). The resume point is used up either way.
void openResumePrompt(App& app) { // TitlePrc 0x1a
    app.resumePrompt = 1;
    app.resumeCursor = 0;
    app.resumeWin = 0;
    app.resumeMark = 0;
}

void offerResume(App& app) {
    if (app.resumePrompt || app.resumeNotice || !app.persist) return;
    if (!cr::readSave(savePath(app, "resume.dat"), app.resumeData)) return;
    cr::splitSharedGenes(app.resumeData.game, app.itemTable);
    if (!allPairsFlags(app).flag(0x44) && !app.game.flag(0x44) && !app.interruptShown) { // 0x16
        app.game.setFlag(0x44, true);
        markSeen(app, 0x44);
        app.interruptShown = true;
        // 0x17: flag 3 (MsgUty's pictures - 0x29 has none: MsgUty_SetMsg's table at 0xd5200 holds
        // 12, 13, 18, 10, 19), MsgUty_WinOpen(0x29): not in that table, so the window's sixth rect
        // with style 8 (__EvtOpen's style 8, one line: kRect 5), fading in with SE 0x97
        app.game.setFlag(3, true);
        app.msgStyle = 8, app.msgLines = 1, app.heldPage = false;
        app.msgWin.state = 1, app.msgWin.t = 0, app.msgWin.alpha = 0;
        app.sound.play(0x97);
        app.dialogue.clear();
        showMessages(app, {56041});
        app.resumeNotice = 1;
        return;
    }
    openResumePrompt(app);
}

void answerResume(App& app, bool yes, bool cancelled) {
    if (app.resumePrompt != 1) return;
    app.resumePrompt = 2; // 0x1c: the windows go
    app.resumeYes = yes;
    SDL_RemovePath(savePath(app, "resume.dat").c_str()); // used up either way, as the original's
    menuSound(app, cancelled ? se::kBack : se::kChoose); // Yes and No alike (ComUty_YesNoChoicePosProc's SE 0x93; the Vita's × 0x94)
    if (!yes) { app.resumeIn = 7; return; } // 0x1d: 4, counted below zero in 0x1e, then TitleClr
    // Yes (0x1d): the music off, black over 10 frames, ten counted below zero, then the load (stepResume)
    playBgm(app, 1u << 27);
    app.fadeGrey = 0, app.fadeFrom = 0, app.fadeTo = 1, app.fadeFrames = 10, app.fadeT = 0;
    app.resumeIn = 13;
}

// ComUty_YesNoChoicePosProc(.., 0x150, 0x7e, 0x150, 0xb0): the boxes 136 x 40 at (336, 126) - Yes
// - and (336, 176) - No - (widescreen: on the screen's right, as drawn)
void resumeClick(App& app, float px, float py) {
    if (app.resumePrompt != 1) return;
    float lx, ly, mx, my;
    app.renderer.toLogical(px, py, lx, ly);
    menuPointAt(app, lx, ly, wideRight(app), mx, my);
    for (int k = 0; k < 2; ++k)
        if (mx >= 336 && mx < 336 + 136 && my >= 126 + 50 * k && my < 126 + 50 * k + 40) {
            app.resumeCursor = k;
            answerResume(app, k == 0);
            return;
        }
}

// One 20 Hz frame of the offer: the message read through (0x18: flag 3 clear, MsgUty_WinClose - SE
// 0x98, the window fading out), its window gone (0x19), the windows' slide, the board's mark, the
// answer's countdown
void stepResume(App& app) {
    if (app.resumeNotice == 1 && app.dialogue.empty()) {
        app.game.setFlag(3, false);
        app.msgWin.state = 4, app.msgWin.t = 0;
        app.sound.play(0x98);
        app.resumeNotice = 2;
    } else if (app.resumeNotice == 2 && (app.msgWin.state == 0 || app.msgWin.state == 6)) {
        app.resumeNotice = 0;
        openResumePrompt(app);
    }
    if (app.resumePrompt == 1) app.resumeWin = std::min(1.0f, app.resumeWin + 0.25f);
    else if (app.resumePrompt == 2) app.resumeWin = std::max(0.0f, app.resumeWin - 0.25f);
    if (app.resumePrompt) ++app.resumeMark;
    if (app.resumeIn > 0 && --app.resumeIn == 0) {
        app.resumePrompt = 0;
        if (!app.resumeYes) titleStart(app);
        else if (!applySave(app, app.resumeData)) { menuSound(app, se::kBuzzer); titleStart(app); }
    }
}

// ComUty_PictAnimDraw out and back (PictAnimInit's last 1): its count goes up to ticks x frames and
// back down to 0, one a draw; the piece is count / ticks steps to the right
int pictAnimFrame(int draws, int ticks, int frames) {
    const int top = ticks * frames, k = draws % (2 * top);
    return (k <= top ? k : 2 * top - k) / ticks;
}

// ComUty_MsgBoardDraw at (0, y): picture 12, the mark (picture 13, 20 px a frame) at (458, y + 78),
// and its lines from (24, y + 14), 16 apart in the menu font (13: Part 8 4936.5 s, "You have save
// data from your last session." 509 px wide of 1280) - those only while the window stands still
void drawMsgBoard(App& app, cr::MenuPainter& m, float y, int frame, float a, bool still, std::initializer_list<int> lines) {
    m.picture(12, 0, y, a);
    const short* p = cr::kMenuPicture[13];
    m.piece(458, y + 78, float(p[0] + 20 * frame), p[1], p[2], p[3], a);
    if (!still) return;
    float ly = y + 14;
    for (int id : lines) {
        if (id > 0) m.text(sysText(app, id), 24, ly, false, a, 13);
        ly += 16;
    }
}

void drawResumePrompt(App& app) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu phoneMenu(app, m); // the iPhone layout: its screen's menu coordinates
    const float w = app.resumeWin, e = 1 - (1 - w) * (1 - w), a = w, out = 1 - e;
    // Window 6: the interrupted game's pair (ComUty_CharInfoInit: the scenario's pair, one while
    // flag 0xfed + scenario holds the second back), as its state has them - in play since 0x1a
    std::swap(app.game, app.resumeData.game);
    r.setOrigin(wideLeft(app)); // (widescreen: the boards to the screen's left, as the save screen's)
    for (int k = 0; k < campMembers(app); ++k) {
        const int ch = campCharacter(app, k);
        int hp, hpMax, mp, mpMax;
        memberHpMp(app, ch, hp, hpMax, mp, mpMax);
        drawFaceBoard(app, m, -240 * out, 24 + 88.0f * float(k), ch, hp, hpMax, mp, mpMax, a);
    }
    std::swap(app.game, app.resumeData.game);
    r.setOrigin(wideRight(app)); // (... the date and Yes / No to its right)
    drawSaveDate(app, m, app.resumeData, a, 360 * out);                    // window 5
    const bool hy = pressIn(app, kPressResume, wideRight(app), 336, 126, 136, 40), hn = pressIn(app, kPressResume, wideRight(app), 336, 176, 136, 40);
    m.plate(336 + 360 * out, 126, 0, 0, hy || (!hn && app.resumeCursor == 0), a, false); // window 4: Yes (a held plate takes the cursor)
    m.plate(336 + 360 * out, 176, 1, 1, hn || (!hy && app.resumeCursor == 1), a, false); // No
    r.setOrigin(r.uiX());
    drawMsgBoard(app, m, 220 + 160 * out, pictAnimFrame(app.resumeMark, 1, 4), a, w >= 1, {57092, 57093}); // window 7
    r.setOrigin(wideLeft(app));
    if (keyHints(app)) // (our key hints)
        m.text("Up/Down  Enter  Esc: no", 20, 372, false, a * 0.8f, 9);
    r.setOrigin(r.uiX());
}

// The flags of every pair's story at once: what the original keeps in its system data (the
// Scenarios chart, the second Character Select sheet). Ours: the pairs' saves and the resume point
// prty_init_new(scenario, mode) - Uty_LoadPrty(var 0 = scenario + 1, var 1 = mode 2 / 3): a new
// game for the scenario. Its flags (_flgclear: 1001..1500 / 1501..2500) are cleared, its block's
// records (4 + 3s ..) start over from _prty_init_equip with their plates and extras in the bag
// (add_item2), its pair is set - scenario 0 Vieg and Olgar (0, 1), scenario 1 Vieg and Dante (0, 2),
// the second held back by flag 0xfed / 0xfee until the story lets them fight together -, and
// ResetFieldFlag_NewGame clears scenario 0's field flags (3000..3015, 3300..3379). Scenario 0 also
// loses the plates and genes 114, 116, 82, 117, 187..190 and (a fresh run) the gear 476, 257, 341,
// 475, 229, 313; scenario 1 gets 480, 82 and 188. With the scenario's clear data (flag 31 + s,
// its slot; Omega's load flag 31 / 32) the run goes on from it, and mode 3 keeps the records' stat bonuses, gear and plates
// and the bag less its kind 3 and 9 items.
void newPairParty(App& app, int pair, bool clearData) {
    const int p = std::clamp(pair, 1, cr::kScenarios) - 1;
    // the one save is the state the title runs on (TitleClr's Load_SaveData): a new game starts from
    // it - the other scenario, the shared bag, the Oz and every flag outside the cleared range stay
    cr::SaveData data;
    const bool haveData = readData(app, data);
    cr::GameState g = haveData ? data.game : app.game;
    if (haveData) {
        for (int f : {0, 4089}) g.setFlag(f, app.game.flag(f)); // the boot script's own
        // (and its variables: prty_init_new leaves the work alone - the vars the boot script has set
        // this session, the scenario 0, the choice 1 and the rest, stay over the save's)
        for (const auto& [k, v] : app.game.vars) g.vars[k] = v;
        g.openedBoxes.clear();
        app.treasureBoxes.clear();
    }
    if (app.interruptShown) g.setFlag(0x44, true); // (TitleClr's Load_SaveData: the Progress Tracker's message seen)
    const bool ng = haveData && clearData; // mode 3: the records' bonuses and the bag go on (the title offers it with the clear flag)
    cr::changeScenario(g, app.itemTable, p);
    static const int kFlagClear[cr::kScenarios][2] = {{1001, 1500}, {1501, 2500}};
    for (int f = kFlagClear[p][0]; f <= kFlagClear[p][1]; ++f) g.setFlag(f, false);
    if (p == 1 && !clearData) // mode 2: scenario 1's 0x1024..0x1043 go too
        for (int f = 0x1024; f < 0x1044; ++f) g.setFlag(f, false);
    auto del = [&](int item) {
        auto it = g.items.find(item);
        if (it != g.items.end() && --it->second <= 0) g.items.erase(it);
    };
    if (!ng) { // prty_init_new's memset of the scenario's own bag comes first (mode 3 puts the saved one
        // back): the Oz and the shared items stay
        for (auto it = g.items.begin(); it != g.items.end();)
            if (!cr::sharedItem(app.itemTable, it->first)) it = g.items.erase(it);
            else ++it;
        if (p == 1) // (0x1024..0x1043: scenario 1's learned entries of its Human Genes plates, 477..480)
            for (int plate = 477; plate <= 480; ++plate)
                for (int entry : cr::plateContents(plate))
                    if (entry > 0) g.items.erase(entry);
    }
    if (p == 0)
        for (int item : {114, 116, 82, 117, 187, 188, 189, 190}) g.items.erase(item); // del_item_cnt0
    // add_item2: one only when none is owned
    auto add2 = [&](int item) { // (_GetItmCnt: the other scenario's Human Genes plates count 0)
        auto it = g.items.find(item);
        const bool owned = !cr::otherScenarioPlate(g, item) && it != g.items.end() && it->second > 0;
        if (item > 0 && !owned) cr::addScenarioItem(g, app.itemTable, p, item, 1);
    };
    cr::Party fresh = g.party;
    cr::newScenarioRecords(fresh, p);
    for (int k = 0; k < 3; ++k) {
        const size_t c = size_t(4 + 3 * p + k);
        cr::PartyMember m = fresh[c];
        if (ng) { // mode 3 copies the six bonus fields only: the starting gear and plate are worn again.
            // Each field holds the levels' growth from 20 on too (pcparam_change_lv adds it there), so
            // what the old record's levels gave goes into the boosts as its level starts again
            const cr::PartyMember& old = g.party[c];
            std::copy(std::begin(old.seeds), std::end(old.seeds), m.seeds);
            std::copy(std::begin(old.boosts), std::end(old.boosts), m.boosts);
            for (int st = 0; st < cr::kStats; ++st) m.boosts[st] += cr::levelGrowth(old.level, cr::Stat(st), old.start);
        }
        // (every mode, add_item2: the weapon and armor worn - a record's jewel slot starts empty - the
        // Human Genes plate and its two known entries)
        add2(m.equip[cr::kWeapon]), add2(m.equip[cr::kArmor]);
        add2(m.genes[0]);
        for (int e = 0; e < 2; ++e) add2(cr::startItem(int(c), e));
        m.hp = m.mp = -1;
        g.party[c] = m;
        g.party[c].mp = cr::memberStat(g, int(c), cr::kMp, app.itemTable); // (prty_init: _pcparam_max_mp, a number)
    }
    if (p == 1) {
        for (int item : {480, 82, 188}) cr::addScenarioItem(g, app.itemTable, 1, item, 1);
        g.pairs[1] = {0, 2};
        g.setFlag(0xfee, true);
    } else {
        g.pairs[0] = {0, 1};
        g.setFlag(0xfed, true);
        for (int v = 0x40; v < 0x50; ++v) g.vars[v] = 0;
        g.leaders[0] = 0;
        if (!ng)
            for (int item : {476, 257, 341, 475, 229, 313}) del(item);
    }
    if (!ng) del(369);
    for (int k = 0; k < 3; ++k) g.party[size_t(k)] = g.party[size_t(4 + 3 * p + k)];
    if (p == 0) {
        for (int f = 3000; f < 3016; ++f) g.setFlag(f, false);
        for (int f = 3300; f < 3380; ++f) g.setFlag(f, false);
    }
    if (ng) {
        // mode 3 walks the scenario's own bag for kinds 3 and 9 - only kind 3 (61..80) lives there,
        // the manual's pages (kind 9) are in the shared bag and stay; scenario 1 also loses its
        // collection (551..631), the OMU items (521..541), 219, 309, 385..387, 394 and vars 0x28 / 0x29
        for (auto it = g.items.begin(); it != g.items.end();) {
            const cr::ItemParam* ip = app.itemTable.item(it->first);
            const int n = it->first;
            const bool gone = (ip && ip->kind == 3) ||
                              (p == 1 && ((n >= 551 && n <= 630) || (n >= 521 && n <= 540) || n == 219 || n == 309 ||
                                          (n >= 385 && n <= 387) || n == 394));
            if (gone) it = g.items.erase(it);
            else ++it;
        }
        if (p == 1) g.vars[0x28] = 0, g.vars[0x29] = 0;
    }
    if (size_t(p) < g.noEncounters.size()) g.noEncounters[size_t(p)] = false; // (both modes: the scenario's unencountflg)
    app.game = std::move(g);
    app.playerChar = app.game.leaders[size_t(p)];
}

// The flags the title and its menus run on: the one save's (__menuu_TitleClr's Load_SaveData - both
// scenarios' flags are one array), and the game's own while one is on
cr::GameState allPairsFlags(const App& app) {
    cr::GameState g;
    cr::SaveData d;
    if (readData(app, d)) g.flags = d.game.flags;
    if (!app.booting)
        for (size_t i = 0; i < g.flags.size() && i < app.game.flags.size(); ++i) g.flags[i] |= app.game.flags[i];
    for (int f : app.seenFlags) g.setFlag(f, true); // (the prompts seen outside a save)
    return g;
}


namespace bingo {
cr::GameState& flags(App& app) { return app.scc.mode == 1 ? app.game : app.scc.flags; }
bool shown(App& app) { return flags(app).flag(0x642); } // the cards' row and the second rate
int cards(App& app) {
    const cr::GameState& g = flags(app);
    return shown(app) && g.flag(0x1f) && g.flag(0x65) && g.flag(0x665) ? 5 : 4;
}
bool done(App& app, int card) {
    for (int i = 0; i < 16; ++i)
        if (!flags(app).flag(kOpenFlag[card] + i)) return false;
    return true;
}
int birthdayRate(App& app) {
    int p = 0;
    for (const auto& e : kBirthday) p += flags(app).flag(e[0]) ? e[1] : 0;
    return p;
}
int festivalRate(App& app) { // (__GetFestivalProgressNum)
    int p = 0;
    for (const auto& e : kFestival) p += flags(app).flag(e[0]) ? e[1] : 0;
    for (int k = 0; k < 5; ++k)
        for (int i = 0; i < 16; ++i) p += flags(app).flag(kOpenFlag[k] + i);
    return p;
}
int presentsUsed(App& app) { // the card page's N / 64 (80)
    int n = 0;
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 5; ++k) n += flags(app).flag(kPushFlag[k] + i);
    return n;
}
// __SetBingoPushAndOpen
void refresh(App& app) {
    App::Scenarios& s = app.scc;
    const cr::GameState& g = flags(app);
    for (int i = 0; i < 16; ++i) {
        s.cell[size_t(i)] = s.card >= 0 && g.flag(kOpenFlag[s.card] + i) ? 2 : 0;
        s.canPush[size_t(i)] = false;
        if (s.cell[size_t(i)] == 2 || s.mode != 1) continue;
        int held = 0, used = 0;
        for (short item : kItems[i]) held += ownedCount(app, item) > 0;
        for (int k = 0; k < 5; ++k) used += g.flag(kPushFlag[k] + i);
        s.canPush[size_t(i)] = held - used > 0;
    }
}
// What the Camp's Scenarios screen lets the player do, all at once (--explore): every cell a held
// present opens, card by card; a card all open is done (its flag, var 0x16 + 1). The cells opened
int openAll(App& app) {
    cr::GameState& g = app.game;
    if (!g.flag(0x642)) return 0; // (no cards yet)
    const int n = g.flag(0x1f) && g.flag(0x65) && g.flag(0x665) ? 5 : 4;
    int opened = 0;
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < 16; ++i) {
            if (g.flag(kOpenFlag[k] + i)) continue;
            int held = 0, used = 0;
            for (short item : kItems[i]) held += ownedCount(app, item) > 0;
            for (int j = 0; j < 5; ++j) used += g.flag(kPushFlag[j] + i);
            if (held - used <= 0) continue;
            g.setFlag(kOpenFlag[k] + i, true);
            for (int j = 0; j < 5; ++j)
                if (!g.flag(kPushFlag[j] + i)) { g.setFlag(kPushFlag[j] + i, true); break; }
            ++opened;
            bool all = true;
            for (int c = 0; c < 16; ++c) all &= g.flag(kOpenFlag[k] + c);
            if (all && !g.flag(kCardFlag + k)) {
                g.setFlag(kCardFlag + k, true);
                g.vars[kDoneVar] = (g.vars.count(kDoneVar) ? g.vars[kDoneVar] : 0) + 1;
            }
        }
    return opened;
}
void loadCard(App& app, int card) {
    App::Scenarios& s = app.scc;
    if (s.cardLoaded != card) {
        app.renderer.destroy(s.cardTex);
        app.renderer.destroy(s.photo);
        cr::Image img;
        if (loadImage(app, kCardFile[card], img)) s.cardTex = app.renderer.upload(img);
        char name[32];
        std::snprintf(name, sizeof name, "present%02d_hd.mct", card + 1);
        if (loadImage(app, name, img)) s.photo = app.renderer.upload(img);
        s.cardLoaded = card;
    }
    s.card = card;
    s.anim = 0;
    s.zoom = kZoomMin, s.panX = s.panY = 0, s.zoomDir = 0;
    s.pushing = -1;
    s.cursor = std::clamp(s.cursor, 0, 15);
    refresh(app);
}

// PrcWin for a card's window (the port's x and alpha of it): opening, t 4 -> 0 from its offset;
// closing, t 0 -> 4 to it (FUN_0001f574 / 5b4 / 5fc / 620)
using Win = App::Scenarios::Win;
void winOn(Win& w, float from) { w.state = 1, w.t = 0, w.from = from; }
void winOff(Win& w, float to) { // (ClrWin: a window not up stays gone)
    if (w.state != 6) w.state = 4;
    w.t = 0, w.to = to;
}
void winStep(Win& w) {
    switch (w.state) {
    case 1: w.t = 4, w.state = 2; break;
    case 2: if (--w.t < 1) w.state = 3; break;
    case 4: w.t = 0, w.state = 5; break;
    case 5: if (++w.t > 4) w.state = 6; break;
    default: break;
    }
}
float winX(const Win& w) {
    switch (w.state) {
    case 1: return w.from;
    case 2: return w.from * float(w.t) / 4.0f;
    case 5: return w.to * float(std::min(w.t, 4)) / 4.0f;
    case 6: return w.to;
    default: return 0;
    }
}
float winAlpha(const Win& w) {
    switch (w.state) {
    case 1: case 6: return 0;
    case 2: return float(4 - w.t) / 4.0f;
    case 5: return std::max(0.0f, float(4 - w.t) / 4.0f);
    default: return 1;
    }
}
// State 6: the card's windows on (from `from`: 6 always from the left; 7 only for a card not done)
void cardWindowsOn(App& app, float from) {
    App::Scenarios& s = app.scc;
    winOn(s.win[0], from == 0 ? 0.0f : -240.0f);
    if (!done(app, s.card)) winOn(s.win[1], from);
    else s.win[1] = Win();
    winOn(s.win[2], from);
    s.phase = 2, s.phaseCount = 0;
}
// The photo's alpha (SccnariosPut): window 8's, but none in state 7 nor - for a card not done - in
// 6 and 0xc..0xe (that card the one going), so only the whole picture shows as the cells slide
float photoAlpha(App& app) {
    const App::Scenarios& s = app.scc;
    if (s.phase == 2) return 0;
    if (s.phase == 1 && !done(app, s.card)) return 0;
    return winAlpha(s.win[2]);
}
} // namespace bingo

void openScenarios(App& app, int mode) {
    App::Scenarios& s = app.scc;
    s.open = true;
    s.mode = mode;
    s.anim = 0;
    s.card = -1;
    s.cursor = 0;
    s.phase = 0, s.win = {};
    if (mode == 0) s.flags = allPairsFlags(app);
    if (!s.sheet.id) {
        cr::Image img;
        if (loadImage(app, "tex_scenario.mct", img)) s.sheet = app.renderer.upload(img);
    }
}
void openScenarios(App& app) { openScenarios(app, 0); }

void closeScenarios(App& app) {
    App::Scenarios& s = app.scc;
    s.open = false;
    app.renderer.destroy(s.sheet);
    app.renderer.destroy(s.cardTex);
    app.renderer.destroy(s.photo);
    app.renderer.destroy(s.completeTex);
    if (s.complete) applyMix(app), s.complete = 0;
    s.cardLoaded = -1;
}

// A frame of the screen (Prc's per-frame parts): the bobbing pictures, the zoom, a cell opening
void stepScenarios(App& app) {
    using namespace bingo;
    App::Scenarios& s = app.scc;
    ++s.anim;
    for (Win& w : s.win) winStep(w);
    if (s.phase == 1) { // < >: 0xc the windows off, 0xd six frames, 0xe and 5 (the next card read), then 6
        const float side = s.switchDir == 1 ? -240.0f : 240.0f;
        if (s.phaseCount == 0) {
            winOff(s.win[0], -240);
            winOff(s.win[1], side);
            winOff(s.win[2], side);
        }
        if (++s.phaseCount >= 10) {
            loadCard(app, s.nextCard);
            s.anim = 100; // (windows 10 and 11 stay up)
            cardWindowsOn(app, -side);
        }
    } else if (s.phase == 2 && ++s.phaseCount >= 5) s.phase = 0; // 7: four frames, then 8
    if (!s.bobBack) { if (++s.bob >= 4) s.bobBack = true; }
    else if (--s.bob <= 0) s.bobBack = false;
    if (s.zoomDir == 1) {
        s.zoom = std::min(kZoomMax, s.zoom + kZoomStep);
        if (s.zoom >= kZoomMax) s.zoomDir = 0;
    } else if (s.zoomDir == 2) { // back to the middle over the way out
        s.zoom = std::max(kZoomMin, s.zoom - kZoomStep);
        s.panX -= s.panX / 5, s.panY -= s.panY / 5;
        if (s.zoom <= kZoomMin) s.zoomDir = 0, s.panX = s.panY = 0;
    }
    if (s.pushing >= 0 && ++s.pushClock >= 2) { // states 0xf / 0x10: shut -> opening -> open
        const size_t i = size_t(s.pushing);
        if (s.cell[i] == 0) s.cell[i] = 1, s.pushClock = 0;
        else {
            s.cell[i] = 2;
            cr::GameState& g = flags(app);
            g.setFlag(kOpenFlag[s.card] + s.pushing, true);
            for (int k = 0; k < 5; ++k)
                if (!g.flag(kPushFlag[k] + s.pushing)) { g.setFlag(kPushFlag[k] + s.pushing, true); break; }
            s.pushing = -1;
            if (done(app, s.card) && !g.flag(kCardFlag + s.card)) { // 0x12: the completion begins
                app.renderer.destroy(s.completeTex);
                cr::Image img;
                if (loadImage(app, "tex_card_complete_hd.mct", img) || loadImage(app, "tex_card_complete.mct", img))
                    s.completeTex = app.renderer.upload(img);
                for (int p = 0; p < 7; ++p) s.piece[size_t(p)] = {kCompletePoint[p][0][0], kCompletePoint[p][0][1]};
                s.complete = 1, s.cStep = 0, s.cCount = 0;
                app.music.setVolume(0); // (the BGM volume byte saved and zeroed: __bgm_vol)
                app.sound.play(0xb0);
            }
            refresh(app);
        }
    }
    if (s.complete == 1) { // 0x13 / 1: each piece from keyframe to keyframe, (to - from) / len a frame
        const int from = kCompleteFrom[s.cStep], to = kCompleteFrom[s.cStep + 1], len = kCompleteLen[s.cStep];
        for (int p = 0; p < 7; ++p)
            for (int k = 0; k < 2; ++k)
                s.piece[size_t(p)][size_t(k)] = (kCompletePoint[p][to][k] - kCompletePoint[p][from][k]) / len * s.cCount + kCompletePoint[p][from][k];
        if (len < s.cCount) {
            s.cCount = 0;
            if (++s.cStep > 10) { s.complete = 2, s.cCount = 0; app.sound.play(0x128); return; }
        }
        ++s.cCount;
    } else if (s.complete == 2) { // white over 1000 ms; 21 frames on, the card's windows go
        if (++s.cCount >= 21) s.complete = 3, s.cCount = 0;
    } else if (s.complete == 3) { // back from white over 21 frames, then the card's done
        if (++s.cCount > 20) {
            cr::GameState& g = flags(app);
            g.setFlag(kCardFlag + s.card, true);
            g.vars[kDoneVar] = (g.vars.count(kDoneVar) ? g.vars[kDoneVar] : 0) + 1;
            applyMix(app); // (0x14: the BGM volume back)
            app.renderer.destroy(s.completeTex);
            s.complete = 0;
            s.anim = 20;
            refresh(app);
        }
    }
}

void drawScenarios(App& app) {
    using namespace bingo;
    App::Scenarios& s = app.scc;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu pm(app, m);
    const float a = std::min(1.0f, float(s.anim) / 6.0f); // (ComUty_WinDispOn's fade)
    if (s.card >= 0) { // window 8: MuComRectDraw's black over the screen - on with a card only
        r.setOrigin(0);
        r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 1);
        r.setOrigin(r.uiX());
    } else { // window 2's masks: what's behind (the title's picture, the Camp's field) shows between
        // them (Part 8 4893 s: the title between y 165 and 284). From the title (mode 0) a band
        // over 0..48 and 284..320, from the Camp the iPad-wide ones, behind the pieces (z -2);
        // Android 1.1.6 (0x784e8) takes the bands out to the phone's edges
        const float black[4] = {0, 0, 0, 1};
        const float ex = androidEdgeX(app), ey = androidEdgeY(app);
        if (s.mode == 0) {
            m.quad(-ex, -ey, 480 + 2 * ex, 48 + ey, black, black);
            m.quad(-ex, 284, 480 + 2 * ex, 36 + ey, black, black);
        } else if (r.vitaScreen()) { // (the Vita's, vita decomp 81470: the bands from x -48, 576 wide, and its side
            // masks with them - 43 wide to its screen's edges, at z -2, behind the clock and Oz that reach past 480: #20)
            m.quad(-48, -32, 576, 80, black, black);
            m.quad(-48, 284, 576, 100, black, black);
            m.quad(-43, 0, 43, 640, black, black);
            m.quad(480, 0, 43, 640, black, black);
        } else {
            m.quad(-32 - ex, -32 - ey, 544 + 2 * ex, 80 + ey, black, black);
            m.quad(-32 - ex, 284, 544 + 2 * ex, 100 + ey, black, black);
        }
    }
    // ... and the side ones in front (z 0: the clock's line past x 480 cut on the phone - Part 8 4893
    // s, the TIME line ending at the screen's edge): from the Camp 16 wide, on Android the phone's
    // margins beyond 0..480
    const auto sideMasks = [&]() {
        const float black[4] = {0, 0, 0, 1};
        const float ex = androidEdgeX(app), ey = androidEdgeY(app), w = (s.mode == 0 ? 0.0f : 16.0f) + ex;
        if (w <= 0 || r.vitaScreen()) return; // (the Vita's are behind: above)
        m.quad(-w, -ey - (s.mode == 0 ? 0.0f : 32.0f), w, 320 + 2 * ey + (s.mode == 0 ? 0.0f : 64.0f), black, black);
        m.quad(480, -ey - (s.mode == 0 ? 0.0f : 32.0f), w, 320 + 2 * ey + (s.mode == 0 ? 0.0f : 64.0f), black, black);
    };
    auto sheet = [&](int p, float x, float y, float alpha) {
        const short* q = kSheetPict[p];
        const float k = sheetScale(s.sheet);
        m.imagePieceTo(s.sheet, x, y, q[2], q[3], q[0] * k, q[1] * k, q[2] * k, q[3] * k, alpha);
    };
    auto card = [&](int p, float x, float y, float alpha) {
        const short* q = kCardPict[p];
        const float k = sheetScale(s.cardTex);
        m.imagePieceTo(s.cardTex, x, y, q[2], q[3], q[0] * k, q[1] * k, q[2] * k, q[3] * k, alpha);
    };
    // A rate: its strip of 24 (picture 15) and the two lines under it (_C_84_13820: grey 0x565853
    // at the ends to white in the middle, 1 high), its label and "Achievement NNN %"
    // (__Common_LineDraw: the strips in window 3 / 4, the lines in window 3 - the second rate's
    // line shows without its strip: Part 8 4893 s, the line at y 282 under the title's picture)
    auto rateLine = [&](int which, float alpha) {
        const float y = which ? 248 : 131;
        const float edge[4] = {0x56 / 255.0f, 0x58 / 255.0f, 0x53 / 255.0f, alpha}, white[4] = {1, 1, 1, alpha};
        m.quad(1, y + 34, 239, 1, edge, white);
        m.quad(240, y + 34, 240, 1, white, edge);
    };
    auto rate = [&](int which, int value, float alpha) {
        const float y = which ? 248 : 131;
        for (int k = 0; k < 24; ++k) sheet(15, 20.0f * float(k), y, alpha);
        rateLine(which, alpha);
        // (__Birthday_PictNumDraw: three digits from (385, y), the first at z -2, under the label)
        const int d[3] = {value / 100 % 10, value / 10 % 10, value % 10};
        const short* q = kSheetPict[5];
        const float k = sheetScale(s.sheet);
        for (int i = 0; i < 3; ++i)
            m.imagePieceTo(s.sheet, 385 + 24.0f * float(i), which ? 254 : 136, q[2], q[3], (q[0] + q[2] * d[i]) * k, q[1] * k,
                           q[2] * k, q[3] * k, alpha);
        sheet(4, 315, which ? 254 : 136, alpha);
        sheet(which ? 3 : 2, which ? 4 : 8, which ? 248 : 138, alpha);
    };
    if (s.card < 0) { // __menuu_SccnariosPut, windows 0..5
        drawClockAndOz(app, m, a);
        m.picture(160, 8, 4, a);
        sheet(0, 13, 48, a);
        for (int i = 2; i < 8; ++i)
            if (flags(app).flag(0x518 + i - 2)) sheet(1, 15 + 57.0f * float(i), 50, a);
        rate(0, birthdayRate(app), a);
        if (!shown(app)) rateLine(1, a);
        if (shown(app)) {
            rate(1, festivalRate(app), a);
            const int n = cards(app);
            for (int i = 0; i + 1 < n; ++i) sheet(6, 89 + 94.0f * float(i), 200, a);
            for (int i = 0; i < n; ++i) {
                const float x = 94.0f * float(i);
                if (done(app, i)) {
                    sheet(9 + i, 8 + x, 177, a);
                    sheet(14, 42 + x, 185, a);
                } else {
                    sheet(7, 8 + x, 177, a);
                    sheet(8, 30 + x, 168 - float(s.bob), a);
                }
                if (s.cursor == i && s.keys) { // (keys: the port's cursor)
                    const float cyan[4] = {0x4B / 255.0f, 1, 0xEB / 255.0f, 0.35f * a};
                    m.quad(8 + x, 177, 90, 62, cyan, cyan);
                }
            }
        }
        { // (the Vita's at 0xeb = 235)
            const float v = vitaEdge43(app);
            m.backBar(192 + v, 288, pressIn(app, kPressScenarios, app.renderer.uiX(), 192 + v, 286, 256, 34), a); // (lit while held: DAT_000ed27c)
        }
        sideMasks();
        return;
    }
    // A card (windows 6..11): the picture (tex2d_add_sca at (pan, 96 + pan), 960 x 640, scaled
    // about its middle; the sheet's 512 rows are all there is of it)
    // (windows 6..8 slide on < >: their x offsets and alphas; 8 is the photo, 7 the frame and cells)
    const float x6 = winX(s.win[0]), a6 = a * winAlpha(s.win[0]), x7 = winX(s.win[1]), a7 = a * winAlpha(s.win[1]);
    if (s.photo.id) {
        const float pa = a * photoAlpha(app);
        const float w = 480 * s.zoom, h = 320 * s.zoom;
        const float cx = 240 + winX(s.win[2]) + s.panX / 2, cy = 48 + 160 + s.panY / 2;
        if (pa > 0) m.imagePieceTo(s.photo, cx - w / 2, cy - h / 2, w, 256 * s.zoom, 0, 0, 960, 512, pa);
    }
    const bool whole = done(app, s.card) && s.complete == 0; // (the photo only once the card's done: ed274)
    if (!whole && s.cardTex.id && s.complete < 3 && a7 > 0) { // window 7: the frame and the cells (off at the white's peak)
        card(1, x7, 48, a7);
        card(2, x7, 276, a7);
        card(3, x7, 55, a7);
        card(4, x7 + 350, 55, a7);
        for (int i = 0; i < 16; ++i) {
            const int st = s.cell[size_t(i)];
            card((st == 0 ? 6 : st == 1 ? 0x16 : 0x26) + i, x7 + cellX(i), cellY(i), a7);
        }
        if (s.mode == 1)
            for (int i = 0; i < 16; ++i)
                if (s.canPush[size_t(i)]) card(5, x7 + cellX(i) - 13, cellY(i) - 3 - float(s.bob), a7);
        if (s.keys && s.mode == 1 && s.phase == 0) {
            const float cyan[4] = {0x4B / 255.0f, 1, 0xEB / 255.0f, 0.35f * a7};
            m.quad(cellX(s.cursor), cellY(s.cursor), 55, 55, cyan, cyan);
        }
    }
    // The bands over the picture's overhang (window 2; ours the whole screen's edges)
    {
        r.setOrigin(0);
        const float L = r.uiX() + 2 * 16, R = r.uiX() + 2 * (480 + 16), T = 2 * (48 + 32), B = 2 * (284 + 32);
        r.fillRect(0, 0, r.screenW(), T, 0, 0, 0, 1);
        r.fillRect(0, B, r.screenW(), cr::Renderer::kLogicalH - B, 0, 0, 0, 1);
        r.fillRect(0, T, L, B - T, 0, 0, 0, 1);
        r.fillRect(R, T, r.screenW() - R, B - T, 0, 0, 0, 1);
        r.setOrigin(r.uiX());
    }
    if (s.cardTex.id && a6 > 0) card(0, x6, 0, a6); // window 6: the card's name
    drawClockAndOz(app, m, a);         // window 1: BattryAndRealTimeAndOzDraw, the list's and the card's alike
    if (s.complete && s.complete < 3 && s.completeTex.id) { // window 9: the pieces (picture i at the i-th place)
        const float k = sheetScale(s.completeTex);
        for (int p = 0; p < 7; ++p) {
            const short* q = kCompletePict[p];
            m.imagePieceTo(s.completeTex, float(s.piece[size_t(p)][0]), float(s.piece[size_t(p)][1]), q[2], q[3], q[0] * k, q[1] * k,
                           q[2] * k, q[3] * k, 1);
        }
    }
    if (s.complete >= 2) { // SetFade(white, 0 -> 255 / 255 -> 0, 1000 ms)
        const float w = s.complete == 2 ? std::min(1.0f, float(s.cCount) / 20.0f) : std::max(0.0f, 1 - float(s.cCount) / 20.0f);
        r.setOrigin(0);
        r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 1, 1, 1, w);
        r.setOrigin(r.uiX());
    }
    { // window 10: < Back > - the part held lit (DAT_000ed5e4: 1 Back, 2 right, 3 left)
        const float ux = app.renderer.uiX(), v = vitaEdge43(app); // (the Vita's at 0xeb = 235)
        m.backButton(192 + v, 288, pressIn(app, kPressScenarios, ux, 248 + v, 286, 120, 34) ? 1 : pressIn(app, kPressScenarios, ux, 373 + v, 286, 51, 34) ? 2
                               : pressIn(app, kPressScenarios, ux, 192 + v, 286, 51, 34) ? 3 : 0, s.bob, a);
    }
    drawShopNumber(m, presentsUsed(app), 0, 296, 2, 12, a); // window 11: presents used / all
    m.picture(66, 24, 296, a);
    drawShopNumber(m, cards(app) == 5 ? 80 : 64, 36, 296, 2, 12, a);
}

void scenariosCard(App& app, int dir) { // the Back bar's < > (-3 / -2): the next card along, sliding (states 0xc..7)
    App::Scenarios& s = app.scc;
    if (s.phase) return;
    const int n = bingo::cards(app);
    menuSound(app, se::kBack);
    s.nextCard = (s.card + dir + n) % n;
    s.switchDir = dir > 0 ? 1 : 2;
    s.phase = 1, s.phaseCount = 0;
}

void scenariosBack(App& app) {
    App::Scenarios& s = app.scc;
    menuSound(app, se::kBack);
    if (s.card >= 0) { s.cursor = s.card; s.card = -1; s.anim = 0; } // (states 9 / 10: to the overview)
    else closeScenarios(app);
}

void scenariosChoose(App& app) {
    using namespace bingo;
    App::Scenarios& s = app.scc;
    if (s.card < 0) {
        if (!shown(app)) return;
        menuSound(app, se::kChoose);
        const int c = std::clamp(s.cursor, 0, cards(app) - 1);
        s.cursor = 0;
        loadCard(app, c);
        cardWindowsOn(app, 0); // (state 6 from the overview: windows 6..8 fade in where they are)
        return;
    }
    if (done(app, s.card)) { // a whole picture: zoom in / out (state 8)
        if (s.zoomDir) return;
        s.zoomDir = s.zoom < kZoomMax ? 1 : 2;
        menuSound(app, se::kChoose);
        return;
    }
    if (s.mode != 1 || s.pushing >= 0) return;
    if (s.canPush[size_t(s.cursor)] && s.cell[size_t(s.cursor)] == 0) {
        menuSound(app, 0xaf);
        s.canPush[size_t(s.cursor)] = false;
        s.pushing = s.cursor, s.pushClock = 0;
    } else menuSound(app, se::kBuzzer);
}

void scenariosKey(App& app, SDL_Keycode key) {
    App::Scenarios& s = app.scc;
    if (s.complete || s.phase) return; // (no input while a card completes, slides or comes on)
    const bool zoomed = s.card >= 0 && s.zoom > bingo::kZoomMin;
    switch (key) {
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: scenariosBack(app); return;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E: scenariosChoose(app); return;
    case SDLK_PAGEUP: case SDLK_Q: if (s.card >= 0 && !zoomed) scenariosCard(app, -1); return;
    case SDLK_PAGEDOWN: case SDLK_R: if (s.card >= 0 && !zoomed) scenariosCard(app, 1); return;
    default: break;
    }
    int dx = 0, dy = 0;
    if (key == SDLK_LEFT || key == SDLK_A) dx = -1;
    else if (key == SDLK_RIGHT || key == SDLK_D) dx = 1;
    else if (key == SDLK_UP || key == SDLK_W) dy = -1;
    else if (key == SDLK_DOWN || key == SDLK_S) dy = 1;
    if (!dx && !dy) return;
    s.keys = true;
    if (s.card < 0) {
        if (!dx || !bingo::shown(app)) return;
        const int n = bingo::cards(app);
        s.cursor = (s.cursor + dx + n) % n;
        menuSound(app, se::kCursor);
    } else if (zoomed) { // the photo pans (ComUty_PadPhotoProc's limits)
        s.panX = std::clamp(s.panX - 96.0f * float(dx), -bingo::kPanX, bingo::kPanX);
        s.panY = std::clamp(s.panY - 96.0f * float(dy), bingo::kPanUp, bingo::kPanDown);
    } else if (s.mode == 1 && !bingo::done(app, s.card)) {
        s.cursor = (s.cursor % 4 + dx + 4) % 4 + 4 * ((s.cursor / 4 + dy + 4) % 4);
        menuSound(app, se::kCursor);
    } else if (dx) scenariosCard(app, dx);
}

// A tap (menu units, the iPhone screen's): Prc's touch boxes
void scenariosClick(App& app, float mx, float my) {
    using namespace bingo;
    App::Scenarios& s = app.scc;
    if (s.complete || s.phase) return;
    auto in = [&](float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; };
    const float v = vitaEdge43(app); // (the Vita's Back - both kinds - at 0xeb = 235: 43 to the right)
    if (s.card < 0) {
        if (shown(app))
            for (int i = 0; i < cards(app); ++i)
                if (in(8 + 94.0f * float(i), 177, 90, 62)) { s.cursor = i; scenariosChoose(app); return; }
        if (in(192 + v, 286, 256, 34)) scenariosBack(app); // BackButtonProc(1)
        return;
    }
    if (in(248 + v, 286, 120, 34)) { scenariosBack(app); return; } // BackButtonProc(2)
    if (in(192 + v, 286, 51, 34)) { scenariosCard(app, -1); return; }
    if (in(373 + v, 286, 51, 34)) { scenariosCard(app, 1); return; }
    if (done(app, s.card)) {
        if (in(0, 48, 480, 236)) scenariosChoose(app);
        return;
    }
    if (s.mode == 1)
        for (int i = 0; i < 16; ++i)
            if (in(cellX(i), cellY(i), 55, 55)) { s.cursor = i; scenariosChoose(app); return; }
}

// A drag over a zoomed picture (tex2d units)
void scenariosDrag(App& app, float dx, float dy) {
    App::Scenarios& s = app.scc;
    if (s.card < 0 || s.zoom <= bingo::kZoomMin || s.zoomDir || s.phase) return;
    s.panX = std::clamp(s.panX + dx, -bingo::kPanX, bingo::kPanX);
    s.panY = std::clamp(s.panY + dy, bingo::kPanUp, bingo::kPanDown);
}

// ---- Collection (__menuu_CollectionSet / Put / Prc, collection_rate.mct) ----
// How much of each character's weapons and armor, and the jewels, the bag has seen
// (_anItemFlagTb: 171 in all), the rate's rank (every 20%: the statues and Bronze .. Platinum),
// over a turning dial. Window 5 (the rows) slides in from the left, 6 (the rank) from the right.
// Positions are the iPhone screen's, the pictures _gkastCollectionMct's (the SD sheet's units)
namespace collection {
constexpr short kPict[29][4] = {
    {240, 372, 212, 32}, {392, 204, 120, 120}, {0, 160, 232, 232}, {392, 160, 72, 44}, {232, 300, 80, 72},
    {232, 160, 160, 140}, {0, 440, 128, 28}, {0, 0, 92, 160}, {92, 0, 92, 160}, {184, 0, 92, 160},
    {276, 0, 92, 160}, {368, 0, 144, 160}, {0, 392, 12, 20}, {0, 412, 16, 28}, {160, 412, 20, 20},
    {0, 496, 12, 12}, {88, 468, 92, 24}, {180, 464, 80, 28}, {260, 464, 56, 28}, {128, 440, 112, 24},
    {120, 392, 68, 20}, {188, 392, 52, 16}, {188, 408, 44, 16}, {0, 468, 88, 28}, {316, 460, 56, 52},
    {372, 460, 56, 52}, {428, 460, 56, 52}, {316, 408, 56, 52}, {12, 504, 124, 4}};
constexpr short kRange[7][2] = {{201, 224}, {285, 309}, {229, 252}, {313, 336}, {257, 280}, {341, 364}, {369, 394}};
constexpr int kTotal = 171;
// The rows (__Collection_ParamDraw): y, kind (0 weapon, 1 armor, 2 jewel)
constexpr short kRow[7][2] = {{56, 0}, {84, 1}, {116, 0}, {144, 1}, {176, 0}, {204, 1}, {239, 2}};
constexpr short kRankWordAt[4][2] = {{300, 76}, {300, 72}, {300, 72}, {300, 76}}; // _anPos: Bronze .. Platinum

int have(const App& app, int row) {
    int n = 0;
    for (int i = kRange[row][0]; i <= kRange[row][1]; ++i) n += ownedCount(app, i) > 0;
    return n;
}
int percent(const App& app) {
    int n = 0;
    for (int r = 0; r < 7; ++r) n += have(app, r);
    return n * 100 / kTotal;
}
} // namespace collection

void openCollection(App& app) {
    App::Collection& c = app.collection;
    c.open = true;
    c.anim = 0;
    if (!c.tex.id) {
        cr::Image img;
        if (loadImage(app, "collection_rate.mct", img)) c.tex = app.renderer.upload(img);
    }
}

void closeCollection(App& app) {
    app.collection.open = false;
    app.renderer.destroy(app.collection.tex);
}

void stepCollection(App& app) {
    App::Collection& c = app.collection;
    ++c.anim;
    c.spin += 0.005f; // (Prc state 3: the dial a turn every 200 frames)
    if (c.spin >= 1) c.spin -= 1;
}

void drawCollection(App& app) {
    using namespace collection;
    App::Collection& c = app.collection;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    r.setOrigin(0);
    r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 1);
    r.setOrigin(r.uiX());
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu pm(app, m);
    const float t = std::min(1.0f, float(c.anim) / 8.0f), ease = 1 - (1 - t) * (1 - t);
    const float k = sheetScale(c.tex);
    auto pict = [&](int p, float x, float y, float a, bool flipU = false, bool flipV = false) {
        const short* q = kPict[p];
        const float u = (q[0] + (flipU ? q[2] : 0)) * k, v = (q[1] + (flipV ? q[3] : 0)) * k;
        m.imagePieceTo(c.tex, x, y, q[2], q[3], u, v, (flipU ? -q[2] : q[2]) * k, (flipV ? -q[3] : q[3]) * k, a);
    };
    auto digits = [&](int p, int value, int n, float x, float y, float step, float a) { // leading zeros left out
        const short* q = kPict[p];
        for (int i = 0; i < n; ++i) {
            int pow = 1;
            for (int j = i + 1; j < n; ++j) pow *= 10;
            if (i + 1 < n && value < pow) continue;
            const int d = value / pow % 10;
            m.imagePieceTo(c.tex, x + step * float(i), y, q[2], q[3], (q[0] + q[2] * d) * k, q[1] * k, q[2] * k, q[3] * k, a);
        }
    };
    for (int i = 0; i < 8; ++i) pict(1, 120.0f * float(i % 4), 48 + 120.0f * float(i / 4), t);
    if (c.tex.id) { // the dial (tex2d_add, turned by DAT_e6410 - a turn = 1)
        const short* q = kPict[2];
        r.drawQuadUVRotated(c.tex, 2 * (16 + 124 + q[2] / 2.0f), 2 * (32 + 50 + q[3] / 2.0f), 2.0f * q[2], 2.0f * q[3], q[0] * k,
                            q[1] * k, q[2] * k, q[3] * k, c.spin * 6.2831853f, t);
    }
    pict(3, 99, 49, t);
    pict(3, 309, 49, t, true);
    pict(3, 99, 239, t, false, true);
    pict(3, 309, 239, t, true, true);
    // The bands (window 2; ours the whole screen's edges)
    r.setOrigin(0);
    const float T = 2 * (48 + 32), B = 2 * (284 + 32);
    r.fillRect(0, 0, r.screenW(), T, 0, 0, 0, 1);
    r.fillRect(0, B, r.screenW(), cr::Renderer::kLogicalH - B, 0, 0, 0, 1);
    r.fillRect(0, T, r.uiX() + 32, B - T, 0, 0, 0, 1);
    r.fillRect(r.uiX() + 2 * (480 + 16), T, r.screenW(), B - T, 0, 0, 0, 1);
    r.setOrigin(r.uiX());
    pict(0, 8, 4, t);
    drawClockAndOz(app, m, t);
    { // window 5, from the left
        const float dx = -120 * (1 - ease);
        for (int i = 0; i < 4; ++i) pict(24 + i, 1 + dx, 51 + 60.0f * float(i), t);
        for (int row = 0; row < 7; ++row) {
            const float x = 58 + dx, y = kRow[row][0];
            const int n = have(app, row), all = kRange[row][1] - kRange[row][0] + 1;
            pict(20 + kRow[row][1], x, y, t);
            if (n < all) {
                digits(12, n, 2, x + 138, y + 4, 12, t);
                digits(12, all, 2, x + 174, y + 4, 12, t);
                pict(15, x + 162, y + 12, t);
            } else pict(23, x + 128, y, t);
            pict(28, x + 2, y + 20, t);
            const float fill[4] = {0x17 / 255.0f, 0xe8 / 255.0f, 1, t};
            if (n > 0) m.quad(x + 6, y + 21, float(n * 116 / all), 2, fill, fill);
        }
    }
    { // window 6, from the right
        const float dx = 120 * (1 - ease);
        const int pct = percent(app), rank = pct / 20;
        if (rank > 1) pict(4, 275 + dx, 53, t);
        if (rank != 0) pict(5, 320 + dx, 88, t);
        pict(4, 389 + dx, 209, t);
        if (rank != 0) pict(rank + 6, (rank == 5 ? 332 : 362) + dx, rank == 5 ? 85 : 78, t);
        if (rank > 1) pict(rank + 14, kRankWordAt[rank - 2][0] + dx, kRankWordAt[rank - 2][1], t);
        pict(6, 304 + dx, 204, t);
        if (pct == 100) pict(23, 308 + dx, 236, t);
        digits(13, pct, 3, 396 + dx, 232, 16, t);
        pict(14, 444 + dx, 240, t);
    }
    const float v = vitaEdge43(app); // (the Vita's Back at 0xeb = 235)
    m.backBar(192 + v, 288, pressIn(app, kPressCollection, app.renderer.uiX(), 192 + v, 286, 256, 34), t); // (lit while held: DAT_000ed638)
}

void collectionKey(App& app, SDL_Keycode key) {
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK || key == SDLK_RETURN || key == SDLK_KP_ENTER ||
        key == SDLK_SPACE || key == SDLK_E) {
        menuSound(app, se::kBack);
        closeCollection(app);
    }
}

void collectionClick(App& app, float mx, float my) { // BackButtonProc(1)
    const float v = vitaEdge43(app); // (the Vita's Back at 0xeb = 235)
    if (mx >= 192 + v && mx < 448 + v && my >= 286 && my < 320) collectionKey(app, SDLK_ESCAPE);
}


// ---- The Instruction Manual (Options; MuScrList + MsgUty: the topics, then the page) ----
// OptionPrc 0xf..0x16: the topics are the manual items the bag holds (make_item_list kind 9,
// 419..473, given as the story goes), by name; each one's page is system message 56000 + (item -
// 419) (MsgUty_SetMsg: block 5). (56040 "About Option" is OptionSet's, through the message
// window: openOptions)
// A manual message's title: its first non-empty line (the messages open with an empty one)
std::string manualTitle(const App& app, int id) {
    std::vector<std::vector<std::string>> pages;
    if (!app.systemText.message(id, textLanguage(app), nullptr, pages)) return std::string();
    for (const auto& pg : pages)
        for (const std::string& l : pg)
            if (!l.empty()) return l;
    return std::string();
}



std::vector<int> manualTopics(const App& app) { // the items
    std::vector<int> items;
    for (int item = kManualFirstItem; item <= kManualLastItem; ++item)
        if (ownedCount(app, item) > 0) items.push_back(item);
    return items;
}

std::vector<std::string> manualPage(const App& app, int id) { // the lines after the title
    std::vector<std::vector<std::string>> pages;
    std::vector<std::string> lines;
    if (!app.systemText.message(id, textLanguage(app), nullptr, pages)) return lines;
    for (const auto& pg : pages)
        for (const std::string& l : pg) lines.push_back(l);
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    if (!lines.empty()) lines.erase(lines.begin()); // the title
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    return lines;
}

// A topic's page (OptionPrc 0x12..0x16): SetFlag(3), MsgUty_WinOpen - the standard window (_SetWin
// style 8: the tutorial band) at _mwin[4] (tex2d y 16, the picture below it) for an id in
// MsgUty_SetMsg's table, else _mwin[5] (y 108; the port's msgLines 2 / 1 pick those rects), SE
// 0x97 - and MsgUty_SetMsg (block 5: 56000 + id), paged by tap
// (PrcMsg). Its table (0xd5200, {id, picture}) gives five of them a picture (MsgUty_TexDispInit: the
// rows at 0xd5560 {slot 3, pages, (row, frames) x 4, x 144, y 160}): it shows from the first page
// turn, a page of the picture a page of text, until its pages run out or the message ends. The
// last page out, the window closes (FUN_0008722c: ClrWin, SE 0x98) and once gone the list is back
// at that item (0x16 -> 0xf: MuScrList_SetListPosFromItemNo)

namespace manualpic {
struct Row { int id; const char* sheet; int pages; int row[4], frames[4]; };
constexpr Row kRows[5] = {
    {12, "battle_01_hd.mct", 4, {0, 1, 2, 3}, {1, 4, 4, 1}}, {13, "battle_02_hd.mct", 4, {0, 1, 2, 3}, {1, 1, 2, 1}},
    {18, "battle_03_hd.mct", 3, {0, 1, 2, -1}, {3, 1, 1, -1}}, {10, "gene_01_hd.mct", 3, {0, 1, 2, -1}, {1, 2, 1, -1}},
    {19, "battle_04_hd.mct", 2, {0, 1, -1, -1}, {1, 1, -1, -1}}};
constexpr float kX = 144, kY = 160;
const Row* find(int id) {
    for (const Row& r : kRows)
        if (kManualFirstPage + r.id == id) return &r;
    return nullptr;
}
} // namespace manualpic

void manualShow(App& app, int id) {
    App::ManualMsg& mm = app.manualMsg;
    app.renderer.destroy(mm.pic.tex);
    mm.pic = App::TutoTex();
    mm.id = id;
    mm.pages.clear();
    if (!app.systemText.message(id, textLanguage(app), nullptr, mm.pages) || mm.pages.empty())
        mm.pages = {{"[message " + std::to_string(id) + "]"}};
    mm.page = 0, mm.typed = 0;
    mm.win = App::MsgWin();
    mm.win.state = 1;
    menuSound(app, 0x97);
    const manualpic::Row* row = manualpic::find(id);
    mm.picture = row != nullptr;
    if (row) {
        App::TutoTex& t = mm.pic;
        cr::Image img;
        std::string name = row->sheet;
        if (loadImage(app, name, img) || loadImage(app, name.replace(name.find("_hd"), 3, ""), img)) t.tex = app.renderer.upload(img);
        t.on = true, t.state = 2, t.pages = row->pages;
        for (int k = 0; k < 4; ++k) t.row[k] = row->row[k], t.frames[k] = row->frames[k];
        t.x = manualpic::kX, t.y = manualpic::kY;
    }
    mm.state = 2;
}

void manualCloseMessage(App& app) { // the message done: ClrFlag(3) - the picture goes - and the window closes
    App::ManualMsg& mm = app.manualMsg;
    app.renderer.destroy(mm.pic.tex);
    mm.pic = App::TutoTex();
    mm.win.state = 4;
    menuSound(app, 0x98);
    mm.state = 3;
}

int manualPageChars(const App& app) {
    const App::ManualMsg& mm = app.manualMsg;
    return mm.page < mm.pages.size() ? pageChars(mm.pages[mm.page]) : 0;
}

// A tap on the page (PrcMsg): one still typing comes out whole, one out turns - the picture to its
// next page (flag 0xfeb), gone past its last - and the last page out ends the message
void manualTap(App& app) {
    App::ManualMsg& mm = app.manualMsg;
    if (mm.state != 2 || mm.win.state != 3) return;
    if (mm.typed < manualPageChars(app)) { mm.typed = manualPageChars(app); return; }
    mm.typed = 0;
    if (mm.pic.on) {
        mm.pic.timer = 20, mm.pic.frame = 0;
        if (++mm.pic.page >= mm.pic.pages) { app.renderer.destroy(mm.pic.tex); mm.pic = App::TutoTex(); }
    }
    if (++mm.page >= mm.pages.size()) manualCloseMessage(app);
}

void openManual(App& app, int intro) { // intro: a page shown before any topic (OptionSet's 56040), 0 none
    app.manualOpen = true;
    app.manualIntro = intro;
    app.manualCursor = 0;
    app.manualTop = 0;
    app.manualAnim = 0;
    App::ManualMsg& mm = app.manualMsg;
    app.renderer.destroy(mm.pic.tex);
    mm = App::ManualMsg();
    if (intro) manualShow(app, intro); // (OptionPrc 0..2: the same window, then Options)
}

void manualChoose(App& app) { // OptionPrc 0x10: a topic chosen - the list's windows go, 4 frames on its page opens
    const std::vector<int> topics = manualTopics(app);
    if (app.manualCursor < 0 || app.manualCursor >= int(topics.size())) return;
    menuSound(app, se::kChoose); // (__ScrListSelectSePlay)
    App::ManualMsg& mm = app.manualMsg;
    mm.id = kManualFirstPage + topics[size_t(app.manualCursor)] - kManualFirstItem;
    mm.state = 1, mm.count = 4;
}

// A frame (20 Hz): 0x12's wait, PrcWin and PrcMsg's typing, the picture's frames, the window closing
void stepManual(App& app) {
    App::ManualMsg& mm = app.manualMsg;
    switch (mm.state) {
    case 1:
        if (--mm.count < 0) manualShow(app, mm.id);
        break;
    case 2:
        stepMsgWin(mm.win);
        if (mm.win.state == 3) mm.typed = app.typeOn ? std::min(mm.typed + 1, manualPageChars(app)) : manualPageChars(app);
        if (mm.pic.on) stepTutoFrames(mm.pic);
        break;
    case 3:
        stepMsgWin(mm.win);
        if (mm.win.state == 6) {
            mm.state = 0;
            if (app.manualIntro) { app.manualOpen = false; break; } // (the intro: on to Options)
            app.manualAnim = 0; // (0xf: MuScrList_Start - the list's windows anew, at the item)
        }
        break;
    default: break;
    }
}

constexpr int kManualRows = 9;

void drawManual(App& app) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    const App::ManualMsg& mm = app.manualMsg;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    r.setOrigin(0);
    if (!app.manualIntro) r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 0.75f); // ("About Option": no mask yet)
    r.setOrigin(r.uiX());
    if (!app.manualIntro) {
        const PhoneFit phoneFit(app, m); // the iPhone layout: its screen's menu coordinates
        const float a = std::min(1.0f, app.manualAnim / 8.0f);
        m.picture(103, 8, 4, a); // "Options" (_stMuPictInfo_Option)
        m.text(sysLabel(app, 58081, "Instruction Manual"), 236, 14, false, a, 16);
        // The list (MuScrList: windows 7 and 8) fades out as a topic opens and is off while it's up
        const float la = mm.state == 0 ? a : mm.state == 1 ? a * float(std::max(0, mm.count)) / 4.0f : 0.0f;
        if (la > 0) {
            const std::vector<int> topics = manualTopics(app);
            for (int i = 0; i < kManualRows && app.manualTop + i < int(topics.size()); ++i) {
                const int n = app.manualTop + i;
                m.rowBar(20, 52 + 32.0f * float(i), itemName(app, topics[size_t(n)]), la, app.manualCursor == n);
            }
            if (app.manualTop > 0) m.text("...", 100, 40, false, la, 12);
            if (app.manualTop + kManualRows < int(topics.size())) m.text("...", 100, 52 + 32.0f * kManualRows - 6, false, la, 12);
            m.backButton(192, 344, false, 0, la);
            if (app.showHud) m.text("Up/Down  Enter: read  Esc: back", 20, 372, false, la * 0.8f, 9); // (our key hints)
        }
    }
    if (mm.state < 2) return;
    // The message in the standard window (drawMessageBox with the manual's window, style and rect)
    const int keepStyle = app.msgStyle, keepLines = app.msgLines, keepColour = app.msgColour, keepLimit = app.textLimit;
    const App::MsgWin keepWin = app.msgWin;
    const bool keepSkip = app.messageSkip;
    app.msgStyle = 8, app.msgLines = mm.picture ? 2 : 1, app.msgWin = mm.win, app.msgColour = -1;
    app.messageSkip = true; // (no skip corner: no scene is playing)
    const bool text = mm.state == 2 && mm.page < mm.pages.size();
    app.textLimit = app.typeOn ? mm.typed : 1 << 30;
    drawMessageBox(app, text ? mm.pages[mm.page] : std::vector<std::string>(), "", text && mm.typed >= manualPageChars(app), "");
    app.msgStyle = keepStyle, app.msgLines = keepLines, app.msgWin = keepWin, app.msgColour = keepColour;
    app.textLimit = keepLimit, app.messageSkip = keepSkip;
    if (mm.state == 2) drawTutoPicture(app, mm.pic); // (MsgUty_TexDispDraw: while flag 3 is set)
    r.setOrigin(r.uiX());
}

void manualKey(App& app, SDL_Keycode key) {
    App::ManualMsg& mm = app.manualMsg;
    const bool confirm = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK;
    if (mm.state == 2) { // the page: a tap turns it (keys: confirm; back closes it at once - the port's)
        if (confirm) manualTap(app);
        else if (back && mm.win.state == 3) manualCloseMessage(app);
        return;
    }
    if (mm.state != 0) return; // (the list going, or the window closing)
    if (confirm) { manualChoose(app); return; }
    const int n = int(manualTopics(app).size());
    int& c = app.manualCursor;
    switch (key) {
    case SDLK_UP: case SDLK_W: c = std::max(0, c - 1); menuSound(app, se::kCursor); break;
    case SDLK_DOWN: case SDLK_S: c = std::min(n - 1, c + 1); menuSound(app, se::kCursor); break;
    case SDLK_PAGEUP: c = std::max(0, c - kManualRows); menuSound(app, se::kPage); break; // a list flicked a page
    case SDLK_PAGEDOWN: c = std::min(n - 1, c + kManualRows); menuSound(app, se::kPage); break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: app.manualOpen = false; menuSound(app, se::kBack); return;
    default: return;
    }
    if (c >= 0 && c < app.manualTop) app.manualTop = c;
    if (c >= app.manualTop + kManualRows) app.manualTop = c - kManualRows + 1;
}

void manualClick(App& app, float px, float py) {
    App::ManualMsg& mm = app.manualMsg;
    if (mm.state == 2) { manualTap(app); return; } // (anywhere: PrcMsg's tap)
    if (mm.state != 0 || app.manualIntro) return;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my;
    menuPointFit(app, lx, ly, mx, my);
    if (my >= 344 && my < 368 && mx >= 192 && mx < 424) { app.manualOpen = false; menuSound(app, se::kBack); return; }
    if (mx >= 20 && mx < 240)
        for (int i = 0; i < kManualRows; ++i)
            if (my >= 52 + 32.0f * i && my < 76 + 32.0f * i && app.manualTop + i < int(manualTopics(app).size())) {
                app.manualCursor = app.manualTop + i;
                manualChoose(app); // a topic tapped
                return;
            }
}

// ---- Character Select (Util 4: __menuu_StartPut) ----
// Select Scenario (Util 4: __menuu_StartSet / Put / Prc / Clr, Omega). StartSet reads the scenario's
// sheet into tex2d slot 0x10 - chara1, chara2 once flag 0x4bd or 0x6a2 is set - and, from flag 31
// on, serect (0x11) and chara3 (0x12) for the second page. StartPut, in group-1 pictures
// (kOutReadPicture, the iPhone screen's units doubled on the iPad): the art (iPad 24 at (-32,
// -64), iPhone 22 at (0, 0)), Back at (124, 288), ONE plate at (172, 232) - word 12, 17 on the
// second page -, and the banner over it all (iPad 27 at (-32, -64), iPhone 26 at (0, 0); from the
// page's sheet). StartClr: var 0 = 0 Back, 1 the first scenario, 2 the second.
// The second page (StartPrc 4..8): chara3's 24 / 32 fall over the first, pulled by serect's cord
// (29, rocking by _gafMoveTbl) over x 360..480; Left / Right (the port's) turn it as well.
constexpr int kScenarioWord[2] = {12, 17};

bool laterPairsOpen(const App& app) { return allPairsFlags(app).flag(31); } // StartSet: the second page


void openCharSelect(App& app) {
    app.charSelectOpen = true;
    app.charSelectAnim = 0;
    app.charSelectPage = 0;
    app.charSelectCursor = 0;
    app.renderer.destroy(app.charSelectArt);
    app.renderer.destroy(app.charSelectArt2);
    const cr::GameState g = allPairsFlags(app);
    cr::Image img;
    if (loadImage(app, g.flag(0x6a2) || g.flag(0x4bd) ? "chara2.mct" : "chara1.mct", img)) app.charSelectArt = app.renderer.upload(img);
    if (g.flag(31) && loadImage(app, "chara3.mct", img)) app.charSelectArt2 = app.renderer.upload(img);
    app.renderer.destroy(app.startPage.serect);
    app.startPage = App::StartPage();
    app.startPage.y = pageTop(app);
    if (g.flag(31) && loadImage(app, "serect.mct", img)) app.startPage.serect = app.renderer.upload(img);
}

// The page's fall and rise (StartPrc states 4 / 6 / 8): down from e59b0 (iPad -440, iPhone -388)
// as 4.5 t^2, a bounce (up at 2 x 20 a frame to -80, back at 20 / 2), or back up at 20 a frame;
// the music and a sound each way
constexpr float kPageSpeed = 20;
float pageTop(const App& app) { return app.renderer.phoneLayout() ? -388.0f : -440.0f; }

// StartSet: flag 0x643 - the card (serect 28 at (DAT_e59a8, 24)) slides in, 16 a frame, as the second
// page settles (StartPrc 7) and out before it goes back up (5); __Start_ChangeModeCardProc's ends,
// the iPad's (e59bc / e59b8): -16 / -176 (the iPhone's 0 / -160)
bool modeCard(const App& app) { return allPairsFlags(app).flag(0x643); }
float cardIn(const App& app) { return app.renderer.phoneLayout() ? 0.0f : -16.0f; }
float cardOut(const App& app) { return app.renderer.phoneLayout() ? -160.0f : -176.0f; }
void charSelectTurn(App& app) { // the second page, once it's open
    App::StartPage& sp = app.startPage;
    if (!laterPairsOpen(app) || !app.charSelectArt2.id) { menuSound(app, se::kBuzzer); return; }
    if (sp.phase) return;
    sp.holding = false, sp.pull = 0;
    sp.t = 0;
    if (app.charSelectPage == 0) {
        sp.shown = true;
        sp.y = pageTop(app);
        sp.phase = 1;
        playBgm(app, 0x13);
        menuSound(app, 0xf8);
    } else if (modeCard(app)) {
        sp.phase = 6; // StartPrc 5: the card out first
    } else {
        sp.phase = 4;
        playBgm(app, 0x1d);
        menuSound(app, 0x91);
    }
}

void stepCharSelect(App& app) {
    App::StartPage& sp = app.startPage;
    // The Vita's pad (__menuu_StartPrc): Down pressed and held pulls the cord 2 a frame - at 24 (12
    // frames) the page turns (0x12d), as the finger's pull; let go, it springs back
    if (app.pad && !sp.phase && laterPairsOpen(app) && !app.pointerLast) {
        const bool down = SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
                          float(SDL_GetGamepadAxis(app.pad, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f >= 0.2549f;
        if (!down) sp.padPull = false, sp.pull = sp.holding ? sp.pull : 0;
        else if (!sp.padPull && !sp.holding) sp.padPull = true, sp.padPullFresh = true, sp.pull = 0;
        else if (sp.padPull && sp.padPullFresh) {
            sp.pull = std::min(24.0f, sp.pull + 2.0f);
            if (sp.pull >= 24) {
                menuSound(app, se::kSlide);
                sp.sway = 0, sp.swayStep = 0, sp.swayBack = false;
                sp.padPullFresh = false, sp.pull = 0;
                charSelectTurn(app);
            }
        }
    }
    const float top = pageTop(app);
    switch (sp.phase) {
    case 1:
        sp.y = top + 4.5f * float(sp.t) * float(sp.t);
        ++sp.t;
        if (sp.y >= 0) sp.y = 0, sp.phase = 2;
        break;
    case 2:
        sp.y -= 2 * kPageSpeed;
        if (sp.y <= -80) sp.y = -80, sp.phase = 3;
        break;
    case 3:
        sp.y += kPageSpeed / 2;
        if (sp.y >= 0) sp.y = 0, sp.phase = modeCard(app) ? 5 : 0, app.charSelectPage = sp.phase ? 0 : 1;
        break;
    case 5: // StartPrc 7, then 8: the page is the second's
        sp.card += 16;
        if (sp.card >= cardIn(app)) sp.card = cardIn(app), sp.phase = 0, app.charSelectPage = 1;
        break;
    case 6: // StartPrc 5, then 4: the way back up
        sp.card -= 16;
        if (sp.card <= cardOut(app)) {
            sp.card = cardOut(app), sp.phase = 4;
            playBgm(app, 0x1d);
            menuSound(app, 0x91);
        }
        break;
    case 4:
        sp.y -= kPageSpeed;
        if (sp.y <= top) sp.y = top, sp.phase = 0, sp.shown = false, app.charSelectPage = 0;
        break;
    default: break;
    }
    if (!sp.phase && laterPairsOpen(app)) { // the cord rocks (state 2)
        static const float kSway[20] = {0.009f, 0.008f, 0.007f, 0.006f, 0.005f, 0.004f, 0.003f, 0.002f, 0.001f, 0,
                                        0, -0.001f, -0.002f, -0.003f, -0.004f, -0.005f, -0.006f, -0.007f, -0.008f, -0.009f};
        sp.sway += sp.swayBack ? -kSway[sp.swayStep] : kSway[sp.swayStep];
        if (++sp.swayStep > 19) sp.swayStep = 0, sp.swayBack = !sp.swayBack;
    }
    if (!sp.bobBack) { if ((sp.bob += 2) >= 8) sp.bobBack = true; }
    else if ((sp.bob -= 2) <= 0) sp.bobBack = false;
}

// The cord (ComUty_PadChkMove over (360, 0, 120, 160)): held and pulled 24 down, the page turns
void charSelectPress(App& app, float mx, float my) {
    App::StartPage& sp = app.startPage;
    if (sp.phase || !laterPairsOpen(app) || mx < 360 || mx >= 480 || my < 0 || my >= 160) return;
    sp.holding = true;
    sp.holdY = my;
    sp.pull = 0;
}
void charSelectDrag(App& app, float my) {
    App::StartPage& sp = app.startPage;
    if (!sp.holding) return;
    sp.pull = std::clamp(my - sp.holdY, 0.0f, 24.0f);
    if (sp.pull >= 24) {
        menuSound(app, se::kSlide);
        sp.sway = 0, sp.swayStep = 0, sp.swayBack = false;
        charSelectTurn(app);
    }
}
void charSelectRelease(App& app) { app.startPage.holding = false, app.startPage.pull = 0; }

void closeCharSelect(App& app, int scenario) { // __menuu_StartClr: var 0 = 0 (Back), 1 / 2 the scenario
    app.charSelectOpen = false;
    app.game.vars[0] = scenario;
    if (scenario) app.chosenPair = scenario;
    else app.renderer.destroy(app.charSelectArt); // (a scenario: the save screen shows it too - showMap frees it)
    if (scenario == 2 && app.charSelectArt2.id) { // SaveLoadPut: slot 0x12 (chara3) for the second scenario
        app.renderer.destroy(app.charSelectArt);
        app.charSelectArt = app.charSelectArt2;
        app.charSelectArt2 = cr::Texture();
    }
    app.renderer.destroy(app.charSelectArt2);
    app.renderer.destroy(app.startPage.serect);
    app.startPage = App::StartPage();
    app.charSelectPage = 0;
}

// The page's art, picture 24 (iPhone 22) - or with `banner` the banner over it, 27 (26) (Omega's
// StartPut); `grey` darkens it. Drawn in menu units under PhoneMenu
// Android 1.1.6 (StartPut, SaveLoadPut on a Retina phone): the art through MuComPictTexNoDrawFull2 -
// the iPhone's 480 x 320 picture over the whole phone, its middle kept (Part 1 18 s, Part 8 4901.5 /
// 4904.5 s: chara1 / 2 / 3 at 4 / 3 on the 1280 x 720 phone, (0, -67)); the banner at - oy
// (StartPut's floor(-oy / scale)). 1 and no shift elsewhere
float charArtScale(const App& app) {
    if (!app.renderer.androidScreen()) return 1;
    return std::max((480 + 2 * androidEdgeX(app)) / 480, (320 + 2 * androidEdgeY(app)) / 320);
}

// The picture `pict` of `tex` (kOutReadPicture, the menu units' size) at (x, y), fitted as
// MuComPictTexNoDrawFull2 fits it in the Android layout (about the screen's middle, `dy` moving it)
void drawFullPicture(App& app, const cr::Texture& tex, int pict, float x, float y, float alpha, float grey = 1) {
    if (!tex.id) return;
    const short* p = cr::kOutReadPicture[pict];
    const float k = sheetScale(tex), s = charArtScale(app);
    app.menu.imagePieceTo(tex, 240 - (240 - x) * s, 160 - (160 - y) * s, p[2] * s, p[3] * s, p[0] * k, p[1] * k, p[2] * k,
                          p[3] * k, alpha, grey, grey, grey);
}

void drawCharArt(App& app, float alpha, bool banner, float grey) {
    const cr::Texture& tex = app.charSelectPage && app.charSelectArt2.id ? app.charSelectArt2 : app.charSelectArt;
    if (!tex.id) return;
    cr::MenuPainter& m = app.menu;
    const bool phone = app.renderer.phoneLayout();
    const float x = phone ? 0.0f : -32.0f, y = phone ? 0.0f : -64.0f;
    const auto draw = [&](int pict, float dy) {
        const short* p = cr::kOutReadPicture[pict];
        const float k = sheetScale(tex);
        m.imagePieceTo(tex, x, y + dy, p[2], p[3], p[0] * k, p[1] * k, p[2] * k, p[3] * k, alpha, grey, grey, grey);
    };
    if (banner) draw(phone ? 26 : 27, -androidEdgeY(app));
    else if (app.renderer.androidScreen()) drawFullPicture(app, tex, 22, 0, 0, alpha, grey);
    else draw(phone ? 22 : 24, 0);
}

void drawCharSelect(App& app) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu phoneMenu(app, m); // the menus' coordinates: the iPhone screen's
    r.setOrigin(0);
    r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 1);
    r.setOrigin(r.uiX());
    const float t = std::min(1.0f, app.charSelectAnim / 10.0f);
    const App::StartPage& sp = app.startPage;
    const bool phone = r.phoneLayout(), later = laterPairsOpen(app) && app.charSelectArt2.id;
    { // window 0: the first page's art (z -20)
        const int page = app.charSelectPage;
        app.charSelectPage = 0;
        drawCharArt(app, t, false);
        app.charSelectPage = page;
    }
    if (later && sp.shown) { // window 1: the second page's, falling (chara3 24 and 32 under it; iPhone 22 / 31)
        if (r.androidScreen()) { // (22 fitted as the first page's; 31, its foot, + oy: _PictTexNoDrawFull2W)
            drawFullPicture(app, app.charSelectArt2, 22, 0, sp.y, t);
            outPicture(m, app.charSelectArt2, 31, 0, sp.y + 320 + androidEdgeY(app), t);
        } else if (phone) {
            outPicture(m, app.charSelectArt2, 22, 0, sp.y, t);
            outPicture(m, app.charSelectArt2, 31, 0, sp.y + 320, t);
        } else {
            outPicture(m, app.charSelectArt2, 24, -32, sp.y - 64, t);
            outPicture(m, app.charSelectArt2, 32, -16, sp.y + 352, t);
        }
    }
    m.backBar(124, 288, pressIn(app, kPressCharSelect, app.renderer.uiX(), 124, 288, 232, 24), t); // ComUty_BackButtonPosSetXYDraw(124, 288), mode 1; lit while held
    const int word = kScenarioWord[app.charSelectPage & 1];
    m.plate(172, 232, plateIcon(word), word,
            (app.charSelectCursor == 0 && !app.pointerLast) || pressIn(app, kPressCharSelect, app.renderer.uiX(), 172, 232, 136, 40), t, false); // (StartPrc's DAT_000ecad8: held)
    if (later && sp.serect.id) { // window 2: the cord (serect 29, tex2d_add at (260, pull - 205), turned), the arrow while held, 28
        const short* q = cr::kOutReadPicture[29];
        const float k = sheetScale(sp.serect);
        // (Android 1.1.6: the cord - oy, the card - ox: StartPut's floor(-oy / scale), floor(-ox / scale))
        const float ex = androidEdgeX(app), ey = androidEdgeY(app);
        r.drawQuadUVRotated(sp.serect, 2 * (16 + 260 + q[2] / 2.0f), 2 * (32 - ey + std::max(sp.pull, 0.0f) - 205 + q[3] / 2.0f),
                            2.0f * q[2], 2.0f * q[3], q[0] * k, q[1] * k, q[2] * k, q[3] * k, sp.sway * 6.2831853f, t);
        if (sp.holding) outPicture(m, sp.serect, 30, 406, 178 - ey - float(sp.bob), t);
        outPicture(m, sp.serect, 28, sp.card - ex, 24, t);
    }
    { // window 3: the banner over it all (z -4) - chara3's once the second page is open
        const int page = app.charSelectPage;
        app.charSelectPage = later ? 1 : 0;
        drawCharArt(app, t, true);
        app.charSelectPage = page;
    }
    if (keyHints(app)) // (our key hints)
        m.text(laterPairsOpen(app) ? "Enter  Left/Right: the other scenario  Esc: back" : "Enter  Esc: back", 20, 352, false,
               t * 0.8f, 9);
}

void charSelectKey(App& app, SDL_Keycode key) {
    if (app.startPage.phase) return; // (the page on its way)
    switch (key) {
    case SDLK_LEFT: case SDLK_A: case SDLK_RIGHT: case SDLK_D: case SDLK_Q: case SDLK_TAB: case SDLK_PAGEUP: case SDLK_PAGEDOWN:
        charSelectTurn(app);
        break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E:
        menuSound(app, se::kChoose);
        closeCharSelect(app, app.charSelectPage + 1);
        break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: menuSound(app, se::kBack); closeCharSelect(app, 0); break;
    default: break;
    }
}

void charSelectClick(App& app, float px, float py) {
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my;
    menuPoint(app, lx, ly, mx, my);
    if (app.startPage.phase) return;
    charSelectPress(app, mx, my);
    if (app.startPage.holding) return;
    if (mx >= 172 && mx < 172 + 136 && my >= 232 && my < 272) { // _pad_chk_updw_box(0xac, 0xe8, 0x88, 0x28)
        menuSound(app, se::kChoose);
        closeCharSelect(app, app.charSelectPage + 1);
        return;
    }
    if (my >= 288 && my < 312 && mx >= 124 && mx < 356) { menuSound(app, se::kBack); closeCharSelect(app, 0); }
}

// ---- The title menu (Util 0: __menuu_Title*) ----
// __menuu_TitlePut (Omega): over the title picture the script shows in Tex2d slot 0, the menu's
// own art from TitleSet's two sheets - title1 (slot 0x11) and chaos (0x12), group-1 pictures
// (kOutReadPicture): on the iPad chaos's 6 (the logo) at (0, -32), title1's 9 at (184, 252) and
// 10 at (0, 312); on the iPhone 6 at (0, 0), 7 at (184, 220), 8 at (0, 280) - then three plates
// (ComUty_ChoicePlateDraw) at (328, 114 + 56k): Start, Scenarios, Options (words 2, 32, 3,
// gnPlateNameTbl), and down the left pictures 125 / 126 at (8, 16) / (8, 96) (127 at (8, 176)
// only with Game Center)
constexpr int kTitleWords[3] = {2, 32, 3};
constexpr int kTitleIcons[3] = {2, 27, 3};

bool titleChoiceReady(int) { return true; }

// The title's plates: Start, Scenarios, Options at 114 + 56 i (TitlePut) - and on the desktop a fourth, Quit
// (ours, #16), the four 48 apart from 96 to keep clear of the copyright line
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
constexpr int kTitlePlates = 3;
#else
constexpr int kTitlePlates = 4;
#endif
float titlePlateY(int i) { return kTitlePlates == 4 ? 96 + 48.0f * float(i) : 114 + 56.0f * float(i); }

// A group-1 picture (MuTex_GetTexInfo(1, pict), half-scale units) of one of a menu's own sheets
// at (x, y): the _hd / _ipad sheets (1024 wide and up) hold them at twice those units
// (`frame`: ComUty_PictAnimDraw's, the next piece to the right)
void outPicture(cr::MenuPainter& m, const cr::Texture& tex, int pict, float x, float y, float a, int frame) {
    if (!tex.id || pict < 0 || pict >= cr::kNumOutReadPictures) return;
    const short* p = cr::kOutReadPicture[pict];
    const float k = sheetScale(tex);
    m.imagePieceTo(tex, x, y, p[2], p[3], float(p[0] + p[2] * frame) * k, p[1] * k, p[2] * k, p[3] * k, a);
}

void drawTitleMenu(App& app) {
    cr::MenuPainter& m = app.menu;
    static const char* const kTitleSheet[2] = {"title1.mct", "chaos.mct"};
    for (int k = 0; k < 2; ++k)
        if (!app.titleArt[k].id) {
            cr::Image img;
            const int ipadBefore = app.ipadImages;
            if (loadImage(app, kTitleSheet[k], img)) app.titleArt[k] = app.renderer.upload(img);
            if (k == 0) app.titleArtIpad = app.ipadImages > ipadBefore;
        }
    m.begin(app.renderer, app.windowTex, &app.font, app.cleanTextOutline);
    const PhoneMenu phoneMenu(app, m); // the iPhone layout: its screen's menu coordinates
    // TitlePrc: the title's art alone (windows 0 / 1: the logo and the name, the strip below) until
    // a tap (state 1); then the logo and the name go (windows 0 and 2 off), the strip stays, and
    // the plates with the store's pictures slide in from x +240 (window 3, state 7)
    const float t = std::min(1.0f, app.titleAnim / 10.0f), ease = 1 - (1 - t) * (1 - t);
    const bool plates = app.titleStage == 1, board = app.titleStage == 2;
    const float logo = plates || board ? std::max(0.0f, 1 - app.titleAnim / 10.0f) : t; // (the logo fading out as the plates come)
    // (the pieces by the sheet loaded - the iPad's 9 / 10 only from title1_ipad: with data that has
    // none, the Vita's, the iPad layout gets title1_hd, cut as the iPhone's 7 / 8 - the places by layout)
    const int name = app.titleArtIpad ? 9 : 7, strip = app.titleArtIpad ? 10 : 8;
    const bool vita = app.renderer.vitaScreen();
    if (vita) {
        // The Vita's __menuu_TitlePut (its table at 0x81123108): the name 7 at (184, 220), the chaos logo 6 at
        // (0, 24), the strip 8 at (0, 264) - title1 / chaos read by _MuCom_MctTexRead, its pictures kind 3
        // (FUN_81015be8: full size, the pictures' layer)
        const int keep = app.renderer.layer();
        app.renderer.setLayer(3);
        outPicture(m, app.titleArt[0], name, 184, 220, logo);
        outPicture(m, app.titleArt[1], 6, 0, 24, logo);
        outPicture(m, app.titleArt[0], strip, 0, 264, plates || board ? 1.0f : t);
        app.renderer.setLayer(keep);
    } else if (app.renderer.phoneLayout()) {
        outPicture(m, app.titleArt[0], name, 184, 220, logo);
        outPicture(m, app.titleArt[1], 6, 0, 0, logo);
        outPicture(m, app.titleArt[0], strip, 0, 280, plates || board ? 1.0f : t);
    } else {
        outPicture(m, app.titleArt[0], name, 184, 252, logo);
        outPicture(m, app.titleArt[1], 6, 0, -32, logo);
        outPicture(m, app.titleArt[0], strip, 0, 312, plates || board ? 1.0f : t);
    }
    if (board) { // the version-up board (ComUty_MsgBoardDraw at y 220, window 8 sliding up 0xa0; its mark 4 ticks a frame)
        const App::TitleBoard& b = app.titleBoard;
        drawMsgBoard(app, m, 220 + 160 * (1 - ease), pictAnimFrame(app.titleAnim, 4, 4), t, t >= 1, {b.lines[0], b.lines[1], b.lines[2]});
    }
    if (plates) {
        // Start with a resume point (TitlePrc 8 -> 0x16): window 3 goes back out to +240, the strip
        // (window 1) stays under the Progress Tracker's windows
        const float q = std::min(1.0f, float(app.titleOut) / 4.0f), dx = 240 * (1 - ease) + 240 * q * q, pa = t * (1 - q);
        // (the Vita: the plates at 0x173 = 371 - vitaRight)
        app.renderer.setOrigin(vitaRight(app)); // (widescreen: the plates to the screen's right)
        for (int i = 0; i < kTitlePlates && !app.quitAsk; ++i) { // (they go while Quit asks, as Options' rows do)
            const bool lit = (app.titleCursor == i && !app.pointerLast) || pressIn(app, kPressTitle, vitaRight(app), 328, titlePlateY(i), 136, 40);
            if (i == 3) { // (the sheet's x; "Exit" in its lettering, else our word)
                const bool english = !app.vita.isOpen() && !(app.fanText && app.fan.isOpen()) && uitext::language() == "en";
                m.plate(328 + dx, titlePlateY(i), 1, -1, lit, pa, false, english ? std::string() : std::string(tr("Quit")));
                if (english) drawExitWord(app, m, 328 + dx + 40, titlePlateY(i) + 8, pa);
            } else m.plate(328 + dx, titlePlateY(i), kTitleIcons[i], kTitleWords[i], lit, pa, !titleChoiceReady(i));
        } // (lit: the keys' cursor, or a touch holding it - TitlePrc's DAT_000ecb60)
        app.renderer.setOrigin(wideLeft(app)); // (... and the pictures down the left to its left)
        if (!vita) { // (the Vita's TitlePut draws none of them)
            m.picture(125, 8 + dx, 16, pa);
            m.picture(126, 8 + dx, 96, pa);
            m.picture(127, 8 + dx, 176, pa); // (3.1.0: with Game Center - its achievements; Android: always - its list; ours: the list)
        }
    }
    if (keyHints(app) && !app.resumeNotice && !app.resumePrompt && !app.quitAsk) // (our key hints)
        m.text("Up/Down  Enter  V: Options  A: Achievements  Esc: quit", 20, 360, false, t * 0.8f, 9);
    if (app.quitAsk) drawYesNoBoard(app, {std::string(tr("Quit the game?"))}, app.quitConfirm); // (Quit's question, as Options' Quit Game asks)
    app.renderer.setOrigin(app.renderer.uiX());
}

bool drawExitWord(const App& app, cr::MenuPainter& m, float x, float y, float alpha) {
    if (app.vita.isOpen() || (app.fanText && app.fan.isOpen()) || uitext::language() != "en") return false;
    x += 44 - 18.25f; // (36.5 wide, centred on the word's 88)
    m.piece(x, y, 196, 584, 24.5f, 24, alpha);        // "Ex" (Extra, word 17)
    m.piece(x + 24.5f, y, 216, 560, 5, 24, alpha);    // "i" (Vieg, word 12)
    m.piece(x + 29.5f, y, 220.5f, 584, 7, 24, alpha); // "t" (Extra)
    return true;
}

// Quit's question answered: Yes - the app closes, as its window's close button does; No - the plates again
static void titleQuitAnswer(App& app, bool yes) {
    app.quitAsk = 0;
    menuSound(app, se::kChoose);
    if (!yes) return;
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

// Start: the boot script goes on (flag 0 clear) to Character Select (Util 4), then the pair's save /
// load screen (Util 5)
void titleStart(App& app) {
    if (app.booting && app.runner.running()) {
        app.game.setFlag(0, false);
        app.titleOpen = false;
    } else {
        openSaveScreen(app, true);
    }
}

// TitlePrc state 1: a tap anywhere - SE 0x91, the logo off, the plates in
// __Title_ChangeMsg: the board's lines (font block 6: 0x86.., here 57000 + n)
void titleBoardMsg(App& app, int mode, int i) {
    App::TitleBoard& b = app.titleBoard;
    b.lines[0] = b.lines[1] = b.lines[2] = 0;
    switch (mode) {
    case 0: b.lines[0] = 57134, b.lines[1] = 57135; break;             // New fun stuff available / after clearing the game!
    case 1: b.lines[0] = 57140; break;                                 // Voices added!
    case 2: b.lines[0] = 57141, b.lines[1] = 57145 + i, b.lines[2] = 57142; break; // You can attempt / 'Breaking the Limit N' / after the extra content.
    default: b.lines[0] = 57143, b.lines[1] = 57144; break;            // Check out the Info Board next to Piu-Piu...
    }
    app.titleAnim = 0; // (the board slides up anew)
}

// TitlePrc state 3: what is new since the last visit, in the one save's flags (TitleClr keeps
// 0x65..0x6a, 0x6f..0x74 and 0x76..0x7b across its Load_SaveData; the later releases open the
// trials 0x65..0x6a every frame, so each is announced once). True: a board to show (titleStage 2)
bool titleAnnounce(App& app) {
    if (app.verUpShown) return false;
    cr::SaveData d;
    const bool haveData = readData(app, d);
    cr::GameState& g = haveData ? d.game : app.game;
    if (!haveData) // (no save to keep the boards' marks: the ones seen before)
        for (int f : app.seenFlags) g.setFlag(f, true);
    const std::vector<uint32_t> before = g.flags;
    for (int f = 101; f <= 106; ++f) g.setFlag(f, true); // (3.1.0's _gameapp_prc)
    int first = -1, count = 0;
    // (3.1.0 counts a pack while 0x6f + i is clear - and an uncleared game never sets it, so "New fun
    // stuff available after clearing the game!" came back at every launch. The owner's choice: once -
    // before the clear, a pack announced (0x76 + i) is not counted again)
    for (int i = 0; i < 6; ++i)
        if (g.flag(0x65 + i) && !g.flag(0x6f + i) && (g.flag(0x1f) || !g.flag(0x76 + i))) {
            if (g.flag(0x1f)) g.setFlag(0x6f + i, true);
            g.setFlag(0x76 + i, true);
            if (first < 0) first = i;
            ++count;
        }
    if (g.flag(0x1f)) {
        if (count == 0)
            for (int i = 0; i < 6; ++i)
                if (g.flag(0x6f + i) != g.flag(0x76 + i)) {
                    g.setFlag(0x6f + i, true), g.setFlag(0x76 + i, true);
                    if (first < 0) first = i;
                    ++count;
                }
    } else
        for (int i = 0; i < 6; ++i)
            if (g.flag(0x65 + i) && !g.flag(0x76 + i)) g.setFlag(0x76 + i, true);
    bool voices = false;
    if (g.flag(0x6b) && !g.flag(0x75)) g.setFlag(0x75, true), voices = true;
    if (haveData && g.flags != before) cr::writeSave(dataPath(app), d); // (the marks go with the save)
    else if (!haveData && g.flags != before) { // (or with the settings, until there is one)
        for (int f = 0x6f; f <= 0x7b; ++f)
            if (g.flag(f)) markSeen(app, f);
    }
    if (!count && !voices) return false;
    app.verUpShown = true;
    App::TitleBoard& b = app.titleBoard;
    b = App::TitleBoard();
    b.state = 4, b.first = first, b.count = count, b.voices = voices, b.cleared = g.flag(0x1f);
    if (!b.cleared) titleBoardMsg(app, 0, 0), b.count = 0;
    else if (count < 1) titleBoardMsg(app, 1, 0), b.voices = false;
    else titleBoardMsg(app, 2, first);
    return true;
}

// __Title_VerUpProc: a tap anywhere (SE 0x93) turns the board's page (TitlePrc 4 / 5), then the plates
void titleBoardTap(App& app) {
    if (app.titleStage != 2) return;
    menuSound(app, se::kChoose);
    App::TitleBoard& b = app.titleBoard;
    auto done = [&] { app.titleStage = 1, app.titleAnim = 0; };
    if (b.state == 4) {
        if (--b.count < 1) {
            if (!b.cleared) done();
            else titleBoardMsg(app, 3, 0), b.state = 5;
        } else titleBoardMsg(app, 2, ++b.first);
    } else if (b.voices) b.voices = false, titleBoardMsg(app, 1, 0);
    else done();
}

void titleTap(App& app) {
    if (app.titleStage != 0) return;
    app.sound.play(0x91);
    app.titleAnim = 0;
    // (the Vita's TitlePrc 3 marks what is new - its flags as 3.1.0's - and turns window 8 on, but goes on to
    // the plates (state 7) at once, and its TitlePut never draws window 8: no version-up board)
    const bool board = titleAnnounce(app);
    app.titleStage = board && !app.renderer.vitaScreen() ? 2 : 1;
}

void titleChoose(App& app, int i) {
    app.titleCursor = i;
    if (!titleChoiceReady(i)) { menuSound(app, se::kBuzzer); return; }
    menuSound(app, se::kChoose);
    if (i == 3) { app.quitAsk = 1, app.quitConfirm = 1; return; } // Quit (ours, the desktop's: #16): asked first
    if (i == 2) { openOptions(app, 0); return; } // SetOptionMenu + ModeOptionMenu(1)
    if (i == 1) { openScenarios(app, 0); return; }  // ModeSccnariosMenu(0), SetSccnariosMenu
    // Start (TitlePrc 8 -> 0x16): the Progress Tracker's offer first, when there is a resume point
    offerResume(app);
    if (app.resumePrompt || app.resumeNotice) return;
    titleStart(app);
}

// Keys on the title; false = quit
bool titleKey(App& app, SDL_Keycode key) {
    if (app.resumeNotice) { // "About Progress Tracker": a page out, or the next
        if (app.resumeNotice == 1 && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E)) tapDialogue(app);
        return true;
    }
    if (app.quitAsk) { // Quit's question: Yes / No
        switch (key) {
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT: case SDLK_W: case SDLK_S: case SDLK_A: case SDLK_D:
            app.quitConfirm ^= 1, menuSound(app, se::kCursor);
            break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E: titleQuitAnswer(app, app.quitConfirm == 0); break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: titleQuitAnswer(app, false); break;
        default: break;
        }
        return true;
    }
    if (app.titleStage == 0 && key != SDLK_ESCAPE && key != SDLK_AC_BACK) { titleTap(app); return true; }
    if (app.titleStage == 2) { // a board: any key but Escape turns its page
        if (key != SDLK_ESCAPE && key != SDLK_AC_BACK) titleBoardTap(app);
        return true;
    }
    switch (key) {
    case SDLK_A: menuSound(app, se::kChoose); openAchievementList(app); break; // (our key for the picture at (8, 176))
    case SDLK_UP: case SDLK_W: app.titleCursor = (app.titleCursor + kTitlePlates - 1) % kTitlePlates; menuSound(app, se::kCursor); break;
    case SDLK_DOWN: case SDLK_S: app.titleCursor = (app.titleCursor + 1) % kTitlePlates; menuSound(app, se::kCursor); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E: titleChoose(app, app.titleCursor); break;
    case SDLK_ESCAPE: case SDLK_AC_BACK: return false;
    default: break;
    }
    return true;
}

void titleClick(App& app, float px, float py) {
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    if (app.resumeNotice) { if (app.resumeNotice == 1) tapDialogue(app); return; } // (the message window: a tap anywhere)
    if (app.titleStage == 0) { titleTap(app); return; } // (a tap anywhere)
    if (app.titleStage == 2) { titleBoardTap(app); return; } // (a tap anywhere)
    float mx, my;
    if (app.quitAsk) { // Quit's question: the Yes / No plates (0x88 x 0x28)
        menuPointAt(app, lx, ly, app.renderer.uiX(), mx, my);
        if (mx >= 336 && mx < 472 && my >= 104 && my < 144) titleQuitAnswer(app, true);
        else if (mx >= 336 && mx < 472 && my >= 160 && my < 200) titleQuitAnswer(app, false);
        return;
    }
    menuPointAt(app, lx, ly, vitaRight(app), mx, my); // (widescreen: the plates on the right; the Vita's at 371)
    for (int i = 0; i < kTitlePlates; ++i)
        if (mx >= 328 && mx < 464 && my >= titlePlateY(i) && my < titlePlateY(i) + 40) titleChoose(app, i);
    if (app.renderer.vitaScreen()) return; // (no pictures down the left on the Vita: its TitlePrc 8 has no boxes there)
    float sx, sy;
    menuPointAt(app, lx, ly, wideLeft(app), sx, sy); // the store's pictures (8, 16) / (8, 96) 80 x 72: their sound, no store here
    if (sx >= 8 && sx < 88 && ((sy >= 16 && sy < 88) || (sy >= 96 && sy < 168))) menuSound(app, se::kChoose);
    // (8, 176): __menuu_TitlePrc 8's third box (3.1.0: SE 0x93, Game Center's achievements - _showa;
    // Android: the Store menu in mode 4, its achievement list)
    if (sx >= 8 && sx < 88 && sy >= 176 && sy < 248) { menuSound(app, se::kChoose); openAchievementList(app); }
}

// ---- Achievements: Android's banner (achievement_prc) and list (the Store menu in mode 4) ----
std::string achievementName(const App& app, int n) {
    return sysLabel(app, cr::achievements::nameId(n), cr::achievements::fallbackName(n));
}

// achievement_prc's drawing, in tex2d units of the phone's 480 x 320 (at (16, 32) of the menus' 512 x
// 384 in every layout): tex2d_add4 slot 0x31 black from x -e to 480 + e - Android's phone edges, here
// the visible screen's - in three bands, y 108..116 fading in, 116..204 at the alpha, 204..212 fading
// out; while held, drawfont_center "Achievement" (0x24, block 10's 0x101) white at (240, 136) and the
// name (0x25, (n + 0x7f) x 2) yellow (0xff00ffff) at (240, 168), 16 high, edged as drawfont is
void drawAchievementBanner(App& app) {
    const cr::achievements::Banner& b = app.ach.banner;
    if (!b.on) return;
    cr::Renderer& r = app.renderer;
    const float keep = r.origin();
    const int layer = r.layer();
    r.setLayer(0);
    r.setOrigin(0);
    const float a = float(b.alphaByte()) / 255.0f;
    const float L = r.phoneLayout() ? r.screenW() * 0.5f - r.screenW() * 0.5f / r.layoutScale() : 0.0f;
    const float R = r.phoneLayout() ? r.layoutRight() : r.screenW();
    auto Y = [](float ty) { return 2 * (32 + ty); };
    auto fade = [&](float y0, float a0, float a1) { // 8 units: the vertex colours' blend, a unit a strip
        for (int i = 0; i < 8; ++i) r.fillRect(L, Y(y0 + float(i)), R - L, 2, 0, 0, 0, a0 + (a1 - a0) * (float(i) + 0.5f) / 8);
    };
    fade(108, 0, a);
    r.fillRect(L, Y(116), R - L, Y(204) - Y(116), 0, 0, 0, a);
    fade(204, a, 0);
    if (b.lines() && app.font.loaded()) {
        cr::MenuPainter& m = app.menu;
        m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
        r.setOrigin(r.uiX());
        const PhoneMenu phoneMenu(app, m);
        const std::string head = sysLabel(app, cr::achievements::headerId(), "Achievement"), name = achievementName(app, b.id);
        m.text(head, 240 - m.textWidth(head, 16) / 2, 136, false, 1, 16);
        m.textTint(name, 240 - m.textWidth(name, 16) / 2, 168, 1, 1, 0, 1, 16);
    }
    r.setOrigin(keep);
    r.setLayer(layer);
}

// The list (InitStoreMenu's functions with SetStoreMode(4): Set 0x82328, Prc 0x81d94, Put 0x823e8):
// the achievements in order - earned ones with their description (block 10's 0x160 + n), the others
// with a hint (0x140 + n) in grey, the story's (the table at 0xb9194) left out until earned. Each row
// (ComUty_ComPlate_GameCenter 0x7a7ec) 435 x 64: the focus row teal (0x00d7a2, opaque at the left to
// 0x33 at the right), a white line under it, picture n - 1 of achievement.mct (57 x 57, eight a row) at
// (24, 4), the name yellow at (88, 12), the line at (88, 36). Put: a white band at alpha 0x80 over
// (0, -2) 480 x 29 with "Achievement" centred at y 6. Rows from y 28 (Part 15 3857.5 s), scrolled by a
// finger until the last is on top (3864.5 s); the focus is the row over y 60 (3867 / 3876 s)
namespace achui {
constexpr float kTop = 28, kRowH = 64, kBottom = 284, kRowW = 435;
}

void openAchievementList(App& app) {
    App::Achievements& a = app.ach;
    a.listOpen = true;
    a.listAnim = 0, a.listCursor = 0, a.listScroll = 0, a.listClock = 0, a.listDrag = false;
    a.listRows.clear();
    for (int n = 1; n <= cr::achievements::kCount; ++n)
        if (a.flags.has(n) || !cr::achievements::secret(n)) a.listRows.push_back(n);
    if (!a.iconsTried) { // (Android's art: the iOS data has none - the rows then go without their pictures)
        a.iconsTried = true;
        cr::Image img;
        if (loadImage(app, "achievement.mct", img)) a.icons = app.renderer.upload(img);
    }
}

void closeAchievementList(App& app) {
    app.ach.listOpen = false;
    app.ach.listDrag = false;
}

float achListMaxScroll(const App& app) { return std::max(0.0f, float(int(app.ach.listRows.size()) - 1) * achui::kRowH); }
int achListFocus(const App& app) {
    const int n = int(app.ach.listRows.size());
    return n ? std::clamp(int((app.ach.listScroll + 32) / achui::kRowH), 0, n - 1) : -1;
}

void stepAchievementList(App& app) {
    App::Achievements& a = app.ach;
    ++a.listAnim;
    if (!a.listDrag) { // (keys: the cursor's row eases to the top, 16 a frame)
        const float want = std::clamp(float(a.listCursor) * achui::kRowH, 0.0f, achListMaxScroll(app));
        if (std::abs(want - a.listScroll) <= 16) a.listScroll = want;
        else a.listScroll += want > a.listScroll ? 16 : -16;
    }
}

void drawAchievementList(App& app) {
    using namespace achui;
    App::Achievements& a = app.ach;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    r.setOrigin(r.uiX());
    const PhoneMenu phoneMenu(app, m);
    drawMenuMaskShape(app, menuMaskAlpha(app)); // (Android: the title's picture under the mask)
    r.setOrigin(r.uiX());
    const float t = std::min(1.0f, float(a.listAnim + 1) / 6.0f); // (the windows coming on)
    const float white[4] = {1, 1, 1, 0.5f * t};
    m.quad(0, -2, 480, 29, white, white);
    const std::string head = sysLabel(app, cr::achievements::headerId(), "Achievement");
    // (MuComFontDisp at ((480 - its width) / 2, 6), its style 1: black edged white - Part 15 3857.5 s)
    m.text(head, 240 - m.textWidth(head, 14) / 2, 6, true, t, 14);
    const int focus = achListFocus(app);
    m.setClip(kTop, kBottom);
    for (size_t k = 0; k < a.listRows.size(); ++k) {
        const float y = kTop + kRowH * float(k) - a.listScroll;
        if (y + kRowH <= kTop || y >= kBottom) continue;
        const int n = a.listRows[k];
        const bool earned = a.flags.has(n);
        if (int(k) == focus) {
            const float l[4] = {0, 0xd7 / 255.0f, 0xa2 / 255.0f, t}, rr[4] = {0, 0xd7 / 255.0f, 0xa2 / 255.0f, 0x33 / 255.0f * t};
            m.quad(0, y, kRowW, kRowH, l, rr);
        }
        const float line[4] = {1, 1, 1, t};
        m.quad(0, y + 63, kRowW, 1, line, line);
        if (a.icons.id) {
            const float k2 = float(a.icons.width) / 512.0f;
            m.imagePieceTo(a.icons, 24, y + 4, 57, 57, float((n - 1) % 8) * 57 * k2, float((n - 1) / 8) * 57 * k2, 57 * k2, 57 * k2, t);
        }
        m.textTint(achievementName(app, n), 88, y + 12, 1, 1, 0, t, 14);
        const std::string line2 = sysText(app, earned ? cr::achievements::descriptionId(n) : cr::achievements::hintId(n));
        // (the line's font is smaller than the name's: 9.5 here gives Part 15 3857.5 s's width, "Defeated
        // Progenitor's Tooth." 246 px of 1280 - the name at the menu font's 13 measures as the video's)
        if (earned) m.text(line2, 88, y + 36, false, t, 9.5f);
        else m.textTint(line2, 88, y + 36, 0x80 / 255.0f, 0x80 / 255.0f, 0x80 / 255.0f, t, 9.5f);
    }
    m.clearClip();
    // MuScrList_Draw's bar beside the rows (ComUty_GuageBarDraw's pieces; Part 15 3857.5 s: dark from
    // x 1080 to 1111, y 98 to 609 of 1280 x 720 - x 460, y 34, 244 long)
    // (the knob as ComUty_GuageBarDraw's: 3876 s, scrolled 683.5, its top at 280; at the end 544 - as here)
    const int n = int(a.listRows.size());
    if (n > 1) drawScrGauge(m, 460, 34, 244, n, -a.listScroll, kRowH, 0, t);
    m.backBar(192, 288, pressIn(app, kPressCollection, app.renderer.uiX(), 192, 286, 256, 34), t); // (lit while held: DAT_000ed638)
    if (keyHints(app)) m.text("Up/Down  Esc: back", 8, 300, false, t * 0.8f, 9); // (our key hints)
    r.setOrigin(r.uiX());
}

void achievementListKey(App& app, SDL_Keycode key) {
    App::Achievements& a = app.ach;
    const int n = int(a.listRows.size());
    switch (key) {
    case SDLK_UP: case SDLK_W:
        if (a.listCursor > 0) --a.listCursor, menuSound(app, se::kCursor);
        break;
    case SDLK_DOWN: case SDLK_S:
        if (a.listCursor + 1 < n) ++a.listCursor, menuSound(app, se::kCursor);
        break;
    case SDLK_PAGEUP: a.listCursor = std::max(0, a.listCursor - 4), menuSound(app, se::kPage); break;
    case SDLK_PAGEDOWN: a.listCursor = std::max(0, std::min(n - 1, a.listCursor + 4)), menuSound(app, se::kPage); break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E:
        menuSound(app, se::kBack);
        closeAchievementList(app);
        break;
    default: break;
    }
}

// A press at (mx, my) (the phone's tex2d units): the Back bar closes it; on the rows, a drag scrolls them
void achievementListDown(App& app, float mx, float my) {
    App::Achievements& a = app.ach;
    if (mx >= 192 && mx < 448 && my >= 286 && my < 320) { achievementListKey(app, SDLK_ESCAPE); return; }
    if (my >= achui::kTop && my < achui::kBottom) a.listDrag = true, a.listDragY = my, a.listDragScroll = a.listScroll;
}
void achievementListDrag(App& app, float my) {
    App::Achievements& a = app.ach;
    if (!a.listDrag) return;
    a.listScroll = std::clamp(a.listDragScroll - (my - a.listDragY), 0.0f, achListMaxScroll(app));
    a.listCursor = achListFocus(app);
}
void achievementListScrollBy(App& app, int rows) {
    App::Achievements& a = app.ach;
    a.listDrag = false;
    a.listCursor = std::clamp(achListFocus(app) + rows, 0, std::max(0, int(a.listRows.size()) - 1));
}

void startNewGame(App& app, int scenario) {
    app.game = cr::GameState();
    app.playSeconds = 0;
    app.game.setFlag(4089, true); // (CR1's new-game flag; the boot script's own)
    if (scenario) app.game.setFlag(31, true); // (the second page open)
    cr::giveStartingItems(app.game, app.itemTable); // prty_init
    newPairParty(app, scenario ? 2 : 1, false);     // Uty_LoadPrty(1, 2)
    app.treasureBoxes.clear();
    app.entryArea = -1;
    const int area = scenario ? 10 : 6, map = scenario ? 214 : 69;
    const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[map]));
    if (it != app.maps.end()) showMap(app, int(it - app.maps.begin()), map, area);
}

// The title, as the game boots: area 0 (j001_t99) and its EvrStart 0 - the logos, the title
// picture and music, then the title menu. Also "Return to Title".
// __menuu_TitleClr's Load_SaveData: the title goes back to the saved block, and the play time is in
// it - the one save the game keeps for all four pairs; here the newest of their slots (none:
// prty_init_all's 0). A pair's new game (prty_init_new) leaves it, so it counts on across pairs
double savedPlaySeconds(const App& app) {
    double seconds = 0;
    bool any = false;
    int64_t newest = 0;
    for (int pair = 1; pair <= 4; ++pair) {
        cr::SaveData d;
        if (cr::readSave(slotPath(app, pair), d) && (!any || d.savedAt >= newest)) any = true, newest = d.savedAt, seconds = d.playSeconds;
    }
    return seconds;
}

void bootTitle(App& app) {
    endFieldTasks(app); // stop puzzle/reset/actor timers before releasing their NPCs
    app.trackerArmed = false; // (SaveData_Load_init: a load or a new game starts it again)
    app.playSeconds = savedPlaySeconds(app);
    endSceneEffects(app); // MuCom_PreReturnTitle: ScnEffectEnd
    app.renderer.resetHistory(); // a cut: the upscaler starts over
    app.runner.stop();
    app.sound.stopAll(); // fldread_prc: returning to the title also ends looping effects
    app.sound.stopDialogueVoices();
    app.voiceCue = 0;
    app.fadeGrey = app.fadeFrom = app.fadeTo = 0;
    app.fadeT = app.fadeFrames = 0;
    app.crystalCam = App::CrystalCam();
    unloadModel(app);
    if (app.battle.active) endBattle(app, false);
    app.runner.stop();
    // Discard the previous session's UI without executing pending confirmations.
    // In particular, a save prompt hidden behind Options must never survive this cut.
    app.campOpen = app.manualOpen = app.scc.open = app.charSelectOpen = false;
    app.itemWindowOpen = app.optionsOpen = app.optionsFromTitle = false;
    app.resetAsk = 0;
    app.saveScreen = App::SaveScreen();
    app.resumePrompt = app.resumeCursor = app.resumeNotice = app.resumeIn = app.titleOut = app.quitAsk = 0;
    app.resumeWin = 0;
    app.saveImport.cancel();
    app.parkedRunner.stop();
    app.renderer.destroy(app.shop.keeper);
    app.shop = App::Shop();
    app.renderer.destroy(app.mapScreen.tex);
    app.mapScreen = App::MapScreen();
    app.renderer.destroy(app.staffRoll.tex);
    app.staffRoll = App::StaffRoll();
    app.renderer.destroy(app.lastBattle.tex);
    app.lastBattle = App::LastBattle();
    app.fieldMenu = App::FieldMenu();
    app.touchPad = App::TouchPad();
    app.menuMask = App::MenuMask();
    app.campPointer = App::CampPointer();
    app.dragging = app.optionsBarDrag = app.tapPending = false;
    clearSkip(app);
    app.padHeld = -1;
    app.padHeldFor = 0;
    app.padHeldOver.cancel();
    app.pointerCancelled = true;
    app.heldPage = false;
    app.lastPage.clear();
    app.dialogueVoices.clear();
    resetTyping(app);
    app.playing = true;
    app.lastTicks = 0;
    app.collection.open = false;
    app.startPage.holding = false;
    app.haveStageCam = false;
    app.havePlayer = false;
    app.npcs.clear();
    app.npcDraws.clear();
    app.npcHeld.clear();
    app.dialogue.clear();
    app.dialogueInfo.clear();
    app.choiceFlag = -1;
    app.worldChoiceOpen = false;
    app.optionsOpen = false;
    for (App::Portrait& p : app.portraits) {
        app.renderer.destroy(p.tex);
        p = App::Portrait();
    }
    app.game = cr::GameState();
    cr::giveStartingItems(app.game, app.itemTable); // gameapp_init: prty_init (a load replaces it)
    app.treasureBoxes.clear();
    app.mode = Mode::Maps;
    app.showBackground = true;
    app.area = 0;
    app.mapNumber = cr::kAreaFirstMap[0];
    app.entryArea = 0;
    app.pendingEntry = App::Entry::None;
    app.musicArea = -1;
    app.fieldHidden = false; // (the boot script toggles it on at its start and off before the warp out)
    app.msgWin = App::MsgWin();
    playBgm(app, 1u << 29); // the script starts the title music
    std::string err;
    if (!app.events.load(app.dataDir, 0, &err)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "title: %s", err.c_str());
        return;
    }
    app.booting = true;
    app.bootClock = 0;
    app.titleOpen = false;
    if (const uint32_t at = app.events.returnEntry(0))
        if (app.runner.startAt(app.events, at, 0, app.game, 0, app.mapNumber)) app.runnerWhat = "title";
    updateTitle(app);
}

} // namespace crapp
