#pragma once
// Save data (ours, not the original's binary layout): what the original's SaveData_Save /
// ContData_Save keep - the scripts' flags, variables, items and opened boxes, where the party
// stands and who leads it, the play time and the date - as a small text file:
//
//   CRSAVE 1
//   area 2 / map 0 / pos x y z / yaw r / leader 0 / play 1234.5 / saved 1760000000 (unix time)
//   flags <128 hex words>
//   vars n=v n=v ...   items n=c ...   oz n   party c level exp hp mp weapon armor jewel (x8)   boxes id ...
//
// One file for the whole game, as the original's data.dat (_prty: both scenarios' records, bags,
// Oz, flags, variables and each scenario's own saved place and "has a save" mark, +0x14a4 + 0x14 s
// and +0x1546 + s): data.dat, written by the Camp's save and the scripts' Util 5 / 6. The lines
// above describe the scenario in play; a `place s has area map x y z yaw leader play saved` line
// per scenario keeps where each one was saved. The Progress Tracker's resume point (resume.dat,
// written as you go on the field) is a whole copy of it, as cont.dat is. Older saves - one per
// pair, save1.dat .. save4.dat - are folded into data.dat once (main.cpp readData).

#include "game/event_vm.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace cr {

struct SaveData {
    int area = -1, map = -1;
    float pos[3] = {0, 0, 0};
    float yaw = 0;
    int leader = 0;          // party leader model (f001.bin + n)
    double playSeconds = 0;
    int64_t savedAt = 0;     // unix time
    GameState game;          // flags, vars, items, Oz, party, opened boxes (not the cutscene frame state)
    // Each scenario's saved place (SaveData_Load_init restores the chosen one's); a file without
    // `place` lines (older, or a resume point) has the scenario in play's from the lines above
    struct Place {
        bool has = false;    // +0x1546 + s: this scenario was saved (Load offered)
        int area = -1, map = -1;
        float pos[3] = {0, 0, 0};
        float yaw = 0;
        int leader = 0;
        double playSeconds = 0;
        int64_t savedAt = 0;
    };
    std::array<Place, kScenarios> places{};
    // The place of the scenario in play, from the lines above
    Place placeInPlay() const { return Place{true, area, map, {pos[0], pos[1], pos[2]}, yaw, leader, playSeconds, savedAt}; }
    void setPlace(int scenario, const Place& p) { if (scenario >= 0 && scenario < kScenarios) places[size_t(scenario)] = p; }
    // The file as scenario `s` would be loaded: its place on top (nothing else changes here)
    void takePlace(int s) {
        if (s < 0 || s >= kScenarios || !places[size_t(s)].has) return;
        const Place& p = places[size_t(s)];
        area = p.area, map = p.map, pos[0] = p.pos[0], pos[1] = p.pos[1], pos[2] = p.pos[2], yaw = p.yaw, leader = p.leader;
        playSeconds = p.playSeconds, savedAt = p.savedAt;
    }
};

bool writeSave(const std::string& path, const SaveData& s);
bool readSave(const std::string& path, SaveData& s, std::string* error = nullptr);

} // namespace cr
