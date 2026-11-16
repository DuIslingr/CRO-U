#include "app/app.hpp"

namespace crapp {

// Shows a field by collision file. `forcedMap` / `forcedArea` select the exact map (and so
// camera and event lists) when arriving by warp; otherwise the best-framing camera is used.
void showMap(App& app, int index, int forcedMap, int forcedArea) {
    app.renderer.resetHistory(); // a cut: the upscaler starts over
    if (app.maps.empty()) return;
    app.booting = false;
    app.titleOpen = false;
    app.renderer.destroy(app.charSelectArt); // (the title's save screen showed it)
    for (cr::Texture& t : app.titleArt) app.renderer.destroy(t); // (the title's own art)
    app.mapIndex = wrap(index, app.maps.size());
    unloadModel(app);
    const std::string& name = app.maps[app.mapIndex];
    std::vector<uint8_t> data;
    cr::Collision col;
    std::string err;
    if (!cr::readFile(app.dataDir + name, data) || !cr::parseCollision(data, col, &err)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", name.c_str(), err.c_str());
        updateTitle(app);
        return;
    }
    // Props for this field: dXXXX.bin next to dXXXX_10.cmp
    const std::string field = name.substr(0, name.find('_'));
    auto loadLayer = [&](const std::string& file, cr::Texture& out) {
        cr::Image img;
        if (loadImage(app, file, img)) out = app.renderer.upload(img);
    };
    // Layer names vary (the game's file list): dXXXX-b00a00, tXXXX_b00a00 (towns, d0512/d0524/
    // d0525) or a plain dXXXX.mct (d0108, d0117, d0208, d0219, d0307, d0320)
    for (const char* suffix : {"-b00a00.mct", "_b00a00.mct", ".mct"})
        if (!app.background.id) loadLayer(field + suffix, app.background);
    for (const char* suffix : {"-f00a00.mct", "_f00a00.mct"})
        if (!app.foreground.id) loadLayer(field + suffix, app.foreground);
    SDL_Log("%s layers: background %dx%d (id %u), foreground %dx%d", field.c_str(), app.background.width,
            app.background.height, app.background.id, app.foreground.width, app.foreground.height);

    app.mapProps.clear(); // (the alpha model: once the stage record is known, below)

    cr::Mesh mesh;
    const float light[3] = {0.37f, 0.86f, 0.35f};
    for (const auto& poly : col.polygons) {
        const float d = poly.normal[0] * light[0] + poly.normal[1] * light[1] + poly.normal[2] * light[2];
        const float shade = 0.55f + 0.45f * std::max(0.0f, d);
        float rgb[3];
        if (poly.isFloor()) {
            const float t = float((poly.attr1 >> 8) & 7) / 4.0f; // surface type tint
            rgb[0] = 0.40f + 0.25f * t; rgb[1] = 0.62f; rgb[2] = 0.42f - 0.15f * t;
        } else {
            rgb[0] = 0.75f; rgb[1] = 0.58f; rgb[2] = 0.40f;
        }
        const uint32_t base = uint32_t(mesh.positions.size() / 3);
        for (int i = 0; i < poly.count; ++i) {
            const float* v = &col.vertices[size_t(poly.index[i]) * 3];
            mesh.positions.insert(mesh.positions.end(), {v[0], v[1], v[2]});
            mesh.uvs.insert(mesh.uvs.end(), {0.0f, 0.0f});
            for (int c = 0; c < 3; ++c) mesh.colors.push_back(uint8_t(std::min(255.0f, rgb[c] * shade * 255.0f)));
            mesh.colors.push_back(255);
        }
        for (int i = 1; i + 1 < poly.count; ++i) mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
    }
    app.collision = app.renderer.upload(mesh);

    // Cameras: a collision file can be shared by several map numbers (p0001_10 serves
    // the 16 puzzle rooms and a dungeon room). Pick the camera that frames it best;
    // M cycles through the others.
    app.stageCams.clear();
    app.stageFit.clear();
    app.stageCamBorrowed = false;
    std::vector<int> candidates = forcedMap >= 0 ? std::vector<int>{forcedMap} : cr::mapNumbersFor(name);
    auto tryCandidates = [&]() {
    for (int mapNo : candidates) {
        cr::StageCamera sc;
        if (!(forcedArea >= 0 && cr::loadStageCamera(app.dataDir, mapNo, sc, nullptr, forcedArea)) &&
            !cr::loadStageCamera(app.dataDir, mapNo, sc))
            continue;
        const float aspect = sc.viewHeight > 0 ? float(sc.viewWidth) / float(sc.viewHeight) : 1.0f;
        const cr::Mat4 vp = cr::Mat4::perspective(sc.fovDegrees * 3.14159265f / 180.0f, aspect, 10.0f, 4000.0f) *
                            cr::Mat4::lookAt({sc.eye[0], sc.eye[1], sc.eye[2]},
                                             {sc.target[0], sc.target[1], sc.target[2]}, {0, 1, 0});
        int inside = 0;
        const size_t nv = col.vertices.size() / 3;
        for (size_t v = 0; v < nv; ++v) {
            const float* q = &col.vertices[v * 3];
            const float* m = vp.m;
            const float cx = m[0] * q[0] + m[4] * q[1] + m[8] * q[2] + m[12];
            const float cy = m[1] * q[0] + m[5] * q[1] + m[9] * q[2] + m[13];
            const float cw = m[3] * q[0] + m[7] * q[1] + m[11] * q[2] + m[15];
            if (cw > 0 && std::fabs(cx / cw) <= 1.05f && std::fabs(cy / cw) <= 1.05f) ++inside;
        }
        app.stageCams.push_back(sc);
        app.stageFit.push_back(nv ? float(inside) / float(nv) : 0.0f);
    }
    };
    tryCandidates();
    if (app.stageCams.empty() && name[0] == 'p') {
        // e.g. p0002/p0004/p0006-8: puzzle-room variants with no record of their own (only those: a
        // scripted 3D room such as Omega's d0550 has no fixed camera, and must not take another room's)
        candidates.clear();
        for (int mapNo = 0; mapNo < cr::kNumMaps; ++mapNo)
            if (name.compare(0, 2, cr::kMapCollisionFile[mapNo], 2) == 0 && name != cr::kMapCollisionFile[mapNo])
                candidates.push_back(mapNo);
        tryCandidates();
        app.stageCamBorrowed = !app.stageCams.empty();
    }
    app.haveStageCam = !app.stageCams.empty();
    app.stageCamIndex = 0;
    for (size_t k = 1; k < app.stageFit.size(); ++k)
        if (app.stageFit[k] > app.stageFit[app.stageCamIndex] + 1e-4f) app.stageCamIndex = int(k);
    if (app.haveStageCam) app.stageCam = app.stageCams[app.stageCamIndex];
    // fldread_prc: the painting and its overlay as the stage record names them (Tex2d's image list) -
    // d0530's are -b00b00 / -b00b01 - in place of the names guessed from the field's
    if (app.haveStageCam && !app.stageCamBorrowed && app.stageCam.background >= 0 && app.stageCam.background < cr::kNumTex2dImages) {
        cr::Texture keep[2] = {app.background, app.foreground};
        app.background = app.foreground = cr::Texture();
        loadLayer(cr::kTex2dImage[app.stageCam.background], app.background);
        if (app.stageCam.foreground >= 0 && app.stageCam.foreground < cr::kNumTex2dImages)
            loadLayer(cr::kTex2dImage[app.stageCam.foreground], app.foreground);
        if (app.background.id) { app.renderer.destroy(keep[0]); app.renderer.destroy(keep[1]); }
        else { app.renderer.destroy(app.foreground); app.background = keep[0], app.foreground = keep[1]; }
    }
    // fldread_prc: the alpha model (the occluders, model slot 0) the stage record's +0x40 names
    // (_stgalphaname) - t0110 / t0113 / t0142 share t0112's, t0131 / t0140 t0101's, d0301 / d0302 and
    // c101_d07's d0601 / 3 / 4 / 5 have none -; a room without a record of its own: the field's namesake
    if (app.haveStageCam && !app.stageCamBorrowed) {
        if (app.stageCam.alphaModel >= 0 && app.stageCam.alphaModel < cr::kNumStageAlphaModels)
            app.mapProps = cr::kStageAlphaModel[app.stageCam.alphaModel];
    } else if (std::find(app.models.begin(), app.models.end(), field + ".bin") != app.models.end()) {
        app.mapProps = field + ".bin";
    }
    if (!app.mapProps.empty()) {
        loadModelFile(app, app.mapProps, app.model, false);
        mdlMemRules(app.model); // (read_mdl_set -> mdl_mdl_init -> read_mdl_mem_mcm)
    }
    if (app.haveStageCam && app.stageCam.scripted) {
        // its own FOV and view, the script's eye and target - until the script sets them, the best
        // framing among the maps sharing its collision's name (as a room with no record borrows)
        cr::StageCamera own = app.stageCam;
        std::vector<cr::StageCamera> mine = app.stageCams;
        std::vector<float> fit = app.stageFit;
        candidates.clear();
        for (int mapNo = 0; mapNo < cr::kNumMaps; ++mapNo)
            if (name.compare(0, 2, cr::kMapCollisionFile[mapNo], 2) == 0 && mapNo != own.mapNumber) candidates.push_back(mapNo);
        app.stageCams.clear(), app.stageFit.clear();
        tryCandidates();
        int best = -1;
        for (size_t k = 0; k < app.stageCams.size(); ++k)
            if (!app.stageCams[k].scripted && (best < 0 || app.stageFit[k] > app.stageFit[size_t(best)] + 1e-4f)) best = int(k);
        if (best >= 0)
            for (int i = 0; i < 3; ++i) own.eye[i] = app.stageCams[size_t(best)].eye[i], own.target[i] = app.stageCams[size_t(best)].target[i];
        app.stageCams = std::move(mine), app.stageFit = std::move(fit);
        app.stageCams[size_t(app.stageCamIndex)] = app.stageCam = own;
    }
    if (SDL_getenv("CR_STAGE_LOG")) // (debugging: which stage camera a room took)
        std::printf("stage camera for %s (map %d area %d): %zu candidates%s, took map %d area %d fov %.1f view %dx%d\n", name.c_str(), forcedMap,
                    forcedArea, app.stageCams.size(), app.stageCamBorrowed ? " (borrowed)" : "", app.haveStageCam ? app.stageCam.mapNumber : -1,
                    app.haveStageCam ? app.stageCam.area : -1, app.stageCam.fovDegrees, app.stageCam.viewWidth, app.stageCam.viewHeight);
    app.collisionPolys = int(col.polygons.size());
    // Frame the collision (props live inside it)
    const auto& b0 = col.boundsMin;
    const auto& b1 = col.boundsMax;
    app.model.center = {(b0[0] + b1[0]) / 2, (b0[1] + b1[1]) / 2, (b0[2] + b1[2]) / 2};
    const cr::Vec3 ext{b1[0] - b0[0], b1[1] - b0[1], b1[2] - b0[2]};
    app.model.radius = std::max(0.5f * std::sqrt(cr::dot(ext, ext)), 0.01f);
    app.collisionData = std::move(col);
    app.mapNumber = app.haveStageCam ? app.stageCam.mapNumber : (candidates.empty() ? -1 : candidates[0]);
    app.area = app.haveStageCam ? app.stageCam.area : cr::areaForMap(app.mapNumber, forcedArea);
    if (forcedMap >= 0) { app.mapNumber = forcedMap; app.area = forcedArea >= 0 ? forcedArea : app.area; }
    enterMap(app);
    app.playerByReadMdl = false; // (fldread_prc's read_mdl_set)
    placePlayer(app);
    updateTitle(app);
}

void showMap(App& app, int index) { showMap(app, index, -1, -1); }

// Kill 0 hides the party leader for a scene; Born 0 brings it back
bool playerShown(const App& app) {
    auto it = app.npcShown.find(0);
    return it == app.npcShown.end() || it->second;
}

// _ChkNpcCreateFlag: the row's show flag (if any) set and its hide flag (if any) clear
bool npcCreateFlags(const App& app, const cr::FieldNpc& n) {
    return (n.flagShow < 0 || app.game.flag(n.flagShow)) && (n.flagHide < 0 || !app.game.flag(n.flagHide));
}

// npc_setup's story actors, killed as a room loads whatever their flags: ids 400..449 (301..349 in
// area 8, c101_d07) - not in the puzzle rooms (area 1)
bool npcStoryActor(const App& app, int id) {
    if (app.area == 1) return false;
    return app.area == 8 ? id >= 301 && id < 350 : id >= 400 && id < 450;
}

// npc_setup (RectWrapper::init, at every field load - a battle's end too, fldread_prc's npc_read_setup
// clearing the records first): each NPC of the room shown (npc ctl +0x8c bit 0 clear) when its create
// flags pass and it isn't a story actor. Only Born / Kill and Uty_TBox change that until the next
// load - a flag set mid-room shows or hides nothing (_ChkNpcDisp reads the bit alone)
void npcSetup(App& app) {
    app.npcShown.clear();
    for (const cr::FieldNpc& n : app.npcs) app.npcShown[n.id] = npcCreateFlags(app, n) && !npcStoryActor(app, n.id);
    app.examineLinger.clear();
    app.flagTalked.clear(); // (FlagWindow::init at the load: every person's state from its flag again)
}

bool npcActive(const App& app, const cr::FieldNpc& n) { // _ChkNpcDisp
    if (auto it = app.npcShown.find(n.id); it != app.npcShown.end()) return it->second;
    return npcCreateFlags(app, n); // (an NPC npc_setup never saw)
}

// After a battle (prc: the field read back with load flags | 0xd - npc_read_setup clears the npc
// records, then npc_setup): every Born / Kill and Uty_TBox of the room undone, the NPCs a script
// read in (NpcMdlRead) gone
void resetFieldNpcs(App& app) {
    std::set<int> room;
    if (app.mapNumber >= 0 && app.area >= 0)
        for (const cr::FieldNpc& n : app.events.npcs(app.mapNumber)) room.insert(n.id);
    npcSetup(app);
    for (const cr::FieldNpc& n : app.npcs)
        if (!room.count(n.id)) app.npcShown[n.id] = false;
    buildNpcDraws(app);
}



// npcActive, but a puzzle room's objects show as the puzzle has them
bool npcVisible(const App& app, size_t index) {
    if (app.puzzle.stage >= 0)
        for (const App::Puzzle::Obj& o : app.puzzle.objs)
            if (o.npc == int(index)) return o.shown;
    return index < app.npcs.size() && npcActive(app, app.npcs[index]);
}

// npcVisible, or a red / blue block put away (npc_kill, then mdl_set_alpha keeps it drawn)
bool npcDrawn(const App& app, size_t index) {
    if (app.puzzle.stage >= 0)
        for (const App::Puzzle::Obj& o : app.puzzle.objs)
            if (o.npc == int(index)) return o.shown || o.ghost;
    return npcVisible(app, index);
}

// Loads the area's event file if needed, the map's NPC list, and the trigger-box overlay.
// _bgmgo: the music command; a play request (no stop / fade bits) is the track Uty_GetBgmNo reports
void playBgm(App& app, uint32_t v) {
    if (!(v & 0x2e000000u)) {
        app.bgmNo = int(v & 0xff);
        if (!app.battle.active && !app.titleOpen && !app.booting) app.lastFieldBgm = int(v & 0xff); // (__bgmgo: on the field, 0x364 & 0x40)
    } else app.bgmNo = 255; // (a stop or a fade: the current-track word goes to -1)
    app.music.command(v);
}

// FieldScene::onTerm -> PuzzleScene::term destroys the old room's puzzle objects;
// npc_read_setup also clears its autoctl records. No task may retain an old NPC index.
void endFieldTasks(App& app) {
    app.moves.clear();
    app.rescales.clear();
    app.puzzle = App::Puzzle();
    app.puzzleBanner = App::PuzzleBanner();
    app.puzzleStartPending = false;
    app.playerAlpha = app.playerAlphaFrom = app.playerAlphaTo = 1;
    app.playerFadeFrames = app.playerFadeLeft = 0;
    app.playerFaded = false; // (fldread_prc reads the leader's model again)
}

void enterMap(App& app) {
    endFieldTasks(app);
    app.npcs.clear();
    app.fieldEffects.clear();
    app.frameEffects.clear();
    app.npcWarps.clear();
    app.renderer.destroy(app.triggerMesh);
    app.lastEvent.clear(); // (lastKick kept: checkEventKick alone clears it, the leader in no box)
    app.npcShown.clear();
    app.examineLinger.clear();
    app.npcJoints.clear();
    app.placedNpcs.clear();
    app.actorSlots.clear();
    app.cam = App::ScriptCam();
    app.crystalCam = App::CrystalCam();
    app.scroll = App::FieldScroll();
    if (app.area < 0 || app.mapNumber < 0) return;
    if (app.events.area() != app.area) {
        std::string err;
        if (!app.events.load(app.dataDir, app.area, &err)) {
            // Once per area: 2.2.0 dropped the two test areas (j001_t91 / j001_d91)
            static std::set<int> reported;
            if (reported.insert(app.area).second) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", err.c_str());
            return;
        }
    }
    app.npcs = app.events.npcs(app.mapNumber);
    app.npcNativesPending = true; // (their rows' natives on the first step: the fire walls' Uty_FireWall_Init)
    { // fldencount_init
        cr::EncounterTable table;
        const bool have = cr::encounterTable(app.events, app.mapNumber, table);
        app.encounters.enterMap(have ? &table : nullptr);
    }
    app.pendingEntry = app.area != app.entryArea ? App::Entry::Area : App::Entry::Room;
    if (app.area != app.entryArea) app.game.frmScene = app.game.frmRecord = 0; // offsets into the old file
    app.entryArea = app.area;
    // Music: none here - _fldread_prc never plays a track on a map or area change; the area's entry
    // script does (its Bgm, by its flags)
    // Treasure boxes (TreasureBoxManager): the first four worlds' (areas 2..5) are drawn again each
    // time the World Select leads into one (fldread_evt_init2 -> _InitDungeon -> randomSelect), the
    // draw kept with the scenario (the save) and its boxes taken there; flags set per map (setup).
    // Elsewhere only fixed boxes (their own items and flags)
    const bool worldBoxes = app.area >= 2 && app.area <= 5;
    const int scen = std::clamp(app.game.scenario, 0, cr::kScenarios - 1);
    auto& pick = app.game.treasure[size_t(scen)];
    if (worldBoxes && (app.treasureReroll || pick.empty())) {
        pick.clear();
        for (const cr::TreasureBox& b : cr::selectTreasureBoxesOmega(app.events, [&app](int n) { return n > 0 ? app.rng.below(n) : 0; },
                                                                    app.game.bands[size_t(scen)], scen))
            pick.push_back({b.id, b.item});
    }
    app.treasureReroll = false; // (cleared by any area's events loading)
    std::vector<cr::TreasureBox>& boxes = app.treasureBoxes[app.area];
    boxes.clear();
    if (worldBoxes)
        for (const auto& e : pick) boxes.push_back({e[0], e[1]});
    cr::applyTreasureFlags(app.events, app.npcs, boxes, app.game, true);
    npcSetup(app); // (FieldScene::onLoading: TreasureBoxManager::setup, RectWrapper::init, then PuzzleScene::init)
    puzzle::enter(app); // PuzzleScene::init in a puzzle room (stage -1 elsewhere)
    app.puzzleStartPending = app.puzzle.stage >= 0;
    app.npcDraws.clear(); // (npc_setup reads their models again: no fade carried over from the last room)
    app.npcHeld.clear();
    buildNpcDraws(app);
    cr::Mesh mesh;
    for (const cr::FieldNpc& n : app.npcs) {
        bool warps = false;
        if (n.touchTrigger()) {
            cr::GameState trial = app.game; // dry run on a copy: does touching it lead somewhere?
            warps = cr::runEvent(app.events, n.id, trial, app.area, app.mapNumber).warp.valid;
        }
        app.npcWarps.push_back(warps);
        if (!n.touchTrigger() || !npcActive(app, n)) continue;
        // Box from the floor up: magenta = exit, yellow = other touch event
        const float cx = n.boxCenterX(), cz = n.boxCenterZ(), hx = n.box[0] / 2, hz = n.box[2] / 2;
        const float y0 = n.pos[1] + n.offset[1], y1 = y0 + std::max(n.box[1], 1.0f);
        const uint8_t rgb[3] = {uint8_t(warps ? 230 : 240), uint8_t(warps ? 60 : 210), uint8_t(warps ? 220 : 40)};
        const float c[8][3] = {{cx - hx, y0, cz - hz}, {cx + hx, y0, cz - hz}, {cx + hx, y0, cz + hz}, {cx - hx, y0, cz + hz},
                               {cx - hx, y1, cz - hz}, {cx + hx, y1, cz - hz}, {cx + hx, y1, cz + hz}, {cx - hx, y1, cz + hz}};
        static const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
        for (const auto& f : faces) {
            const uint32_t base = uint32_t(mesh.positions.size() / 3);
            for (int k : f) {
                mesh.positions.insert(mesh.positions.end(), {c[k][0], c[k][1], c[k][2]});
                mesh.uvs.insert(mesh.uvs.end(), {0.0f, 0.0f});
                mesh.colors.insert(mesh.colors.end(), {rgb[0], rgb[1], rgb[2], 255});
            }
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
    if (!mesh.indices.empty()) app.triggerMesh = app.renderer.upload(mesh);
}

// The touch trigger box containing the player, 0 if none (RectWrapper::checkEventKick: x/z only).
int triggerAt(const App& app, const cr::Vec3& p) {
    for (const cr::FieldNpc& n : app.npcs) {
        if (!n.touchTrigger() || !npcActive(app, n)) continue;
        // RectWrapper::checkEventKick: the box's corners (the offset +- half its size) turned by the
        // record's angle (RectWrapper::init) about npc_setup's position (SetupRect): the player's
        // point taken into the box's own frame
        const float dx = p.x - n.boxCenterX(), dz = p.z - n.boxCenterZ();
        const float cs = std::cos(n.rectAngle), sn = std::sin(n.rectAngle);
        const float lx = cs * dx - sn * dz, lz = sn * dx + cs * dz;
        if (std::fabs(lx) <= n.box[0] / 2 && std::fabs(lz) <= n.box[2] / 2) return n.id;
    }
    return 0;
}

// Arrives at a warp destination: loads the map if it changes, places and turns the player.
bool performWarp(App& app, const cr::WarpRequest& wIn, int fromEvent) {
    // __EvtWarp: map -1 reloads this map and puts the player at the place; -98 reloads it with him
    // where he stands; a warp within the map only moves him - except on maps 13, 29, 47, 66, 71,
    // 91, 191 and 214, which it reloads (fldread_stg_end / InitFieldScene)
    cr::WarpRequest w = wIn;
    const bool keepPlace = w.map == -98, again = w.map < 0;
    if (w.map < 0) w.map = app.mapNumber;
    const int area = w.area >= 0 ? w.area : app.area;
    if (w.map < 0 || w.map >= cr::kNumMaps) return false;
    const auto it = std::find(app.maps.begin(), app.maps.end(), std::string(cr::kMapCollisionFile[w.map]));
    if (it == app.maps.end()) return false;
    const int fromMap = app.mapNumber;
    static const int kReloads[] = {13, 29, 47, 66, 71, 91, 191, 214}; // (__EvtWarp's eight; 152 is only the cameras' list's)
    const bool reloads = std::find(std::begin(kReloads), std::end(kReloads), w.map) != std::end(kReloads);
    if (keepPlace && app.havePlayer) w.pos[0] = app.walker.pos.x, w.pos[1] = app.walker.pos.y, w.pos[2] = app.walker.pos.z,
                                     w.angleDegrees = app.walker.heading * 180.0f / 3.14159265f;
    if (w.map != app.mapNumber || area != app.area || again || reloads) {
        showMap(app, int(it - app.maps.begin()), w.map, area);
        // FieldScene::onLoading calls SetMask2(1): retain the outgoing mask's
        // colour and reveal the loaded field over 500 ms. Area-changing scripts
        // end at Warp and rely on this, notably puzzle crystals' white-out.
        app.fadeFrom = 1, app.fadeTo = 0, app.fadeFrames = 10, app.fadeT = 0;
        // A script's warp within its area goes on with the script, and no room entry script
        // (EvrStart 5) follows it: fldread_prc runs that only on the load flag's bit 3, which
        // __EvtWarp never sets (0x22 / 1 / 4) - the lost battle's return does (prc: | 0xd)
        if (fromEvent >= 0 && app.pendingEntry == App::Entry::Room) app.pendingEntry = App::Entry::None;
    }
    if (!app.havePlayer) return false;
    cr::Vec3 p{w.pos[0], w.pos[1], w.pos[2]};
    float y;
    if (cr::floorHeight(app.collisionData, p.x, p.y, p.z, 5.0f, y) ||
        cr::floorHeight(app.collisionData, p.x, p.y, p.z, 1000.0f, y)) {
        p.y = y;
    } else {
        // A few destinations sit just past a floor edge (the original's floor search snaps
        // them in); take the nearest floor point within 20 units, else keep the spawn point
        float best = 1e30f;
        cr::Vec3 q = app.walker.pos;
        for (float dx = -20; dx <= 20; dx += 0.5f)
            for (float dz = -20; dz <= 20; dz += 0.5f)
                if (dx * dx + dz * dz < best &&
                    cr::floorHeight(app.collisionData, p.x + dx, p.y, p.z + dz, 1000.0f, y)) {
                    best = dx * dx + dz * dz;
                    q = {p.x + dx, y, p.z + dz};
                }
        p = q;
    }
    const bool moved = w.map != fromMap || area != app.area || again || reloads; // (a same-map warp writes the position alone)
    app.fieldLoad[0] = w.pos[0], app.fieldLoad[1] = w.pos[1], app.fieldLoad[2] = w.pos[2];
    app.fieldLoad[3] = w.angleDegrees * 3.14159265f / 180.0f, app.fieldLoadSet = true;
    app.walker.pos = p;
    if (moved) app.walker.heading = w.angleDegrees * 3.14159265f / 180.0f; // FieldPCCtrl::reset: DegToRad
    app.walker.gait = cr::Gait::Stand;
    // (lastKick as it was - the warping box's id: a box of another id where he lands kicks once the
    // scripts end, as every dungeon's way back to the Ark lands in NPC 260's)
    app.lastWarp = "warp: event " + std::to_string(fromEvent) + " map " + std::to_string(fromMap) + " -> " +
                   std::to_string(w.map);
    SDL_Log("%s (%s) at (%.1f %.1f %.1f) facing %g", app.lastWarp.c_str(), cr::kMapCollisionFile[w.map], p.x, p.y, p.z,
            w.angleDegrees);
    updateTitle(app);
    return true;
}

// An NPC whose window can be up: kind bit 3, displayed, not an opened box (FollowWindow::
// isOpenedTreasureBox: its lid's, id + 1, hide flag set - no badge, no examine)
bool npcExaminable(const App& app, const cr::FieldNpc& n) {
    if (!(n.kind & 8) || !npcActive(app, n)) return false;
    if (n.pos[1] >= 900.0f || (n.id >= 400 && n.pos[0] == 0 && n.pos[1] == 0 && n.pos[2] == 0)) return false; // (story actors waiting)
    if (n.treasure & 3) {
        cr::NpcRow lid;
        if (app.events.npcRow(n.id + 1, lid) && lid.flagHide >= 0 && app.game.flag(lid.flagHide)) return false;
    }
    return true;
}

// RectWrapper::checkFollowWindow / checkFlagWindow: the leader inside the NPC's box - about npc_setup's
// position plus the offset, not turned - grown by the walker radius + 1.5
bool npcInReach(const App& app, const cr::FieldNpc& n) {
    if (!npcExaminable(app, n)) return false;
    const float cx = n.base(0) + n.offset[0], cz = n.base(2) + n.offset[2];
    const float hx = n.box[0] / 2 + app.walker.radius + 1.5f, hz = n.box[2] / 2 + app.walker.radius + 1.5f;
    return std::fabs(app.walker.pos.x - cx) <= hx && std::fabs(app.walker.pos.z - cz) <= hz;
}

// A field frame of the windows: one put up (FollowWindow::set / FlagWindow::setFollowWindow) for each
// NPC in reach lasts 10 frames, so it lingers a moment after the leader steps out; the checks don't
// run while a script holds the field (G+0x364 bit 0)
void stepExamineWindows(App& app) {
    for (auto it = app.examineLinger.begin(); it != app.examineLinger.end();)
        it = --it->second <= 0 ? app.examineLinger.erase(it) : std::next(it);
    if (!app.havePlayer || app.runner.running()) return;
    for (const cr::FieldNpc& n : app.npcs)
        if (npcInReach(app, n)) app.examineLinger[n.id] = 10, app.flagTalked.erase(n.id); // (setFollowWindow: state 0)
}

// The NPC the "!" button examines or talks to. FollowWindow::update (objects: kind & 0x18 == 8) runs
// before FlagWindow::update (people: 0x18), and an examine from the first starts its script, which
// holds the second back: an object in reach wins over a person. Each takes, of its windows up (in
// reach, or lingering), the nearest by 3D distance from npc_setup's position + the offset to the leader.
const cr::FieldNpc* interactTarget(const App& app) {
    if (!app.havePlayer) return nullptr;
    for (int people = 0; people < 2; ++people) {
        const cr::FieldNpc* best = nullptr;
        float bestD = 1e30f;
        for (const cr::FieldNpc& n : app.npcs) {
            if (bool(n.kind & 0x10) != bool(people)) continue;
            const auto up = app.examineLinger.find(n.id);
            if (!(up != app.examineLinger.end() && npcExaminable(app, n)) && !npcInReach(app, n)) continue;
            const float dx = app.walker.pos.x - (n.base(0) + n.offset[0]), dy = app.walker.pos.y - (n.base(1) + n.offset[1]),
                        dz = app.walker.pos.z - (n.base(2) + n.offset[2]);
            const float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) bestD = d, best = &n;
        }
        if (best) return best;
    }
    return nullptr;
}

// Examines / talks to the NPC in reach, as FollowWindow::TouchFollowWindowUtil does: a treasure
// box sets var 11 = box id, var 10 = its item, and runs the script of its lid (id + 1);
// anything else runs its own script.
bool interact(App& app, bool instant) {
    if (app.runner.running()) return false;
    const cr::FieldNpc* n = interactTarget(app);
    if (!n) return false;
    int script = n->id;
    const std::string what = n->model >= 0 && n->model < cr::kNumNpcModels ? cr::kNpcModelFile[n->model] : "trigger";
    if (n->treasure & 3) {
        cr::NpcRow row;
        app.game.vars[11] = n->id;
        app.game.vars[10] = app.events.npcRow(n->id, row) && row.item >= 0
                                ? row.item
                                : cr::treasureItem(app.treasureBoxes[app.area], n->id);
        script = n->id + 1;
    }
    const int id = n->id;
    app.examineLinger.erase(id); // (_FollowWindowOff)
    if (n->kind & 0x10) app.flagTalked.insert(id); // (FlagWindow::update: TouchFollowWindowUtil, then state 2)
    menuSound(app, se::kChoose); // TouchFollowWindowUtil: SE 0x93 on every examine and talk
    if (!instant) { // played over the next frames (animations, item window, fades)
        app.runnerWhat = "examined " + what.substr(0, what.size() - 4) + " (" + std::to_string(id) + ")";
        app.lastEvent = app.runnerWhat;
        app.runner.start(app.events, script, app.game, app.area, app.mapNumber);
        updateTitle(app);
        return true;
    }
    const cr::EventResult r = cr::runEvent(app.events, script, app.game, app.area, app.mapNumber);
    if (!r.error.empty()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "event %d: %s", script, r.error.c_str());
    app.lastEvent = "examined " + what.substr(0, what.size() - 4) + " (" + std::to_string(id) + ")";
    for (int item : r.itemsGained) { // item names: sys.evt block 0 (descriptions are block 1)
        std::vector<std::vector<std::string>> name;
        app.lastEvent += ", got " + itemName(app, item);
    }
    SDL_Log("%s, script %d, %d instructions, %zu messages", app.lastEvent.c_str(), script, r.instructions,
            r.messages.size());
    showMessages(app, r.messages);
    if (r.warp.valid && performWarp(app, r.warp, script)) return true;
    // Scripts change flags (opened boxes hide their lid): refresh what is shown and solid
    buildNpcDraws(app);
    updateTitle(app);
    return true;
}

// An item's name (sys.evt block 0; descriptions are block 1)
// Whether `language` can be shown: the data has it, and Japanese / Chinese / Korean need the font.
bool languageUsable(const App& app, int language) {
    if (language < 0 || language >= cr::kLanguages || !app.systemText.hasLanguage(language)) return false;
    return (language >= 1 && language <= 4) || app.font.loaded();
}

// 3.1.0's data: eight text sections (1.0.0 has Japanese and English only)
bool multilingualData(const App& app) { return app.systemText.hasLanguage(2); }
// _MuComFontSet (3.1.0 @0x3d058): the language is Japanese, Chinese or Korean
bool cjkLanguage(int language) { return language == 0 || language >= 5; }

// The font face for the language: the collection's regional Han forms (Noto Sans CJK JP / SC /
// TC / KR); Latin text uses the Japanese face like the Japanese release's system font. The
// menus' text (_MuComFont*Regist) is 14 pt, in 3.1.0 13 for the Latin languages.
void applyLanguage(App& app) {
    if (!languageUsable(app, app.language)) app.language = 1;
    static const char* const kRegion[cr::kLanguages] = {"JP", "JP", "JP", "JP", "JP", "SC", "TC", "KR"};
    // Your own font (font/ in the game folder - cr::ownFont): over the Vita copy's faces and the system's (CR_FONT over
    // it) - for Japanese, Chinese and Korean the first with their glyphs, else the first; in Noto Sans CJK's line box,
    // which the port's text places were set with. One without CJK glyphs while those are wanted: the system's
    if (!SDL_getenv("CR_FONT")) {
        const std::string own = cr::ownFont(cjkLanguage(app.language));
        if (!own.empty()) {
            if (app.font.path() != own && app.font.load(own)) SDL_Log("font/: %s", own.c_str());
            if (app.font.path() == own) {
                app.font.setLineBox(1.16f, -0.288f);
                app.font.selectRegion(kRegion[app.language]); // (a CJK collection's regional face; else nothing)
                app.menu.setMenuFont(multilingualData(app) && !cjkLanguage(app.language) ? 13.0f : 14.0f);
                return;
            }
        } else if (cr::isOwnFont(app.font.path())) cr::loadSystemFont(app.font), app.font.setLineBox(0, 0);
    }
    // The Vita copy as the game: its own faces, as its scePvf draws them (resources/font: TB Gothic Pro B for
    // Japanese and the Latin languages, DFHeiW9-GB for Simplified Chinese, DFLiHei-Md for Traditional,
    // DFKMincho-Bd for Korean); the system's (Noto) when the copy has none or CR_FONT picks one
    if (app.vitaOnly && !SDL_getenv("CR_FONT")) {
        static const char* const kVitaFace[cr::kLanguages] = {"TBGoPro-Bold.otf", "TBGoPro-Bold.otf", "TBGoPro-Bold.otf",
                                                              "TBGoPro-Bold.otf", "TBGoPro-Bold.otf", "DFGB_H9.TTC",
                                                              "DFT_C5.ttc",       "DFMINBD.TTF"};
        const std::string want = app.vitaRoot + "/resources/font/" + kVitaFace[std::clamp(app.language, 0, cr::kLanguages - 1)];
        if (app.font.path() == want && app.font.loaded()) { app.menu.setMenuFont(multilingualData(app) && !cjkLanguage(app.language) ? 13.0f : 14.0f); return; }
        if (app.font.load(want)) {
            SDL_Log("font: %s", want.c_str());
            // (in Noto Sans CJK's line box - hhea 1.16 / -0.288 em -, which the port's text places were set
            // with: the glyphs' baselines where they were)
            app.font.setLineBox(1.16f, -0.288f);

            app.menu.setMenuFont(multilingualData(app) && !cjkLanguage(app.language) ? 13.0f : 14.0f);
            return;
        }
        if (!app.font.loaded()) cr::loadSystemFont(app.font);
        app.font.setLineBox(0, 0);
    }

#ifdef SDL_PLATFORM_IOS
    // iOS: the system's own faces (CoreText; none at fixed paths). 3.1.0 asks UIKit for HiraKakuProN-W6 and
    // ArialMT (its binary's names): Japanese the former; ArialMT can't draw the others' scripts and the port's
    // text is one face, so the Latin languages Hiragino's W3 (Latin and kana / kanji, ArialMT's weight), and
    // Chinese / Korean what iOS falls back to from ArialMT - PingFang SC / TC, Apple SD Gothic Neo. In Noto
    // Sans CJK's line box, as the Vita's faces and font/'s
    if (!SDL_getenv("CR_FONT") && !cr::isOwnFont(app.font.path()) && !app.vitaOnly) {
        static const char* const kIos[cr::kLanguages][2] = {
            {"HiraKakuProN-W6", "HiraginoSans-W6"}, {"HiraKakuProN-W3", "HiraginoSans-W3"}, {"HiraKakuProN-W3", "HiraginoSans-W3"},
            {"HiraKakuProN-W3", "HiraginoSans-W3"}, {"HiraKakuProN-W3", "HiraginoSans-W3"}, {"PingFangSC-Regular", "STHeitiSC-Light"},
            {"PingFangTC-Regular", "STHeitiTC-Light"}, {"AppleSDGothicNeo-Regular", "AppleSDGothicNeo-Medium"}};
        const int l = std::clamp(app.language, 0, cr::kLanguages - 1);
        if (cr::loadAppleFont(app.font, kIos[l][0]) || cr::loadAppleFont(app.font, kIos[l][1])) {
            SDL_Log("font: %s (iOS)", app.font.path().c_str());
            app.font.setLineBox(1.16f, -0.288f);
            app.menu.setMenuFont(multilingualData(app) && !cjkLanguage(app.language) ? 13.0f : 14.0f);
            return;
        }
    }
#endif
    app.font.selectRegion(kRegion[app.language]);

    app.menu.setMenuFont(multilingualData(app) && !cjkLanguage(app.language) ? 13.0f : 14.0f);
}

std::string itemName(const App& app, int item) {
    std::vector<std::vector<std::string>> name;
    if (app.systemText.message(cr::itemNameId(item), app.language, nullptr, name) && !name.empty() && !name[0].empty())
        return name[0][0];
    return "item " + std::to_string(item);
}

// Turns the dialogue page (E / Enter); the last page read stays available for a following choice.
void resetTyping(App& app) {
    app.dialogueTyped = app.dialogueHold = app.dialoguePause = 0;
    app.dialogueInstant = false;
}


void stepMsgWin(App& app) { stepMsgWin(app.msgWin); }
void stepMsgWin(App::MsgWin& w) {
    switch (w.state) {
    case 1: w.t = 4, w.state = 2; break;
    case 2:
        --w.t;
        w.alpha = float(4 - w.t) / 4.0f;
        if (w.t <= 0) w.state = 3, w.alpha = 1;
        break;
    case 4: w.t = 0, w.state = 5; break;
    case 5:
        w.alpha = float(4 - w.t) / 4.0f;
        if (++w.t > 4) w.state = 6, w.alpha = 0;
        break;
    default: break;
    }
}

// `tapped`: the reader's tap turned it (PrcMsg's 0x12 / its last page's wait); a page the window
// clears or closes by itself (0x1a then 0x10 / 0x14) sets no flag
void advanceDialogue(App& app, bool tapped) {
    if (app.dialogue.empty()) return;
    app.sound.stopDialogueVoices();
    app.voiceCue = 0;
    if (tapped) app.game.setFlag(4075, true); // PrcMsg: a page turned (or the last one closed) - MsgUty_TexDispProc's cue
    resetTyping(app);
    if (++app.dialoguePage < app.dialogue.size()) {
        // A page keeping the lines before it (0x12 without 0x10) starts with them out, typing on
        // (0x11) or out at once (anything else: _PrcMsg leaves the typing flag down)
        const cr::MessageInfo* next = app.dialoguePage < app.dialogueInfo.size() ? &app.dialogueInfo[app.dialoguePage] : nullptr;
        const int keep = next ? next->keep : 0;
        app.dialogueTyped = keep;
        app.dialogueInstant = next && next->instant;
        // (PrcMsg: the 0x12's frame, then the 0x10's, then the next page's first character; a kept
        // page's 0x11 is its first unit)
        app.dialogueHold = keep > 0 ? 1 : 2;
        return;
    }
    app.lastPage = app.dialogue.back();
    app.lastPageInfo = app.dialogueInfo.size() == app.dialogue.size() ? app.dialogueInfo.back() : cr::MessageInfo();
    app.dialogue.clear();
    app.dialoguePage = 0;
}

// A page's characters, as PrcMsg types them (UTF-8 lead bytes; not the lines' \x01 marks), and its
// line breaks: each 0x11 takes PrcMsg a frame of its own, typing or not
int pageChars(const std::vector<std::string>& page) {
    int n = page.size() > 1 ? int(page.size()) - 1 : 0;
    for (const std::string& l : page)
        for (unsigned char ch : l) n += (ch & 0xC0) != 0x80 && ch != 0x01;
    return n;
}

// Whether the page's unit `at` (as pageChars counts them) is a line break
bool pageBreakAt(const std::vector<std::string>& page, int at) {
    for (size_t k = 0; k < page.size(); ++k) {
        int n = 0;
        for (unsigned char ch : page[k]) n += (ch & 0xC0) != 0x80 && ch != 0x01;
        if (at < n) return false;
        at -= n;
        if (k + 1 < page.size() && at-- == 0) return true;
    }
    return false;
}

// The page cut to its first `limit` characters (the \x01 marks kept; a line break counts as one)
std::vector<std::string> typedPage(const std::vector<std::string>& page, int limit) {
    std::vector<std::string> out;
    for (const std::string& l : page) {
        if (!out.empty() && limit-- <= 0) break;
        std::string t;
        for (char c : l) {
            const unsigned char ch = static_cast<unsigned char>(c);
            if (ch != 0x01 && (ch & 0xC0) != 0x80 && limit-- <= 0) break;
            t += c;
        }
        out.push_back(t);
    }
    return out;
}

const cr::MessageInfo* pageInfo(const App& app) {
    const size_t pi = std::min(app.dialoguePage, app.dialogue.size() - 1);
    return pi < app.dialogueInfo.size() ? &app.dialogueInfo[pi] : nullptr;
}

// Out: every character typed and every pause over (a 0x1a before the wait holds the "next" mark too)
bool pageTyped(const App& app) {
    if (app.dialogue.empty() || !app.typeOn) return true;
    const cr::MessageInfo* info = pageInfo(app);
    return app.dialogueTyped >= pageChars(app.dialogue[std::min(app.dialoguePage, app.dialogue.size() - 1)]) &&
           (!info || app.dialoguePause >= int(info->pauses.size())) && app.dialogueHold == 0;
}

// G+0x88882 (0x1b) where the typing is: the tap and the skip corner can't bring the page out at once
bool dialogueNoSkip(const App& app) {
    if (app.dialogue.empty()) return false;
    const cr::MessageInfo* info = pageInfo(app);
    if (!info) return false;
    bool on = info->noSkipOn;
    for (const auto& t : info->noSkip) on ^= t.at <= app.dialogueTyped;
    return on;
}

// _PrcMsg's 0x1a n: the next unit comes n frames after the one before (its counter set to n - 1
// on reaching it, down a frame at a time, on at 0 - for n 0 it starts at -1: two frames)
int pauseHold(int frames) { return frames == 0 ? 1 : frames - 1; }



// PrcMsg a frame: the next character, or - the typing flag down (a tap) - every character up to the
// next stop: a 0x1a pause, a line break (0x11 ends _PrcMsg's frame), a 0x1e waiting for the clip
// before it, the page's end. A page ending the message with 0x14 (or one the window clears by
// itself, 0x1a then 0x10) goes once out and its pauses are over
void stepTyping(App& app) {
    if (app.dialogue.empty()) return;
    if (app.msgWin.state == 1 || app.msgWin.state == 2) return; // PrcMsg: nothing until the window is up (state 3)
    const cr::MessageInfo* info = pageInfo(app);
    const std::vector<std::string>& page = app.dialogue[std::min(app.dialoguePage, app.dialogue.size() - 1)];
    const int total = pageChars(page);
    if (!app.typeOn) app.dialogueTyped = total, app.dialoguePause = info ? int(info->pauses.size()) : 0;
    if (app.dialogueHold > 0 && --app.dialogueHold > 0) return;
    for (;;) {
        if (stepVoiceCue(app)) return;
        if (info && app.dialoguePause < int(info->pauses.size()) && info->pauses[size_t(app.dialoguePause)].at <= app.dialogueTyped) {
            app.dialogueHold = pauseHold(info->pauses[size_t(app.dialoguePause++)].frames);
            if (app.dialogueHold > 0) return;
            continue;
        }
        if (app.dialogueTyped >= total) {
            if (!info || info->closeAfter < 0 || app.choiceFlag >= 0) return;
            advanceDialogue(app, false);
            // PrcMsg 0x14: the message is done (the script goes on) but its text stays in the window
            // until Close, the next message or the script's end; 0x10: the window clears, the next
            // page's first character comes the frame after
            if (app.dialogue.empty()) app.heldPage = true;
            else app.dialogueHold = 0;
            return;
        }
        const bool lineBreak = pageBreakAt(page, app.dialogueTyped);
        ++app.dialogueTyped;
        if (!app.dialogueInstant || lineBreak) return;
    }
}

// The typing flag down (_PrcMsg's top: a tap, or the skip corner, while it types - not under 0x1b)
void hurryDialogue(App& app) {
    if (!app.dialogue.empty() && !pageTyped(app) && !dialogueNoSkip(app)) app.dialogueInstant = true;
}

// The player's tap on a page: one still typing comes out at once up to its next stop (PrcMsg
// +0x8887b; not while 0x1b holds it), one out turns; a page closing by itself takes no tap
void tapDialogue(App& app) {
    if (app.dialogue.empty()) return;
    const cr::MessageInfo* info = pageInfo(app);
    if (!pageTyped(app)) {
        hurryDialogue(app);
        return;
    }
    if (info && info->closeAfter >= 0) return;
    advanceDialogue(app);
}

// Answers the open yes/no choice (Util 3): yes sets its flag, no clears it (_menuu_EventClr).
void answerChoice(App& app, bool yes, bool cancelled) {
    if (app.choiceFlag < 0) return;
    app.game.setFlag(app.choiceFlag, yes);
    app.choiceFlag = -1;
    // Yes and No alike (EventPrc's ComUty_YesNoChoiceProc); the Vita's × for No: 0x94
    menuSound(app, cancelled ? se::kBack : se::kChoose);
}


void answerWorldChoice(App& app, int pos, bool skip) {
    if (!app.worldChoiceOpen) return;
    App::WorldSelect& ws = app.worldSelect;
    ws.cancelled = pos < 0 || pos >= int(ws.worlds.size());
    if (!ws.cancelled)
        ws.world = ws.worlds[size_t(pos)], ws.level = 0,
        ws.skip = skip && worldAsksSkip(app, ws.world, ws.pair);
    menuSound(app, ws.cancelled ? se::kBack : se::kChoose); // MuScrList: an item, or Back
    closeWorldSelect(app);
}

// Queues the messages an event showed (English text) for the dialogue box.
// Uty_GetItem_Init (Call 2005, var 9 the item): its line - font block 6's 10 "You got <item>!" (0x5e for
// item 438), with flag 10 var 9 Oz into the scenario's purse (0x55), 999 the box is empty (0x71) - and
// its icon, and a sound (0xa1 items 61..80, 0xfa the collection's 551..630, 0xa0 the rest, 0x97 empty)
void openItemWindow(App& app, int item) {
    app.itemWindowOpen = true;
    app.itemWindowItem = item;
    app.itemWindowRow = app.itemWindowFade = 0;
    app.itemWindowClosing = false;
    app.itemWindowIcon = -1;
    if (app.game.flag(10)) {
        app.game.oz = std::min(9999999, app.game.oz + item);
        app.itemWindowText = sysText(app, 57085, std::to_string(item));
        app.itemWindowIcon = 0x3f;
        app.sound.play(0xa0);
    } else if (item == 999) {
        app.itemWindowText = sysText(app, 57113);
        app.sound.play(0x97);
    } else {
        app.itemWindowText = sysText(app, item == 438 ? 57094 : 57010, itemName(app, item));
        if (const cr::ItemParam* p = app.itemTable.item(item); p && p->icon >= 0) app.itemWindowIcon = p->icon;
        app.sound.play(item >= 61 && item < 81 ? 0xa1 : item >= 551 && item < 631 ? 0xfa : 0xa0);
    }
    if (app.itemWindowText.empty()) app.itemWindowText = itemName(app, item);
}

void showMessages(App& app, const std::vector<int>& ids) {
    app.heldPage = false;
    if (!ids.empty()) app.lastMessageId = ids.back();
    if (app.dialogue.empty()) {
        app.dialogueInfo.clear(), app.dialogueVoices.clear(), app.dialoguePage = 0;
        ++app.dialogueSerial;
        resetTyping(app);
    }
    for (int id : ids) {
        std::vector<std::vector<std::string>> pages;
        cr::MessageInfo info;
        auto var = [&](int v) { auto it = app.game.vars.find(v); return it == app.game.vars.end() ? 0 : it->second; };
        if (app.events.message(id, app.language, var, pages, &info) ||
            app.systemText.message(id, app.language, var, pages, &info))
            app.dialogue.insert(app.dialogue.end(), pages.begin(), pages.end());
        else
            pages = {{"[message " + std::to_string(id) + "]"}}, app.dialogue.push_back(pages[0]);
        app.dialogueInfo.resize(app.dialogue.size() - pages.size());
        // A code on page k at `at` - one after the last page's text belongs to it, and one past a
        // page's end (its trailing empty lines dropped) comes as it is out
        auto onPage = [&](int page, size_t k) { return page == int(k) || (k + 1 == pages.size() && page >= int(pages.size())); };
        auto clampAt = [&](int at, size_t k) { return std::min(at, pageChars(pages[k])); };
        bool noSkip = false; // (0x1b: the message's flag as each page starts)
        for (size_t k = 0; k < pages.size(); ++k) { // each page its own pauses; the last the closing
            cr::MessageInfo pi = info;
            pi.pauses.clear();
            for (const auto& pz : info.pauses)
                if (onPage(pz.page, k)) pi.pauses.push_back({pz.page, clampAt(pz.at, k), pz.frames});
            if (k + 1 < pages.size()) pi.closeAfter = -1;
            for (const auto& t : info.turns) // (a page the window clears by itself after its pause turns so)
                if (t.page == int(k) && k + 1 < pages.size()) pi.closeAfter = t.frames;
            for (const auto& kp : info.kept) // (0x12 without 0x10: the lines before stay)
                if (kp.page == int(k)) pi.keep = std::min(kp.chars, pageChars(pages[k])), pi.instant = kp.instant;
            pi.noSkipOn = noSkip;
            pi.noSkip.clear();
            for (const auto& t : info.noSkip)
                if (onPage(t.page, k)) pi.noSkip.push_back({t.page, clampAt(t.at, k)}), noSkip = !noSkip;
            app.dialogueInfo.push_back(pi);
        }
        app.dialogueVoices.resize(app.dialogue.size() - pages.size());
        for (size_t k = 0; k < pages.size(); ++k) {
            std::vector<App::VoiceCue> clips;
            for (const auto& v : info.voices)
                if (onPage(v.page, k)) clips.push_back({clampAt(v.at, k), v.clip});
            app.dialogueVoices.push_back(clips);
        }
    }
}

// Fires the touch trigger the player just stepped into. Returns true if it warped.
bool checkTriggers(App& app, bool instant) {
    if (app.runner.running()) return false;
    const int id = triggerAt(app, app.walker.pos);
    if (id == 0) { app.lastKick = 0; return false; }
    if (id == app.lastKick) return false;
    app.lastKick = id;
    if (!instant) {
        app.runnerWhat.clear();
        app.runner.start(app.events, id, app.game, app.area, app.mapNumber);
        return false;
    }
    const cr::EventResult r = cr::runEvent(app.events, id, app.game, app.area, app.mapNumber);
    if (!r.error.empty()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "event %d: %s", id, r.error.c_str());
    showMessages(app, r.messages);
    return r.warp.valid && performWarp(app, r.warp, id);
}

// A model's node world matrices for `act` at `time`, under the old action while a change blends
// (mdl_actctl_prc: two action controllers weighted 1 - k / 2 and k / 2)
void poseWorld(const cr::Skeleton& skel, const std::vector<cr::Action>& actions, const cr::Action* act, float time,
               const cr::MotionBlend& blend, std::vector<cr::Mat4>& world) {
    if (const cr::Action* under = blend.under(actions))
        cr::computeBlendedWorld(skel, under, blend.fromTime, act, time, blend.weight(), world);
    else cr::computeWorld(skel, act, time, world);
}

// Advances a model's current animation and re-skins every mesh on the CPU.
void animate(App& app, LoadedModel& lm, float dt) {
    const cr::Model& m = lm.cpu;
    if (m.skeleton.nodes.empty() || lm.meshes.size() != m.meshes.size()) return;
    const cr::Action* act = lm.action >= 0 && lm.action < int(lm.actions.size()) ? &lm.actions[lm.action] : nullptr;
    if (act && (app.mode != Mode::Models || app.playing)) {
        if (app.mode == Mode::Models) lm.blend.reset(); // (the viewer shows a chosen action at once)
        if (!lm.blend.step(lm.action, lm.time, dt * kFramesPerSecond * app.speed, lm.actions))
            lm.time += dt * kFramesPerSecond * app.speed;
        if (lm.loopFrom > 0 && lm.loopFrom < act->duration && lm.time >= act->duration)
            lm.time = float(lm.loopFrom) + std::fmod(lm.time - float(lm.loopFrom), act->duration - float(lm.loopFrom));
        else if (act->duration > 0) lm.time = std::fmod(lm.time, act->duration);
    }
    poseWorld(m.skeleton, lm.actions, act, lm.time, lm.blend, lm.world);
    for (size_t i = 0; i < m.meshes.size(); ++i) {
        cr::skinMesh(m, m.meshes[i], lm.world, lm.skinned);
        app.renderer.updatePositions(lm.meshes[i], lm.skinned, m.meshes[i].uvs, m.meshes[i].colors);
    }
}

void animateModel(App& app, float dt) { animate(app, app.model, dt); }


// The leader's field model (Omega's Set_Prtyno / fldread_prc): leader 0 f009a - f009b in the second
// scenario (prty + 8) or once flag 0x468 is set (MuCom_GetViegOlgarCheck) -, 1 f010, 2 f011, 3 f009b.
std::string playerModelName(const App& app) {
    switch (app.playerChar & 3) {
    case 0: return viegIsOlgar(app) ? "f009b.bin" : "f009a.bin";
    case 1: return "f010.bin";
    case 2: return "f011.bin";
    default: return "f009b.bin";
    }
}

// Puts the party leader on the current field, where the stage camera's line of sight meets
// the floor (the real start point comes from the event script, which isn't interpreted yet).
void placePlayer(App& app) {
    app.havePlayer = false;
    if (!app.showPlayer || app.collisionData.polygons.empty()) return;
    const std::string name = playerModelName(app);
    if (app.playerName != name || app.playerLoadedByReadMdl != app.playerByReadMdl) {
        unloadLoaded(app, app.player);
        loadModelFile(app, name, app.player, true);
        if (app.playerByReadMdl) readMdlRules(app.player, name);
        else mdlMemRules(app.player);
        app.playerName = name;
        app.playerLoadedByReadMdl = app.playerByReadMdl;
    }
    if (app.player.meshes.empty()) return;
    const cr::Collision& col = app.collisionData;
    cr::Vec3 eye, tgt;
    if (app.haveStageCam) {
        eye = {app.stageCam.eye[0], app.stageCam.eye[1], app.stageCam.eye[2]};
        tgt = {app.stageCam.target[0], app.stageCam.target[1], app.stageCam.target[2]};
    } else {
        const float cx = (col.boundsMin[0] + col.boundsMax[0]) / 2, cz = (col.boundsMin[2] + col.boundsMax[2]) / 2;
        eye = {cx, col.boundsMax[1] + 100, cz};
        tgt = {cx, col.boundsMin[1] - 100, cz};
    }
    const float aspect = app.haveStageCam && app.stageCam.viewHeight > 0
                             ? float(app.stageCam.viewWidth) / float(app.stageCam.viewHeight) : 1.0f;
    const float fov = app.haveStageCam ? app.stageCam.fovDegrees : 60.0f;
    const cr::Mat4 vp = cr::Mat4::perspective(fov * 3.14159265f / 180.0f, aspect, 10.0f, 4000.0f) *
                        cr::Mat4::lookAt(eye, tgt, app.haveStageCam ? cr::Vec3{0, 1, 0} : cr::Vec3{0, 0, -1}); // top-down otherwise
    // Start at the map's first entrance in the area's warp table (where the game puts you when
    // you arrive); without one, where the camera looks at the floor
    bool entrance = false;
    for (const cr::WarpEntry& w : app.events.warps()) {
        if (w.map != app.mapNumber || (w.area >= 0 && w.area != app.area)) continue;
        float y;
        if (!cr::floorHeight(col, w.pos[0], w.pos[1], w.pos[2], 5.0f, y) &&
            !cr::floorHeight(col, w.pos[0], w.pos[1], w.pos[2], 1000.0f, y))
            continue;
        app.walker.pos = {w.pos[0], y, w.pos[2]};
        app.walker.heading = float(w.angleDegrees) * 3.14159265f / 180.0f;
        entrance = true;
        break;
    }
    if (!entrance) {
        if (!cr::spawnPoint(col, eye, tgt, vp, app.walker.pos)) return;
        app.walker.heading = std::atan2(eye.x - tgt.x, eye.z - tgt.z); // face the camera
    }
    app.walker.radius = app.haveStageCam && app.stageCam.hitRadius > 0 ? float(app.stageCam.hitRadius) : 5.0f;
    if (app.area == 1) app.walker.radius = 4.0f; // FieldPCCtrl::init: the puzzle rooms' (G+0x8a59e == 1) is 4, whatever the record says
    app.walker.gait = cr::Gait::Stand;
    app.player.action = app.player.actions.empty() ? -1 : 0;
    app.player.time = 0;
    app.walkClock = 0;
    app.havePlayer = true;
}

// The NPC's models, shown or hidden: the event ops act on the model whatever its NPC's hidden bit
// (__EvtAnim / __EvtAnim2 / __EvtNpcFade / __EvtUnloadAct take npc ctl +0)
std::vector<App::NpcDraw*> ViewerHost::npc(int id) {
    std::vector<App::NpcDraw*> out;
    for (auto* v : {&app.npcDraws, &app.npcHeld})
        for (auto& d : *v)
            if (d.id == id) out.push_back(&d);
    return out;
}
void ViewerHost::showMessage(int id) { showMessages(app, {id}); }
bool ViewerHost::messageOpen() { return !app.dialogue.empty(); }
bool ViewerHost::tapped() { const bool t = app.tapPending; app.tapPending = false; return t; }
void ViewerHost::showItemWindow(int item) { // Uty_GetItem_Init: ClrFlag(0), the confirmation its own
    app.game.setFlag(0, false);
    openItemWindow(app, item);
}
void ViewerHost::playAnim(int id, int slot, int loopFrom, int loopTo) {
    if (id == 0) { // the leader (the turn-backs' Anim 0, 1 / FrmAdjc 0 / Anim 0, 0: he walks the steps back)
        app.leaderMotion = slot, app.leaderLoopFrom = loopFrom;
        return;
    }
    int action = slot; // a slot LoadAct filled, else the model's own action of that number
    if (auto a = app.actorSlots.find(id); a != app.actorSlots.end())
        if (auto it = a->second.find(slot); it != a->second.end()) action = it->second;
    for (auto* d : npc(id)) {
        if (d->action != action) { // the same action keeps running; a new one blends in over 2 frames (mdl_set_act_motion_loop)
            const LoadedModel& lm = app.npcModels[d->model];
            d->action = action, d->time = 0;
            d->blend.restart(action, 0, 2, lm.actions);
        }
        d->once = loopFrom < 0;
        d->loopFrom = loopFrom;
        d->loopTo = loopTo;
    }
}
// Anim2 (mdl_set_act_motion_loop_frm): started over from `start` whatever plays, the pose before
// blended under it over `blend` frames - a cut when 0, as the opening's are
void ViewerHost::playAnim2(int id, int slot, int loopFrom, int loopTo, int start, int blend) {
    int action = slot;
    if (auto a = app.actorSlots.find(id); a != app.actorSlots.end())
        if (auto it = a->second.find(slot); it != a->second.end()) action = it->second;
    for (auto* d : npc(id)) {
        const LoadedModel& lm = app.npcModels[d->model];
        d->action = action;
        d->time = float(std::max(start, 0));
        d->once = loopFrom < 0;
        d->loopFrom = loopFrom;
        d->loopTo = loopTo;
        d->blend.restart(action, d->time, float(std::max(blend, 0)), lm.actions);
    }
}
void ViewerHost::loadAction(int id, int action, int slot) {
    const cr::FieldNpc* n = findNpc(app, id);
    if (!n || n->model < 0 || n->model >= cr::kNumNpcModels || action < 0 || action >= cr::kNumActionFiles) {
        if (SDL_getenv("CR_OP_TRACE")) std::printf("  load action %d into npc %d slot %d: no such NPC or action\n", action, id, slot);
        return;
    }
    const std::string file = cr::kNpcModelFile[n->model];
    LoadedModel& lm = npcModel(app, file);
    const std::string mca = cr::kActionFile[action];
    auto known = lm.loaded.find(mca);
    if (known == lm.loaded.end()) {
        std::vector<uint8_t> ad;
        std::vector<cr::Action> acts;
        int index = -1;
        if (cr::readFile(app.dataDir + mca, ad) && cr::parseMca(ad.data(), ad.size(), 0, acts))
            for (auto& a : acts) // (__EvtLoadAct binds whatever the file holds: a motion a few
                // segments short, or short of n010's twelve rest helpers, too - j010e16 on Olgar,
                // j001_t01's arrival, Part 1 811-830 s: his arms down, the axe out of sight)
                if (cr::actionFits(lm.cpu.skeleton, a) || cr::actionNearlyFits(lm.cpu.skeleton, a)) {
                    a.name = mca.substr(0, mca.size() - 4);
                    index = int(lm.actions.size());
                    lm.actions.push_back(std::move(a));
                    break;
                }
        known = lm.loaded.emplace(mca, index).first;
    }
    if (known->second >= 0) app.actorSlots[id][slot] = known->second;
    if (SDL_getenv("CR_OP_TRACE"))
        std::printf("  load action %s into npc %d (%s) slot %d: %s\n", mca.c_str(), id, file.c_str(), slot,
                    known->second >= 0 ? "ok" : "doesn't fit its skeleton");
}
void ViewerHost::unloadAction(int id, int slot) { // (__EvtUnloadAct: the slot freed and the model put on its action 0)
    if (auto a = app.actorSlots.find(id); a != app.actorSlots.end()) a->second.erase(slot);
    for (auto* d : npc(id)) {
        if (d->action == 0) continue;
        d->action = 0, d->time = 0, d->once = false, d->loopFrom = d->loopTo = 0;
        d->blend.restart(0, 0, 0, app.npcModels[d->model].actions);
    }
}
// Wait 0xFFFD (npc_autoctl_chk): only a scripted move keeps it waiting, not an animation
bool ViewerHost::animBusy(int id) {
    for (const auto& m : app.moves)
        if (m.npc == id) return true;
    for (const auto& r : app.rescales)
        if (r.npc == id) return true;
    return false;
}
// SetScale (frames 0) / AdjScale: an NPC's x y z scale (the player's, gac +0xd84 / prty_autoctl_set_scale,
// no script changes); one autoctl an NPC, so a rescale ends its move and a move its rescale
void ViewerHost::scaleNpc(int id, const float* s, int frames) {
    cr::FieldNpc* n = findNpc(app, id);
    if (!n || id == 0) return;
    app.rescales.erase(std::remove_if(app.rescales.begin(), app.rescales.end(), [&](const App::Rescale& o) { return o.npc == id; }),
                       app.rescales.end());
    if (frames <= 0) {
        for (int k = 0; k < 3; ++k) n->axes[k] = s[k];
        for (auto& d : app.npcDraws)
            if (d.id == id) d.world = npcWorld(*n);
        return;
    }
    app.moves.erase(std::remove_if(app.moves.begin(), app.moves.end(), [&](const App::Move& o) { return o.npc == id; }), app.moves.end());
    App::Rescale r;
    r.npc = id, r.frames = frames;
    for (int k = 0; k < 3; ++k) r.from[k] = n->axes[0] >= 0 ? n->axes[k] : n->scale, r.to[k] = s[k];
    app.rescales.push_back(r);
    if (stepRescale(app, app.rescales.back())) app.rescales.pop_back(); // (the first step this frame's)
}
// Autoadj (npc_autoctl_set / prty_autoctl_set): a straight move to (x, its y, z) at `speed` a frame -
// dist / speed frames, truncated (FPToFixed) - facing kept
void ViewerHost::autoMove(int id, float x, float z, float speed) {
    cr::Vec3 p;
    if (!actorPos(app, id, p) || speed <= 0) return;
    const float v[3] = {x, p.y, z};
    moveNpc(id, v, 0, Facing::Keep, int(std::hypot(p.x - x, p.z - z) / speed), 0);
}
// mdl_set_alpha: from -> to over `frames` (mdl_put's steps); 0 frames leaves the alpha as it is -
// either way the model is alpha-blended from now on (fadedMaterial)
void ViewerHost::fadeNpc(int id, float from, float to, int frames) {
    if (id == 0) { // the player's model (NpcFade 0: the field's slot 1), as mdl_set_alpha
        app.playerFaded = true;
        if (frames <= 0) { app.playerFadeLeft = 0; return; }
        app.playerAlphaFrom = from, app.playerAlphaTo = to, app.playerAlpha = from;
        app.playerFadeFrames = app.playerFadeLeft = frames;
        return;
    }
    bool lit = false;
    for (auto* d : npc(id)) {
        d->faded = true;
        if (frames <= 0) { d->fadeLeft = 0; continue; }
        d->alphaFrom = from; d->alphaTo = to; d->alpha = from;
        d->fadeFrames = d->fadeLeft = frames;
        if (d >= app.npcHeld.data() && d < app.npcHeld.data() + app.npcHeld.size()) d->lit = lit = true; // (+0xfb4 0 -> 1)
    }
    if (lit) buildNpcDraws(app);
}
void ViewerHost::jointNpc(int npc, int parent, int node) {
    if (parent < 1) { // back where its row puts it
        if (app.npcJoints.erase(npc)) buildNpcDraws(app);
    } else app.npcJoints[npc] = {parent, node};
}
void ViewerHost::showNpc(int id, bool shown) {
    app.npcShown[id] = shown;
    if (id == 0) return; // the player: drawn while showPlayer and not hidden here
    if (!shown) // (npc_kill: +0xfb4 = 0 - a fade that lit it stops drawing it too)
        for (auto* d : npc(id)) d->lit = false;
    // Born explicitly activates the actor at its current position, the origin if a script loaded
    // it (d0208's 425, d06's 421 and 467, no Setc): __EvtBorn clears its hide bit and no more.
    if (shown) app.placedNpcs.insert(id);
    buildNpcDraws(app);
}
void ViewerHost::screenFade(float grey, float from, float to, int frames, bool back) {
    if (back) { app.fadeBackGrey = grey; app.fadeBackFrom = from; app.fadeBackTo = to; app.fadeBackFrames = std::max(frames, 1); app.fadeBackT = 0; return; }
    app.fadeGrey = grey; app.fadeFrom = from; app.fadeTo = to; app.fadeFrames = std::max(frames, 1); app.fadeT = 0;
}
bool ViewerHost::screenFading(bool back) { return back ? app.fadeBackT < app.fadeBackFrames : app.fadeT < app.fadeFrames; }
void ViewerHost::warp(const cr::WarpRequest& w) {
    app.scriptAreaWarped = w.area >= 0 && w.area != app.area; // (the VM ends the script there)
    performWarp(app, w, app.runner.eventNo());
}
void ViewerHost::playSound(uint32_t id) {
    if ((id & 0x10000) && app.mix.voice > 0 && app.sound.hasVoices()) return; // (__effgo: a stand-in for a muted voice)
    const bool speech = (id & 0x400000) || app.sound.isVoice(int(id & 0xfff));
    const bool control = id & (0x20000000u | 0xe000000u | 0xe0000u);
    if (speech && !control && !app.battle.active) {
        app.sound.stopDialogueVoices();
        if (app.skipScene && !app.messageSkip) return; // (skipping - not while the scene bars it)
    }
    app.sound.play(id);
    app.soundLog.push_back(id);
}
void ViewerHost::playMusic(uint32_t v) { playBgm(app, v); app.soundLog.push_back(0x40000000u | v); }
int ViewerHost::random(int n) { return app.rng.get(n); } // (the scripts' rand(): _get_random, the one stream)
void ViewerHost::recoverPair() { // pcparam_hpmp_recovery: the scenario's pair
    for (int c : app.game.pairs[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))])
        if (c >= 0 && c < cr::kCharacters) { // (the most MP as it is now, a number - gear changed later
            app.game.party[size_t(c)].hp = -1; // doesn't move it)
            app.game.party[size_t(c)].mp = cr::memberStat(app.game, c, cr::kMp, app.itemTable);
        }
}
void ViewerHost::addLevels(int c, int n) { // pcparam_change_lv: to 255; the growth goes with it
    if (c < 0 || c >= cr::kCharacters) return;
    cr::PartyMember& m = app.game.party[size_t(c)];
    m.level = std::clamp(m.level + n, 1, cr::kMaxLevel);
    // (3.1.0 _pcparam_change_lv: the level-20-on growth only for the levels below 255, but the
    // 164..169 boosts for every level asked, past the cap too)
    if (n > 0) cr::levelUpBoosts(app.game, c, n);
}
void ViewerHost::treasureBoxesShown(bool on) { // Uty_TBox_Init: every box of the room and its lid (id + 1) -
    // 0 Kills them (not drawn, not solid, not examined); 1 Borns those killed whose create flags pass
    std::vector<int> ids;
    for (const cr::FieldNpc& n : app.npcs)
        if (n.treasure & 3) ids.push_back(n.id), ids.push_back(n.id + 1);
    for (int id : ids) {
        const cr::FieldNpc* n = findNpc(app, id);
        if (!n) continue;
        if (!on) app.npcShown[id] = false;
        else if (!npcActive(app, *n) && npcCreateFlags(app, *n)) app.npcShown[id] = true;
    }
    app.examineLinger.clear();
    buildNpcDraws(app);
}
void ViewerHost::saveEvent() { saveGame(app); } // SaveData_Save
bool ViewerHost::inBattle() { return app.battle.active; }
void ViewerHost::changeBattleStage(int stage) { // Uty_Chg_BtlFld: the stage model swapped mid-battle
    if (!app.battle.active || stage < 0 || stage >= cr::kNumBattleStages) return;
    app.battle.start.stage = stage;
    loadModelFile(app, cr::kBattleStageFile[stage], app.battle.stage, false);
    readMdlHdRules(app.battle.stage, stage);
}
void ViewerHost::messageSkip(bool on) { app.messageSkip = on; }
void ViewerHost::setLeader(int c) { // Uty_Chg_char: Set_Prtyno(c) when it isn't already the leader
    c = std::clamp(c, 0, 3);
    auto& lead = app.game.leaders[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))];
    if (lead == c) return;
    lead = c;
    app.playerChar = c;
    app.playerByReadMdl = true; // (Set_Prtyno: read_mdl)
    placePlayer(app);
}
int ViewerHost::omuItemValue() {
    int v = 0;
    for (int i = 521; i <= 540; ++i)
        if (const cr::ItemParam* ip = app.itemTable.item(i); ip && app.game.items.count(i) && app.game.items.at(i) > 0) v += ip->price;
    return v;
}
void ViewerHost::npcShadow(int npc, bool on) {
    if (cr::FieldNpc* n = findNpc(app, npc)) n->treasure = uint16_t(on ? n->treasure | 0x8000 : n->treasure & 0x7fff); // (+0x12 bit 15: npc_shodow's switch)
}
void ViewerHost::puzzleTeleportCount(int n) { app.padLock = n; } // Util 2046 (FUN_0002b41c): var 9 into gac + 0x1e2006, the pad lock
void ViewerHost::fireWall(int npc) { // Uty_FireWall_Init: walls 454 / 453 burn (effect 0x98 / 0x99, looping)
    // until their puzzle's clear flag (_PUZZLE_CLEAR_FLAG[wall * 4 + scenario]) is set
    static const int kClear[2][2] = {{1125, 1525}, {1124, 1524}};
    const int k = npc == 0x1c6 ? 0 : npc == 0x1c5 ? 1 : -1;
    cr::Vec3 p;
    if (k < 0 || app.game.flag(kClear[k][std::clamp(app.game.scenario, 0, 1)]) || !actorPos(app, npc, p)) return;
    const int h = startEffect(app, app.fieldEffects, 0x98 + k, p, true, 1.0f, 0.0f);
    if (h) // (Uty_FireWall_Init: eff3d_set_with_pos - facing the camera in a battle only)
        for (Effect& fx : app.fieldEffects)
            if (fx.handle == h) fx.faceCamera = app.battle.active;
}
int ViewerHost::bgmNo() { return app.bgmNo; }
void ViewerHost::endMenu(int kind) { // 2: the shop - SetShopBgmNoRecovery, __mc_SetMenuStat(7, 4)
    if (kind != 2 || !app.shop.open) return;
    app.shop.noBgmRecovery = true;
    closeShop(app);
}
int ViewerHost::pairNo() { return std::clamp(app.game.scenario, 0, cr::kScenarios - 1); } // gac + 0x8434c
void ViewerHost::nowLoading(bool on) { // NowLoadStart / NowLoadEnd
    if (on && !app.nowLoading.on) app.nowLoading = App::NowLoading{true, 0};
    else if (!on) app.nowLoading.on = false;
}
// _NpcSetDirConv (dir 2000, "face the way it moves"): turns = (1 - dz / len) / 4, and 1 - that
// when dx < 0 - not atan2: a 45-degree walk faces 26.4 degrees
float ViewerHost::travelYaw(float dx, float dz) {
    const float len = std::sqrt(dx * dx + dz * dz);
    float t = (1.0f - dz / len) * 0.25f;
    if (dx < 0) t = 1.0f - t;
    return t * 2.0f * 3.14159265f;
}
void ViewerHost::placeNpc(int id, const float* v, float yawDeg, Facing facing) {
    cr::Vec3 old;
    if (!actorPos(app, id, old)) return;
    const cr::Vec3 p{v[0], v[1], v[2]};
    float yaw = yawDeg * kDeg;
    if (facing == Facing::Travel) {
        if (std::fabs(p.x - old.x) + std::fabs(p.z - old.z) < 1e-4f) facing = Facing::Keep;
        else yaw = travelYaw(p.x - old.x, p.z - old.z);
    }
    if (id == 0) { setActor(app, id, p, facing == Facing::Keep ? nullptr : &yaw, false); return; } // (the leader:
    // FrmSetc2 leaves its y as the record's, FieldPCCtrl::checkHit's getFloor 1000 takes it to the floor)
    cr::Vec3 at = p; // __EvtFrmSetc2 / __EvtFrmSetc / __EvtSetc: _get_floor(x, y, z, 10) - y the floor's only
    float fy;        // when its KD node lists one within 20 of the record's y, else the record's own
    if (scriptFloor(app, at, 10.0f, fy)) at.y = fy;
    setActor(app, id, at, facing == Facing::Keep ? nullptr : &yaw, true);
}
void ViewerHost::moveNpc(int id, const float* v, float yawDeg, Facing facing, int frames, int mode) {
    App::Move m;
    if (!actorPos(app, id, m.from)) return;
    m.npc = id;
    m.to = {v[0], v[1], v[2]};
    m.mode = mode;
    m.frames = std::max(frames, 1);
    float yawNow = id == 0 ? app.walker.heading : findNpc(app, id)->angle;
    if (facing == Facing::Travel && std::fabs(m.to.x - m.from.x) + std::fabs(m.to.z - m.from.z) > 1e-4f) {
        const float yaw = travelYaw(m.to.x - m.from.x, m.to.z - m.from.z);
        setActor(app, id, m.from, &yaw, true);
    } else if (facing == Facing::Snap) { // (FrmAdjc2 mode 0: the record's angles set as it starts)
        const float yaw = yawDeg * kDeg;
        setActor(app, id, m.from, &yaw, true);
    } else if (facing == Facing::Yaw) {
        m.turn = true;
        m.yawFrom = yawNow;
        m.yawTo = yawDeg * kDeg;
    }
    app.moves.erase(std::remove_if(app.moves.begin(), app.moves.end(), [&](const App::Move& o) { return o.npc == id; }),
                    app.moves.end());
    app.rescales.erase(std::remove_if(app.rescales.begin(), app.rescales.end(), [&](const App::Rescale& o) { return o.npc == id; }),
                       app.rescales.end());
    app.moves.push_back(m);
    if (stepMove(app, app.moves.back())) app.moves.pop_back(); // (its first step is this frame's: npc_autoctl_prc runs after PrcEvnt)
}
void ViewerHost::tex2d(int slot, uint16_t f, int image, const int16_t (*extra)[3]) {
    if (SDL_getenv("CR_TEXDISP_DEBUG")) std::printf("tex2d: slot %d flags %04x image %d\n", slot, f, image);
    if (slot < 0 || slot >= 16) return;
    App::Portrait& p = app.portraits[slot];
    if ((f & 1) && image >= 0 && image < cr::kNumTex2dImages) { // read_img_set
        app.renderer.destroy(p.tex);
        p = App::Portrait();
        cr::Image img;
        // (3.1.0's __EvtTex2d: images 11 / 12 - MAPtitle, MAPtitle02 - come in the language's own
        // sheet, MAPtitle_<fr|de|es|cs|ct|kr>, for languages 2..7; the plain one otherwise)
        static const char* const kLangSuffix[6] = {"fr", "de", "es", "cs", "ct", "kr"};
        bool loaded = false;
        if ((image == 11 || image == 12) && app.language >= 2 && app.language <= 7) {
            std::string name = cr::kTex2dImage[image];
            const size_t dot = name.rfind('.');
            if (dot != std::string::npos) {
                name.insert(dot, std::string("_") + kLangSuffix[app.language - 2]);
                loaded = loadImage(app, name, img);
            }
        }
        // (Android 1.1.6 draws a full-screen card (0x8000) from the iPad's sheet - its 1024 x 768
        // picture fitted to the phone's width, centred: Part 1 11 s, the title)
        if (!loaded && (f & 0x8000) && app.renderer.androidScreen()) {
            std::string name = cr::kTex2dImage[image];
            const size_t dot = name.rfind('.');
            if (dot != std::string::npos) name.insert(dot, "_ipad");
            p.ipadCard = loaded = loadImage(app, name, img) && img.width >= 1024;
        }
        if (loaded || loadImage(app, cr::kTex2dImage[image], img)) p.tex = app.renderer.upload(img);
        p.image = image;
        // (0x8000 only sizes it - a full-screen card; a load alone shows nothing: tex2d_img_prc
        // draws a slot once 0x200 / 0x400 has turned it on - Uty_TexDisp draws loaded ones itself)
    }
    // __EvtTex2d: the flags stay in the slot (placement bits replaced); every animation is a
    // straight blend over a few 20 Hz frames (tex2d_img_prc)
    p.flags = uint16_t((p.flags & ~0x3000) | f);
    // (the iPad's tex2d frame: screen = 2 tex2d + (32, 64); __EvtTex2d homes 0x1000 at tex2d
    // (-16, 0) - flush left -, 0x2000 at (256, 0) - 32 px past the right edge -, 0x4000 at (176, 96).
    // On the iPhone (is_iPadDisplay 0; Android's path too) 0x1000 at (0, 0) and 0x2000 at (224, 0):
    // the phone layouts' screen = 2 tex2d + (32, 64) - the reference video's Part 1 298 s puts
    // Vieg's left portrait at tex2d (-80, 0) on the 1280 x 720 phone, (0 - ox, 0))
    const bool phone = app.renderer.phoneLayout();
    if (f & 0x1000) p.home = phone ? 32 : 0, p.y = phone ? 64 : 0, p.side = -1;
    if (f & 0x2000) p.home = phone ? 480 : 544, p.y = phone ? 64 : 0, p.side = 1; // drawn mirrored (+0xbd0e)
    if (f & 0x4000) {
        p.home = 384, p.y = 256, p.side = 0;
        if (f & 0x40) { p.scale = 2; p.zoomT = 0; p.zoomFrames = 4; } // 200% -> 100%
    }
    if (f & 0x7000) { p.x = p.xFrom = p.xTo = p.home; p.yFrom = p.yTo = p.y; p.frames = p.t = 0; }
    if (f & 0xC00) { // show: 3 frames of fade in and a 32 px slide in from its side; hide: a 32 px jump out
                     // at once (the slide's home moved to its start) and 3 frames of fade out; a show
                     // puts the grey level back to 255
        const bool show = f & 0x400;
        const float out = 32.0f * float(p.side);
        p.aFrom = show ? 0 : 1;
        p.aTo = show ? 1 : 0;
        p.xFrom = show ? p.home + out : p.home + out;
        p.xTo = show ? p.home : p.home + out;
        p.yFrom = p.yTo = p.y;
        p.x = p.xFrom;
        p.alpha = p.aFrom;
        if (show) p.shade = p.gFrom = p.gTo = 1, p.gT = p.gFrames = 0;
        p.frames = p.aFrames = 3;
        p.t = p.aT = 0;
    }
    if (f & 0x200) { // swap in (a new expression): shown at once in place; whatever else
                     // stands on that side goes
        if (p.side)
            for (int i = 0; i < 16; ++i) {
                App::Portrait& o = app.portraits[i];
                if (i != slot && o.tex.id && (o.flags & (p.side < 0 ? 0x1000 : 0x2000))) {
                    o.alpha = o.aFrom = o.aTo = 0;
                    o.frames = o.t = o.aFrames = o.aT = 0;
                }
            }
        p.x = p.xFrom = p.xTo = p.home;
        p.yFrom = p.yTo = p.y;
        p.alpha = p.aFrom = p.aTo = 1;
        p.frames = p.t = p.aFrames = p.aT = 0;
    }
    // The low byte's extra operands, 3 shorts each: 4 a fade (from, to /255, frames), 0x20 the
    // grey level the same way, 8 a slide (dx, dy, frames) - from the offset back to where it
    // stands; fewer than 0 frames: there at once, and staying
    if (f & 2) p.dying = true;
    if (f & 4) {
        p.aFrom = float(extra[0][0]) / 255.0f, p.aTo = float(extra[0][1]) / 255.0f;
        p.aFrames = std::max<int>(1, extra[0][2]), p.aT = 0;
        p.alpha = p.aFrom;
    }
    if (f & 0x20) {
        p.gFrom = float(extra[1][0]) / 255.0f, p.gTo = float(extra[1][1]) / 255.0f;
        p.gFrames = std::max<int>(1, extra[1][2]), p.gT = 0;
        p.shade = p.gFrom;
    }
    if (f & 8) {
        const float dx = extra[2][0], dy = extra[2][1];
        const int n = extra[2][2];
        if (n < 0) { p.xTo += dx, p.yTo += dy; p.home += dx; }
        p.xFrom = p.xTo + (n < 0 ? 0 : dx), p.yFrom = p.yTo + (n < 0 ? 0 : dy);
        p.x = p.xFrom, p.y = p.yFrom;
        p.frames = std::max(1, std::abs(n)), p.t = 0;
    }
    if (p.dying && p.t >= p.frames && p.aT >= p.aFrames) { app.renderer.destroy(p.tex); p = App::Portrait(); } // nothing to wait for
}
int ViewerHost::npcMode(int id, int word, int bit, int op) { // Uty_Mode
    int r = 0;
    for (cr::FieldNpc& n : app.npcs) {
        if (n.id != id) continue;
        uint16_t* w = word == 0 ? &n.kind : word == 2 ? &n.treasure : word == 3 ? &n.puzzle : nullptr;
        if (!w) continue;
        const uint16_t m = uint16_t(1u << (bit & 15));
        if (op == 1) *w |= m;
        else if (op == 2) *w &= uint16_t(~m);
        else r = (*w & m) ? 1 : 0;
    }
    if (op != 0) buildNpcDraws(app);
    return r;
}
void ViewerHost::loadParty(int pair, bool clearData) { // Uty_LoadPrty: prty_init_new(pair - 1, mode)
    if (pair >= 1 && pair <= 4) newPairParty(app, pair, clearData);
}
void ViewerHost::loadNpc(int id) {
    if (cr::FieldNpc* had = findNpc(app, id)) { // (+0x98: the standing push leaves it out)
        if (had->model >= 0) had->mdlRead = true;
        return;
    }
    cr::NpcRow row;
    if (!app.events.npcRow(id, row)) return;
    cr::FieldNpc n; // __EvtNpcMdlRead: at the origin, facing 0, and hidden (+0x8c bit 0) when its
    n.id = id;      // create flags fail or it's a story actor (id 400 on) - until a Born
    n.flagShow = row.flagShow;
    n.flagHide = row.flagHide;
    n.model = row.model;
    n.kind = row.kind;
    n.treasure = row.box;
    std::memcpy(n.shadow, row.shadow, sizeof n.shadow); // (npc_shodow reads the row the slot points at: +30..+36)
    n.puzzle = row.puzzle;
    n.scale = row.scale;
    n.mdlRead = n.model >= 0;
    app.npcShown[id] = id < 400 && npcCreateFlags(app, n);
    app.npcs.push_back(n);
    app.npcWarps.push_back(false);
    buildNpcDraws(app); // (its model read now: drawn, or held for the ops that act on it hidden)
}
void ViewerHost::freeCamera(bool on) { // __EvtCameraPlayer: the mode and the auto flags only - the eye stays where the last shot put it
    app.cam.free = on;
    if (!on) app.cam.t = app.cam.frames = 0;
}
void ViewerHost::setCamera(const float* eye, const float* target, int frames) {
    if (scrollingField(app)) { // a painted field: FrmAdjCam slides the painting to the shot's (x, y);
        // FrmSetCam on the field (flag word bit 64) takes the 3D path, which starts with SetScroll(0, 0)
        if (!app.cam.free) return;
        if (frames > 0) scriptFieldScroll(app, eye[0], eye[1], frames);
        else {
            app.scroll.offX = app.scroll.offY = 0;
            app.scroll.sliding = false;
        }
        return;
    }
    if (paintedField(app)) return; // shown whole: nothing moves
    const cr::Vec3 e{eye[0], eye[1], eye[2]}, t{target[0], target[1], target[2]};
    if (SDL_getenv("CR_OP_TRACE"))
        std::printf("  set camera eye (%.1f %.1f %.1f) target (%.1f %.1f %.1f) over %d\n", e.x, e.y, e.z, t.x, t.y, t.z, frames);
    if (std::fabs(e.x - t.x) + std::fabs(e.y - t.y) + std::fabs(e.z - t.z) < 1e-4f) return; // no target actor
    App::ScriptCam& c = app.cam;
    if (frames <= 0 || !c.set) { c.eye = e; c.target = t; c.frames = c.t = 0; }
    else { c.eyeFrom = c.eye; c.tgtFrom = c.target; c.eyeTo = e; c.tgtTo = t; c.frames = frames; c.t = 0; }
    c.set = true;
    stepScriptCam(app); // (the slide's first step is this frame's: updateCamera runs after PrcEvnt)
}
void ViewerHost::panCamera(const float* d, int frames) {
    if (scrollingField(app)) { // FrmPanCam's 2D path: twice the deltas on the iPad
        if (app.cam.free) { // (from GetScroll's value: past the painting's edge, as the script left it)
            const App::FieldScroll& fs = app.scroll;
            const float x = fs.scripted ? (fs.sliding ? fs.toX : fs.curX) : fs.offX;
            const float y = fs.scripted ? (fs.sliding ? fs.toY : fs.curY) : fs.offY;
            // (GetScroll's x is the painting's offset, as this scroll's is, but its y the camera's - + y
            // up -, this one's the other way: the opening's 0, 600 then 0, -600 over 300 frames goes up
            // to the sky and slowly back down to the player, as the reference video shows, and c101_t01's
            // -270 follows 409 across to 413 for their talk)
            scriptFieldScroll(app, x + 2 * d[0], y - 2 * d[1], std::max(frames, 1));
        }
        return;
    }
    if (paintedField(app)) return;
    App::ScriptCam& c = app.cam;
    if (!c.set) return;
    const cr::Vec3 dv{d[0], d[1], d[2]};
    c.eyeFrom = c.eye; c.tgtFrom = c.target;
    c.eyeTo = c.eye + dv; c.tgtTo = c.target + dv;
    c.frames = std::max(frames, 1); c.t = 0;
}
void ViewerHost::sceneEffectSet(int set) { loadSceneEffectSet(app, set); }
void ViewerHost::quake(int amp, int from, int to, int frames) { // EvtQuake: < 0 frames starts one held, else EndQuake
    ++app.scriptQuakes;
    if (frames < 0) startQuake(app.fieldQuake, float(amp), float(from), float(to), 1, true);
    else endQuake(app.fieldQuake, float(amp), float(from), float(to), frames);
    if (!app.battle.active) stepQuake(app.fieldQuake); // (UpdateQuake runs after the script in the same frame)
}
void ViewerHost::vitaLogo(int slot, int picture) { // read_img_set(the table at 0x811122b0 [picture])
    static const char* const kLogo[8] = {"sqex", "mve", "mclogo", "title1", "title2", "chara3", "chara1", "chara2"};
    const char* name = picture == 254 ? "warning" : picture >= 0 && picture < 8 ? kLogo[picture] : nullptr;
    App::VitaLogo& l = app.vitaLogo;
    const int k = std::clamp(std::abs(slot), 0, 1);
    app.renderer.destroy(l.tex[k]);
    cr::Image img;
    if (name && loadImage(app, std::string(name) + "_hd.mct", img)) l.tex[k] = app.renderer.upload(img);
    l.warning[k] = picture == 254;
}
void ViewerHost::vitaLogoShow(int slot, int in, int hold, int out) {
    App::VitaLogo& l = app.vitaLogo;
    l.slot = std::clamp(std::abs(slot), 0, 1), l.phase = 1, l.frame = 0, l.in = in, l.hold = hold, l.out = out, l.skip = false;
    if (!l.tex[l.slot].id) l.phase = 0; // (nothing read: nothing to show)
}
bool ViewerHost::vitaLogoBusy() { return app.vitaLogo.phase != 0; }
void ViewerHost::texDisp(const int32_t* g) {
    App::TexDisp& t = app.texDisp;
    t = App::TexDisp();
    t.on = true;
    for (int k = 0; k < 11; ++k) t.g[k] = g[k];
    ++app.texDispCalls;
    if (SDL_getenv("CR_TEXDISP_DEBUG"))
        std::printf("texdisp: slot %d (%s, image %d) x %d u %d v %d %dx%d fade %d/%d/%d\n", g[0],
                    g[0] >= 0 && g[0] < 16 && app.portraits[g[0]].tex.id ? "loaded" : "EMPTY", g[0] >= 0 && g[0] < 16 ? app.portraits[g[0]].image : -1,
                    g[1], g[3], g[4], g[5], g[6], g[7], g[8], g[9]);
}
void ViewerHost::tutoTexDisp(const int32_t* g) { // MsgUty_TexDispInit(var 1, ...)
    App::TutoTex& t = app.tutoTex;
    app.renderer.destroy(t.tex);
    t = App::TutoTex();
    // 3.1.0's table (0xd5230, 24 bytes a name): the puzzles' (0..7), the battles' (8..11, 13, 20 -
    // the deck's first-battle picture), the field's and the rest, then the Info Board's
    // versionup_e_2..7 / versionup_t_2..7 (22..33). MsgUty_TexDispInit counts var 1 from
    // telepo_01, or - var 2 set, the Info Board's four-push form - from versionup_e_2
    static const char* const kTuto[34] = {
        "telepo_01_hd.mct", "telepo_02_hd.mct", "psyco_01_hd.mct", "psyco_02_hd.mct", "tough_01_hd.mct",
        "tough_02_hd.mct",  "pyro_01_hd.mct",   "pyro_02_hd.mct",  "battle_01_hd.mct", "battle_02_hd.mct",
        "battle_03_hd.mct", "gene_01_hd.mct",   "puzzle_01_hd.mct", "battle_04_hd.mct", "puzzle_02_hd.mct",
        "encounter_hd.mct", "pheromone_hd.mct", "piupiucard_hd.mct", "collection_hd.mct", "labyrinth_hd.mct",
        "battle_start_hd.mct", "change_hd.mct", "versionup_e_2_hd.mct", "versionup_e_3_hd.mct", "versionup_e_4_hd.mct",
        "versionup_e_5_hd.mct", "versionup_e_6_hd.mct", "versionup_e_7_hd.mct", "versionup_t_2_hd.mct",
        "versionup_t_3_hd.mct", "versionup_t_4_hd.mct", "versionup_t_5_hd.mct", "versionup_t_6_hd.mct",
        "versionup_t_7_hd.mct"};
    const int pic = app.game.vars[1] + (app.game.vars[2] != 0 ? 22 : 0);
    cr::Image img;
    if (pic >= 0 && pic < 34) { // the iPad's _hd sheet, else the iPhone's
        std::string name = kTuto[pic];
        if (loadImage(app, name, img) || loadImage(app, name.replace(name.find("_hd"), 3, ""), img)) t.tex = app.renderer.upload(img);
    }
    t.on = true, t.state = 1;
    t.pages = g[1];
    for (int k = 0; k < 4; ++k) t.row[k] = g[2 + 2 * k], t.frames[k] = g[3 + 2 * k];
    t.x = float(g[10]), t.y = float(g[11]);
    app.game.setFlag(4075, false);
    ++app.tutoCalls;
}
void ViewerHost::exclamationMark() { app.exclamation = true; ++app.exclamationCalls; }
void ViewerHost::openWindow(int style, int lines) { // __EvtOpen -> _SetWin: fading in (at once for style 0x10), SE 0x97 but for 0x30
    app.msgStyle = style, app.msgLines = lines, app.heldPage = false;
    app.msgWin.state = style & 0x10 ? 3 : 1, app.msgWin.t = 0, app.msgWin.alpha = style & 0x10 ? 1.0f : 0.0f;
    if (!(style & 0x30)) app.sound.play(0x97);
    stepMsgWin(app); // (PrcWin runs after PrcEvnt in the original's frame; ours ran before the script)
}
void ViewerHost::closeWindow() { // __EvtClose -> _ClrWin: fading out (at once for 0x110), SE 0x98 but for 0x30
    app.heldPage = false; // Close: a page that ended itself goes with its window
    app.msgWin.state = app.msgStyle & 0x110 ? 6 : 4, app.msgWin.t = 0;
    if (app.msgWin.state == 6) app.msgWin.alpha = 0;
    if (!(app.msgStyle & 0x30)) app.sound.play(0x98);
    stepMsgWin(app);
}
bool ViewerHost::windowClosed() { return app.msgWin.state == 0 || app.msgWin.state == 6; }
void ViewerHost::toggleFieldHidden() { app.fieldHidden = !app.fieldHidden; }
bool ViewerHost::logoSeen() { return app.logoSeen; }       // Uty_ChkLogo_flg with var 0 = 0: read alone
void ViewerHost::setLogoSeen() { app.logoSeen = true; }    // ... with var 0 set: set alone (a battle's start sets it too)
void ViewerHost::padArt() { app.padArt = true; }
// Uty_Btl_ParamUp01 / 02: battle fighter 1 / 0's attack, defense, speed and luck up for 6 rounds
// (+0x8b..+0x8e = 6); outside a battle, for the next one
void ViewerHost::battleParamUp(int fighter) {
    // Uty_Btl_ParamUp01 / 02 set the fighter's +0x97 - the "doubled" byte the Ward skill (btlcalc_0435)
    // sets: attack, defense, speed and luck doubled for the rest of the battle, an Up then
    // +100 %; outside a battle it is nothing (prc's memset wipes it as a battle starts)
    if (fighter < 0 || fighter > 1 || !app.battle.active) return;
    app.battle.rules.f[size_t(fighter)].status[cr::kStatusWard] = 1;
}
void ViewerHost::puzzleStart() { app.puzzleBanner = App::PuzzleBanner(); app.puzzleBanner.on = true; } // Uty_PuzzleStart_Init
void ViewerHost::warpCrystalCamera(int npc) { // SetFreeCamera, then the camera task
    app.crystalCam = App::CrystalCam{npc, 0};
    app.cam.free = true;
}
void ViewerHost::sceneEffect(bool on, int id) { setSceneEffect(app, on, id); }
void ViewerHost::frameEffect(int actor, int effect, int scale, const float* v, bool angled) {
    ++app.frameEffectCalls;
    cr::Vec3 p;
    if (SDL_getenv("CR_OP_TRACE"))
        std::printf("  frame effect %d actor %d%s scale %d%s at (%.1f %.1f %.1f) angles (%.1f %.1f %.1f)\n", effect, actor, v ? "" : " (no record)", scale,
                    angled ? " angled" : "", v ? v[0] : 0.0f, v ? v[1] : 0.0f, v ? v[2] : 0.0f, v ? v[3] : 0.0f, v ? v[4] : 0.0f,
                    v ? v[5] : 0.0f);
    float pitch = 0, yaw = 0, roll = 0;
    bool withAngles = false; // (eff3d_set_with_pos_and_ang: FrmPlayEff2 at a record's point - an NPC's goes through eff3d_set)
    bool atRecord = false;
    if (!actorPos(app, actor, p)) { // not an NPC: where the scene's record has it
        atRecord = true;
        if (!v) return;
        p = {v[0], v[1], v[2]};
        // FrmPlayEff2 (eff3d_set_with_pos_and_ang) takes its angles too; in a battle the Y one
        // gives way to the camera (the camera-facing byte +0xfc7, set only with G+0x364 bit 0x20)
        if (angled) pitch = v[3] * kDeg, yaw = v[4] * kDeg, roll = v[5] * kDeg, withAngles = true;
    }
    const int h = startEffect(app, app.fieldEffects, effect, p, true, float(scale) / 100.0f, 0.0f);
    if (h)
        for (Effect& fx : app.fieldEffects)
            // (the camera-facing byte +0xfc7: eff3d_set / FrmPlayEff2 set it in a battle only (G+0x364
            // bit 0x20); FrmPlayEff at a record's point - 3.1.0 __EvtFrmPlayEff, decomp 21268 - always)
            if (fx.handle == h)
                fx.pitch = pitch, fx.yaw = yaw, fx.roll = roll, fx.faceCamera = app.battle.active || (atRecord && !angled),
                fx.keepDepthWrite = withAngles;
    if (h && app.frameEffects.size() < 16) app.frameEffects.push_back({(effect + 1) * (actor + 1), effect, h});
}
void ViewerHost::stopFrameEffect(int effect, int actor) {
    for (auto it = app.frameEffects.begin(); it != app.frameEffects.end();)
        if (actor < 0 ? (*it)[1] == effect : (*it)[0] == (effect + 1) * (actor + 1)) {
            stopEffect(app.fieldEffects, (*it)[2]);
            it = app.frameEffects.erase(it);
        } else ++it;
}
void ViewerHost::npcEffect(int effect, int npc, int dy) {
    ++app.scriptEffectCalls;
    cr::Vec3 p;
    if (!actorPos(app, npc, p)) return;
    const int h = startEffect(app, app.fieldEffects, effect, p + cr::Vec3{0, float(dy), 0}, false, 1.0f, 0.0f);
    if (h) // (Uty_eff3d_set_fld goes through eff3d_set_with_pos: the camera-facing byte in a battle only)
        for (Effect& fx : app.fieldEffects)
            if (fx.handle == h) fx.faceCamera = app.battle.active;
}
void ViewerHost::mapScroll(int npc, float x, float y, float rate) {
    ++app.scroll.mapScrolls;
    if (!scrollingField(app) || !app.cam.free) return; // setScroll2D / 3D only act with the camera free
    app.scroll.sliding = false;
    if (npc < 0) { scrollFieldTo(app, x, y, rate); return; }
    cr::Vec3 p;
    float vx, vy;
    if (actorPos(app, npc, p) && fieldViewPoint(app, p, vx, vy)) scrollFieldTo(app, vx, vy, rate);
}
void ViewerHost::lookAtNpc(int npc) { // SetFreeCamera, SetScroll3D(the npc's place, 1.0)
    app.cam.free = true;
    mapScroll(npc, 0, 0, 1.0f);
}
void ViewerHost::battle(int group, int stage, int flags) { // starts when this frame's script step ends
    app.battle.requested = true;
    app.lastFieldBgm = 0; // (__EvtBattle: G+0x8a97c = 0)
    app.battle.request = {group, stage, -1, 0};
    app.battle.requestFlags = flags;
}
bool ViewerHost::menu(int n) {
    if (n == 0) { // the title menu: the title alone until a tap, then the plates (the Progress Tracker's offer follows Start)
        app.titleOpen = true;
        app.titleCursor = 0;
        app.titleAnim = 0;
        app.titleStage = 0;
        return true;
    }
    if (n == 1) { openWorldSelect(app); return true; }
    if (n == 2) { openShop(app); return true; }
    if (n == 4) { openCharSelect(app); return true; } // the boot script: Character Select
    if (n == 7) { openStaffRoll(app, app.game.vars[0]); return true; } // ModeStaffRollMenu(var 0)
    if (n == 8) { // flag 0 clear: StartLastBtMenu (the rounds start over); else the pair choice
        if (!app.game.flag(0)) { app.lastBattle.count = app.lastBattle.mask = 0; return false; }
        openLastBattle(app);
        return true;
    }
    if (n == 5 || n == 6) { // SetSaveLoadMenu: at the title, the chosen pair's Load / New; else "Save data?"
        openSaveScreen(app, app.booting);
        // Util 6 off the title (SetModeSaveLoadMenu(3) - 4 with flag 0): "Save data?" on the way to the
        // title - saved or not, SaveLoadClr returns there (MuCom_ReturnTitle); No asks about leaving unsaved
        if (n == 6 && !app.booting) app.saveScreen.toTitle = true;
        return true;
    }
    static const char* const kMenu[] = {"title", "world choice", "shop", "yes/no", "start", "save/load", "save/load",
                                        "staff roll", "last battle"};
    app.unsupported.push_back(std::string("menu ") + (n >= 0 && n < 9 ? kMenu[n] : "?") + " on map " +
                              std::to_string(app.mapNumber));
    return false;
}
void ViewerHost::askYesNo(int flag) { app.choiceFlag = flag; app.choiceCursor = 0; app.choiceAnim = 0; }
bool ViewerHost::choiceOpen() {
    return app.choiceFlag >= 0 || app.worldChoiceOpen || app.titleOpen || app.saveScreen.open || app.charSelectOpen || app.staffRoll.open || app.lastBattle.open ||
           (app.shop.open && !app.shop.talkRunning); // (the shopkeeper's talk mustn't wait on the shop)
}
void ViewerHost::faceNpc(int id, int target) {
    cr::Vec3 a, b;
    if (!actorPos(app, id, a) || !actorPos(app, target, b)) return;
    if (std::fabs(b.x - a.x) + std::fabs(b.z - a.z) < 1e-4f) return;
    const float yaw = std::atan2(b.x - a.x, b.z - a.z);
    setActor(app, id, a, &yaw, true);
}
// menuf_mainPut in a puzzle room (G+0x35526 == 1), in tex2d units from the anchor (0, 0): a dark
// panel (0, 12)-(80, 48) (0xb8 black), the infinity mark (792, 58, 72 x 28) at (4, 16) when the
// count is unlimited, else "Left" (484, 184, 30 x 12) at (4, 24) and cmd_number_puzzle's two digits
// ((512 + 28 d, 58), 28 x 28, from x 24 - 44 for one - by 20, y 16; swelling 1.4 .. 1.1 for the 4
// frames after a change); with the help state on, the retry button (420, 184, 44 x 30) at (8, 56)
// - blinking (5 frames of 11) at 0 once the count was above it - and Help (984, 64, 40 x 32) at
// (84, 14); the third button (420, 216, 44 x 32) at (160, 14) while var 64 + stage is above 2 in a
// room not cleared (whatever the help state); the punishment mark (448, 120, 64 x 64) blinking over
// a refused touch and psychokinesis' arrows
void drawPuzzleHud(App& app, float a) {
    cr::MenuPainter& m = app.menu;
    const App::Puzzle& pz = app.puzzle;
    auto piece = [&](float x, float y, float u, float v, float w, float h, float scale = 1) {
        m.pieceTo(x + 16 + w * (1 - scale) / 2, y + 32 + h * (1 - scale) / 2, w * scale, h * scale, u, v, w, h, a);
    };
    const float dark[4] = {0, 0, 0, float(0xb8) / 255.0f * a};
    app.renderer.setOrigin(wideLeft(app)); // (widescreen: the panel in the screen's corner)
    // (Android's _menuf_mainPut: the phone's top left; the Vita's, its panel at x - 43.352966: vita decomp 64384-64430)
    m.setOffset(-androidEdgeX(app) - vitaEdgeX(app), -androidEdgeY(app));

    m.quad(16, 12 + 32, 80, 36, dark, dark);
    const int esp = pz.esp < 0 ? -1 : std::max(0, pz.esp - (pz.pkState == 2 && pz.pkMoved));
    if (esp < 0) piece(4, 16, 792, 58, 72, 28);
    else {
        static const float kSwell[5] = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f};
        const char d[3] = {char('0' + esp / 10 % 10), char('0' + esp % 10), 0};
        const float scale = kSwell[std::clamp(pz.pop, 0, 4)];
        float x = esp < 10 ? 44 : 24;
        for (int k = esp < 10 ? 1 : 0; k < 2; ++k, x += 20) piece(x, 16, 512 + 28.0f * float(d[k] - '0'), 58, 28, 28, scale);
        piece(4, 24, 484, 184, 30, 12);
    }
    if (pz.help && pz.blink < 5) piece(8, 56, 420, 184, 44, 30);
    if (pz.stage >= 0 && !app.game.flag(3000 + pz.stage)) { // the third button: var 64 + stage above 2, the room not cleared
        const auto it = app.game.vars.find(64 + pz.stage);
        if (it != app.game.vars.end() && it->second > 2) piece(160, 14, 420, 216, 44, 32);
    }
    if (pz.help) piece(84, 14, 984, 64, 40, 32);
    m.setOffset(0, 0);
    app.renderer.setOrigin(app.renderer.uiX()); // (the mark and the arrows: where on the field)
    // the psychokinesis arrows (0xe037c..: on, or fading - their colour's alpha byte), 24 x 24 at
    // their point less 12: left (432, 72), up (480, 48), right (456, 72), down (480, 72)
    static const float kArrowUV[4][2] = {{432, 72}, {480, 48}, {456, 72}, {480, 72}};
    for (int k = 0; k < 4; ++k) {
        const App::Puzzle::Arrow& ar = pz.arrows[k];
        if (ar.on != 1 && ar.timer <= 0) continue;
        const float aa = a * float(ar.alpha) / 255.0f;
        m.pieceTo(ar.x - 12 + 16, ar.y - 12 + 32, 24, 24, kArrowUV[k][0], kArrowUV[k][1], 24, 24, aa);
    }
    if (pz.punished > 0 && pz.punishPhase == 0) piece(pz.punishX, pz.punishY, 448, 120, 64, 64);
}

// menuf_mainPut's counters, a 20 Hz frame: the count's swell (set as it changes:
// SetFldMenu_puzzle_count), the retry button's blink at 0, the punishment mark's
void stepPuzzleHud(App& app) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0) return;
    const int esp = pz.esp < 0 ? -1 : std::max(0, pz.esp - (pz.pkState == 2 && pz.pkMoved));
    if (esp != pz.shownEsp) {
        if (pz.shownEsp != -1000) pz.pop = 4;
        pz.shownEsp = esp;
    }
    if (pz.pop > 0) --pz.pop;
    for (App::Puzzle::Arrow& ar : pz.arrows) // _ArrowOff's fade: the alpha byte down 0x50 (wrapping) / 0x33 a frame
        if (ar.timer > 0) {
            if (ar.mode == 2) ar.alpha = (ar.alpha - 0x50) & 0xff;
            else if (ar.mode == 1) ar.alpha = (ar.alpha - 0x33) & 0xff;
            --ar.timer;
        }
    if (esp > 0) pz.hadEsp = true;
    if (esp == 0 && pz.hadEsp) pz.blink = pz.blink + 1 < 11 ? pz.blink + 1 : 0;
    else pz.blink = 0;
    if (pz.punished > 0) {
        pz.punishPhase = (pz.punishPhase + 1) % 3;
        --pz.punished;
    }
}

// Uty_PuzzleStart_Put: (516, 264) 224 x 64 at (x1, y1), and (744, 264) - flag 1: (768, 900) - at
// (x2, y2), white at the banner's alpha, in front
void drawPuzzleBanner(App& app) {
    const App::PuzzleBanner& b = app.puzzleBanner;
    if (!b.on || b.alpha <= 0 || !app.windowTex.id) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font);
    r.setOrigin(r.uiX());
    const float a = float(b.alpha) / 255.0f;
    m.piece(b.x1 + 16, b.y1 + 32, 516, 264, 224, 64, a);
    const bool other = app.game.flag(1);
    m.piece(b.x2 + 16, b.y2 + 32, other ? 768 : 744, other ? 900 : 264, 224, 64, a);
}

// ---- The field menu (menuf_mainPut / menuf_mainPrc) ----
// A button in the top right corner opens a bar: Camp, Save, Map (maps below 64) and, once skill
// 170 is known (owned - it needn't be worn), the switch for random encounters (this pair's
// unencountflg; flag 4076 refuses it). Laid out in tex2d coordinates; menu units are those +
// (16, 32). The buttons slide 4 frames between the corner (460, -28) and their places.
namespace fieldui {
constexpr float kCorner[2] = {460, -28};
constexpr float kPlace[4][2] = {{432, 32}, {384, -24}, {312, -24}, {240, -24}}; // Camp, Save, Map, encounters
constexpr float kSize[4] = {56, 40, 40, 40};
// window_hd pieces: the corner button closed / open, then the buttons (encounters on / off)
constexpr float kCornerPiece[2][2] = {{432, 992}, {464, 992}};
constexpr float kPiece[4][2] = {{452, 516}, {468, 576}, {512, 388}, {468, 620}};
constexpr float kEncountersOff[2] = {468, 664};
constexpr int kEncounterSkill = 170;
} // namespace fieldui

int fieldRoute(const App& app) { return std::clamp(app.game.scenario, 0, cr::kScenarios - 1); } // (Omega: the scenario)

// 2.2.0's iPhone layout: the bar keeps to the screen's corner, (-16, +32) (its menuf_mainPut /
// Prc and kganMfMainButtonPosTbl)
// The corner button and the bar's offset from the iPad's places: the iPhone's (444, 4) corner; on
// Android (SetFldMenu, _menuf_mainPut / Prc) + ex, - ey - the phone's top right, taps with them
// The Vita's (its __menuf_mainPrc's table 0x8110b114): the corner (487, 4) and the bar's places, Camp (459, 64),
// Save (411, 8), Map (339, 8), encounters (267, 8) - the iPhone's 43 to the right; the corner's box at 0x1e3
float fieldDx(const App& app) { return app.renderer.phoneLayout() ? -16.0f + androidEdgeX(app) + vitaEdge43(app) : 0.0f; }
float fieldDy(const App& app) { return app.renderer.phoneLayout() ? 32.0f - androidEdgeY(app) : 0.0f; }

bool fieldMenuWanted(const App& app) { // the field, the player free to walk
    return app.mode == Mode::Maps && app.havePlayer && !app.titleOpen && !app.booting && !app.battle.active && !app.campOpen &&
           !app.shop.open && !app.saveScreen.open && !app.optionsOpen && !app.resumePrompt && !app.mapScreen.open;
}

bool fieldSceneRunning(const App& app) { // G+0x2c0 bits 0 / 3: a script or a message holds the field
    return app.runner.running() || !app.dialogue.empty() || app.itemWindowOpen || app.choiceFlag >= 0 || app.worldChoiceOpen;
}

bool fieldEncountersOff(const App& app) { return app.game.noEncounters[size_t(fieldRoute(app))] || app.game.flag(4076); }

// menuf_mainPut draws nothing while a script or a message holds the field (G+0x364 & 9) or the field
// is still loading (G+0x8a976 != 0 - here: a room's entry script yet to start), and draws at full
// alpha otherwise: its +0x20 a frame count starts at 0x140 (menuf_mainSet) and moves only while
// drawn, so the HUD goes the frame a script starts and comes back whole - no fade
// (and Android's _menuf_mainPut / Prc: nothing while isAchievementDraw)
bool fieldHudShown(const App& app) { return !fieldSceneRunning(app) && app.pendingEntry == App::Entry::None && !achievementBannerUp(app); }

void stepFieldMenu(App& app) {
    App::FieldMenu& f = app.fieldMenu;
    f.fade = fieldHudShown(app) ? 320 : 0;
    if (f.anim > 0) --f.anim;
}

// _MfMenu_RestFieldState (CampMainClr, OptionClr, SaveLoadClr): the bar shut at once - its buttons
// back at the corner -, the pad, the "!" button's state and the punishment mark cleared
void restFieldState(App& app) {
    app.fieldMenu.open = false;
    app.fieldMenu.anim = 0;
    app.touchPad = App::TouchPad();
    app.puzzle.punished = 0, app.puzzle.punishPhase = 0;
}

// A frame: the Camp, Options or the save screen just closed - their Clr's _MfMenu_RestFieldState
void trackFieldMenus(App& app) {
    const bool menus = app.campOpen || app.optionsOpen || app.saveScreen.open;
    if (app.hudMenusWere && !menus) restFieldState(app);
    app.hudMenusWere = menus;
}

// A button's top-left this frame (tex2d coordinates)
void fieldButtonPos(const App& app, int k, float& x, float& y) {
    using namespace fieldui;
    const App::FieldMenu& f = app.fieldMenu;
    const float t = float(4 - f.anim) / 4; // how far along this slide is
    const float from[2] = {f.open ? kCorner[0] : kPlace[k][0], f.open ? kCorner[1] : kPlace[k][1]};
    const float to[2] = {f.open ? kPlace[k][0] : kCorner[0], f.open ? kPlace[k][1] : kCorner[1]};
    x = from[0] + (to[0] - from[0]) * t + fieldDx(app);
    y = from[1] + (to[1] - from[1]) * t + fieldDy(app);
}

// __GetBattleDispCondision: skill 191 (Pheromones) known, the room's encounters table there
// (fldencount_chk) and flag 4076 clear - its button at the top left, (-8, -20) (the iPhone's
// (8, 12)), 48 x 32
bool fieldBattleButton(const App& app) {
    return app.game.items.count(191) && app.game.items.at(191) > 0 && app.encounters.active() && !app.game.flag(4076);
}

bool fieldButtonShown(const App& app, int k) {
    // Map: only in the worlds the map screen has - areas 2..5 and 8 (__menuf_mainPrc's mask 0x13c)
    if (k == 2) return app.area >= 0 && app.area < 9 && (0x13c >> app.area & 1);
    if (k == 3) return app.game.items.count(fieldui::kEncounterSkill) && app.game.items.at(fieldui::kEncounterSkill) > 0;
    return true;
}

// Uty_GetItem_Put: a black band (144 alpha, 18 less a fade step) across the screen at tex2d
// y = 32 x row - 32, 32 high (the iPad's x -16..496); the line centred on x 240 at y + 8 (255 alpha,
// 50 less a step), its icon (ComUty_GetIconPictInfo) 34 to its left at y + 1
void drawItemWindow(App& app) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font);
    const PhoneMenu phoneMenu(app, m);
    r.setOrigin(r.uiX());
    const float top = 32.0f * float(app.itemWindowRow) - 32;
    const float ba = std::max(0, 144 - 18 * app.itemWindowFade) / 255.0f, ta = std::max(0, 255 - 50 * app.itemWindowFade) / 255.0f;
    const float black[4] = {0, 0, 0, ba};
    const float left = app.renderer.phoneLayout() ? 0.0f : -16.0f;
    m.quad(left - r.uiX() / 2, top, 512 - 2 * left + r.uiX(), 32, black, black);
    constexpr float kSize = 16;
    // (Android 1.1.6 (0x5da30): x 240 - ceil((width - ox) / 2 / scale) - its centre ox / 2 right of
    // the screen's: Part 3 342.5 s and 783.0 s, "You got Key!" and "You got Billiken Clover!"
    // centred on x 720 of the 1280 x 720 phone)
    const float w = m.textWidth(app.itemWindowText, kSize), tx = 240 - w / 2 + androidEdgeX(app) / 2;
    if (app.itemWindowIcon >= 0)
        m.piece(tx - 34, top + 1, float(app.itemWindowIcon % 16) * 32, 248 + float(app.itemWindowIcon / 16) * 32, 32, 32, ta);
    m.text(app.itemWindowText, tx, top + 8, false, ta, kSize);
}



// menuf_mainPut, in its order: the low battery (drawn even while a script runs); then, the HUD shown,
// the battle (Pheromones) button, the touch pad, the corner button, the bar, the puzzle room's
// panel, arrows and punishment mark, the "!" button and the dungeon's key icons
void drawFieldMenu(App& app) {
    using namespace fieldui;
    const App::FieldMenu& f = app.fieldMenu;
    if (!app.windowTex.id) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font);
    const bool phone = app.renderer.phoneLayout();
    const float ex = androidEdgeX(app), ey = androidEdgeY(app); // (Android: the phone's corners)
    // (the Vita's __menuf_mainPut draws no low battery)
    if (const int u = app.renderer.vitaScreen() ? -1 : fieldLowBattery(); u >= 0) { // (Android: - ex, - ey)
        r.setOrigin(wideLeft(app));
        m.piece(phone ? 16.0f - ex : 0.0f, phone ? 32.0f - ey : 0.0f, float(u), 206, 21, 11, 1);
    }
    if (f.fade <= 0) { r.setOrigin(r.uiX()); return; }
    const float a = 1;
    if (fieldBattleButton(app)) { // __menuf_mainPut: window piece (656, 400) at tex2d (-8, -20) / (8, 12) (Android - ex, - ey)
        r.setOrigin(wideLeft(app));
        // (the Vita: at (8 - 43.352966, 12) - vita decomp 64277)
        m.piece(-8 + 16 + (phone ? 16.0f - ex - vitaEdgeX(app) : 0.0f), -20 + 32 + (phone ? 32.0f - ey : 0.0f), 656, 400, 48, 32, a);
    }
    drawTouchPad(app, a);
    r.setOrigin(wideRight(app)); // (widescreen: in the screen's corner)
    const bool bar = f.open || f.anim > 0; // the corner at tex2d (460, -28), the iPhone's (444, 4) (0x3ba6c / 0x3ba74)
    // (the Vita's: 445 + 43.352966 - its 30 x 30 inset of the piece - vita decomp 64314 / 64318)
    m.piece(460 + 16 + fieldDx(app) + vitaEdgeX(app) - vitaEdge43(app), -28 + 32 + fieldDy(app), kCornerPiece[bar][0], kCornerPiece[bar][1], 32, 32, a);
    for (int k = 0; bar && k < 4; ++k) {
        if (!fieldButtonShown(app, k)) continue;
        float x, y;
        fieldButtonPos(app, k, x, y);
        const float* src = k == 3 && fieldEncountersOff(app) ? kEncountersOff : kPiece[k];
        // (the Vita's menuf_mainPut: the button under the cursor in 0xffff00 - A B G R, cyan)
        const std::vector<int> e = fieldBarPad(app) ? fieldBarEntries(app) : std::vector<int>();
        const bool lit = f.open && !e.empty() && e[size_t(std::clamp(f.cursor, 0, int(e.size()) - 1))] == k;
        if (lit) m.piece(x + 16, y + 32, src[0], src[1], kSize[k], kSize[k], a, 0.0f, 1.0f, 1.0f);
        else m.piece(x + 16, y + 32, src[0], src[1], kSize[k], kSize[k], a);
    }
    r.setOrigin(r.uiX());
    if (app.puzzle.stage >= 0) drawPuzzleHud(app, a);
    drawEasyButton(app);
    r.setOrigin(wideLeft(app));
    for (const KeyIcon& k : fieldKeyIcons(app)) { // (Android: - ex, + ey - the phone's bottom left)
        const float c = k.lit ? 1.0f : float(0x40) / 255.0f;
        m.piece(k.x + 16 - ex, k.y + 32 + ey, k.u, k.v, 32, 32, 1, c, c, c);
    }
    r.setOrigin(r.uiX());
}



// The Vita's bar under the buttons (its menuf_mainPrc / Put): the entries in its order - the encounters'
// switch (once skill 170 is known), the map (its worlds), Save, Camp (_ComUty_GetBattryState's list,
// misnamed) - as this file's buttons (0 Camp, 1 Save, 2 Map, 3 encounters)
bool fieldBarPad(const App& app) { return app.renderer.vitaScreen(); }
std::vector<int> fieldBarEntries(const App& app) {
    std::vector<int> e;
    for (int k : {3, 2, 1, 0})
        if (fieldButtonShown(app, k)) e.push_back(k);
    return e;
}

void toggleFieldMenu(App& app) {
    App::FieldMenu& f = app.fieldMenu; // (__menuf_mainPrc: the corner button waits only for the bar's slide - not the pad lock)
    if (f.anim > 0) return;
    f.open = !f.open;
    f.anim = 4;
    if (f.open) f.cursor = int(fieldBarEntries(app).size()) - 1; // (the Vita's: the last - Camp - under the cursor)
    menuSound(app, f.open ? se::kCampOpen : se::kCampClose);
}

// The Vita's buttons on the bar (menuf_mainPrc): ← ↑ / → ↓ move the cursor round the entries (SE 0x92),
// ○ presses the one under it, ✕ shuts the bar; true when the bar took the button
bool fieldBarButton(App& app, int b, bool confirm, bool cancel) {
    App::FieldMenu& f = app.fieldMenu;
    if (!fieldBarPad(app) || !f.open || f.anim > 0 || !fieldMenuWanted(app) || fieldSceneRunning(app)) return false;
    const std::vector<int> e = fieldBarEntries(app);
    if (e.empty()) return false;
    const int step = b == SDL_GAMEPAD_BUTTON_DPAD_LEFT || b == SDL_GAMEPAD_BUTTON_DPAD_UP      ? -1
                     : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? 1
                                                                                                : 0;
    const int n = int(e.size());
    f.cursor = std::clamp(f.cursor, 0, n - 1);
    if (step) {
        const int was = f.cursor;
        f.cursor = (f.cursor + step + n) % n;
        if (f.cursor != was) menuSound(app, se::kCursor);
        return true;
    }
    if (confirm) { pressFieldButton(app, e[size_t(f.cursor)]); return true; }
    if (cancel) { toggleFieldMenu(app); return true; }
    return false;
}

// The bar's buttons (the bar open and still): 0 Camp, 1 Save, 2 Map, 3 encounters
void pressFieldButton(App& app, int k) {
    if (!app.fieldMenu.open || app.fieldMenu.anim > 0 || !fieldButtonShown(app, k)) return;
    if (k < 2 && app.padLock > 0) return; // (menuf_mainPrc: Camp and Save wait out the pad lock, gac + 0x1e2006)
    switch (k) {
    case 0:
        menuSound(app, se::kCampOpen);
        openCamp(app);
        break;
    case 1:
        menuSound(app, se::kChoose);
        app.game.vars[0] = 0;
        openSaveScreen(app, false);
        break;
    case 2: // SetMapMenu
        menuSound(app, se::kChoose);
        openMapScreen(app);
        break;
    case 3: { // the original's sounds: 3 switching off, 2 on, the buzzer while flag 4076 holds it
        if (app.game.flag(4076)) {
            menuSound(app, se::kBuzzer);
            break;
        }
        bool& off = app.game.noEncounters[size_t(fieldRoute(app))];
        off = !off;
        menuSound(app, off ? se::kBack : se::kChoose);
        break;
    }
    default: break;
    }
}

// A click in the frame's pixels; true when the field menu took it
bool fieldMenuClick(App& app, float px, float py) {
    if (!fieldMenuWanted(app) || fieldSceneRunning(app) || app.fieldMenu.fade <= 0) return false;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    const float tx = (lx - wideRight(app)) / 2 - 16 - fieldDx(app), ty = ly / 2 - 32 - fieldDy(app); // tex2d
    if (fieldBattleButton(app)) { // __menuf_mainPrc: the battle button's box, then fldencount_encount_debug
        const bool phone = app.renderer.phoneLayout();
        // (the Vita's box at (-35, 12): 8 - 43)
        const float x0 = phone ? 8.0f - androidEdgeX(app) - vitaEdge43(app) : -8.0f, y0 = phone ? 12.0f - androidEdgeY(app) : -20.0f;
        const float bx = (lx - wideLeft(app)) / 2 - 16, by = ly / 2 - 32;
        if (bx >= x0 && bx < x0 + 48 && by >= y0 && by < y0 + 32) {
            app.encounters.force();
            return true;
        }
    }
    if (tx >= 456 && tx < 496 && ty >= -32 && ty < 8) { // _pad_chk_trg_down_box(456, -32, 40, 40) (2.2.0: 440, 0)
        toggleFieldMenu(app);
        return true;
    }
    if (!app.fieldMenu.open || app.fieldMenu.anim > 0) return false;
    static const float kCentre[4][3] = {{460, 60, 28}, {404, -4, 20}, {332, -4, 20}, {260, -4, 20}};
    for (int k = 0; k < 4; ++k) {
        const float dx = tx - kCentre[k][0], dy = ty - kCentre[k][1];
        if (fieldButtonShown(app, k) && dx * dx + dy * dy <= kCentre[k][2] * kCentre[k][2]) {
            pressFieldButton(app, k);
            return true;
        }
    }
    return false;
}

// ---- The touch pad (CrossKey, FieldPCCtrl::operate) and the "!" button ----
// A finger put down on the field where nothing else takes it holds a pad there (CrossKey::update):
// its offset from that point steers the leader as the keys do, turned by the camera's heading -
// standing under 14.5 touch units (tex2d), walking under 29, running beyond. The pad shows once the
// finger has moved or after 5 frames: its base where the finger went down, its knob after the
// finger (menuf_mainPut): Omega's 32 x offset / max(|offset|, 48), a third of the way a frame (its
// atlases all have CR1 2.2.0's larger pad; CR1's iPad one, 24 x offset / max(|offset|, 72), half). With something in reach, the "!" button
// (FollowWindow::checkTouchEasyButton: within 24 of (420, 260), + (16, 32) on the iPad display) examines it, as
// a tap within 30 of its bubble does (FollowWindow::update).
namespace touchpad {
constexpr float kWalk2 = 210.25f, kRun2 = 841.0f; // squared offsets: walking, running
constexpr int kShowAfter = 5;
struct Art { float base[2], baseSize, knob[2], knobSize, reach, limit, ease; }; // window_hd, half scale
constexpr Art kIpad = {{928, 792}, 72, {928, 724}, 48, 24, 72, 0.5f};
constexpr Art kBig = {{928, 796}, 96, {944, 724}, 64, 32, 48, 1.0f / 3}; // Omega's (CR1 2.2.0's)
constexpr float kEasyPiece[2] = {464, 196};
constexpr float kEasySize = 48;
} // namespace touchpad

// The UI atlas's pad: Omega's sheets have the larger one (CR1 2.2.0's) - a texel of its ring
// (half-scale (1000, 850): the sheets are 1024 of those across) where CR1's iPad one is clear
void noteWindowAtlas(App& app, const cr::Image& wi) {
    const int lw = wi.logicalW ? wi.logicalW : wi.width, lh = wi.logicalH ? wi.logicalH : wi.height;
    if (lw < 1024 || lh < 1024 || wi.rgba.empty()) return;
    const int x = 1000 * wi.width / 1024, y = 850 * wi.height / 1024;
    app.bigPad = wi.rgba[(size_t(y) * size_t(wi.width) + size_t(x)) * 4 + 3] > 64;
}

const touchpad::Art& padArt(const App& app) { return app.bigPad ? touchpad::kBig : touchpad::kIpad; }

// The "!" button's centre (tex2d, from wideRight): the iPad display's, or the iPhone's in its layout
// (Android's _menuf_mainPut: + ex, + ey - the phone's bottom right; the tap follows it here)
void easyButtonCentre(const App& app, float& x, float& y) {
    // (the Vita's: (420 + 43.352966, 260) 48 x 48 - vita decomp 64544)
    x = app.renderer.phoneLayout() ? 444.0f + androidEdgeX(app) + vitaEdgeX(app) : 460.0f, y = app.renderer.phoneLayout() ? 284.0f + androidEdgeY(app) : 316.0f;
}

bool easyButtonShown(const App& app) {
    return fieldMenuWanted(app) && !fieldSceneRunning(app) && app.fieldMenu.fade > 0 && interactTarget(app) != nullptr;
}

// A finger down at frame pixel (px, py) on the field: the "!" button or bubble, else the pad.
// True when it took it.
bool touchPadDown(App& app, float px, float py) {
    if (!fieldMenuWanted(app) || fieldSceneRunning(app) || !app.havePlayer || app.puzzle.tpState != 0 ||
        app.puzzle.pkState != 0)
        return false;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    if (easyButtonShown(app)) {
        float ex, ey;
        easyButtonCentre(app, ex, ey);
        const float dx = (lx - wideRight(app)) / 2 - 16 - ex, dy = ly / 2 - 32 - ey;
        const float bx = (lx - app.promptX) / 2, by = (ly - app.promptY) / 2;
        if (dx * dx + dy * dy <= 24 * 24 || (app.promptX >= 0 && bx * bx + by * by <= 30 * 30)) {
            interact(app);
            return true;
        }
    }
    App::TouchPad& t = app.touchPad;
    t = App::TouchPad();
    t.down = true;
    t.ox = (lx - app.renderer.uiX()) / 2 - 16, t.oy = ly / 2 - 32;
    t.hold = touchpad::kShowAfter;
    return true;
}

void touchPadMove(App& app, float px, float py) {
    App::TouchPad& t = app.touchPad;
    if (!t.down) return;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    t.dx = (lx - app.renderer.uiX()) / 2 - 16 - t.ox, t.dy = ly / 2 - 32 - t.oy;
}

void touchPadUp(App& app) { // CheckTap state 2: a touch let go within 5 frames examines what the "!" stands over
    App::TouchPad& t = app.touchPad;
    const bool tap = t.down && t.hold > 0;
    t = App::TouchPad();
    if (tap && !fieldSceneRunning(app) && app.dialogue.empty() && interactTarget(app)) interact(app);
}

// The pad's stick for this step: false standing (or no finger)
bool touchPadStick(const App& app, float& sx, float& sy, bool& run) {
    const App::TouchPad& t = app.touchPad;
    const float d2 = t.dx * t.dx + t.dy * t.dy;
    if (!t.down || d2 < touchpad::kWalk2) return false;
    sx = t.dx, sy = t.dy, run = d2 >= touchpad::kRun2;
    return true;
}

// One 20 Hz frame: whether it shows (moved, or held 5 frames), the knob after the finger
void stepTouchPad(App& app) {
    App::TouchPad& t = app.touchPad;
    if (!t.down) return;
    const float d2 = t.dx * t.dx + t.dy * t.dy;
    if (--t.hold <= 0 || d2 > 1.0f) t.shown = true;
    const touchpad::Art& a = padArt(app);
    const float len = std::max(std::sqrt(d2), a.limit);
    t.knob[0] += (a.reach * t.dx / len - t.knob[0]) * a.ease;
    t.knob[1] += (a.reach * t.dy / len - t.knob[1]) * a.ease;
}

// The pad's base and knob (menuf_mainPut, after the battle button), in the menu painter begun by drawFieldMenu
void drawTouchPad(App& app, float alpha) {
    const App::TouchPad& t = app.touchPad;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    r.setOrigin(r.uiX());
    if (!t.down || !t.shown) return;
    const touchpad::Art& a = padArt(app);
    const float b = a.baseSize / 2, k = a.knobSize / 2;
    m.piece(t.ox - b + 16, t.oy - b + 32, a.base[0], a.base[1], a.baseSize, a.baseSize, alpha);
    m.piece(t.ox + t.knob[0] - k + 16, t.oy + t.knob[1] - k + 32, a.knob[0], a.knob[1], a.knobSize, a.knobSize, alpha);
}

// The "!" button (menuf_mainPut, after the puzzle's marks: the easy button's state on, no script)
void drawEasyButton(App& app) {
    if (!easyButtonShown(app)) return;
    cr::Renderer& r = app.renderer;
    r.setOrigin(wideRight(app)); // (widescreen: in the screen's corner)
    float ex, ey;
    easyButtonCentre(app, ex, ey);
    const float h = touchpad::kEasySize / 2;
    app.menu.piece(ex - h + 16, ey - h + 32, touchpad::kEasyPiece[0], touchpad::kEasyPiece[1], touchpad::kEasySize,
                   touchpad::kEasySize, 1);
    r.setOrigin(r.uiX());
}

// menuf_mainPut's dungeon key icons (its last draw): a world's keys while its door isn't open yet,
// 32 x 32 pieces in a row along the bottom left - white once the key's flag is set, 0xff404040 grey
// before. Maps 36..47: (128, 312) by flag 0x4b4 and (160, 312) by 0x4b5, none once 0x4b6; maps
// 50..54: (192, 312) by 0x4c8 / 0x4c9, none once 0x4cd; maps 55..66: (192, 312) by 0x4ca..0x4cc,
// none once 0x4ce. Every flag + 400 x the scenario. At tex2d (4 + 32k, 284) on the iPhone, the
// iPad's (-16, +32) from there.
std::vector<KeyIcon> fieldKeyIcons(const App& app) {
    std::vector<KeyIcon> out;
    const int scen = app.game.scenario * 400, map = app.mapNumber;
    const bool phone = app.renderer.phoneLayout();
    const float dx = phone ? 0.0f : -16.0f, dy = phone ? 0.0f : 32.0f;
    auto add = [&](int k, float u, int flag) { out.push_back({4 + 32.0f * float(k) + dx, 284 + dy, u, 312, app.game.flag(scen + flag)}); };
    if (map >= 36 && map < 48) {
        if (!app.game.flag(scen + 0x4b6)) add(0, 128, 0x4b4), add(1, 160, 0x4b5);
    } else if (map >= 50 && map < 55) {
        if (!app.game.flag(scen + 0x4cd)) add(0, 192, 0x4c8), add(1, 192, 0x4c9);
    } else if (map >= 55 && map < 67) {
        if (!app.game.flag(scen + 0x4ce)) add(0, 192, 0x4ca), add(1, 192, 0x4cb), add(2, 192, 0x4cc);
    }
    return out;
}

// _ComUty_GetBattryState: low under 18% (_MuCom_GetBatteryRest), not once above 22%, as it was between
bool lowBatteryStep(bool& low, int rest) {
    if (rest > 22) low = false;
    else if (rest < 18) low = true;
    return low;
}

// menuf_mainPut's first draw: the low battery (21 x 11, (393, 206) charging - _Get_BatteryState 1 -,
// else (372, 206)) at tex2d (-16, -32) on the iPad, (0, 0) on the iPhone; -1 none (no battery: SDL's
// power info, looked at once a second)
int fieldLowBattery() {
    static Uint64 at = 0;
    static int u = -1;
    static bool low = false;
    if (at && SDL_GetTicks() - at < 1000) return u;
    at = SDL_GetTicks();
    int pct = -1;
    const SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
    if (st != SDL_POWERSTATE_ON_BATTERY && st != SDL_POWERSTATE_CHARGING && st != SDL_POWERSTATE_CHARGED) return u = -1;
    if (pct < 0) return u = -1;
    u = lowBatteryStep(low, pct) ? (st == SDL_POWERSTATE_CHARGING ? 393 : 372) : -1;
    return u;
}


namespace mapui {
int field(int area) { // ReadMapMenu's _gnFieldNo; -1 no map
    switch (area) {
    case 2: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 3;
    case 8: return kMazeField;
    default: return -1;
    }
}
// Page 1's icon for a room: 19 its door open (the second flag: 1 always; 101..106 with
// 0x60e + n too) and the third flag clear, else 20 while the first is clear, else none
int doorIcon(const App& app, const short* ic) {
    const int door = ic[1];
    const bool open = door >= 0 && (door == 1 || ((door < 101 || door > 106 || app.game.flag(door + 0x60e)) && app.game.flag(door)));
    if (open && !app.game.flag(ic[2])) return 19;
    if (ic[0] >= 0 && !app.game.flag(ic[0])) return 20;
    return -1;
}
} // namespace mapui

void openMapScreen(App& app) {
    using namespace mapui;
    App::MapScreen& ms = app.mapScreen;
    const int fld = field(app.area);
    if (fld < 0) return;
    if (ms.field != fld || !ms.tex.id) { // ReadMapMenu
        if (ms.tex.id) app.renderer.destroy(ms.tex);
        cr::Image img;
        static const char* const kFile[7] = {"map_D01.mct", "map_D02.mct", "map_D03.mct", "map_D04.mct", "", "", "map_D07.mct"};
        if (loadImage(app, kFile[fld], img)) ms.tex = app.renderer.upload(img);
        ms.field = fld;
    }
    // (the original draws a map without a spot off the map's corner, at (-25, -21); the port leaves it out)
    const int map = std::clamp(app.mapNumber, 0, 149);
    const short* sp = kMapSpot[map];
    const bool spot = app.mapNumber != 151 && sp[0] >= 0;
    ms.spot[0] = spot ? sp[0] - 24 : -1;
    ms.spot[1] = spot ? sp[1] - 20 : -1;
    for (auto& m : ms.marks) m[0] = m[1] = -1;
    if (fld < 4) {
        const int base = kFlagBase + kFlagStride * fieldRoute(app);
        for (int step = 0; step < kSteps; ++step)
            if (app.game.flag(base + step)) {
                for (int k = 0; k < 2; ++k) {
                    const short* m = kMapMark[fld][step][k];
                    if (m[0] - 8 > -1 && m[1] - 16 > -1) ms.marks[k][0] = m[0] - 8, ms.marks[k][1] = m[1] - 16;
                }
                break;
            }
    }
    // SetVar 0..3: the boxes and doors found, the five trials passed + 25, the rest found, 12
    int found = 0, rest = 0, trials = 0;
    for (const auto& ic : kMazeIcon) {
        found += (ic[0] >= 0 && app.game.flag(ic[0])) + (ic[2] >= 0 && app.game.flag(ic[2]));
        rest += ic[3] >= 0 && app.game.flag(ic[3]);
    }
    for (const auto& v : kVerFlag) trials += app.game.flag(v[0]) && app.game.flag(v[1]);
    app.game.vars[0] = found, app.game.vars[1] = trials + 25, app.game.vars[2] = rest, app.game.vars[3] = 12;
    ms.open = true;
    ms.closing = false;
    ms.fade = 0;
    ms.blink = 0;
    ms.page = 0;
    ms.phase = 2, ms.count = 0, ms.alpha = 0;
    ms.clock = 0;
}

void closeMapScreen(App& app) {
    if (!app.mapScreen.open || app.mapScreen.closing) return;
    app.mapScreen.closing = true;
    app.mapScreen.alpha = 0, app.mapScreen.count = 0;
}

// __menuu_MapPrc's tap (_pad_chk_updw_box(0, 0, 480, 320)), once the window is in
void mapScreenTap(App& app) {
    App::MapScreen& ms = app.mapScreen;
    if (!ms.open || ms.closing || ms.fade < 4) return;
    menuSound(app, se::kChoose); // __effgo(0x93)
    if (ms.field == mapui::kMazeField && ms.page + 1 < 3) {
        ++ms.page;
        return;
    }
    closeMapScreen(app);
}

void stepMapScreen(App& app) {
    App::MapScreen& ms = app.mapScreen;
    if (!ms.open) return;
    ms.blink = (ms.blink + 1) % 12; // ComUty_PictAnimDraw: 2 frames of 6
    if (ms.closing) {
        if (--ms.fade <= 0) ms.open = false, ms.closing = false;
        return;
    }
    if (ms.fade < 4) {
        ++ms.fade;
        return;
    }
    // the pair's marker: unseen 3 frames, up by 51 for 6, held 11, down by 51 for 6
    switch (ms.phase) {
    case 0:
        ms.alpha = 0;
        if (ms.count >= 3) ms.phase = 1, ms.count = 0;
        break;
    case 1:
        if (ms.count < 6) ms.alpha += 51;
        else ms.alpha = 255, ms.phase = 2, ms.count = 0;
        break;
    case 2:
        ms.alpha = 255;
        if (ms.count >= 11) ms.phase = 3, ms.count = 0;
        break;
    default:
        if (ms.count < 6) ms.alpha -= 51;
        else ms.count = 0, ms.alpha = 0, ms.phase = 0;
        break;
    }
    ms.count = (ms.count + 1) & 0xff;
}

void drawMapScreen(App& app) {
    using namespace mapui;
    App::MapScreen& ms = app.mapScreen;
    if (!ms.open) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    const float a = float(ms.fade) / 4;
    // __menuu_MapPut: PutWin of window 0 - the menu's def at e1238 is the Camp's plain 64 x 64 window, which draws
    // nothing - then the sheet's picture 15 (its own dark panel) at (0, 0); no mask (MapSet / Prc set none). Part 10
    // (TnlQuNZ6qCY) 384 / 1376 s: the field a quarter as bright inside 160..1120 x 40..680 and untouched outside -
    // the port dimmed the whole screen under the panel too (inside a sixteenth)
    r.setOrigin(r.uiX());
    m.begin(r, app.windowTex, &app.font);
    if (!ms.tex.id) return;
    const PhoneMenu phoneMenu(app, m);
    const cr::Texture& t = ms.tex;
    outPicture(m, t, 15, 0, 0, a);
    if (ms.field == 2)
        for (int k = 0; k < 2; ++k)
            if (!app.game.flag(0x4b4 + k)) outPicture(m, t, 21, kD03Cover[k][0], kD03Cover[k][1] - 8, a);
    if (ms.field == 3)
        for (int k = 0; k < 5; ++k)
            if (!app.game.flag(0x4c8 + k)) outPicture(m, t, 21, kD04Cover[k][0], kD04Cover[k][1] - 8, a);
    if (ms.field == kMazeField) {
        size_t warp = 0;
        for (int room = 0; room < kRooms; ++room) {
            const short* ic = kMazeIcon[room];
            const float x = float(kMapSpot[kFirstRoom + room][0] - 8), y = float(kMapSpot[kFirstRoom + room][1] + 10);
            int pict = -1;
            if (ms.page == 0 && warp < std::size(kMazeWarp) && kMazeWarp[warp] == room) pict = 18, ++warp;
            if (ms.page == 1) pict = doorIcon(app, ic);
            if (ms.page == 2 && ic[3] >= 0 && !app.game.flag(ic[3])) pict = 21;
            if (pict >= 0) outPicture(m, t, pict, x, y, a);
        }
        if (ms.page > 0) { // the legend: picture 19 / 21 at (316, 296), its line at (336, 296)
            outPicture(m, t, ms.page == 1 ? 19 : 21, 316, 296, a);
            std::vector<std::vector<std::string>> pages;
            if (app.systemText.message(kText[ms.page - 1], app.language,
                                       [&](int n) { auto it = app.game.vars.find(n); return it == app.game.vars.end() ? 0 : int(it->second); },
                                       pages) &&
                !pages.empty() && !pages[0].empty())
                m.text(pages[0][0], 336, 296, false, a);
        }
    }
    const int frame = ms.blink / 6;
    for (const auto& mk : ms.marks)
        if (mk[0] >= 0) outPicture(m, t, 17, float(mk[0]), float(mk[1]), a, frame);
    if (ms.spot[0] >= 0) outPicture(m, t, 16, float(ms.spot[0]), float(ms.spot[1]), a * float(ms.alpha) / 255, frame);
}

// One 20 Hz frame of the running script and of the things it set going.
// Voiced builds: a page's clips start as the typing reaches their 0x1e (stepVoiceCue); a page turn
// cuts the one still playing unless 0x1e 0xffff kept it, and so does the closing tap.
void voicePageSync(App& app) {
    const int page = int(std::min(app.dialoguePage, app.dialogue.size() - 1));
    if (app.dialogueSerial == app.voicedSerial && page == app.voicedPage) return;
    app.voicedSerial = app.dialogueSerial;
    app.voicedPage = page;
    app.voiceCue = 0;
    if (!app.voiceKeep) app.sound.stopLine(); // an unvoiced page replaces the previous page too (a kept clip goes on)
    app.voiceKeep = false;
}
void updateVoices(App& app) {
    if (app.dialogue.empty()) {
        app.voiceCue = 0; // a closed/replaced scene cannot restart its remaining clips
        if (app.voicedPage >= 0 && app.sound.hasVoices()) { // PrcMsg: the closing tap cuts the clip (unless 0x1e 0xffff kept it)
            if (!app.voiceKeep) app.sound.stopLine();
            app.voiceKeep = false, app.voicedPage = -1;
        }
        return;
    }
    if (!app.sound.hasVoices()) return;
    voicePageSync(app);
}
// _PrcMsg's 0x1e, reached by the typing: 0xffff marks the page's clip kept over the next page turn;
// any other, while a field clip still plays (this page's last, or one kept from the page before:
// FUN_00009408), holds the typing there, and starts once it is quiet. True: holding
bool stepVoiceCue(App& app) {
    if (app.dialogue.empty() || !app.sound.hasVoices()) return false;
    voicePageSync(app);
    const size_t page = std::min(app.dialoguePage, app.dialogue.size() - 1);
    if (page >= app.dialogueVoices.size()) return false;
    const std::vector<App::VoiceCue>& cues = app.dialogueVoices[page];
    if (app.voiceCue >= cues.size() || cues[app.voiceCue].at > app.dialogueTyped) return false;
    const int clip = cues[app.voiceCue].clip;
    if (clip < 0) { app.voiceKeep = true; ++app.voiceCue; return false; }
    if (app.sound.linePlaying()) return true;
    app.sound.playLine(clip);
    ++app.voicesPlayed;
    ++app.voiceCue;
    return false;
}

// ---- Puzzle rooms (PuzzleScene, PuzzleManager, the touch abilities, the PuzzleObjects) ----
// See game/puzzle.hpp. The room's NPCs with NPC table +20 bits sit on the stage's grid; the
// pair's touch ability acts on them (teleport: tap a block or gem anywhere on the grid and swap
// places with it, one ESP each). Gems are picked up by walking onto them (+1 ESP); the gem
// crystal (WarpCrystalNG) comes when none is left, the switch crystal (WarpCrystalWS) while every
// weigh switch is pressed: the first time its NPC's script plays (flag 4000, var 1 = its event),
// which marks the room (flag 3300 + stage) - afterwards it shows. A cleared room (flag 3000 +
// stage) keeps its crystals and loses its blocks and gems. Field effects (eff3d_set_fld 2..9)
// aren't drawn yet.

// eff3d_set_fld: effect `id` where an NPC (index) or the leader (-1) stands; a looped one runs
// until stopped by its handle
int fieldEffect(App& app, int id, cr::Vec3 at, bool loop = false) {
    return startEffect(app, app.fieldEffects, id, at, loop, 1.0f, 0.0f);
}
int npcEffect(App& app, int id, int npc, bool loop = false) {
    if (npc < 0) return fieldEffect(app, id, app.walker.pos, loop);
    if (npc >= int(app.npcs.size())) return 0;
    const cr::FieldNpc& n = app.npcs[size_t(npc)];
    return fieldEffect(app, id, {n.pos[0], n.pos[1], n.pos[2]}, loop);
}

// A world point on the frame (logical 1024 x 768 pixels, the whole screen's width), as drawFrame
// lays out the stage view (false when it's off the view or behind the camera)
void shownScroll(const App& app, float& x, float& y);
bool fieldToLogical(const App& app, cr::Vec3 p, float& lx, float& ly) {
    const float SW = app.renderer.screenW(), H = cr::Renderer::kLogicalH;
    float vx, vy, vw, vh;
    if (scrollingField(app)) { // (as drawFrame places the view: the scroll as shown, the quake's offset)
        const float fz = fieldFillZoom(app);
        vw = fieldViewW(app) * fz, vh = fieldViewH(app) * fz;
        float sx, sy;
        shownScroll(app, sx, sy);
        vx = (SW - vw) / 2 + sx * fz, vy = (H - vh) / 2 - sy * fz;
    } else {
        vw = vh = std::min(SW, H);
        vx = (SW - vw) / 2, vy = (H - vh) / 2;
    }
    vx += std::trunc(vx + app.fieldQuake.off[0]) - std::trunc(vx), vy += std::trunc(vy + app.fieldQuake.off[1]) - std::trunc(vy);
    float hx, hy;
    if (!fieldViewPoint(app, p, hx, hy)) return false;
    lx = vx + hx * vw / fieldViewW(app), ly = vy + hy * vh / fieldViewH(app);
    // The view is drawn in the paintings' layer 3 - on the Vita's screen larger than the 2D's and
    // 12 px higher - and taps, the punishment mark and the arrows are in layer 0: the point in
    // layer 0's units (as the "!" prompt's; the same point in the other layouts)
    cr::Renderer& r = const_cast<cr::Renderer&>(app.renderer);
    const int keep = r.layer();
    r.setLayer(3), r.layoutToScreen(lx, ly), r.setLayer(0), r.screenToLayout(lx, ly), r.setLayer(keep);
    return true;
}
// Logical pixels <-> tex2d (the 480 x 320 layout: menu units less (16, 32), half the pixels)
void logicalToTex2d(const App& app, float lx, float ly, float& tx, float& ty) {
    tx = (lx - app.renderer.uiX()) / 2 - 16, ty = ly / 2 - 32;
}

namespace puzzle {

const cr::PuzzleStage* stage(const App& app) {
    return app.puzzle.stage >= 0 ? &cr::kPuzzleStage[app.puzzle.stage] : nullptr;
}

bool cleared(const App& app) { return app.puzzle.stage >= 0 && app.game.flag(3000 + app.puzzle.stage); }

int* cellSlot(App& app, int layer, int r, int c) {
    const cr::PuzzleStage* st = stage(app);
    if (!st || layer < 0 || layer > 2 || r < 0 || c < 0 || r >= st->rows || c >= st->cols) return nullptr;
    return &app.puzzle.grid[size_t((layer * st->rows + r) * st->cols + c)];
}

int objectAt(App& app, int layer, int r, int c) {
    const int* slot = cellSlot(app, layer, r, c);
    return slot ? *slot : -1;
}

cr::Vec3 centre(const App& app, int r, int c) { // getCenterPosAtCell
    const cr::PuzzleStage* st = stage(app);
    if (!st) return {};
    return {st->x0 + cr::kPuzzleCell * (0.5f + float(c)), st->y, st->z0 + cr::kPuzzleCell * (0.5f + float(r))};
}

bool cellOf(const App& app, cr::Vec3 p, int& r, int& c) { // getCellNo
    const cr::PuzzleStage* st = stage(app);
    if (!st) return false;
    const float fr = (p.z - st->z0) / cr::kPuzzleCell, fc = (p.x - st->x0) / cr::kPuzzleCell;
    if (fr < 0 || fc < 0) return false;
    r = int(fr), c = int(fc);
    return r < st->rows && c < st->cols;
}

App::NpcDraw* drawOf(App& app, int npc) {
    for (App::NpcDraw& d : app.npcDraws)
        if (d.index == npc) return &d;
    return nullptr;
}

void fade(App& app, int npc, float from, float to, int frames) { // mdl_set_alpha
    if (App::NpcDraw* d = drawOf(app, npc)) {
        d->faded = true;
        d->alphaFrom = from, d->alphaTo = to, d->alpha = from;
        d->fadeFrames = d->fadeLeft = std::max(frames, 1);
    }
}

void show(App& app, App::Puzzle::Obj& o, bool on) { // npc_born / npc_kill
    if (o.shown == on) return;
    o.shown = on;
    buildNpcDraws(app);
}

void place(App& app, App::Puzzle::Obj& o, cr::Vec3 p) { // PuzzleObject::setPos
    if (o.npc < 0 || o.npc >= int(app.npcs.size())) return;
    cr::FieldNpc& n = app.npcs[size_t(o.npc)];
    n.pos[0] = p.x, n.pos[1] = p.y, n.pos[2] = p.z;
    if (App::NpcDraw* d = drawOf(app, o.npc)) d->world = npcWorld(n);
}

// A weigh switch below a cell: pressed (5, `silent` at the start) or let go (6)
void press(App& app, int r, int c, bool on, bool silent = false) {
    const int s = objectAt(app, 0, r, c);
    if (s < 0) return;
    App::Puzzle::Obj& sw = app.puzzle.objs[size_t(s)];
    if (sw.type != cr::kTypeWeighSwitch) return;
    if (on && !silent) app.sound.play(0x131);
    sw.on = on;
}

bool switchAt(App& app, int r, int c) {
    const int s = objectAt(app, 0, r, c);
    return s >= 0 && app.puzzle.objs[size_t(s)].type == cr::kTypeWeighSwitch;
}

bool allSwitchesOn(App& app) { // IsOnAllSwitch
    for (const App::Puzzle::Obj& o : app.puzzle.objs)
        if (o.type == cr::kTypeWeighSwitch && !o.on) return false;
    return true;
}

bool gemsLeft(App& app) {
    for (const App::Puzzle::Obj& o : app.puzzle.objs)
        if (o.type == cr::kTypeGem && o.state != 0) return true;
    return false;
}

constexpr float kGhostAlpha = 0x60 / 255.0f;

// PuzzleManager::init (PuzzleScene::init, and again on a reset): the objects from the room's NPCs
void enter(App& app) {
    App::Puzzle& pz = app.puzzle;
    pz = App::Puzzle();
    app.fieldEffects.clear(); // the objects' effects go with them
    pz.stage = cr::puzzleStage(app.area, app.mapNumber);
    const cr::PuzzleStage* st = stage(app);
    if (!st) { pz.stage = -1; return; }
    pz.leaderWas = app.walker.pos;
    // _MfMain_ClrPzlData: the count 0, Retry / Help shown; a cleared room (PuzzleManager::init) goes
    // no further - MfMain_SetPzlHelpState(0), no objects: its NPCs stay as the map has them
    pz.esp = 0;
    if (cleared(app)) {
        pz.help = false;
        buildNpcDraws(app);
        return;
    }
    pz.esp = st->esp;
    app.game.setFlag(3300 + pz.stage, false);
    app.game.vars[13] = 0; // the uses
    pz.grid.assign(size_t(3 * st->rows * st->cols), -1);
    for (size_t i = 0; i < app.npcs.size(); ++i) {
        const int type = cr::puzzleType(app.npcs[i].puzzle, st->ability);
        if (!type) continue;
        App::Puzzle::Obj o;
        o.npc = int(i);
        o.type = type;
        o.layer = cr::puzzleLayer(type);
        const cr::Vec3 p{app.npcs[i].pos[0], app.npcs[i].pos[1], app.npcs[i].pos[2]};
        const bool inGrid = cellOf(app, p, o.row, o.col);
        o.shown = npcActive(app, app.npcs[i]);
        o.baseY = app.npcs[i].pos[1];
        if ((type == cr::kTypeNormal && st->ability != cr::kAbilityPsychokinesis) || type == cr::kTypeGem) { // their init: npc_born
            o.shown = true;
            o.state = 1;
        } // (NormalBlockPK's init / postInit are PuzzleObject's - empty: as the map has it, no switch pressed)
        if (type == cr::kTypeCrystal) o.shown = false;
        if (type == cr::kTypeFragile || type == cr::kTypeRed) o.shown = true; // npc_born
        if (type == cr::kTypeBlue) o.shown = false, o.ghost = true; // npc_kill, alpha 0x60
        if (type == cr::kTypeCandle) { // lit at the start (+22)
            o.shown = app.npcs[i].link[0] != 0;
            o.ghost = !o.shown;
        }
        pz.objs.push_back(o);
        if (inGrid && type != cr::kTypeCrystal)
            if (int* slot = cellSlot(app, o.layer, o.row, o.col)) *slot = int(pz.objs.size() - 1);
    }
    for (App::Puzzle::Obj& o : pz.objs) // InitSwitchOn (NormalBlockTP's): a block standing on a switch presses it
        if (o.type == cr::kTypeNormal && st->ability != cr::kAbilityPsychokinesis && o.state == 1 && switchAt(app, o.row, o.col)) {
            o.onSwitch = true;
            press(app, o.row, o.col, true, true);
            cr::Vec3 p = centre(app, o.row, o.col);
            p.y = app.npcs[size_t(o.npc)].pos[1] + cr::kPuzzleSwitchRise;
            place(app, o, p);
        }
    for (App::Puzzle::Obj& o : pz.objs) // WarpCrystalWS::postInit
        if (o.type == cr::kTypeCrystal && (app.npcs[size_t(o.npc)].puzzle & cr::kPuzzleObject40))
            o.shown = allSwitchesOn(app);
    buildNpcDraws(app);
    for (const App::Puzzle::Obj& o : pz.objs)
        if (o.ghost) fade(app, o.npc, kGhostAlpha, kGhostAlpha, 1);
}

void stepBlock(App& app, App::Puzzle::Obj& o) { // NormalBlockTP / NormalGem (the teleported side)
    switch (o.state) {
    case 1: // onIdle: a teleport asked for - fade out
        if (o.type == cr::kTypeGem && o.pending != 8) { // picked up by walking onto it
            const cr::Vec3 c = centre(app, o.row, o.col);
            const cr::Vec3 p = app.walker.pos;
            if (std::fabs(p.x - c.x) < cr::kPuzzleCell * 0.5f && std::fabs(p.z - c.z) < cr::kPuzzleCell * 0.5f) {
                npcEffect(app, 0x88, o.npc); // (NormalGem::onIdle: 0x88 alone as it is picked up)
                show(app, o, false);
                app.sound.play(0x130);
                if (int* slot = cellSlot(app, o.layer, o.row, o.col); slot && *slot >= 0 && &app.puzzle.objs[size_t(*slot)] == &o)
                    *slot = -1;
                if (app.puzzle.esp >= 0) ++app.puzzle.esp; // incESPCount
                o.state = 0;
                return;
            }
        }
        if (o.pending == 8) {
            npcEffect(app, 0x89, o.npc); // NormalBlockTP::onIdle / NormalGem::onIdle: the going-away effect
            fade(app, o.npc, 1, 0, o.outFrames);
            if (o.onSwitch) press(app, o.row, o.col, false);
            o.state = 2;
            o.pending = 0;
        }
        break;
    case 2: // onDisappear: then at its new cell
        if (--o.waitOut > 0) break;
        {
            const bool sw = switchAt(app, o.toRow, o.toCol);
            if (int* from = cellSlot(app, o.layer, o.row, o.col); from && *from >= 0 && &app.puzzle.objs[size_t(*from)] == &o) *from = -1;
            const int self = int(&o - app.puzzle.objs.data());
            o.row = o.toRow, o.col = o.toCol;
            if (int* to = cellSlot(app, o.layer, o.row, o.col)) *to = self; // replacePuzzleObject
            o.onSwitch = o.type == cr::kTypeNormal && sw;
            cr::Vec3 p = centre(app, o.row, o.col);
            p.y = cr::kPuzzleStage[app.puzzle.stage].y; // the room's height, raised on a switch
            if (sw) p.y += cr::kPuzzleSwitchRise;
            place(app, o, p);
            npcEffect(app, 0x8a, o.npc); // onDisappear (the block's and the gem's)
            fade(app, o.npc, 0, 1, o.inFrames);
            o.state = 3;
        }
        break;
    case 3: // onAppear
        if (--o.waitIn > 0) break;
        if (o.onSwitch) press(app, o.row, o.col, true);
        o.state = 1;
        break;
    default: break;
    }
}

// The warp crystals: the gem one (WarpCrystalNG) once the gems are gone, the switch one
// (WarpCrystalWS) while every switch is pressed; the first time the crystal's NPC script plays
bool allCandlesLit(App& app);

void stepCrystal(App& app, App::Puzzle::Obj& o) {
    const unsigned bits = app.npcs[size_t(o.npc)].puzzle;
    bool ready = false;
    if (bits & cr::kPuzzleObject100) ready = !gemsLeft(app);
    else if (bits & cr::kPuzzleObject40) {
        ready = allSwitchesOn(app);
        if (!ready && o.shown) { // a switch let go: it goes again
            npcEffect(app, 0x89, o.npc);
            fade(app, o.npc, 1, 0, 4);
            show(app, o, false);
            app.sound.play(0x132);
            return;
        }
    } else if (bits & cr::kPuzzleObject800) {
        ready = allCandlesLit(app);
        if (!ready && o.shown) { // one gone out: it goes again
            npcEffect(app, 0x89, o.npc);
            fade(app, o.npc, 1, 0, 4);
            show(app, o, false);
            app.sound.play(0x132);
            return;
        }
    } else return;
    if (!ready || o.shown) return;
    if (!app.game.flag(3300 + app.puzzle.stage)) { // the scene
        if (app.runner.running()) return;
        app.game.setFlag(4000, true);
        const int evt = app.npcs[size_t(o.npc)].id;
        app.game.vars[1] = evt;
        app.runner.start(app.events, evt, app.game, app.area, app.mapNumber);
        app.runnerWhat = "puzzle crystal";
        return;
    }
    show(app, o, true);
    npcEffect(app, 0x8a, o.npc);
    fade(app, o.npc, 0, 1, 4);
    app.sound.play(0x133);
}

void punish(App& app, int t);

// Teleport::onCheckTap's choice made: swap places with object `t`
void teleportTo(App& app, int t) {
    App::Puzzle& pz = app.puzzle;
    if (pz.busy || pz.tpState || t < 0 || t >= int(pz.objs.size())) return;
    App::Puzzle::Obj& o = pz.objs[size_t(t)];
    if ((o.type != cr::kTypeNormal && o.type != cr::kTypeGem) || o.state != 1) return;
    int pr, pc;
    if (!cellOf(app, app.walker.pos, pr, pc)) return;
    if (pz.esp == 0) { punish(app, t); return; } // no ESP left - only the mark
    pz.busy = true;
    ++app.game.vars[13]; // decESPCount: var 13 counts the uses; the count stops at 0
    if (pz.esp > 0) --pz.esp;
    o.pending = 8; // TeleportMessage(the player's cell, 4, 4, 6, 6)
    o.toRow = pr, o.toCol = pc, o.outFrames = 4, o.inFrames = 4, o.waitOut = 6, o.waitIn = 6;
    app.playerAlphaFrom = 1, app.playerAlphaTo = 0, app.playerFadeFrames = app.playerFadeLeft = 4, app.playerFaded = true;
    npcEffect(app, 0x8d, -1);
    npcEffect(app, 0x8e, -1);
    app.sound.play(0x134);
    app.padLock = 12;
    pz.tpState = 1, pz.tpTimer = 6, pz.tpTarget = t;
}

void stepTeleport(App& app) {
    App::Puzzle& pz = app.puzzle;
    if (pz.tpState == 1 && --pz.tpTimer <= 0) { // onDisappear: to the block's (old) cell
        const App::Puzzle::Obj& o = pz.objs[size_t(pz.tpTarget)];
        setActor(app, 0, centre(app, o.row, o.col), nullptr, false);
        app.playerAlphaFrom = 0, app.playerAlphaTo = 1, app.playerFadeFrames = app.playerFadeLeft = 4;
        npcEffect(app, 0x8f, -1);
        app.sound.play(0x135);
        pz.tpState = 2, pz.tpTimer = 6;
    } else if (pz.tpState == 2 && --pz.tpTimer <= 0) { // onAppear
        pz.busy = false;
        pz.tpState = 0;
    }
}


// SetPunishmentDispOn: no ESP left for what was asked of object `t` - the mark (448, 120, 64 x 64)
// over the touch, blinking a frame in three for 11 frames, the count swelling, sound 4
void punish(App& app, int t) {
    App::Puzzle& pz = app.puzzle;
    float tx = pz.tapX, ty = pz.tapY;
    if (tx < 0 && t >= 0 && t < int(pz.objs.size())) { // the keyboard: over the object
        const cr::FieldNpc& n = app.npcs[size_t(pz.objs[size_t(t)].npc)];
        float lx, ly;
        if (fieldToLogical(app, {n.pos[0], n.pos[1] + 5.0f, n.pos[2]}, lx, ly)) logicalToTex2d(app, lx, ly, tx, ty);
    }
    if (tx < 0) tx = 240, ty = 160;
    pz.punishX = tx - 32, pz.punishY = ty - 32;
    pz.punished = 11, pz.punishPhase = 0;
    pz.pop = 4;
    app.sound.play(se::kBuzzer);
}

// ---- Psychokinesis (Psychokinesis, PsychokinesisDetector, NormalBlockPK) ----
// Touch a block: the leader holds still (effect 5), the block rises (sound 0x22, +2 a frame for 2
// frames, then 5 above its place) and bobs; a swipe up / down / left / right (past a dead zone of
// |dx| < 29, -35 < dy < 20 in the 480 x 320 screen) sends it along its row or column to the cell
// before the next block or wall (or the edge): sound 0x2d, 3 units a frame, then it falls (gravity
// 0.8, sound 0x23) or settles 2.5 up on a switch and presses it. A block that reaches the leader
// on its way squashes the try: effect 0x19, sound 0x54, the room resets. A slide costs one ESP
// (counted as the block lands); letting go without one costs nothing.

void reset(App& app);

// IsHitPC: the leader inside the NPC's box (+0x1c / +0x24 its size, +0x28 / +0x30 its offset; x, z)
bool inBox(App& app, const cr::FieldNpc& n) {
    const cr::Vec3 p = app.walker.pos;
    return std::fabs(p.x - (n.pos[0] + n.offset[0])) <= n.box[0] * 0.5f &&
           std::fabs(p.z - (n.pos[2] + n.offset[2])) <= n.box[2] * 0.5f;
}

void squash(App& app) {
    fieldEffect(app, 5, app.walker.pos + cr::Vec3{0, 5, 0});
    app.sound.play(0x30);
    reset(app);
}

void pkLanded(App& app) { // the block's message 4: Psychokinesis::onWaitMove
    App::Puzzle& pz = app.puzzle;
    if (pz.pkMoved) {
        ++app.game.vars[13];
        if (pz.esp > 0) --pz.esp;
    }
    pz.busy = false;
    pz.pkState = 0, pz.pkTarget = -1, pz.pkMoved = false;
    stopEffect(app.fieldEffects, pz.pkEffect); // eff3d_delete(5)
    pz.pkEffect = 0;
    app.padLock = 2;
}

// _ArrowOn(k, x, y, z): the arrow at the screen point of (x, y + 5, z) moved by its table's
// offset (0xd13a0: (-36, 0), (0, -24), (36, 0), (0, 24) iPhone points, doubled on the iPad's
// pixels - tex2d units either way); it stays there, opaque (0xffffffff)
void arrowOn(App& app, int k, cr::Vec3 p) {
    static const float kOffset[4][2] = {{-36, 0}, {0, -24}, {36, 0}, {0, 24}};
    App::Puzzle::Arrow& a = app.puzzle.arrows[k];
    float lx, ly, tx = 240, ty = 160;
    if (fieldToLogical(app, {p.x, p.y + 5.0f, p.z}, lx, ly)) logicalToTex2d(app, lx, ly, tx, ty);
    a.on = 1, a.alpha = 0xff;
    a.x = tx + kOffset[k][0], a.y = ty + kOffset[k][1];
}

// _ArrowOff(k, mode): one that's on goes - at once (0), over 5 frames (1: alpha - 0x33 a frame) or
// 10 (2: - 0x50 a frame, the byte wrapping round - the chosen arrow flickers out)
void arrowOff(App& app, int k, int mode) {
    App::Puzzle::Arrow& a = app.puzzle.arrows[k];
    if (a.on == -1) return;
    a.on = -1, a.mode = mode;
    a.timer = mode == 2 ? 10 : mode == 1 ? 5 : 0;
}
void arrowsOff(App& app, int mode) { for (int k = 0; k < 4; ++k) arrowOff(app, k, mode); } // _ArrowOffAll

// NormalBlockPK::onRise's end (ShowArrows): an arrow each way the block could go - the neighbour
// cell inside the grid and empty: 0 left (column - 1), 1 up (row - 1), 2 right, 3 down
void showArrows(App& app, const App::Puzzle::Obj& o) {
    const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
    const cr::Vec3 p{n.pos[0], n.pos[1], n.pos[2]};
    static const int kStep[4][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
    for (int k = 0; k < 4; ++k) {
        const int r = o.row + kStep[k][0], c = o.col + kStep[k][1];
        if (cellSlot(app, 1, r, c) && objectAt(app, 1, r, c) < 0) arrowOn(app, k, p);
    }
}

void stepPkBlock(App& app, App::Puzzle::Obj& o) {
    cr::FieldNpc& n = app.npcs[size_t(o.npc)];
    auto setY = [&](float y) { place(app, o, {n.pos[0], y, n.pos[2]}); };
    switch (o.pk) {
    case 0: // onIdle: taken hold of
        if (o.pkMsg != 1) break;
        app.sound.play(0x12b); // (NormalBlockPK::onIdle: __effgo(299))
        if (o.onSwitch) press(app, o.row, o.col, false);
        o.onSwitch = false;
        o.pkTimer = 2;
        o.pkMsg = 0;
        o.pk = 1;
        break;
    case 1: // onRise: then the arrows, unless it was let go meanwhile
        setY(n.pos[1] + 2.0f);
        if (--o.pkTimer > 0) break;
        setY(o.baseY + 5.0f);
        if (o.pkMsg != 2) showArrows(app, o);
        o.bob = 0;
        o.pk = 2;
        break;
    case 2: { // onWaitSlide: bobbing; dropped (2) or sent (3)
        o.bob = (o.bob + 9) % 360;
        setY(o.baseY + 5.0f + std::sin(float(o.bob) * 3.14159265f / 180.0f));
        if (o.pkMsg == 2) {
            o.pkMsg = 0;
            o.vy = 0;
            o.pk = switchAt(app, o.row, o.col) ? 5 : 4;
        } else if (o.pkMsg == 3) {
            o.pkMsg = 0;
            // the way it goes: its arrow over 10 frames, the others at once
            const int chosen = o.toCol < o.col ? 0 : o.toRow < o.row ? 1 : o.toCol > o.col ? 2 : 3;
            for (int k = 0; k < 4; ++k) arrowOff(app, k, k == chosen ? 2 : 0);
            app.sound.play(0x136);
            const cr::Vec3 from = centre(app, o.row, o.col), to = centre(app, o.toRow, o.toCol);
            const float dx = to.x - from.x, dz = to.z - from.z, d = std::sqrt(dx * dx + dz * dz);
            o.pkTimer = std::max(1, int(d / 3.0f));
            o.vx = d > 0 ? dx / d * 3.0f : 0, o.vz = d > 0 ? dz / d * 3.0f : 0;
            o.pk = 3;
        }
        break;
    }
    case 3: // onMove: the leader in the block's box squashes the try
        place(app, o, {n.pos[0] + o.vx, n.pos[1], n.pos[2] + o.vz});
        if (inBox(app, n)) { squash(app); return; }
        if (--o.pkTimer > 0) break;
        {
            if (int* from = cellSlot(app, o.layer, o.row, o.col); from && *from >= 0 && &app.puzzle.objs[size_t(*from)] == &o) *from = -1;
            const int self = int(&o - app.puzzle.objs.data());
            o.row = o.toRow, o.col = o.toCol;
            if (int* to = cellSlot(app, o.layer, o.row, o.col)) *to = self;
            const cr::Vec3 c = centre(app, o.row, o.col);
            place(app, o, {c.x, n.pos[1], c.z});
            startQuake(app.fieldQuake, 0.5f, 100, 0, 8, false); // Quake::start(0.5, 100 -> 0, 8 frames)
            o.vy = 0;
            o.pk = switchAt(app, o.row, o.col) ? 5 : 4;
        }
        break;
    case 4: case 5: { // onDown (the box tested every frame) / onDownToSwitch (never)
        const float floor = o.baseY + (o.pk == 5 ? cr::kPuzzleSwitchRise : 0.0f);
        const float y = n.pos[1] + o.vy;
        o.vy -= 0.8f;
        setY(std::max(y, floor));
        if (o.pk == 4 && inBox(app, n)) { squash(app); return; }
        if (y <= floor) {
            if (o.pk == 5) {
                o.onSwitch = true;
                press(app, o.row, o.col, true);
            } else app.sound.play(0x12c); // (NormalBlockPK::onDown: __effgo(300) as it meets the floor)
            o.pk = 0;
            pkLanded(app);
        }
        break;
    }
    default: break;
    }
}

// Take hold of block `t` (onCheckTouch); (sx, sy) the touch (tex2d), -1e9 by key
void pkGrab(App& app, int t, float sx, float sy) {
    App::Puzzle& pz = app.puzzle;
    if (pz.busy || pz.pkState || t < 0 || t >= int(pz.objs.size())) return;
    App::Puzzle::Obj& o = pz.objs[size_t(t)];
    int pr, pc;
    if (o.type != cr::kTypeNormal || o.state != 1 || o.pk != 0 || !cellOf(app, app.walker.pos, pr, pc)) return;
    if (pz.esp == 0) { punish(app, t); return; } // no ESP left - only the mark
    pz.busy = true;
    o.pkMsg = 1;
    pz.pkState = 1, pz.pkTarget = t, pz.pkMoved = false, pz.pkFromX = sx, pz.pkFromY = sy;
    pz.pkAtX = sx, pz.pkAtY = sy, pz.pkHeld = sx > -1e8f; // PsychokinesisDetector::init: that touch
    pz.pkPointerCancelled = false;
    pz.pkEffect = npcEffect(app, 0x8b, -1, true);
}

// Psychokinesis::onCheckSlide's choice: the direction (1 up the grid / row - 1, 2 down, 3 left /
// column - 1, 4 right) sends the block to the cell before the next block or wall (or the edge) -
// the count shown one less at once (counted as it lands), the pad locked 10 frames; 0, or no room
// that way, lets it go (every arrow over 5 frames). A block not yet taken hold of waits
void pkSend(App& app, int dir) {
    App::Puzzle& pz = app.puzzle;
    if (pz.pkState != 1 || pz.pkTarget < 0) return;
    App::Puzzle::Obj& o = pz.objs[size_t(pz.pkTarget)];
    if (o.pk == 0 && o.pkMsg == 1) return; // (the hold's message not taken yet)
    const int dr = dir == 1 ? -1 : dir == 2 ? 1 : 0, dc = dir == 3 ? -1 : dir == 4 ? 1 : 0;
    int r = o.row, c = o.col;
    if (dir)
        for (;;) {
            const int nr = r + dr, nc = c + dc;
            if (!cellSlot(app, 1, nr, nc)) break;
            const int at = objectAt(app, 1, nr, nc);
            if (at >= 0) {
                const int t = pz.objs[size_t(at)].type;
                if (t == cr::kTypeNormal || t == cr::kTypeFixed) break;
            }
            r = nr, c = nc;
        }
    if (r == o.row && c == o.col) { // nowhere to go: let go
        arrowsOff(app, 1);
        o.pkMsg = 2;
    } else {
        o.toRow = r, o.toCol = c, o.pkMsg = 3; // PsychokinesisMessage
        pz.pkMoved = true;
        app.padLock = 10;
    }
    pz.pkState = 2;
    pz.pkPointerCancelled = false;
}

// PsychokinesisDetector::update, a frame while a block is held by a touch: that touch let go -
// let go of the block; still down and out of the dead zone (-29 < dx < 29, -35 < dy < 20 in
// tex2d units from where it took hold) - the way it went; else wait
void pkDetect(App& app) {
    App::Puzzle& pz = app.puzzle;
    if (pz.pkState != 1 || pz.pkFromX < -1e8f || pz.pkPointerCancelled) return;
    if (!pz.pkHeld) { pkSend(app, 0); return; }
    const float dx = pz.pkAtX - pz.pkFromX, dy = pz.pkAtY - pz.pkFromY;
    if (dx > -29 && dx < 29 && dy > -35 && dy < 20) return;
    // (0x5bd80: a = atan2(dx, dy) in double, then float; strict bands of float pi / 4 - down, right,
    // up (Repeat2PI), else left - so an exact diagonal to the right falls to LEFT, as in 3.1.0)
    const float a = float(std::atan2(double(dx), double(dy)));
    const float pi = 3.14159274f, q = pi * 0.25f, h = pi * 0.5f, two = 6.28318548f;
    int dir = 3;
    if (a > -q && a < q) dir = 2;
    else if (h - q < a && a < h + q) dir = 4;
    else {
        const float r = a >= two ? a - two : a < 0 ? a + two : a;
        if (pi - q < r && r < pi + q) dir = 1;
    }
    pkSend(app, dir);
}

// ---- Toughness (Toughness, FragileBlock, RedBlock, BlueBlock) ----
// A tap on a fragile, red or blue block that's there (isValid) costs one ESP (none left: only the
// mark) - effect 7 on the leader, sound 0x2f - and sends message 9: to the fragile block alone (it
// breaks: effect 10, sound 0x29, off the grid), or to every object on the block layer, where each
// red and blue block changes over: one put away goes solid again (alpha 0x60 to 1 in 4 frames,
// effect 4, sound 0x2a - once a frame, NoOverlapSE), squashing the try when the leader stands in
// its box (effect 0x19, sound 0x54, the room resets); a solid one is put away (1 to 0x60, effect
// 3, sound 0x29). Red blocks start solid, blue ones put away.

void toughTap(App& app, int t) {
    App::Puzzle& pz = app.puzzle;
    if (pz.busy || t < 0 || t >= int(pz.objs.size())) return;
    App::Puzzle::Obj& o = pz.objs[size_t(t)];
    int pr, pc;
    if ((o.type != cr::kTypeFragile && o.type != cr::kTypeRed && o.type != cr::kTypeBlue) || !o.shown ||
        !cellOf(app, app.walker.pos, pr, pc))
        return;
    if (pz.esp == 0) { punish(app, t); return; } // no ESP left - only the mark
    ++app.game.vars[13]; // decESPCount
    if (pz.esp > 0) --pz.esp;
    npcEffect(app, 0x8d, -1);
    app.sound.play(0x138);
    if (o.type == cr::kTypeFragile) o.hit = true;
    else
        for (App::Puzzle::Obj& b : pz.objs) b.hit |= b.layer == 1; // postMessage(1, 9)
}

void stepTough(App& app) {
    App::Puzzle& pz = app.puzzle;
    bool born = false, killed = false, rebuild = false;
    for (App::Puzzle::Obj& o : pz.objs) {
        if (!o.hit) continue;
        o.hit = false;
        const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
        if (o.type == cr::kTypeFragile) {
            if (!o.shown) continue;
            o.shown = false; // npc_kill, removePuzzleObject
            npcEffect(app, 0x90, o.npc);
            if (int* slot = cellSlot(app, o.layer, o.row, o.col); slot && *slot == int(&o - pz.objs.data())) *slot = -1;
            app.sound.play(0x132);
            rebuild = true;
        } else if (o.type == cr::kTypeRed || o.type == cr::kTypeBlue || o.type == cr::kTypeCandle) {
            if (!o.shown) {
                o.shown = true, o.ghost = false;
                born = rebuild = true;
                npcEffect(app, 0x8a, o.npc);
                if (inBox(app, n)) { // came in on the leader
                    buildNpcDraws(app);
                    fade(app, o.npc, kGhostAlpha, 1, 4);
                    app.sound.play(0x133);
                    fieldEffect(app, 5, app.walker.pos + cr::Vec3{0, 5, 0});
                    app.sound.play(0x30);
                    reset(app);
                    return;
                }
            } else {
                o.shown = false, o.ghost = true;
                killed = rebuild = true;
                npcEffect(app, 0x89, o.npc);
            }
        }
    }
    if (!rebuild) return;
    buildNpcDraws(app);
    if (born) app.sound.play(0x133);
    if (killed) app.sound.play(0x132);
    for (const App::Puzzle::Obj& o : pz.objs) {
        if (o.type != cr::kTypeRed && o.type != cr::kTypeBlue && o.type != cr::kTypeCandle) continue;
        const App::NpcDraw* d = drawOf(app, o.npc);
        if (!d) continue;
        const bool wasGhost = d->alphaTo < 0.5f;
        if (o.ghost && !wasGhost) fade(app, o.npc, 1, kGhostAlpha, 4);
        else if (!o.ghost && wasGhost) fade(app, o.npc, kGhostAlpha, 1, 4);
    }
}

// ---- Pyrokinesis (Pyrokinesis, CandleSwitch, CandleBlock, WarpCrystalCS) ----
// A tap on a candle switch (within the window; none left: only the mark) costs one ESP and sends it
// message 10. Out (0): it lights - effect 0xd, sound 0x31, SendMessage, 15 frames, then lit (3,
// flame effect 0xc). Lit: a tap puts it out - sound 0x32, the flame off, effect 0xe, SendMessage,
// 20 frames cooling (5), then out. SendMessage: in stages 48..55, message 12 with the switch's id
// to the block layer - each candle block whose +24 is that id changes over (as a red block; +22 =
// lit at the start); from stage 56, message 11 to the objects at the cells of the (up to four)
// NPCs listed at the switch's +22: a switch out catches (5 frames, effect 0xd, sound 0x31, lit 15
// later), a lit one goes out (5 frames, then as a tap) - neither passes it on - and draw3D runs a
// red beam (0x600000ff, 1.5 wide) from the sender for 20 frames. A message waits while a switch is
// lighting or cooling. WarpCrystalCS comes once every switch on the grid is lit and goes again
// when one isn't.

void sendCandle(App& app, App::Puzzle::Obj& sw) { // (anonymous)::SendMessage
    App::Puzzle& pz = app.puzzle;
    const cr::FieldNpc& n = app.npcs[size_t(sw.npc)];
    if (pz.stage < 12) { // (Omega's SendMessage: `stage < 0xc` the candle blocks (CR1's 48..55) - its own 12..15 all link switches)
        for (App::Puzzle::Obj& b : pz.objs)
            if (b.type == cr::kTypeCandle && app.npcs[size_t(b.npc)].link[1] == n.id) b.hit = true;
        return;
    }
    for (int k = 0; k < 4 && n.link[k] != 0; ++k)
        for (size_t i = 0; i < app.npcs.size(); ++i) {
            if (app.npcs[i].id != n.link[k]) continue;
            int r, c;
            if (cellOf(app, {app.npcs[i].pos[0], app.npcs[i].pos[1], app.npcs[i].pos[2]}, r, c)) {
                const int t = objectAt(app, 1, r, c);
                if (t >= 0 && pz.objs[size_t(t)].type == cr::kTypeCandleSwitch) {
                    App::Puzzle::Obj& to = pz.objs[size_t(t)];
                    to.csMsg = 11, to.csFrom = sw.npc, to.beamT = 0;
                }
            }
            break; // _GetNpcCtl: the first with that id
        }
}

void stepCandleSwitch(App& app, App::Puzzle::Obj& o, bool& lightSE, bool& outSE) {
    if (o.csFrom >= 0 && ++o.beamT > 20) o.csFrom = -1;
    const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
    const cr::Vec3 top{n.pos[0], n.pos[1] + 4.5f, n.pos[2]}; // the effects' point
    auto goOut = [&] { // the flame off, effect 0xe
        stopEffect(app.fieldEffects, o.flame);
        o.flame = 0;
        fieldEffect(app, 0x94, top);
    };
    switch (o.cs) {
    case 0:
        if (o.csMsg == 11) o.csTimer = 5, o.cs = 1;
        else if (o.csMsg == 10) {
            fieldEffect(app, 0x93, top);
            app.sound.play(0x13a);
            sendCandle(app, o);
            o.csTimer = 15, o.cs = 2;
        }
        o.csMsg = 0;
        break;
    case 1: // onIgnitionDelay
        if (--o.csTimer < 1) { fieldEffect(app, 0x93, top); lightSE = true; o.csTimer = 15, o.cs = 2; }
        break;
    case 2:
        if (--o.csTimer < 1) { o.flame = fieldEffect(app, 0x92, top, true); o.cs = 3; }
        break;
    case 3: // onLighted
        if (o.csMsg == 11) o.csTimer = 5, o.cs = 4;
        else if (o.csMsg == 10) {
            outSE = true;
            goOut();
            sendCandle(app, o);
            o.csTimer = 20, o.cs = 5;
        }
        o.csMsg = 0;
        break;
    case 4: // onExtinguishDelay
        if (--o.csTimer < 1) { outSE = true; goOut(); o.csTimer = 20, o.cs = 5; }
        break;
    case 5:
        if (--o.csTimer < 1) o.cs = 0;
        break;
    default: break;
    }
}

bool allCandlesLit(App& app) {
    for (const App::Puzzle::Obj& o : app.puzzle.objs)
        if (o.type == cr::kTypeCandleSwitch && objectAt(app, 1, o.row, o.col) == int(&o - app.puzzle.objs.data()) && o.cs != 3)
            return false;
    return true;
}

void pyroTap(App& app, int t) {
    App::Puzzle& pz = app.puzzle;
    if (t < 0 || t >= int(pz.objs.size())) return;
    App::Puzzle::Obj& o = pz.objs[size_t(t)];
    int pr, pc;
    if (o.type != cr::kTypeCandleSwitch || !cellOf(app, app.walker.pos, pr, pc)) return;
    if (pz.pyroCool > 0) return; // (Pyrokinesis::update: no tap looked at for 20 frames after one taken)
    if (pz.esp == 0) { punish(app, t); return; } // no ESP left - only the mark
    pz.pyroCool = 20;
    ++app.game.vars[13]; // decESPCount
    if (pz.esp > 0) --pz.esp;
    o.csMsg = 10;
}

void stepPyro(App& app) {
    bool lightSE = false, outSE = false;
    for (App::Puzzle::Obj& o : app.puzzle.objs)
        if (o.type == cr::kTypeCandleSwitch) stepCandleSwitch(app, o, lightSE, outSE);
    if (lightSE) app.sound.play(0x13a); // NoOverlapSE
    if (outSE) app.sound.play(0x13b);
}

// PuzzleScene: a reset fades out (sound 0x44), builds the room again, puts the leader back where
// it came in, fades in; the pad locked 24 frames
void reset(App& app) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0 || pz.scene != 4) return;
    app.sound.play(0x14d);
    app.fadeGrey = 0, app.fadeFrom = 0, app.fadeTo = 1, app.fadeFrames = 10, app.fadeT = 0; // (PuzzleScene::reset: _SetMask(0), 500 ms)
    app.padLock = 24;
    pz.scene = 2;
}

// What the room's ability works on: teleport - a block or gem in play; psychokinesis - a block;
// toughness - a fragile, red or blue block that's there (isValid); pyrokinesis - a candle switch
bool target(const App& app, const App::Puzzle::Obj& o) {
    const cr::PuzzleStage* st = stage(app);
    if (!st || o.layer != 1 || !o.shown) return false;
    switch (st->ability) {
    case cr::kAbilityTeleport: return (o.type == cr::kTypeNormal || o.type == cr::kTypeGem) && o.state == 1;
    case cr::kAbilityPsychokinesis: return o.type == cr::kTypeNormal && o.state == 1;
    case cr::kAbilityToughness: return o.type == cr::kTypeFragile || o.type == cr::kTypeRed || o.type == cr::kTypeBlue;
    case cr::kAbilityPyrokinesis: return o.type == cr::kTypeCandleSwitch;
    default: return false;
    }
}

// getNearestPuzzleObject (the block layer; its first best the screen's width squared - no limit
// in effect): the object whose point - (x, y + its record's +0x20 height, z) on the screen - is
// nearest the touch (tex2d), then the ability's window around that point (the touch less it): a
// gem |dx| <= 17, 7 <= dy <= 33, anything else |dx| <= 20, -16 <= dy <= 21. -1 when it's out
// of its window or isn't one the room's ability works on. Objects put away (red / blue / candle
// blocks) are still there to be nearest; broken or taken ones are gone
bool inWindow(int type, float dx, float dy) {
    return type == cr::kTypeGem ? std::fabs(dx) <= 17 && dy >= 7 && dy <= 33 : std::fabs(dx) <= 20 && dy >= -16 && dy <= 21;
}
int pick(App& app, float tx, float ty) {
    const App::Puzzle& pz = app.puzzle;
    int best = -1;
    float bestD = 1e30f, bx = 0, by = 0;
    for (size_t i = 0; i < pz.objs.size(); ++i) {
        const App::Puzzle::Obj& o = pz.objs[i];
        if (o.layer != 1 || (!o.shown && !o.ghost)) continue;
        const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
        float lx, ly, px, py;
        if (!fieldToLogical(app, {n.pos[0], n.pos[1] + n.box[1], n.pos[2]}, lx, ly)) continue;
        logicalToTex2d(app, lx, ly, px, py);
        const float d = (px - tx) * (px - tx) + (py - ty) * (py - ty);
        if (d < bestD) bestD = d, best = int(i), bx = px, by = py;
    }
    if (best < 0) return -1;
    const App::Puzzle::Obj& o = pz.objs[size_t(best)];
    return inWindow(o.type, tx - bx, ty - by) && target(app, o) ? best : -1;
}

// Teleport::onCheckTap / Toughness::update / Pyrokinesis::update: with the leader in a cell and
// not moved this frame (FieldPCCtrl + 0x28), and pyrokinesis' 20 frames over, CheckTap - the
// touch let go within 5 frames of going down is a tap, where it went down; then the object there
void stepTap(App& app) {
    App::Puzzle& pz = app.puzzle;
    const int ability = stage(app)->ability;
    if (ability == cr::kAbilityPyrokinesis && pz.pyroCool > 0 && --pz.pyroCool > 0) return;
    if ((ability == cr::kAbilityTeleport && pz.tpState) || pz.leaderMoved) return;
    int r, c;
    if (!cellOf(app, app.walker.pos, r, c)) return;
    App::Puzzle::Tap& tap = pz.tap;
    if (tap.state != 1) return;
    ++tap.frames;
    if (!tap.released) return;
    const bool tapped = tap.frames < 6; // (state 2; 3 held too long)
    const float tx = tap.x, ty = tap.y;
    tap = App::Puzzle::Tap();
    if (!tapped || pz.padHold > 0) return;
    const int t = pick(app, tx, ty);
    if (t < 0) return;
    pz.tapX = tx, pz.tapY = ty;
    if (ability == cr::kAbilityToughness) toughTap(app, t);
    else if (ability == cr::kAbilityPyrokinesis) pyroTap(app, t);
    else teleportTo(app, t);
    pz.tapX = pz.tapY = -1;
}

// A logical point -> the field view's pixels (FieldCamera::convertPos_ScreenToViewport)
void logicalToView(const App& app, float lx, float ly, float& hx, float& hy) {
    const float SW = app.renderer.screenW(), H = cr::Renderer::kLogicalH;
    float vx, vy, vw, vh; // the stage view on screen, as drawFrame lays it out
    if (scrollingField(app)) {
        const float fz = fieldFillZoom(app);
        vw = fieldViewW(app) * fz, vh = fieldViewH(app) * fz;
        vx = (SW - vw) / 2 + app.scroll.offX * fz, vy = (H - vh) / 2 - app.scroll.offY * fz;
    } else {
        vw = vh = std::min(SW, H);
        vx = (SW - vw) / 2, vy = (H - vh) / 2;
    }
    hx = (lx - vx) * fieldViewW(app) / vw, hy = (ly - vy) * fieldViewH(app) / vh;
}

// ScrollCamera::update: checkTouch - 5 frames on, one finger down opens 6 frames for a second:
// one coming down then scrolls the view to it (state 1: the camera off the leader, the ability
// paused) - else, or once the next single touch's frames pass without one, state 0 again. While
// scrolled, setScroll2D(the point, 0.25) every frame
void stepScroll(App& app, int fresh) {
    App::Puzzle::Scroll& sc = app.puzzle.scroll;
    const App::Fingers& f = app.fingers;
    if (sc.phase == 1) {
        bool done = true;
        if (++sc.count < 6) {
            if (f.held >= 2 && fresh == 1) {
                float lx, ly;
                app.renderer.toLogical(f.x, f.y, lx, ly);
                logicalToView(app, lx, ly, sc.x, sc.y);
                sc.state = 1;
            } else done = false;
        } else sc.state = 0;
        if (done) sc.phase = 0, sc.count = 0;
    } else if (++sc.count > 4 && f.held == 1) sc.phase = 1, sc.count = 0;
    if (sc.state == 1 && scrollingField(app)) scrollFieldTo(app, sc.x, sc.y, 0.25f);
}

// One 20 Hz frame (PuzzleManager::update, under PuzzleScene::update)
void step(App& app) {
    App::Puzzle& pz = app.puzzle;
    const int fresh = app.fingers.fresh;
    app.fingers.fresh = 0;
    if (pz.stage < 0) return;
    if (pz.scene == 2) { // fading out to the reset (PuzzleScene::onFadeOut)
        if (app.fadeT < app.fadeFrames) return;
        const cr::Vec3 start = app.puzzleStart;
        const float heading = app.puzzleStartHeading;
        const int stageNo = pz.stage;
        app.npcs = app.events.npcs(app.mapNumber); // every object back where the room has it
        enter(app);
        if (app.puzzle.stage == stageNo) { // FieldPCCtrl::reset: where and how it came in, standing
            setActor(app, 0, start, &heading, false);
            app.walker.gait = cr::Gait::Stand;
            app.puzzle.leaderWas = start;
            app.scroll.snap = true; // (FieldCamera::setScrollPlayer(1.0): the view on the leader at once)
        }
        app.fadeFrom = 1, app.fadeTo = 0, app.fadeFrames = 10, app.fadeT = 0; // (onFadeOut: _SetMask(1), 500 ms)
        app.puzzle.scene = 3;
        return;
    }
    if (pz.scene == 3) {
        if (app.fadeT >= app.fadeFrames) pz.scene = 4;
        return;
    }
    // FieldPCCtrl + 0x28: the leader moved this frame; gac + 0x1e2004: 3 frames after the pad was held
    pz.leaderMoved = app.walker.gait != cr::Gait::Stand || app.walker.pos.x != pz.leaderWas.x || app.walker.pos.z != pz.leaderWas.z;
    pz.leaderWas = app.walker.pos;
    if (app.touchPad.down) pz.padHold = 3;
    else if (pz.padHold > 0) --pz.padHold;
    if (cleared(app) || app.runner.running() || !app.dialogue.empty()) return;
    for (int k = 0; k < 2; ++k) // the buttons' scripts done: retry (var 0 = 1) / the other (var 1 = 666)
        if (pz.asked[k]) {
            pz.asked[k] = false;
            if (k == 0 && app.game.vars[0] == 1) { reset(app); return; }
        }
    ++pz.clock;
    stepScroll(app, fresh);
    if (pz.scroll.state != 1) { // the ability's update
        stepTeleport(app);
        if (stage(app)->ability == cr::kAbilityPsychokinesis) {
            pkDetect(app); // onCheckSlide
            if (pz.pkState == 2) app.padLock = 10; // onWaitMove
        } else stepTap(app);
    }
    if (stage(app)->ability == cr::kAbilityPyrokinesis) stepPyro(app);
    if (stage(app)->ability == cr::kAbilityToughness || stage(app)->ability == cr::kAbilityPyrokinesis) {
        stepTough(app); // the red / blue and candle blocks' changing over
        if (pz.scene != 4) return; // squashed: the room resets
    }
    const bool pk = stage(app)->ability == cr::kAbilityPsychokinesis;
    for (size_t i = 0; i < pz.objs.size(); ++i) {
        App::Puzzle::Obj& o = pz.objs[i];
        if (pk && o.type == cr::kTypeNormal) {
            stepPkBlock(app, o);
            if (app.puzzle.scene != 4) return; // squashed: the room resets
        } else if (o.type == cr::kTypeNormal || o.type == cr::kTypeGem) stepBlock(app, o);
        else if (o.type == cr::kTypeCrystal) stepCrystal(app, o);
    }
}

// The pad's third button (menuf_mainPut / PuzzleManager::update): shown, and taken, while var
// 64 + stage is above 2 in a room not cleared
bool thirdButton(const App& app) {
    const App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0 || cleared(app)) return false;
    const auto it = app.game.vars.find(64 + pz.stage);
    return it != app.game.vars.end() && it->second > 2;
}

// The pad's buttons (PuzzleManager::update: EvrStart 6 Retry, 7 Help, 8 the third), refused while
// the pad is locked (gac + 0x1e2006); Retry and Help then wait for their script's answer
bool ask(App& app, int k) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0 || app.padLock > 0 || app.runner.running()) return false;
    if (k == 2 ? !thirdButton(app) : !pz.help) return false;
    const uint32_t at = app.events.returnEntry(6 + k);
    if (!at || !app.runner.startAt(app.events, at, 6 + k, app.game, app.area, app.mapNumber)) return false;
    app.runnerWhat = k == 0 ? "puzzle retry" : k == 1 ? "puzzle help" : "puzzle button 8";
    if (k < 2) pz.asked[k] = true;
    return true;
}

// The keyboard's way: the first object straight ahead of the leader along its row / column (past
// put-away blocks), if the ability works on it
int ahead(App& app) {
    int r, c;
    if (!cellOf(app, app.walker.pos, r, c)) return -1;
    const float h = app.walker.heading;
    const float dx = std::sin(h), dz = std::cos(h);
    const int sr = std::fabs(dz) >= std::fabs(dx) ? (dz > 0 ? 1 : -1) : 0, sc = sr ? 0 : (dx > 0 ? 1 : -1);
    for (int k = 1; k < 12; ++k) {
        const int o = objectAt(app, 1, r + sr * k, c + sc * k);
        if (o < 0 || !app.puzzle.objs[size_t(o)].shown) continue;
        return target(app, app.puzzle.objs[size_t(o)]) ? o : -1;
    }
    return -1;
}

// Uty_PuzzleStart_Proc: flag 0 ends it; var 0 = 0 slides the pieces 8 apart a frame and fades
// them in (alpha + 0x33), 1 puts them in place (60 / 196, opaque), 2 slides them on, fading out.
// The puzzle area's script 0 runs it: 5 frames of 0, 30 of 1, 5 of 2, then flag 0 (flag 1 picks
// the second piece: the start (sound 0x43) or the other banner (sound 0x45))
void stepBanner(App& app) {
    App::PuzzleBanner& b = app.puzzleBanner;
    if (!b.on) return;
    if (app.game.flag(0)) { b.on = false; return; }
    const int v = app.game.vars[0];
    if (v == 0) b.x1 -= 8, b.x2 += 8, b.alpha = std::min(255, b.alpha + 0x33);
    else if (v == 1) b.x1 = 60, b.x2 = 196, b.alpha = 255;
    else if (v == 2) b.x1 -= 8, b.x2 += 8, b.alpha = std::max(0, b.alpha - 0x33);
}

// Uty_WarpCrystalCamera_Proc: the view slides to the crystal (0.25 a frame); there it comes in
// (fade, sound 0x2a) and var 0 = 1; once the script sets 2 the view slides back to the leader,
// var 0 = 3 and the camera is the leader's again
void stepCrystalCam(App& app) {
    App::CrystalCam& cc = app.crystalCam;
    if (cc.npc < 0) return;
    const cr::FieldNpc* n = nullptr;
    int index = -1;
    for (size_t i = 0; i < app.npcs.size(); ++i)
        if (app.npcs[i].id == cc.npc) n = &app.npcs[i], index = int(i);
    if (!n) { cc.npc = -1; app.cam.free = false; return; }
    const cr::Vec3 at = cc.phase == 0 ? cr::Vec3{n->pos[0], n->pos[1], n->pos[2]}
                                      : app.walker.pos + cr::Vec3{0, 7.0f + float(app.stageCam.lookHeight), 0};
    const float ox = app.scroll.offX, oy = app.scroll.offY;
    float vx, vy;
    if (scrollingField(app) && fieldViewPoint(app, at, vx, vy)) scrollFieldTo(app, vx, vy, 0.25f);
    const bool settled = std::fabs(app.scroll.offX - ox) < 0.5f && std::fabs(app.scroll.offY - oy) < 0.5f;
    if (!settled) return;
    if (cc.phase == 0 && app.game.vars[0] == 0) { // there: the crystal comes
        for (App::Puzzle::Obj& o : app.puzzle.objs)
            if (o.npc == index) {
                show(app, o, true);
                npcEffect(app, 0x8a, o.npc); // (Uty_WarpCrystalCamera_Proc: 0x8a, 0x133)
                fade(app, o.npc, 0, 1, 4);
            }
        app.sound.play(0x133);
        app.game.vars[0] = 1;
    } else if (cc.phase == 0 && app.game.vars[0] == 2) cc.phase = 1;
    else if (cc.phase == 1) {
        app.game.vars[0] = 3;
        cc.npc = -1;
        app.cam.free = false; // SetPlayerCamera
    }
}

// A touch going down (the frame's pixels; true when the puzzle took it): the pad's buttons -
// (160..204, 14..46) the third (EvrStart 8), (8..52, 56..84) Retry (6), (84..124, 14..46) Help
// (7) - then the ability: psychokinesis takes hold of the block there at once (onCheckTouch);
// the others' CheckTap follows the touch from here, a tap once it's let go in time
bool click(App& app, float px, float py) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0 || app.runner.running() || !app.dialogue.empty() || !paintedField(app)) return false;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float tx, ty;
    logicalToTex2d(app, lx, ly, tx, ty);
    // (widescreen: the pad in the corner; Android's PuzzleManager::update: the phone's top left)
    // (the Vita's boxes from x 116.647 / -35.353 / 40.647 - 3.1.0's less 43.352966: FUN_810735b8 / 8107351e / 8107345c)
    const float bx = tx + (app.renderer.uiX() - wideLeft(app)) / 2 + androidEdgeX(app) + vitaEdgeX(app), by = ty + androidEdgeY(app);

    if (bx >= 160 && bx < 204 && by >= 14 && by < 46 && ask(app, 2)) return true;
    if (bx >= 8 && bx < 52 && by >= 56 && by < 84 && ask(app, 0)) return true;
    if (bx >= 84 && bx < 124 && by >= 14 && by < 46 && ask(app, 1)) return true;
    if (cleared(app) || pz.scene != 4 || pz.scroll.state == 1) return false;
    const int t = pick(app, tx, ty);
    if (t < 0) return false;
    if (stage(app)->ability == cr::kAbilityPsychokinesis) {
        int r, c; // (onCheckTouch: the leader in a cell, not moving, the pad not held)
        if (pz.leaderMoved || pz.padHold > 0 || !cellOf(app, app.walker.pos, r, c)) return false;
        pz.tapX = tx, pz.tapY = ty;
        pkGrab(app, t, tx, ty);
        pz.tapX = pz.tapY = -1;
        return true;
    }
    pz.tap = App::Puzzle::Tap(); // CheckTap state 1: this touch, from here
    pz.tap.state = 1, pz.tap.x = tx, pz.tap.y = ty;
    return true;
}

// The held touch moved (the frame's pixels): where the detector sees it
void drag(App& app, float px, float py) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0 || !pz.pkHeld) return;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    logicalToTex2d(app, lx, ly, pz.pkAtX, pz.pkAtY);
}

// The touch let go (the frame's pixels): CheckTap's end, the detector's let-go
void release(App& app, float px, float py) {
    App::Puzzle& pz = app.puzzle;
    if (pz.stage < 0) return;
    if (pz.tap.state == 1) pz.tap.released = true;
    if (pz.pkHeld) {
        drag(app, px, py);
        pz.pkHeld = false;
    }
}

// The ability's key: teleport to / take hold of the block ahead, or let a held one go
void abilityKey(App& app) {
    App::Puzzle& pz = app.puzzle;
    const cr::PuzzleStage* st = stage(app);
    if (!st) return;
    if (st->ability == cr::kAbilityPsychokinesis) {
        if (pz.pkState == 1) pkSend(app, 0);
        else pkGrab(app, ahead(app));
    } else if (st->ability == cr::kAbilityToughness) toughTap(app, ahead(app));
    else if (st->ability == cr::kAbilityPyrokinesis) pyroTap(app, ahead(app));
    else teleportTo(app, ahead(app));
}


// CandleSwitch::draw3D: the beam from a switch that passed a light on (message 11) - 1.5 wide,
// colour 0x600000ff, its head reaching the switch in 4 frames, its tail following from frame 8 to 12
void drawFx(App& app, const cr::Mat4& mvp) {
    App::Puzzle& pz = app.puzzle;
    const cr::PuzzleStage* st = stage(app);
    if (!st || st->ability != cr::kAbilityPyrokinesis) return;
    constexpr int kQuads = 64;
    auto make = [&](cr::GpuMesh& m) {
        if (m.vao) return;
        cr::Mesh mesh;
        mesh.positions.assign(kQuads * 4 * 3, 0.0f);
        mesh.uvs.assign(kQuads * 4 * 2, 0.0f);
        mesh.colors.assign(kQuads * 4 * 4, 0);
        for (uint32_t q = 0; q < kQuads; ++q)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(q * 4 + k);
        m = app.renderer.upload(mesh);
    };
    make(app.puzzleBeams);
    struct Quads {
        std::vector<float> pos, uv;
        std::vector<uint8_t> col;
        int n = 0;
        void add(const cr::Vec3 (&v)[4], const uint8_t (&c)[4][4]) {
            if (n >= kQuads) return;
            for (int k = 0; k < 4; ++k) {
                pos.insert(pos.end(), {v[k].x, v[k].y, v[k].z});
                col.insert(col.end(), c[k], c[k] + 4);
            }
            ++n;
        }
        void finish() {
            pos.resize(kQuads * 4 * 3, 0.0f);
            col.resize(kQuads * 4 * 4, 0);
            uv.assign(kQuads * 4 * 2, 0.0f);
        }
    } beams;
    for (const App::Puzzle::Obj& o : pz.objs) {
        if (o.type != cr::kTypeCandleSwitch || !o.shown) continue;
        const cr::FieldNpc& n = app.npcs[size_t(o.npc)];
        if (o.csFrom >= 0 && o.beamT <= 20) {
            const cr::FieldNpc& f = app.npcs[size_t(o.csFrom)];
            const float dx = n.pos[0] - f.pos[0], dz = n.pos[2] - f.pos[2], dist = std::sqrt(dx * dx + dz * dz);
            const float head = std::clamp(float(o.beamT) * 2.5f, 0.0f, 10.0f) / 10.0f * dist;
            const float tail = std::clamp(float(o.beamT) * 2.5f - 20.0f, 0.0f, 10.0f) / 10.0f * dist;
            if (head > tail + 0.01f) {
                const float ang = std::atan2(dx, dz), cs = std::cos(ang), sn = std::sin(ang), y = n.pos[1] + 0.3f;
                auto at = [&](float lx, float lz) { return cr::Vec3{f.pos[0] + lx * cs + lz * sn, y, f.pos[2] + lz * cs - lx * sn}; };
                const cr::Vec3 v[4] = {at(-0.75f, tail), at(0.75f, tail), at(0.75f, head), at(-0.75f, head)};
                const uint8_t c[4][4] = {{255, 0, 0, 0x60}, {255, 0, 0, 0x60}, {255, 0, 0, 0x60}, {255, 0, 0, 0x60}};
                beams.add(v, c);
            }
        }
    }
    beams.finish();
    app.renderer.updatePositions(app.puzzleBeams, beams.pos, beams.uv, beams.col);
    cr::Material m; // untextured, double-sided, depth tested, no depth write
    m.culling = 2; // (none)
    m.properties = 8;
    m.blendSrc = 4, m.blendDst = 5; // alpha
    if (beams.n) app.renderer.drawMesh(app.puzzleBeams, nullptr, mvp, m);
}

} // namespace puzzle

void stepEvents(App& app) {
    // The later releases' extras (3.1.0's and the Vita's _gameapp_prc set these every frame; 1.0.0
    // left them for its updates): the present packs - the second scenario's five extra trials and
    // their presents - open
    for (int f = 101; f <= 106; ++f) app.game.setFlag(f, true);
    if (app.npcNativesPending) { // npc_setup: SetVar(0, id); UtyPrcTbl[row + 8].init() for every created, shown NPC with one
        app.npcNativesPending = false;
        ViewerHost natives(app);
        for (const cr::FieldNpc& n : app.npcs)
            if (n.native == 15 && npcActive(app, n)) app.game.vars[0] = n.id, natives.fireWall(n.id); // (the only one the rows use)
    }
    // fast-forward while the skip is held (_PrcMsg: FUN_00031978, the corner, as a tap every frame):
    // the typing out at once - not under 0x1b - and each page turned the frame it is out (choices
    // still wait for an answer; pauses, line breaks and voice clips still take their frames)
    const bool skipping = app.skipScene && !app.messageSkip && app.runner.running() && !app.dialogue.empty(); // (Uty_Chg_MsgSkip: the Vita's □ held refused too - _PrcMsg)
    if (skipping) hurryDialogue(app);
    stepTyping(app);
    if (skipping && !app.dialogue.empty() && pageTyped(app) && !(pageInfo(app) && pageInfo(app)->closeAfter >= 0))
        advanceDialogue(app);
    if (app.titleOpen) ++app.titleAnim; // the title menu fades in
    app.titleOut = app.titleOpen && (app.resumeNotice || app.resumePrompt) ? app.titleOut + 1 : 0; // (its plates going for the offer)
    stepResume(app); // the Progress Tracker's offer: its message, windows, and the load / Start after the answer
    if (app.titleReturnIn > 0 && --app.titleReturnIn == 0) { app.optionsOpen = false; bootTitle(app); } // Options' Return to Title, after its black
    if (app.choiceFlag >= 0 && app.choiceAnim < 4) ++app.choiceAnim;
    updateVoices(app);
    if (!app.battle.active) stepEffects(app, app.fieldEffects), stepQuake(app.fieldQuake); // eff3d_prc, UpdateQuake
    if (app.nowLoading.on) app.nowLoading.frame = (app.nowLoading.frame + 1) & 15; // NowLoadProc
    stepTexDisp(app);
    stepVitaLogo(app);
    stepTutoTex(app);
    stepMsgWin(app);
    if (app.fadeT < app.fadeFrames) ++app.fadeT;
    if (app.fadeBackT < app.fadeBackFrames) ++app.fadeBackT;
    bool unlit = false; // (only the drawn models' fades step: mdl_put's)
    for (auto& d : app.npcDraws)
        if (d.fadeLeft > 0) {
            --d.fadeLeft;
            d.alpha = d.alphaTo + (d.alphaFrom - d.alphaTo) * float(d.fadeLeft) / float(d.fadeFrames);
            if (d.fadeLeft == 0 && d.lit && d.alpha <= 0) d.lit = false, unlit = true; // (+0xfb4 0 again)
        }
    if (unlit) buildNpcDraws(app);
    if (app.playerFadeLeft > 0) {
        --app.playerFadeLeft;
        app.playerAlpha = app.playerAlphaTo + (app.playerAlphaFrom - app.playerAlphaTo) * float(app.playerFadeLeft) /
                                                  float(std::max(1, app.playerFadeFrames));
    }
    if (app.puzzleStartPending && app.havePlayer)
        app.puzzleStart = app.walker.pos, app.puzzleStartHeading = app.walker.heading, app.puzzleStartPending = false;
    if (app.padLock > 0) --app.padLock; // (the main frame's: gac + 0x1e2006 counts down before _gameapp_prc)
    puzzle::step(app);
    puzzle::stepCrystalCam(app);
    puzzle::stepBanner(app);
    // Script moves (npc_autoctl_prc): straight lines, y on the floor unless mode 2. (The original's
    // frame runs the script first, then Field_Main's steps: a move, scale, scroll or camera slide a
    // script starts takes its first step on that same frame - the starters step once themselves)
    for (size_t i = 0; i < app.moves.size();) {
        if (stepMove(app, app.moves[i])) app.moves.erase(app.moves.begin() + long(i));
        else ++i;
    }
    for (size_t i = 0; i < app.rescales.size();) { // (npc_autoctl_prc mode 3: the scale in a straight line)
        if (stepRescale(app, app.rescales[i])) app.rescales.erase(app.rescales.begin() + long(i));
        else ++i;
    }
    stepFieldScroll(app);
    stepSceneEffects(app);
    stepScriptCam(app);
    for (App::Portrait& p : app.portraits) // Tex2d slides and fades (tex2d_img_prc)
    {
        if (p.t < p.frames) {
            ++p.t;
            const float t = float(p.t) / float(p.frames);
            p.x = p.xFrom + (p.xTo - p.xFrom) * t;
            p.y = p.yFrom + (p.yTo - p.yFrom) * t;
        }
        if (p.aT < p.aFrames) {
            ++p.aT;
            p.alpha = p.aFrom + (p.aTo - p.aFrom) * float(p.aT) / float(p.aFrames);
        }
        if (p.gT < p.gFrames) {
            ++p.gT;
            p.shade = p.gFrom + (p.gTo - p.gFrom) * float(p.gT) / float(p.gFrames);
        }
        if (p.dying && p.t >= p.frames && p.aT >= p.aFrames) {
            app.renderer.destroy(p.tex);
            p = App::Portrait();
        }
        if (p.zoomT < p.zoomFrames) {
            ++p.zoomT;
            p.scale = 2.0f - float(p.zoomT) / float(p.zoomFrames);
        }
    }
    // Uty_GetItem_Proc: the band comes down to row 2; once the script raises flag 0 (after the tap)
    // it goes on down a row a frame, fading, and past row 6 it's gone and flag 0 clears
    if (app.itemWindowOpen) {
        if (!app.itemWindowClosing && app.game.flag(0)) app.itemWindowClosing = true;
        if (app.itemWindowClosing) {
            if (app.itemWindowRow > 6) app.itemWindowOpen = false, app.game.setFlag(0, false);
            ++app.itemWindowRow, ++app.itemWindowFade;
        } else if (app.itemWindowRow < 2)
            ++app.itemWindowRow;
    }
    if (!app.runner.running() && app.pendingEntry != App::Entry::None) {
        const bool area = app.pendingEntry == App::Entry::Area;
        app.pendingEntry = App::Entry::None;
        const bool started = area ? app.runner.start(app.events, 0, app.game, app.area, app.mapNumber)
                                  : app.runner.startAt(app.events, app.events.returnEntry(5), 0, app.game, app.area,
                                                       app.mapNumber);
        if (started) {
            app.runnerWhat = area ? "area entry script" : "room entry script";
            SDL_Log("%s: area %d map %d", app.runnerWhat.c_str(), app.area, app.mapNumber);
        }
    }
    if (!app.runner.running()) return;
    ViewerHost host(app);
    const bool more = app.runner.step(&host);
    if (app.battle.requested) { // G+0x2c0 bit 7: the field gives way to the battle
        app.battle.requested = false;
        // btl_main: a script still running (G+0x2c0 bit 0) makes it an event battle (G+0xa4e) - the
        // script waits behind the battle's gate and goes on later; else nothing's left of it
        const bool event = more && app.runner.running();
        if (!event) app.runner.stop();
        startBattle(app, app.battle.request, app.battle.requestFlags);
        app.battle.eventBattle = event && app.battle.active;
        return;
    }
    if (more) return;
    // Finished: report, and refresh what the script's flags changed (an opened box loses its lid)
    app.messageSkip = false; // (__EvtAbort clears G+0x2d; a Return leaves it, but the scenes turn it back on before ending)
    trackerSceneEnd(app);
    const bool abandoned = app.scriptAreaWarped; // (an area-changing Warp: the script is dropped with its event
    app.scriptAreaWarped = false;                // file, no Return / Abort - no _chk_tuto_kick until the next one)
    if (!abandoned) { // _chk_tuto_kick (__EvtReturn / __EvtAbort): the first of these not yet seen gives its item and
      // runs EvrStart 3 with var 0 naming the tutorial - 170 held: flag 69, item 469, 51; 191: 70,
      // 470, 52; flag 1602: 71, 471, 53
        int kick = 0;
        auto has = [&](int item) { auto it = app.game.items.find(item); return it != app.game.items.end() && it->second > 0; };
        if (!app.game.flag(69) && has(170)) app.game.setFlag(69, true), kick = 469;
        else if (!app.game.flag(70) && has(191)) app.game.setFlag(70, true), kick = 470;
        else if (!app.game.flag(71) && app.game.flag(1602)) app.game.setFlag(71, true), kick = 471;
        if (kick) {
            if (app.game.items[kick] < 99) ++app.game.items[kick];
            app.game.vars[0] = 51 + (kick - 469);
            if (const uint32_t at = app.events.returnEntry(3); at && app.runner.startAt(app.events, at, 3, app.game, app.area, app.mapNumber)) {
                app.runnerWhat = "tutorial kick " + std::to_string(kick);
                return;
            }
        }
        // (__EvtReturn / 0x23cd4 with no tutorial kicked: _ContData_Save_set_fld, then _fld_checkAchievements)
        fieldCheckAchievements(app);
    }
    const cr::EventResult& r = app.runner.result();
    if (!r.error.empty())
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "event (%s, area %d map %d): %s", app.runnerWhat.c_str(), app.area,
                    app.mapNumber, r.error.c_str());
    if (!app.runnerWhat.empty()) {
        app.lastEvent = app.runnerWhat;
        for (int item : r.itemsGained) {
            std::vector<std::vector<std::string>> name;
            app.lastEvent += ", got " + itemName(app, item);
        }
        SDL_Log("%s, %d instructions, %zu messages", app.lastEvent.c_str(), r.instructions, r.messages.size());
    }
    app.itemWindowOpen = false;
    app.tapPending = false;
    buildNpcDraws(app);
    updateTitle(app);
}

// fldencount_walk, for a step of `dist` units: the encounter distance counts unless encounters
// are off (flag 4076, the pair's unencountflg); with skill 174 on either member every 10 units
// banks 1 EXP for the pair (to 100000; one a step at most)
void fieldWalked(App& app, float dist) {
    if (!app.noEncounters && !fieldEncountersOff(app) && !app.cheats.noEncounters) app.encounters.walked(dist); // (Cheats: No Random Battles)
    // (_chk_gene on the scenario's current pair - sectname[scen*2+2 / +3], as btl_prty_init reads it)
    const auto& pair = app.game.pairs[size_t(fieldRoute(app))];
    if (!(pair[0] >= 0 && cr::hasSkill(app.game, pair[0], 174)) && !(pair[1] >= 0 && cr::hasSkill(app.game, pair[1], 174))) return;
    app.walkExpDistance += dist;
    if (app.walkExpDistance >= 10) {
        int& bank = app.game.walkExp[size_t(fieldRoute(app))];
        bank = std::min(100000, bank + 1);
        app.walkExpDistance -= 10;
    }
}

void playFootsteps(App& app) {
    // PlayFootstepSE: a step sound when the walk / run animation passes one of its two step
    // frames, chosen (one of three) by the colour of the floor polygon underfoot, and the dust
    // (Omega: the tables' first set for everyone, effect 0x87)
    if (app.walker.gait != cr::Gait::Stand) {
        const int route = 0;
        const short* frames = app.walker.gait == cr::Gait::Run ? cr::kFootstepFrameRun[route] : cr::kFootstepFrameWalk[route];
        const float prev = app.footPrev, now = app.player.time;
        for (int k = 0; k < 2; ++k) {
            const float f = frames[k];
            const bool crossed = prev <= now ? (prev < f && f <= now) : (f > prev || f <= now); // wrapped
            if (!crossed) continue;
            float y;
            int poly = -1;
            if (!cr::floorHeight(app.collisionData, app.walker.pos.x, app.walker.pos.y + 1, app.walker.pos.z, 2.0f, y, &poly))
                break;
            static const uint8_t kSurface[5][3] = {{254, 0, 0}, {175, 175, 175}, {125, 185, 175}, {205, 220, 230}, {140, 190, 190}};
            const uint8_t* c = app.collisionData.polygons[size_t(poly)].color;
            for (int t = 0; t < 5; ++t)
                if (c[0] == kSurface[t][0] && c[1] == kSurface[t][1] && c[2] == kSurface[t][2])
                {
                    npcEffect(app, 0x87, -1); // the dust (eff3d_set_fld 0x87 on the leader)
                    const uint32_t id = uint32_t(cr::kFootstepSound[t][SDL_rand(3)]);
                    app.sound.play(id);
                    app.soundLog.push_back(id);
                }
        }
    }
    app.footPrev = app.player.time;
}

// WASD moves the leader relative to the camera (camYaw), as the original's cross key does.
// A field step's start and end (App::Between): what the step moves, recorded on either side of it
void betweenRecord(App& app, int side) {
    App::Between& b = app.between;
    b.walker[side] = app.walker.pos, b.heading[side] = app.walker.heading, b.playerAlpha[side] = app.playerAlpha;
    b.eye[side] = app.cam.eye, b.target[side] = app.cam.target;
    b.scroll[side][0] = app.scroll.offX, b.scroll[side][1] = app.scroll.offY;
    if (side == 0) {
        b.npcs.clear();
        for (const App::NpcDraw& d : app.npcDraws) {
            App::Between::Npc n;
            n.index = d.index, n.id = d.id, n.world[0] = n.world[1] = d.world, n.alpha[0] = n.alpha[1] = d.alpha;
            b.npcs.push_back(n);
        }
        b.valid = false;
        return;
    }
    // (the NPCs the step began with that are still drawn: matched by their place and id)
    std::vector<App::Between::Npc> kept;
    for (App::Between::Npc n : b.npcs)
        for (const App::NpcDraw& d : app.npcDraws)
            if (d.index == n.index && d.id == n.id) {
                n.world[1] = d.world, n.alpha[1] = d.alpha;
                kept.push_back(n);
                break;
            }
    b.npcs.swap(kept);
    b.valid = true;
}

struct BetweenStep { // betweenRecord on either side of a step, however the step's body leaves
    App& app;
    explicit BetweenStep(App& a) : app(a) { betweenRecord(app, 0); }
    ~BetweenStep() { updateNpcJoints(app); betweenRecord(app, 1); }
};

bool same(const cr::Vec3& a, const cr::Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same(const cr::Mat4& a, const cr::Mat4& b) { return std::memcmp(a.m, b.m, sizeof a.m) == 0; }

// how far between the last step's start and end a frame shows (1: no step to show between)
float betweenK(const App& app) { return app.between.valid ? std::clamp(app.between.alpha, 0.0f, 1.0f) : 1.0f; }

cr::Vec3 lerp(const cr::Vec3& a, const cr::Vec3& b, float k) { return a + (b - a) * k; }
float lerpAngle(float a, float b, float k) {
    float d = std::fmod(b - a, 6.2831853f);
    if (d > 3.14159265f) d -= 6.2831853f;
    if (d < -3.14159265f) d += 6.2831853f;
    return a + d * k;
}

// The leader as a frame between steps shows it (a step's warp - further than it can walk - is
// shown at once)
cr::FieldWalker shownWalker(const App& app) {
    cr::FieldWalker w = app.walker;
    const App::Between& b = app.between;
    if (!b.valid || !same(w.pos, b.walker[1]) || w.heading != b.heading[1]) return w;
    const cr::Vec3 d = b.walker[1] - b.walker[0];
    if (cr::dot(d, d) > 40.0f * 40.0f) return w;
    const float k = betweenK(app);
    w.pos = lerp(b.walker[0], b.walker[1], k);
    w.heading = lerpAngle(b.heading[0], b.heading[1], k);
    return w;
}

float shownPlayerAlpha(const App& app) {
    const App::Between& b = app.between;
    if (!b.valid || app.playerAlpha != b.playerAlpha[1]) return app.playerAlpha;
    return b.playerAlpha[0] + (b.playerAlpha[1] - b.playerAlpha[0]) * betweenK(app);
}

// An NPC's model matrix and opacity as a frame between steps shows them (a turn of more than
// about 45 degrees or a jump in one step is shown at once)
void shownNpc(const App& app, const App::NpcDraw& d, cr::Mat4& world, float& alpha) {
    world = d.world, alpha = d.alpha;
    for (const App::Between::Npc& n : app.between.npcs) {
        if (!app.between.valid || n.index != d.index || n.id != d.id) continue;
        const float k = betweenK(app);
        if (d.alpha == n.alpha[1]) alpha = n.alpha[0] + (n.alpha[1] - n.alpha[0]) * k;
        if (!same(d.world, n.world[1])) return;
        const float* a = n.world[0].m, *c = n.world[1].m;
        const float turn = (a[0] * c[0] + a[1] * c[1] + a[2] * c[2]) /
                           std::max(std::sqrt((a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) * (c[0] * c[0] + c[1] * c[1] + c[2] * c[2])), 1e-6f);
        const float jx = c[12] - a[12], jy = c[13] - a[13], jz = c[14] - a[14];
        if (turn < 0.7f || jx * jx + jy * jy + jz * jz > 40.0f * 40.0f) return;
        for (int i = 0; i < 16; ++i) world.m[i] = a[i] + (c[i] - a[i]) * k;
        return;
    }
}

// The script's free camera as a frame between steps shows it
void shownCamera(const App& app, cr::Vec3& eye, cr::Vec3& target) {
    eye = app.cam.eye, target = app.cam.target;
    const App::Between& b = app.between;
    if (!b.valid || !same(eye, b.eye[1]) || !same(target, b.target[1])) return;
    const float k = betweenK(app);
    eye = lerp(b.eye[0], b.eye[1], k), target = lerp(b.target[0], b.target[1], k);
}

// The painted field's scroll as a frame between steps shows it (a script's slide; otherwise it
// follows the leader as shown)
void shownScroll(const App& app, float& x, float& y) {
    x = app.scroll.offX, y = app.scroll.offY;
    const App::Between& b = app.between;
    if (!b.valid || !app.cam.free || x != b.scroll[1][0] || y != b.scroll[1][1]) return;
    const float k = betweenK(app);
    x = b.scroll[0][0] + (b.scroll[1][0] - b.scroll[0][0]) * k;
    y = b.scroll[0][1] + (b.scroll[1][1] - b.scroll[0][1]) * k;
}

void updatePlayer(App& app, float dt, float camYaw) {
    if (!app.havePlayer) return;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    // A running script, its dialogue box or item window hold the player in place
    // (Android: FieldScene::onUpdate does nothing while isAchievementDraw - the banner holds the field)
    const bool talking = fieldSceneRunning(app) || app.optionsOpen || app.campOpen || app.saveScreen.open ||
                         app.shop.open || app.mapScreen.open || app.puzzle.tpState != 0 || app.puzzle.pkState != 0 ||
                         (app.puzzle.stage >= 0 && app.puzzle.scene != 4) || achievementBannerUp(app);
    // EvtStart: an event stands the player (mdl_set_act_motion(leader, 0, 2)) unless its record's
    // +0xe has 0x4000; held here, stepWalker doesn't run, so the walk / run clip would play in place
    auto standHeld = [&] {
        if (!app.runner.running() || !(app.events.scriptFlags(app.runner.eventNo()) & 0x4000)) app.walker.gait = cr::Gait::Stand;
    };
    if (fieldSceneRunning(app)) standHeld();
    float sx = talking ? 0.0f : float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
    float sy = talking ? 0.0f : float(keys[SDL_SCANCODE_S]) - float(keys[SDL_SCANCODE_W]);
    bool run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    // A controller as the Vita's CrossKey::update (8103cc38): each stick axis zeroed below 0.2549 (the
    // pad reader, FUN_81078ee4), then x 50 and held to +-50; with the stick at rest the D-pad sets its
    // axes to +-50 - a full push. The length walks from 14.5 and runs from 29 (squared 210.25 / 841):
    // the stick walks past about 0.29 and runs past 0.58, the D-pad always runs. The field bar open
    // (FieldPCCtrl::operate) the leader stands
    if (padLive(app) && !talking && sx == 0 && sy == 0 && !(fieldBarPad(app) && app.fieldMenu.open)) {
        auto axis = [&](SDL_GamepadAxis a) {
            const float v = float(SDL_GetGamepadAxis(app.pad, a)) / 32767.0f;
            return std::fabs(v) < 0.2549f ? 0.0f : std::clamp(v * 50.0f, -50.0f, 50.0f);
        };
        float lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
        if (lx == 0 && ly == 0) {
            lx = 50.0f * (float(SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) - float(SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
            ly = 50.0f * (float(SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) - float(SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_DPAD_UP)));
        }
        const float len2 = lx * lx + ly * ly;
        if (len2 >= 210.25f) {
            run = run || len2 >= 841.0f;
            sx = std::clamp(lx / 50.0f, -1.0f, 1.0f), sy = std::clamp(ly / 50.0f, -1.0f, 1.0f);
        }
    }
    if (!talking && sx == 0 && sy == 0) { // the touch pad (CrossKey): its offset steers, far out runs
        float tx, ty;
        bool trun;
        if (touchPadStick(app, tx, ty, trun)) sx = tx, sy = ty, run = trun;
    }
    if (const char* w = SDL_getenv("CR_CAPTURE_WALK"); w && !talking && sx == 0 && sy == 0) // (tests: x,y held - a --settle-moving walk)
        if (SDL_sscanf(w, "%f,%f", &sx, &sy) != 2) sx = sy = 0;
    constexpr float kStep = 1.0f / kFramesPerSecond;
    app.walkClock += dt;
    while (app.walkClock >= kStep) {
        app.walkClock -= kStep;
        BetweenStep between(app);
        stepEvents(app);
        if (app.battle.active) break;
        if (!app.havePlayer) continue;
        // FieldScene::onUpdate: FieldPCCtrl's update and checkHit every frame - a script holding the
        // field (G+0x364 bit 0) stops the stick, not the floor; the pushes wait for its end
        const std::vector<cr::WalkBody> bodies = walkBodies(app);
        if (fieldSceneRunning(app)) { // (a touch trigger may start a script this step)
            stepExamineWindows(app); // (the windows count down, none put up)
            standHeld();
            const cr::Gait gait = app.walker.gait;
            app.walker.velocity = {};
            cr::checkWalkerHit(app.walker, app.collisionData, bodies, !app.runner.running());
            app.walker.gait = gait;
            continue;
        }
        const float walkedDist = cr::stepWalker(app.walker, app.collisionData, bodies, sx, sy, run, camYaw, !app.runner.running());
        if (sx == 0 && sy == 0) // LookAtRect (FieldPCCtrl::receiveMessage 3): standing in reach, the leader turns to the
            if (const cr::FieldNpc* n = interactTarget(app)) { // target by 0.35 of the angle left each frame
                const float k = n->model == 0 || n->model == 1 ? 1.0f : 0.0f;
                const float cx = n->pos[0] + k * n->offset[0] + n->offset[0], cz = n->pos[2] + k * n->offset[2] + n->offset[2];
                float d = std::atan2(cx - app.walker.pos.x, cz - app.walker.pos.z) - app.walker.heading;
                while (d > 3.14159265f) d -= 2 * 3.14159265f;
                while (d < -3.14159265f) d += 2 * 3.14159265f;
                app.walker.heading += 0.35f * d;
            }
        if (walkedDist >= 0) fieldWalked(app, walkedDist); // fldencount_walk(|velocity + pushes|)
        stepExamineWindows(app);
        checkTriggers(app);
        if (fieldSceneRunning(app) || !app.havePlayer) {
            if (fieldSceneRunning(app)) standHeld();
            continue;
        }
        cr::BattleStart start; // fldencount_encount
        auto random = [&](int n) { return app.rng.get(n); };
        const auto& pr = app.game.pairs[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))]; // 171 / 172 on either
        auto has = [&](int c, int sk) { return c >= 0 && c < cr::kCharacters && cr::hasSkill(app.game, c, sk); };
        const bool first = has(pr[0], 171) || has(pr[1], 171);
        const bool alert = has(pr[0], 172) || has(pr[1], 172);
        if (app.encounters.check(random, partyLevel(app), first, alert, start)) {
            if (app.cheats.firstStrike) start.strike = 1; // (Options' Cheats: the party's first strike, never an ambush)
            writeResume(app); // (fldencount_encount: the Progress Tracker at the fight's start)
            app.game.vars[58] = 999; // the usual battle music, victory tune 10
            app.game.vars[59] = 10;
            app.game.vars[60] = 999;
            startBattle(app, start, 0);
            break;
        }
    }
    app.between.alpha = app.walkClock / kStep; // (how far into the next step this frame is)
    // Actions 0/1/2 = stand / walk / run (mdl_set_act_motion in FieldPCCtrl::operate); while a
    // block is held, slot 3 looped from frame 2 (Psychokinesis::onCheckTouch's
    // mdl_set_act_motion_loop(leader, 3, 2, 2))
    const bool hold = app.puzzle.pkState != 0 && app.player.actions.size() > 3;
    if (!fieldSceneRunning(app)) app.leaderMotion = -1; // (operate's motion again once the field is free)
    const bool scripted = !hold && app.leaderMotion >= 0 && app.leaderMotion < int(app.player.actions.size());
    const int want = hold ? 3 : scripted ? app.leaderMotion : std::min(int(app.walker.gait), int(app.player.actions.size()) - 1);
    if (want != app.player.action) {
        app.player.action = want;
        app.player.time = 0;
        app.footPrev = 0;
    }
    app.player.loopFrom = hold ? 2 : scripted && app.leaderLoopFrom > 0 ? app.leaderLoopFrom : -1;
    animate(app, app.player, dt);
    playFootsteps(app);
}

cr::Mat4 playerWorld(const cr::FieldWalker& w) {
    cr::Mat4 m;
    const float c = std::cos(w.heading), s = std::sin(w.heading);
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    m.m[12] = w.pos.x; m.m[13] = w.pos.y; m.m[14] = w.pos.z;
    return m;
}

// The map's visible NPCs (npc_setup): model from the NPC table, position (plus the offset
// for models 0/1), turned by the record's angle about Y, scaled by the table's percentage.
// An NPC's model matrix: position (plus the offset for models 0/1), turned about Y, scaled
cr::Mat4 npcWorld(const cr::FieldNpc& n) {
    const float k = n.model == 0 || n.model == 1 ? 1.0f : 0.0f;
    const float c = std::cos(n.angle), sn = std::sin(n.angle);
    const bool set = n.axes[0] >= 0;
    const float sx = set ? n.axes[0] : n.scale, sy = set ? n.axes[1] : n.scale, sz = set ? n.axes[2] : n.scale;
    cr::Mat4 w;
    w.m[0] = c * sx; w.m[2] = -sn * sx;
    w.m[5] = sy;
    w.m[8] = sn * sz; w.m[10] = c * sz;
    w.m[12] = n.pos[0] + k * n.offset[0];
    w.m[13] = n.pos[1] + k * n.offset[1];
    w.m[14] = n.pos[2] + k * n.offset[2];
    return w;
}

cr::FieldNpc* findNpc(App& app, int id) {
    for (cr::FieldNpc& n : app.npcs)
        if (n.id == id) return &n;
    return nullptr;
}

// Where a script actor is: npc 0 is the party leader
bool actorPos(App& app, int id, cr::Vec3& out) {
    if (id == 0) { if (!app.havePlayer) return false; out = app.walker.pos; return true; }
    const cr::FieldNpc* n = findNpc(app, id);
    if (!n) return false;
    out = {n->pos[0], n->pos[1], n->pos[2]};
    return true;
}

// _get_floor(out, x, y, z, r) (1.0.0 0x71d8c, 3.1.0 0x75dc0): CmpBase::searchNode(pos, r), then that node's
// CmpBasic::getFloor with twice r (vadd.f32 d0, d8, d8 at 3.1.0 0x75e58) - the highest floor polygon under
// (x, z) with |trunc(y - h)| <= 2r; false (y left as it is) when there is no tree or no such floor
bool scriptFloor(const App& app, cr::Vec3 p, float r, float& y) {
    const int node = cr::searchNode(app.collisionData, p, r);
    float h;
    if (node < 0 || cr::getFloor(app.collisionData, node, p, 2.0f * r, h) < 0) return false;
    y = h;
    return true;
}

// Moves / turns a script actor and its drawn model (get_floor snaps y unless keepY)
void setActor(App& app, int id, cr::Vec3 p, const float* yaw, bool keepY) {
    float y;
    if (!keepY && (cr::floorHeight(app.collisionData, p.x, p.y, p.z, 1000.0f, y) ||
                   cr::floorHeight(app.collisionData, p.x, p.y + 1000.0f, p.z, 2000.0f, y)))
        p.y = y;
    if (id == 0) {
        app.walker.pos = p;
        if (yaw) app.walker.heading = *yaw;
        return;
    }
    cr::FieldNpc* n = findNpc(app, id);
    if (!n) return;
    const bool wasParked = !app.placedNpcs.count(id);
    app.placedNpcs.insert(id);
    n->pos[0] = p.x; n->pos[1] = p.y; n->pos[2] = p.z;
    if (yaw) n->angle = *yaw;
    bool drawn = false;
    for (auto& d : app.npcDraws)
        if (d.id == id) { d.world = npcWorld(*n); drawn = true; }
    if (wasParked || !drawn) buildNpcDraws(app); // a parked actor enters the scene
    else updateNpcJoints(app);
}

void buildNpcDraws(App& app) {
    std::vector<App::NpcDraw> old;
    old.swap(app.npcDraws);
    old.insert(old.end(), std::make_move_iterator(app.npcHeld.begin()), std::make_move_iterator(app.npcHeld.end()));
    app.npcHeld.clear();
    if (!app.showNpcs) { app.npcHeld = std::move(old); return; } // (kept for when they're shown again)
    for (size_t i = 0; i < app.npcs.size(); ++i) {
        const cr::FieldNpc& n = app.npcs[i];
        if (n.model < 0 || n.model >= cr::kNumNpcModels) continue;
        const std::string file = cr::kNpcModelFile[n.model];
        // (the same NPC: its model as it was left - action, time, blend, alpha and fade - shown or hidden)
        const App::NpcDraw* kept = nullptr;
        for (const App::NpcDraw& o : old)
            if (o.index == int(i) && o.model == file) { kept = &o; break; }
        const bool lit = kept && kept->lit;
        bool drawn = npcDrawn(app, i) || lit;
        // Cutscene actor slots wait parked at y 995..1000 or at the origin until a scene places
        // them (FrmSetc2) - but not a stage model (Omega's scenes bring a battle stage in as an
        // actor at the origin: d0550's deck is NPC 517, t0155bt), nor one a fade put back in mdl_put
        const bool stageModel = file.size() > 6 && file.compare(file.size() - 6, 6, "bt.bin") == 0;
        if (!stageModel && !lit && (n.pos[1] >= 900.0f || (n.id >= 400 && n.pos[0] == 0 && n.pos[1] == 0 && n.pos[2] == 0)) &&
            !app.placedNpcs.count(n.id)) // (a table NPC below 400 may stand at a room's origin: c101_d07's 142)
            drawn = false;
        // (hidden, and its model never read: npc_setup reads one only when the create flags pass, 1.0.0 35260)
        if (!drawn && !kept && !n.mdlRead && !npcCreateFlags(app, n)) continue;
        if (npcModel(app, file).meshes.empty()) continue;
        App::NpcDraw d;
        if (kept) d = *kept;
        d.model = file;
        d.world = npcWorld(n);
        d.id = n.id;
        d.index = int(i);
        (drawn ? app.npcDraws : app.npcHeld).push_back(d);
    }
    updateNpcJoints(app); // rebuilding draw instances must not detach Joint children
}

// RectWrapper's NPCs, read live each frame (SetupRect / getFloorHeight take the npc ctl's position as
// it is): solid ones (kind bit 2 set, bit 4 clear) wall the leader in with their box's four sides,
// floor ones (bit 4) lift it onto their top; both only while shown (_ChkNpcDisp). The box turns by
// the record's angle (RectWrapper::init), the position is the record's plus, for models 0..1, its
// offset (npc_setup).
std::vector<cr::WalkBody> walkBodies(const App& app) {
    std::vector<cr::WalkBody> out;
    for (size_t idx = 0; idx < app.npcs.size(); ++idx) {
        const cr::FieldNpc& n = app.npcs[idx];
        cr::WalkBody b;
        b.wall = (n.kind & 2) && !(n.kind & 4);
        b.floor = (n.kind & 4) != 0;
        if (!b.wall && !b.floor) continue;
        b.shown = npcVisible(app, idx) && // (story actors waiting off stage: hidden in the original)
                  !(n.pos[1] >= 900.0f || (n.id >= 400 && n.pos[0] == 0 && n.pos[1] == 0 && n.pos[2] == 0));
        const float k = n.model == 0 || n.model == 1 ? 1.0f : 0.0f;
        b.pos = {n.pos[0] + k * n.offset[0], n.pos[1] + k * n.offset[1], n.pos[2] + k * n.offset[2]};
        for (int c = 0; c < 3; ++c) b.offset[c] = n.offset[c], b.box[c] = n.box[c];
        b.angle = n.rectAngle;
        b.scriptModel = n.mdlRead;
        out.push_back(b);
    }
    return out;
}

// Each NPC instance has its own action and time: action 0 (idle) loops, others play once and
// hold their last frame (a chest lid stays open).
void animateNpcs(App& app, float dt) {
    for (auto& d : app.npcDraws) {
        const LoadedModel& lm = app.npcModels[d.model];
        if ((d.action < 0 || d.action >= int(lm.actions.size())) && !lm.packageActions.empty()) {
            float dur = 0; // only its materials move: they loop
            for (const auto& a : lm.packageActions) dur = std::max(dur, a.duration);
            d.time += dt * kFramesPerSecond;
            if (dur > 0) d.time = std::fmod(d.time, dur);
            continue;
        }
        if (d.action < 0 || d.action >= int(lm.actions.size())) continue;
        const float dur = lm.actions[d.action].duration;
        if (d.blend.step(d.action, d.time, dt * kFramesPerSecond, lm.actions)) continue; // (held at its first frame)
        d.time += dt * kFramesPerSecond;
        if (const char* tr = SDL_getenv("CR_NPC_TRACE"); tr && (SDL_atoi(tr) < 0 || d.id == SDL_atoi(tr))) // (debugging: an NPC's animation clock; -1 every one)
            std::printf("npc %d action %d time %.3f dur %.1f once %d loop %d..%d\n", d.id, d.action, d.time, dur, int(d.once), d.loopFrom, d.loopTo);
        if (dur <= 0) continue;
        if (d.once) { d.time = std::min(d.time, dur); continue; } // loop from -1: play once, hold the end
        const float end = d.loopTo > 0 && d.loopTo < dur ? float(d.loopTo) : dur;
        if (d.time >= end) { // then repeat frames loopFrom..loopTo
            const float start = std::clamp(float(d.loopFrom), 0.0f, end), len = end - start;
            d.time = len > 0 ? start + std::fmod(d.time - start, len) : start;
        }
    }
}

// Resolve Joint attachments parent-first, so chains do not depend on NPC id ordering.
// A missing/hidden parent or child leaves that instance at its regular NPC-table transform.
void updateNpcJoints(App& app) {
    if (app.npcJoints.empty() || app.npcDraws.empty()) return;

    std::set<int> resolved, resolving;
    std::function<void(int)> update = [&](int childId) {
        if (resolved.count(childId) || !resolving.insert(childId).second) return;

        const auto ji = app.npcJoints.find(childId);
        if (ji == app.npcJoints.end()) {
            resolving.erase(childId);
            resolved.insert(childId);
            return;
        }
        const int parentId = ji->second.first;
        const int userId = ji->second.second;

        // Resolve a parent that is itself attached before using its world matrix.
        if (parentId != childId && !resolving.count(parentId)) update(parentId);

        App::NpcDraw* parent = nullptr;
        std::vector<App::NpcDraw*> children;
        for (auto& d : app.npcDraws) {
            if (d.id == parentId) parent = &d; // retain the former last-match parent semantics
            if (d.id == childId) children.push_back(&d);
        }

        if (parent && !children.empty() && parentId != childId) {
            const auto modelIt = app.npcModels.find(parent->model);
            if (modelIt != app.npcModels.end()) {
                LoadedModel& pm = modelIt->second;
                const cr::Model& m = pm.cpu;
                int node = -1;
                for (const auto& record : m.records) {
                    if (record.user != userId) continue;
                    for (const auto& [objectId, nodeIndex] : m.objectNodes) {
                        if (objectId == record.id) {
                            node = nodeIndex;
                            break;
                        }
                    }
                    if (node >= 0) break;
                }
                if (node >= 0 && node < int(m.skeleton.nodes.size())) {
                    const cr::Action* act =
                        parent->action >= 0 && parent->action < int(pm.actions.size())
                            ? &pm.actions[size_t(parent->action)]
                            : nullptr;
                    poseWorld(m.skeleton, pm.actions, act, parent->time, parent->blend, pm.world);
                    if (node < int(pm.world.size())) {
                        const cr::Mat4 at = parent->world * pm.world[size_t(node)];
                        for (App::NpcDraw* child : children) child->world = at;
                    }
                }
            }
        }

        resolving.erase(childId);
        resolved.insert(childId);
    };

    for (const auto& [childId, attachment] : app.npcJoints) update(childId);
}


void drawNpcs(App& app, const cr::Mat4& vp) {
    for (auto& d : app.npcDraws) {
        if (d.alpha <= 0.001f) continue;
        LoadedModel& lm = app.npcModels[d.model];
        const cr::Model& m = lm.cpu;
        const bool matAnim = !lm.packageActions.empty();
        auto matOf = [&](size_t i) -> const cr::Material* {
            const int ap = i < m.meshes.size() ? m.meshes[i].appearance : -1;
            return ap >= 0 && ap < int(m.materials.size()) ? &m.materials[size_t(ap)] : nullptr;
        };
        const bool posed = !m.skeleton.nodes.empty() && lm.meshes.size() == m.meshes.size();
        if (posed || (matAnim && lm.meshes.size() == m.meshes.size())) { // skin this instance; scroll its textures
            const cr::Action* act = d.action >= 0 && d.action < int(lm.actions.size()) ? &lm.actions[d.action] : nullptr;
            if (posed) poseWorld(m.skeleton, lm.actions, act, d.time, d.blend, lm.world);
            for (size_t i = 0; i < m.meshes.size(); ++i) {
                float shift[2] = {0, 0}, scale[2] = {1, 1};
                const bool moves = matAnim && effectUvMotion(lm, matOf(i), d.time, shift, scale);
                const bool spins = spinsTexture(m, i);
                if (!posed && !moves && !spins) continue;
                if (posed) cr::skinMesh(m, m.meshes[i], lm.world, lm.skinned);
                const std::vector<float>& pos = posed ? lm.skinned : m.meshes[i].positions;
                if (moves || spins) { // uv * scale + offset (texture unit 0's: the material action's segment 6)
                    lm.uvScratch = m.meshes[i].uvs;
                    for (size_t k = 0; moves && k + 1 < lm.uvScratch.size(); k += 2) {
                        lm.uvScratch[k] = lm.uvScratch[k] * scale[0] + shift[0];
                        lm.uvScratch[k + 1] = lm.uvScratch[k + 1] * scale[1] + shift[1];
                    }
                    if (spins) spinUvs(lm.uvScratch, app.texSpin);
                }
                app.renderer.updatePositions(lm.meshes[i], pos, moves || spins ? lm.uvScratch : m.meshes[i].uvs, m.meshes[i].colors);
            }
        }
        if (matAnim) ++app.matAnimDraws[d.model];
        cr::Mat4 world; // (as a frame between the game's steps shows it)
        float alpha;
        shownNpc(app, d, world, alpha);
        drawLoaded(app, lm, vp * world, nullptr, alpha, matAnim ? d.time : -1.0f, 0, d.faded);
        modelDrawn(app);
    }
}

} // namespace crapp
