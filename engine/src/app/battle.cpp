#include "app/app.hpp"

namespace crapp {

// _btl_anm_load: the package slots a battle fighter's model has - those _btlanmtbl marks
// (_read_mdl_mem_mca2: model * 17 + slot, flat, so past the row's end the next model's), the party's
// 1, 4, 6 and 7 always (_read_mdl_set3), a monster's 6 when its record's +0x1c is set; the rest stay empty
void battleSlotsLoaded(LoadedModel& lm, int modelId, bool party, bool six) {
    int count = 0; // (the package's slots: its built-in actions' numbers)
    for (int i = 0; i < lm.builtin && i < int(lm.actions.size()); ++i) {
        const std::string& n = lm.actions[size_t(i)].name;
        if (n.rfind("built-in ", 0) == 0) count = std::max(count, SDL_atoi(n.c_str() + 9));
    }
    lm.slotOn.assign(size_t(std::max(count, 1)), 0);
    const signed char* table = &cr::kBattleAnimTable[0][0];
    const int total = cr::kNumBattleModels * cr::kBattleAnimTableWidth;
    for (int s = 0; s < int(lm.slotOn.size()); ++s) {
        const int at = modelId * cr::kBattleAnimTableWidth + s;
        lm.slotOn[size_t(s)] = (modelId >= 0 && at < total && table[at] != 0) || (party && (s == 1 || s == 4 || s == 6 || s == 7)) ||
                               (six && s == 6);
    }
}

int battleAction(const LoadedModel& lm, int slot) {
    if (!lm.slotOn.empty() && (slot < 0 || slot >= int(lm.slotOn.size()) || !lm.slotOn[size_t(slot)])) return -1; // (not loaded)
    const std::string want = "built-in " + std::to_string(slot + 1) + ".";
    for (int i = 0; i < lm.builtin && i < int(lm.actions.size()); ++i)
        if (lm.actions[size_t(i)].name.rfind(want, 0) == 0) return i;
    return -1;
}


App::Battle::Actor* battleActor(App& app, int fighter) {
    for (App::Battle::Actor& a : app.battle.actors)
        if (a.fighter == fighter) return &a;
    return nullptr;
}

// Plays action slot `slot` from its start; `then` >= 0 follows it once it ends (else it loops)
// _mdl_set_act_motion_nextanm: a slot whose motion is 3 frames or less (an empty one, or a 1-frame
// stand-in) passes to the next, round the model's slots, until one has more
int realSlot(const LoadedModel& lm, int slot) {
    int count = 0; // (the package's slots: its built-in actions' numbers)
    for (int i = 0; i < lm.builtin && i < int(lm.actions.size()); ++i) {
        const std::string& n = lm.actions[size_t(i)].name;
        if (n.rfind("built-in ", 0) == 0) count = std::max(count, SDL_atoi(n.c_str() + 9));
    }
    for (int k = 0; k < count; ++k) {
        const int s = (slot + k) % count, i = battleAction(lm, s);
        if (i >= 0 && lm.actions[size_t(i)].duration > 3) return s;
    }
    return slot;
}

void playSlot(App& app, App::Battle::Actor& a, int slot, int then) {
    const LoadedModel& lm = app.battle.models[a.model];
    slot = realSlot(lm, slot);
    a.slot = slot;
    a.action = battleAction(lm, slot);
    if (a.action < 0) { // the model hasn't that one: stand
        a.slot = btlui::kSlotIdle;
        a.action = battleAction(lm, btlui::kSlotIdle);
    }
    a.time = 0;
    a.then = then;
    a.loopFrom = a.loopTo = -1;
    a.castRest = false;
}

float actorDuration(App& app, const App::Battle::Actor& a) {
    const LoadedModel& lm = app.battle.models[a.model];
    return a.action >= 0 && a.action < int(lm.actions.size()) ? lm.actions[size_t(a.action)].duration : 0.0f;
}


cr::Vec3 actorHead(App& app, const App::Battle::Actor& a) {
    // __dmg_set: btlcamera_getpos(actor, 0x44e) - the model's node 1102 (else G+0xc0, the origin)
    bool found = false;
    const cr::Vec3 p = actorPoint(app, a, 1102, &found);
    return found ? p : cr::Vec3{0, 0, 0};
}

// btlcamera_set(id, actor, held): camera `id` (sys.evt section 15), its values over 9000 relative to
// fighter `actor`. A moving (raw[7]) or turning (raw[14]) camera starts its count again over raw[15]
// frames, unless the actor is -98..-1 (then nothing restarts and btlcamera_prc shows it still); a
// turning one given an actor under -98 makes one step of its turn (raw[15] negated, the actor 0); a
// turning one's 10000..10002 is btl +0xae8 (camHold: 25 for the casts, a boss's fall + 20), 10003 40
void setBattleCamera(App& app, int id, int actor, bool held) {
    App::Battle& b = app.battle;
    const cr::BattleCamera* c = app.battleData.camera(id);
    if (!c) return;
    const int16_t* r = c->raw;
    b.cam = id;
    b.camActor = actor;
    b.camHeld = held;
    b.camLive = true;
    ++b.camSets;
    if ((r[7] != 0 || r[14] != 0) && (actor >= 0 || actor < -98)) {
        b.camFrame = 0;
        b.camDur = r[15];
        if (r[7] == 0) {
            if (actor < 0) b.camDur = -r[15], b.camActor = 0;
            if (b.camDur >= 10000 && b.camDur <= 10002) b.camDur = b.camHold;
            if (b.camDur == 10003) b.camDur = 40;
        }
    }
}


// btlcamera_prc (end of btl_main), then btl_prc's eye and FUN_0000c56c / c614: the wanted eye and
// target - still (once; any negative actor), turning raw[14] degrees about the target over the
// count (the eye at the target + (sin yaw, -sin pitch, -cos yaw) x the whole 3D distance), or moving
// from the first pair of points to the second (raw[6] -> raw[13] the roll, 0x1fe..0x202 6 higher and
// 3 nearer); the count goes up before it's used. A camera ends when its count is over (a still one at
// once) but 10001 / 10002 hold and 0x48 turns on for ever. Unless set `held` (a target cursor's: 1/2),
// the shown eye and target chase the wanted by a fifth a frame for the formation (< 0x0e), victory
// (0x45..0x47), cast (0x49..0x5c) and 0x21b cameras once their count passes 1, and cut for the rest
void stepBattleCamera(App& app) {
    App::Battle& b = app.battle;
    b.camSnapped = false;
    if (b.camLive) {
        const cr::BattleCamera* c = app.battleData.camera(b.cam);
        if (!c) { b.camLive = false; return; }
        const int16_t* r = c->raw;
        cr::Vec3 base{0, 0, 0};
        if (b.camActor >= 0)
            for (const App::Battle::Actor& a : b.actors) // btlcamera_getpos: its node 1003 as it moves (else the actor)
                if (a.fighter == b.camActor) base = actorPoint(app, a, 1003);
        auto rel = [](int v, float at) { return v > 9000 ? float(v - 10000) + at : float(v); };
        b.camRoll = r[6];
        if ((r[7] == 0 && r[14] == 0) || b.camActor < 0) {
            b.camWantEye = {rel(r[0], base.x), rel(r[1], base.y), rel(r[2], base.z)};
            b.camWantTarget = {rel(r[3], base.x), rel(r[4], base.y), rel(r[5], base.z)};
            b.camLive = false;
        } else if (r[7] == 0) {
            const cr::Vec3 e{rel(r[0], base.x), rel(r[1], base.y), rel(r[2], base.z)};
            const cr::Vec3 t{rel(r[3], base.x), rel(r[4], base.y), rel(r[5], base.z)};
            const cr::Vec3 d = t - e;
            const float dist = std::sqrt(cr::dot(d, d)), flat = std::sqrt(d.x * d.x + d.z * d.z);
            ++b.camFrame;
            const float turn = 2.0f * 3.14159265f;
            float yaw = std::atan2(d.z, d.x) - turn * 0.25f; // (mceUtil3D_atan2 in turns, less a quarter)
            yaw += float(r[14]) * float(b.camFrame) / float(std::max(1, std::abs(b.camDur))) / 360.0f * turn;
            const float pitch = std::atan2(d.y, flat);
            b.camWantTarget = t;
            b.camWantEye = {t.x + std::sin(yaw) * dist, t.y - std::sin(pitch) * dist, t.z - std::cos(yaw) * dist};
            if (b.camFrame >= b.camDur) {
                if (r[15] == 10001 || r[15] == 10002) --b.camFrame;
                else if (b.cam == 0x48) b.camFrame = 0;
                else b.camLive = false;
            }
        } else {
            ++b.camFrame;
            const float k = b.camDur != 0 ? float(b.camFrame) / float(b.camDur) : 1.0f;
            auto at = [&](int i) { return float(r[i]) + float(r[i + 7] - r[i]) * k; };
            b.camWantEye = {at(0), at(1), at(2)};
            b.camWantTarget = {at(3), at(4), at(5)};
            if (b.cam >= 0x1fe && b.cam <= 0x202) b.camWantEye.y += 6, b.camWantEye.z += 3;
            b.camRoll = float(r[6]) + float(r[13] - r[6]) * k;
            if (b.camFrame >= b.camDur) b.camLive = false;
        }
        if (!b.camHeld) {
            const int id = b.cam;
            if (b.camFrame > 1 && (id < 0x0e || (id >= 0x49 && id <= 0x5c) || (id >= 0x45 && id <= 0x47) || id == 0x21b))
                b.camEase = 5;
            else b.camSnapped = true; // (_Clr_evcv: flag 0x800, cleared after the frame)
        }
    }
    if (b.camSnapped) b.camEye = b.camWantEye, b.camTarget = b.camWantTarget;
    b.camEye = b.camEye + (b.camWantEye - b.camEye) * (1.0f / b.camEase);
    b.camTarget = b.camTarget + (b.camWantTarget - b.camTarget) * (1.0f / b.camEase);
}

// The camera shown this frame: the eased eye and target, and the roll (degrees about the view)
void battleCameraView(App& app, cr::Vec3& eye, cr::Vec3& target, float* roll) {
    const App::Battle& b = app.battle;
    eye = b.camEye, target = b.camTarget;
    if (roll) *roll = b.camRoll;
    if (const char* o = SDL_getenv("CR_BATTLE_EYE")) { // debugging: "ex ey ez tx ty tz"
        if (std::sscanf(o, "%f %f %f %f %f %f", &eye.x, &eye.y, &eye.z, &target.x, &target.y, &target.z) == 6) return;
    }
}

// The cameras the menus and actions use (menub_mainPrc, btlord_make_select, btlact_*_init)
int monsterSize(const App& app, int fighter) {
    const cr::Fighter& f = app.battle.rules.f[size_t(std::clamp(fighter, 0, 4))];
    return f.def ? f.def->size : 1;
}
// read_btlcpl's per-slot camera index (0xd3220): formation row, enemy slot
int enemyCameraSlot(const App& app, int fighter) {
    static const short kSlot[16][3] = {{0, 0, 0},   {0, 0, 0},    {23, 24, 0},  {25, 0, 0},   {0, 0, 0},    {1, 2, 0},
                                       {3, 4, 5},   {6, 0, 0},    {7, 8, 0},    {9, 10, 11},  {12, 13, 0},  {14, 0, 0},
                                       {15, 16, 0}, {17, 18, 19}, {20, 21, 0},  {22, 0, 0}};
    if (fighter < 2) return 0;
    return kSlot[std::clamp(app.battle.formation, 0, 15)][std::clamp(fighter - 2, 0, 2)];
}

// (`cursor`: __menub_mainPrc's cursor move - the shown camera 1/2 of the way a frame, btlcamera_set(.., 1))
void cameraForTarget(App& app, int target, bool group, bool cursor) {
    App::Battle& b = app.battle;
    if (cursor) b.camEase = 2;
    if (target < 2) setBattleCamera(app, group ? 0x26 + (b.rules.pair ? 1 : 0) : b.rules.pair ? 30 : 28 + target, 0, cursor); // (_btlord_make_select: a party-wide scope's 0x26 + pair)
    else if (group) setBattleCamera(app, b.formation + 0x26, 2, cursor);
    else {
        const int size = monsterSize(app, target);
        setBattleCamera(app, size == 1 ? target + 0x1d : size + 0x20, target, cursor);
    }
}



int firstTarget(const cr::BattleTurn& t);
void cameraForTurn(App& app, const cr::BattleTurn& t) {
    App::Battle& b = app.battle;
    b.attackVariant = -1, b.attackCam2At = -1;
    const int user = t.actors[0], target = firstTarget(t);
    const bool userPair = t.actors[1] >= 0;
    if (t.kind == cr::kCmdAttack && target >= 0 && (user < 2) != (target < 2)) {
        // _btlact_attack_init: a variant drawn for every attack (groups 0x22d / 0x292: random(2) -> 0 / 2);
        // the target one of the party - the attacking monster's slot x 3 + 0x1b0 / 0x1b1 by the member hit
        // (0x1b2 the pair; an enemy pair's slot 0x19); a monster hit - its slot x 4 + the variant + 0x78
        // for member 0's blow, 0xe0 for member 1's, 0x148 the pair's
        int r = b.rules.random(3);
        if (b.rules.group == 0x22d || b.rules.group == 0x292) { r = b.rules.random(2); r += r == 1; }
        b.attackVariant = r;
        (void)userPair;
        if (target < 2) {
            const int slot = b.rules.enemyPair ? 0x19 : enemyCameraSlot(app, user);
            setBattleCamera(app, slot * 3 + (b.rules.pair ? 0x1b2 : target == 0 ? 0x1b0 : 0x1b1));
        } else
            setBattleCamera(app, enemyCameraSlot(app, target) * 4 + r + (b.rules.pair ? 0x148 : user == 0 ? 0x78 : 0xe0));
        return;
    }
    if (t.kind == cr::kCmdGene || t.kind == cr::kCmdItem || t.kind == cr::kCmdAttack) castCamera(app, t.kind, user, userPair);
}

// The camera on a gene's or item's user (or the pair) as it casts
void castCamera(App& app, int kind, int user, bool userPair) {
    app.battle.camHold = 25; // (_btlact_gene_init / _btlact_item_init: +0xae8 = 0x19, the turn's frames)
    const int first = kind == cr::kCmdItem ? 0x53 : 0x49;
    if (user < 2) setBattleCamera(app, userPair ? first + 2 : first + user, userPair ? 0 : user);
    else if (monsterSize(app, user) == 1) setBattleCamera(app, userPair ? first + 5 : user + first + 1, user);
    else setBattleCamera(app, monsterSize(app, user) + first + 4, user);
}

// _btlact_gene / _btlact_item at the hit (state 1000, 42152 / 42770; after a barrier's bounce state
// 0x100e on the users, now the targets): a side-wide action (scope 3 / 5) frames the first target's
// side - 0x67 / 0x68 (the pair) for the party, 0x69 / 0x6a (an enemy pair) for a lone-size monster,
// formation + 0x67 else; a single target its own - member + 0x5d (the pair's 0x5f), monster + 0x5e
// (an enemy pair's 0x62) or size + 0x61; some actions stay on the formation's view (formation + 0xc);
// at 1000 an action on its own user keeps the cast camera
void hitCamera(App& app, const cr::BattleTurn& t, int target, bool bounce) {
    App::Battle& b = app.battle;
    const cr::BattleActionDef* a = app.battleActions.get(t.action);
    if (!a || target < 0) return;
    if (a->scope == 3 || a->scope == 5) {
        const int id = target < 2 ? (b.rules.pair ? 0x68 : 0x67) : monsterSize(app, target) == 1 ? (b.rules.enemyPair ? 0x6a : 0x69) : b.formation + 0x67;
        return setBattleCamera(app, id, 0);
    }
    const int x = t.action;
    const bool still = bounce ? (x >= 0xd7 && x <= 0xd9) || x == 0xde || x == 0xe8 || x == 0x197 || x == 0x19a || (t.kind == cr::kCmdItem && x == 0xe0)
                       : t.kind == cr::kCmdItem ? x >= 0x76 && x <= 0x78
                                                : (x >= 0xd7 && x <= 0xd9) || x == 0xde || x == 0xe8 || x == 0xf0 || x == 0xf2 || x == 0x197 ||
                                                      x == 0x19a || x == 0x1b1 || x == 0x1b6 || x == 0x1fc || x == 0x1fe;
    if (still) return setBattleCamera(app, b.formation + 0xc, 0);
    if (!bounce && target == t.actors[0]) return;
    if (target < 2) {
        if (b.rules.pair) setBattleCamera(app, 0x5f, 0);
        else setBattleCamera(app, target + 0x5d, target);
    } else if (monsterSize(app, target) == 1) setBattleCamera(app, b.rules.enemyPair ? 0x62 : target + 0x5e, target);
    else setBattleCamera(app, monsterSize(app, target) + 0x61, target);
}


LoadedModel* effectModel(App& app, int id) {
    if (id < 0 || id >= effects::kEffects) return nullptr;
    if (auto it = app.effectModels.find(id); it != app.effectModels.end()) return it->second.meshes.empty() ? nullptr : &it->second;
    LoadedModel& out = app.effectModels[id];
    if (app.effBin.empty() && !cr::readFile(app.dataDir + "eff.bin", app.effBin)) return nullptr;
    auto u32 = [&](size_t p) {
        return p + 4 <= app.effBin.size() ? uint32_t(app.effBin[p]) | uint32_t(app.effBin[p + 1]) << 8 |
                                                 uint32_t(app.effBin[p + 2]) << 16 | uint32_t(app.effBin[p + 3]) << 24
                                           : 0u;
    };
    const uint32_t off = u32(size_t(id) * 8), size = u32(size_t(id) * 8 + 4);
    if (!size || size_t(off) + size > app.effBin.size()) return nullptr;
    const std::vector<uint8_t> part(app.effBin.begin() + off, app.effBin.begin() + off + size);
    cr::Model m;
    if (!cr::loadModelPackage(part, m)) return nullptr;
    // eff3d_set_with_pos[_and_ang] (read_mdl_mem): property bits 0 and 1 cleared - glColor colour 1 -
    // and no alpha test (threshold 0); the depth writes: drawEffects
    if (const char* dump = SDL_getenv("CR_EFFECT_DUMP"); dump && SDL_atoi(dump) == id) { // (debugging: an effect's materials)
        std::printf("effect %d: %zu meshes, %zu textures, %zu materials\n", id, m.meshes.size(), m.textures.size(), m.materials.size());
        for (const cr::Material& mt : m.materials)
            std::printf("  mat %d: diffuse %.2f %.2f %.2f %.2f emission %.2f %.2f %.2f blend %d/%d props %#x tex %d (%d) tex2 %d (%d) "
                        "shading %d cull %d order %d unit %#x %#x\n", mt.id, mt.diffuse[0], mt.diffuse[1], mt.diffuse[2], mt.diffuse[3],
                        mt.emission[0], mt.emission[1], mt.emission[2], mt.blendSrc, mt.blendDst, mt.properties, mt.textureRef,
                        mt.texBlend, mt.textureRef2, mt.texBlend2, mt.shading, mt.culling, mt.drawOrder, mt.unitWord[0], mt.unitWord[1]);
        for (const cr::Material& mt : m.materials) {
            std::printf("  mat %d uv:", mt.id);
            for (float v : mt.uvTransform) std::printf(" %.3g", v);
            std::printf("\n");
        }
        for (size_t k = 0; k < m.meshes.size(); ++k) {
            const auto& uv = m.meshes[k].uvs;
            float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
            for (size_t j = 0; j + 1 < uv.size(); j += 2)
                for (int c = 0; c < 2; ++c) lo[c] = std::min(lo[c], uv[j + c]), hi[c] = std::max(hi[c], uv[j + c]);
            std::printf("  mesh %zu: app %d tex %d, %zu uvs u %.3f..%.3f v %.3f..%.3f, %zu colours\n", k, m.meshes[k].appearance,
                        m.meshes[k].texture, uv.size() / 2, lo[0], hi[0], lo[1], hi[1], m.meshes[k].colors.size());

        }
        for (size_t k = 0; k < m.textures.size(); ++k) {
            const cr::Image& ti = m.textures[k];
            std::printf("  tex %zu: %dx%d alphaOnly %d noAlpha %d\n", k, ti.width, ti.height, int(ti.alphaOnly), int(ti.noAlpha));
            if (const char* dir = SDL_getenv("CR_EFFECT_DUMP_DIR")) { // raw RGBA, for magick -size WxH -depth 8 rgba:
                const std::string f = std::string(dir) + "/eff" + std::to_string(id) + "_" + std::to_string(k) + ".rgba";
                if (FILE* fp = std::fopen(f.c_str(), "wb")) std::fwrite(ti.rgba.data(), 1, ti.rgba.size(), fp), std::fclose(fp);
            }
        }
    }
    for (cr::Material& mt : m.materials) mt.unlit = 1, mt.alphaThreshold = 0;
    for (const auto& img : m.textures) {
        cr::Image t = img;
        if (app.video.hdTextures) applyHd(app, t);
        out.textures.push_back(app.renderer.upload(t));
    }
    for (const auto& mesh : m.meshes) out.meshes.push_back(app.renderer.upload(mesh));
    out.packageActions = m.actions;
    out.materialTable = boundActionTable(m.actions);
    // Its motion: the action table's action for the transform tree - one segment a node, and not one
    // of its materials' (9 segments, only segment 1's colour and 6 / 8's texture offset and scale:
    // they can come first - 30 of Omega's effects - and with 9 nodes they'd fit) - else the first
    // that fits the skeleton (or its first nodes)
    auto materials = [](const cr::Action& a) {
        for (size_t k = 0; k < a.segments.size(); ++k)
            for (const cr::Track& t : a.segments[k])
                if (!(k == 1 && t.type == 12) && !((k == 6 || k == 8) && (t.type == 1 || t.type == 11))) return false;
        return true;
    };
    for (auto& a : m.actions)
        if (out.actions.empty() && !materials(a) && a.segments.size() == m.skeleton.nodes.size() && !a.segments.empty())
            out.actions.push_back(a);
    for (auto& a : m.actions)
        if (out.actions.empty() && a.segments.size() == m.skeleton.nodes.size() && !a.segments.empty()) out.actions.push_back(a);
    for (auto& a : m.actions)
        if (out.actions.empty() && !a.segments.empty() && a.segments.size() <= m.skeleton.nodes.size())
            out.actions.push_back(a);
    m.actions.clear();
    out.action = out.actions.empty() ? -1 : 0;
    out.cpu = std::move(m);
    return out.meshes.empty() ? nullptr : &out;
}

// btlcamera_getpos: the world position of an actor's bone by its record user id (0: the actor)
cr::Vec3 actorPoint(App& app, const App::Battle::Actor& a, int user, bool* found) {
    if (found) *found = false;
    LoadedModel& lm = app.battle.models[a.model];
    const cr::Model& m = lm.cpu;
    int node = -1;
    if (user > 0)
        for (const auto& t : m.records)
            if (t.user == user)
                for (const auto& [obj, n] : m.objectNodes)
                    if (obj == t.id) node = n;
    if (node < 0 || m.skeleton.nodes.empty()) return a.pos;
    const cr::Action* act = a.action >= 0 && a.action < int(lm.actions.size()) ? &lm.actions[size_t(a.action)] : nullptr;
    poseWorld(m.skeleton, lm.actions, act, a.time, a.blend, lm.world);
    if (node >= int(lm.world.size())) return a.pos;
    if (found) *found = true;
    const float* w = lm.world[size_t(node)].m;
    const float c = std::cos(a.yaw), sn = std::sin(a.yaw);
    const float x = w[12] * a.scale, y = w[13] * a.scaleY, z = w[14] * a.scale;
    return {a.pos.x + c * x + sn * z, a.pos.y + y, a.pos.z - sn * x + c * z};
}

// eff3d_set_btl: effect `id` on fighter `fighter` (at its table node)
void spawnEffect(App& app, int id, int fighter) {
    App::Battle& b = app.battle;
    if (id < 0 || id >= effects::kEffects || b.effects.size() >= 32 || !effectModel(app, id)) return;
    const App::Battle::Actor* a = battleActor(app, fighter);
    if (!a) return;
    Effect e;
    e.id = id, e.pos = actorPoint(app, *a, effects::kNode[id]);
    // on a monster all but 2, 3 and 14 at its size class's scale (0xd8a12: 100 100 100 200 250 300),
    // and eff3d_set holds 0xd, 0x18 .. 0x20 (not 0x1a, 0x1d) and 0x26 at 100
    const cr::Fighter& f = b.rules.f[size_t(std::clamp(fighter, 0, 4))];
    const bool held = id == 0xd || id == 0x18 || id == 0x19 || id == 0x1b || id == 0x1c || id == 0x1e || id == 0x1f || id == 0x20 || id == 0x26;
    if (fighter > 1 && f.def && id != 2 && id != 3 && id != 14 && !held) {
        static const short kScale[6] = {100, 100, 100, 200, 250, 300};
        e.scale = float(kScale[std::clamp(f.def->rawSize, 0, 5)]) / 100.0f;
    }
    b.effects.push_back(e);
}

// eff3d_set_with_pos into `list` (32 slots): its handle, 0 when none was free
int startEffect(App& app, std::vector<Effect>& list, int id, cr::Vec3 at, bool loop, float scale, float yaw) {
    if (id < 0 || id >= effects::kEffects || list.size() >= 32 || !effectModel(app, id)) return 0;
    if (&list == &app.fieldEffects) ++app.fieldEffectCount[size_t(id)];
    Effect e;
    e.id = id, e.pos = at, e.loop = loop, e.scale = scale, e.yaw = yaw, e.handle = ++app.effectHandles;
    list.push_back(e);
    return e.handle;
}

// eff3d_set_btl, then +0xfc7 cleared: the effect keeps its own angles instead of facing the camera (the
// bosses' 0xe - btlact_434_init -, 0x1e / 0x1f - 441 -, 0x20 - 442 -, 0x19 / 0x1a - 509: Part 14 824.5 s, the
// fist's charge glow toward the pair)
void spawnEffectFixed(App& app, int id, int fighter) {
    const size_t before = app.battle.effects.size();
    spawnEffect(app, id, fighter);
    if (app.battle.effects.size() > before) app.battle.effects.back().keepAngles = true;
}
void spawnEffectAt(App& app, int id, cr::Vec3 at) { startEffect(app, app.battle.effects, id, at, false, 1.0f, 0.0f); }

void stopEffect(std::vector<Effect>& list, int handle) { // eff3d_delete
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Effect& e) { return handle && e.handle == handle; }), list.end());
}

void stepEffects(App& app, std::vector<Effect>& list) { // eff3d_prc: each ends with its action (a loop starts again)
    for (auto& e : list) {
        e.time += 1;
        auto it = app.effectModels.find(e.id);
        const float dur = it == app.effectModels.end() || it->second.actions.empty() ? 30 : it->second.actions[0].duration;
        if (e.loop && e.time >= std::max(dur, 1.0f)) e.time = std::fmod(e.time, std::max(dur, 1.0f));
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Effect& e) {
                                  auto it = app.effectModels.find(e.id);
                                  const float dur = it == app.effectModels.end() || it->second.actions.empty()
                                                        ? 30
                                                        : it->second.actions[0].duration;
                                  return !e.loop && e.time >= std::max(dur, 1.0f);
                              }),
               list.end());
}
void stepEffects(App& app) { stepEffects(app, app.battle.effects); }

// An effect material's action (its refGid): segment 1 the colour, 6 texture unit 0's transform, 8
// unit 1's (keyType 1 the offset, 11 the scale; segment 8 only on two-unit materials). The refGid is
// an action's gid in the bound table, not its place (_mceFigure_bindAction -> _mceActionTable_findAction:
// action +0x28 == refGid); none there, the Appearance has no action and keeps its own colours
// (effect 46's shards: gids 0, 15..19 and the tree's - refGids 1..14 name none)
const cr::Action* effectMaterialAction(const LoadedModel& lm, const cr::Material* mt) {
    const int64_t g = mt ? int64_t(int32_t(mt->refGid)) : -1;
    if (g < 0) return nullptr;
    for (const cr::Action& a : lm.packageActions)
        if (a.table == lm.materialTable && int64_t(a.gid) == g) return &a;
    return nullptr;
}

// A texture unit's motion at `time`: uv * scale + shift (false: it has none)
bool effectUvMotion(const LoadedModel& lm, const cr::Material* mt, float time, float shift[2], float scale[2], int unit) {
    const cr::Action* ma = effectMaterialAction(lm, mt);
    const size_t seg = unit == 0 ? 6 : 8;
    bool moves = false;
    if (ma && ma->segments.size() > seg)
        for (const cr::Track& t : ma->segments[seg]) {
            float v[4] = {};
            if (t.comps != 3 || !cr::evaluateTrack(t, std::min(time, ma->duration), v)) continue;
            if (t.type == 1) shift[0] = v[0], shift[1] = v[1], moves = true;
            if (t.type == 11) scale[0] = v[0], scale[1] = v[1], moves = true;
        }
    return moves;
}

// Texture unit 1 of a mesh (its material's second texture), and where it samples: uv2 maps the
// coordinates the mesh holds - unit 0's, moved - to unit 1's (uv * xy + zw). Null: none
const cr::Texture* secondUnit(const LoadedModel& lm, size_t mesh, const cr::Material* mt, float time, float uv2[4]) {
    const cr::Model& m = lm.cpu;
    if (!mt || mt->textureRef2 < 0 || mesh >= m.meshes.size()) return nullptr;
    const int t = m.meshes[mesh].texture2;
    if (t < 0 || t >= int(lm.textures.size())) return nullptr;
    float o0[2] = {0, 0}, s0[2] = {1, 1}, o1[2] = {0, 0}, s1[2] = {1, 1};
    if (time >= 0) effectUvMotion(lm, mt, time, o0, s0, 0), effectUvMotion(lm, mt, time, o1, s1, 1);
    for (int k = 0; k < 2; ++k) {
        const float r = s1[k] / (std::fabs(s0[k]) > 1e-6f ? s0[k] : 1.0f);
        uv2[k] = r, uv2[2 + k] = o1[k] - o0[k] * r;
    }
    return &lm.textures[size_t(t)];
}

// A battle's effects face the camera: mdl_put turns a model eff3d placed with its flag +0xfc7 about
// Y to the camera's heading, atan2(eye - target) in x and z, each frame - and eff3d_set_with_pos sets
// that flag only in a battle (gac +0x364 bit 5; CR1 sets it always). The field's keep their own yaw.
void drawEffects(App& app, const std::vector<Effect>& list, const cr::Mat4& vp, const cr::Vec3* eye, const cr::Vec3* target) {
    static const bool noScroll = SDL_getenv("CR_NO_UV_SCROLL") != nullptr; // tests: before / after
    for (const Effect& e : list) {
        LoadedModel* found = effectModel(app, e.id);
        auto it = app.effectModels.find(e.id);
        if (!found || it == app.effectModels.end()) continue;
        LoadedModel& lm = it->second;
        const cr::Model& m = lm.cpu;
        auto matOf = [&](size_t i) -> const cr::Material* {
            const int ap = i < m.meshes.size() ? m.meshes[i].appearance : -1;
            return ap >= 0 && ap < int(m.materials.size()) ? &m.materials[size_t(ap)] : nullptr;
        };
        auto materialAction = [&](const cr::Material* mt) { return effectMaterialAction(lm, mt); };
        auto uvMotion = [&](const cr::Material* mt, float shift[2], float scale[2]) {
            return effectUvMotion(lm, mt, e.time, shift, scale);
        };
        if (lm.meshes.size() == m.meshes.size()) {
            const bool posed = !m.skeleton.nodes.empty();
            if (posed) cr::computeWorld(m.skeleton, lm.actions.empty() ? nullptr : &lm.actions[0], e.time, lm.world);
            for (size_t i = 0; i < m.meshes.size(); ++i) {
                float shift[2] = {0, 0}, scale[2] = {1, 1};
                const bool moves = !noScroll && uvMotion(matOf(i), shift, scale);
                if (!posed && !moves) continue;
                cr::skinMesh(m, m.meshes[i], posed ? lm.world : std::vector<cr::Mat4>(), lm.skinned);
                if (moves) { // the texture scrolls (uv * scale + offset)
                    lm.uvScratch = m.meshes[i].uvs;
                    for (size_t k = 0; k + 1 < lm.uvScratch.size(); k += 2) {
                        lm.uvScratch[k] = lm.uvScratch[k] * scale[0] + shift[0];
                        lm.uvScratch[k + 1] = lm.uvScratch[k + 1] * scale[1] + shift[1];
                    }
                }
                app.renderer.updatePositions(lm.meshes[i], lm.skinned, moves ? lm.uvScratch : m.meshes[i].uvs, m.meshes[i].colors);
            }
        }
        // scaled, turned (Euler XYZ, mdl_put: R = Rz Ry Rx), at its point
        // (a battle's effects all face the camera; a field's only those FrmPlayEff flagged)
        const bool face = eye && target && ((&list == &app.battle.effects && !e.keepAngles) || e.faceCamera);
        const float yaw = face ? std::atan2(eye->x - target->x, eye->z - target->z) : e.yaw;
        const float cx = std::cos(e.pitch), sx = std::sin(e.pitch), cy = std::cos(yaw), sy = std::sin(yaw);
        const float cz = std::cos(e.roll), sz = std::sin(e.roll), k = e.scale;
        cr::Mat4 world;
        world.m[0] = cy * cz * k, world.m[1] = cy * sz * k, world.m[2] = -sy * k;
        world.m[4] = (sx * sy * cz - cx * sz) * k, world.m[5] = (sx * sy * sz + cx * cz) * k, world.m[6] = sx * cy * k;
        world.m[8] = (cx * sy * cz + sx * sz) * k, world.m[9] = (cx * sy * sz - sx * cz) * k, world.m[10] = cx * cy * k;
        world.m[12] = e.pos.x, world.m[13] = e.pos.y, world.m[14] = e.pos.z;
        const cr::Mat4 mvp = vp * world;
        // drawn last (draw order + 10000), depth tested; eff3d_set_with_pos clears the depth writes
        // (property bit 4), eff3d_set_with_pos_and_ang keeps each material's
        std::vector<size_t> order(lm.meshes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
            const cr::Material *a = matOf(x), *c = matOf(y);
            return (a ? a->drawOrder : 0) < (c ? c->drawOrder : 0);
        });
        for (size_t i : order) {
            if (i < m.meshes.size() && !m.meshes[i].visible) continue;
            const cr::Material* base = matOf(i);
            if (!base) continue;
            cr::Material mat = *base;
            if (const cr::Action* ma = materialAction(base); ma && ma->segments.size() > 1) // the colour track
                for (const cr::Track& t : ma->segments[1]) {
                    float v[4] = {1, 1, 1, 1};
                    if (t.type == 12 && t.comps == 4 && cr::evaluateTrack(t, std::min(e.time, ma->duration), v))
                        for (int k = 0; k < 4; ++k) mat.diffuse[k] = std::clamp(v[k], 0.0f, 1.0f);
                }
            if (mat.diffuse[3] <= 0.004f && mat.blended()) continue;
            if (!e.keepDepthWrite) mat.properties &= ~16u; // (eff3d_set_with_pos: no depth writes)
            app.effectDepthWrites += mat.depthWrite();
            const cr::GpuMesh& mesh = lm.meshes[i];
            const cr::Texture* tex = mesh.texture >= 0 && mesh.texture < int(lm.textures.size()) ? &lm.textures[size_t(mesh.texture)] : nullptr;
            float uv2[4];
            const cr::Texture* tex2 = secondUnit(lm, i, base, noScroll ? -1.0f : e.time, uv2);
            app.renderer.drawMesh(mesh, tex, mvp, mat, app.smoothFilter, tex2, uv2);
        }
    }
}
void drawEffects(App& app, const cr::Mat4& vp, const cr::Vec3& eye, const cr::Vec3& target) {
    drawEffects(app, app.battle.effects, vp, &eye, &target);
}

// read_btlcpl / btl_prty_init: the stage (kBattleStageFile), the pair at formation 0's first two
// places facing the enemies, the group's monsters at their formation's places; then the battle
// music (__bgmgo: var 58 when it names a track, else 8)
// ---- The battles' talks (_chk_karaok_*, EvrStart 2) ----
//
// In a battle the event script (PrcEvnt) runs only while flag 0xfff is set, and the battle waits
// meanwhile (btl_main's states check it); the HUD gives way to the portraits and the message
// box. Two kinds: an event battle's own script, waiting after its Battle, goes on when one of the
// story bosses' moments comes (by group and route, below) until it clears the flag; and in other
// battles the area's EvrStart 2 - the tutorials, keyed by var 1 - starts the first time each
// thing happens (flags 0xff1.. mark them seen), the battle's script ending with the flag.
namespace battletalk {

// Omega's chk_karaok_*: in event battles (+0xa6a) the story's talks (flag 0xfff, __menub_karaok_on)
// by group and scenario - at the start (frist), as a round ends (between), as the next opens
// (between_start), as the last enemy falls (end) - and its HP floors (dmg_hp)
bool on(const App& app) { return app.battle.active && app.game.flag(kFlag); }
void open(App& app) { app.game.setFlag(kFlag, true); } // (__menub_karaok_on)
bool any(int g, std::initializer_list<int> list) { return std::find(list.begin(), list.end(), g) != list.end(); }

// chk_karaok_dmg_hp (event battles, enemies only): until each moment's flag, the least HP its
// damage leaves - fighter 2's max HP x 80 / 60 / 40 / 20% then 1 in group 0x291, half then 1 in
// 0x2a4 / 0x2a5, 1 in the others listed, and 1 for a monster whose row's +0x3a is set
void rules(App& app) {
    App::Battle& b = app.battle;
    cr::BattleRules& r = b.rules;
    r.hpFloor = {};
    r.regenFull = {};
    if (!b.eventBattle) return;
    const int g = b.start.group, sc = app.game.scenario;
    auto fl = [&](int n) { return app.game.flag(n); };
    const int most = r.f[2].hpMax;
    int floor = 0;
    if (sc == 0) {
        if ((g == 0x28b && !fl(0x57b)) || (g == 0x28c && !fl(0x57d)) || (g == 0x28e && !fl(0x580)) ||
            (g == 0x290 && !fl(0x585)) || (g == 0x292 && !fl(0x58a)) || (g == 0x293 && !fl(0x58c)) ||
            (g == 0x294 && !fl(0x58f)))
            floor = 1;
        if (g == 0x291) {
            if (!fl(0x587)) floor = most * 80 / 100 + 1;
            else if (!fl(0x588)) floor = most * 60 / 100 + 1;
            else if (!fl(0x592)) floor = most * 40 / 100 + 1;
            else if (!fl(0x595)) floor = most / 5 + 1;
            else if (!fl(0x593)) floor = 1;
        }
    } else {
        if (g == 0x2a4) floor = !fl(0x780) ? most / 2 + 1 : !fl(0x781) ? 1 : 0;
        if (g == 0x2a5) floor = !fl(0x785) ? most / 2 + 1 : !fl(0x792) ? 1 : 0;
        if ((g == 0x297 && !fl(0x76f)) || (g == 0x298 && !fl(0x775)) || (g == 0x299 && !fl(0x777)) ||
            (g == 0x29c && !fl(0x77c)) || (g == 0x2a2 && !fl(0x789)))
            floor = 1;
    }
    for (int i = 2; i < 5; ++i) {
        const cr::Fighter& x = r.f[size_t(i)];
        const bool undying = x.def && int16_t(x.def->raw[0x3a] | x.def->raw[0x3b] << 8) >= 1; // the row's +0x3a
        r.hpFloor[size_t(i)] = std::max(floor, undying ? 1 : 0);
    }
    // btlcalc_status_prc: group 0x293's enemies whole again each round until flag 0x58c
    if (sc == 0 && g == 0x293 && !fl(0x58c))
        for (int i = 2; i < 5; ++i) r.regenFull[size_t(i)] = true;
}

// btl_main 300 -> 400: the battle's first moment, else the first battle's tutorial (0xff1)
bool tutorial(App& app, int key);
void first(App& app) {
    const App::Battle& b = app.battle;
    const int g = b.start.group;
    const bool talk = b.eventBattle && (app.game.scenario == 0
        ? any(g, {0x2e1, 0x28a, 0x2b7, 0x28c, 0x28d, 0x28f, 0x290, 0x291, 0x292, 0x293, 0x294}) || (g == 0x2ad && !app.game.flag(0xff1))
        : any(g, {0x297, 0x29a, 0x298, 0x29b, 0x29c, 0x2a4, 0x2a5, 0x2a2}));
    if (talk) open(app);
    else tutorial(app, 0xff1);
}
// btl_main 0x352: a round over and everyone standing (the round counter, enemy 2's HP); group 0x28d
// at half HP sets flag 0xffe (the battle's own end) instead
void between(App& app) {
    const App::Battle& b = app.battle;
    if (!b.eventBattle) return;
    const cr::BattleRules& r = b.rules;
    const int g = b.start.group, n = r.round, hp = r.f[2].hp, most = r.f[2].hpMax;
    auto fl = [&](int k) { return app.game.flag(k); };
    bool talk = false;
    if (app.game.scenario == 0) {
        talk |= (g == 0x28b && n == 3) || (g == 0x28c && hp <= most / 2 && !fl(0x57d)) || (g == 0x28e && n == 2) ||
                (g == 0x290 && n == 5) || (g == 0x292 && n == 3) || (g == 0x294 && (n == 5 || n == 9));
        if (g == 0x28d && hp <= most / 2) app.game.setFlag(0xffe, true);
        if (g == 0x291)
            talk |= ((r.f[0].lastAction == 0x1b5 || r.f[1].lastAction == 0x1b5) && !fl(0x587)) ||
                    (fl(0x587) && hp <= most * 80 / 100 && !fl(0x588)) || (fl(0x592) && hp <= most * 40 / 100 && !fl(0x595));
    } else
        talk |= (g == 0x297 && n == 1) || (g == 0x298 && n >= 1 && n <= 3) || (g == 0x299 && n == 1) || (g == 0x29c && n == 1) ||
                (g == 0x2a4 && fl(0x780) && hp <= most / 2 && !fl(0x781)) ||
                (g == 0x2a5 && fl(0x785) && hp <= most / 2 && !fl(0x792)) || (g == 0x2a2 && (n == 3 || n == 4));
    if (talk) open(app);
}
// btl_main 0: the next round's commands open
void betweenStart(App& app) {
    const App::Battle& b = app.battle;
    if (!b.eventBattle) return;
    const int g = b.start.group, n = b.rules.round;
    const bool talk = app.game.scenario == 0
        ? (g == 0x293 && n == 3) || (g == 0x290 && (n == 2 || n == 7)) || (g == 0x294 && n == 3)
        : (any(g, {0x297, 0x299, 0x2a4, 0x2a5}) && (n == 1 || n == 2)) || (g == 0x29c && n >= 1 && n <= 3) ||
              (g == 0x2a2 && n >= 4 && n <= 6);
    if (talk) open(app);
}
// btlord_chk 2: the enemies are down - a last word first? (btl_main 8000 waits for it)
bool end(App& app) {
    const App::Battle& b = app.battle;
    const int g = b.start.group;
    if (!b.eventBattle || !(app.game.scenario == 0 ? any(g, {0x293, 0x294}) : any(g, {0x29a, 0x29b, 0x2a4, 0x2a5, 0x2a2})))
        return false;
    open(app);
    return true;
}
// EvrStart 2 with var 1 = `key` (0xff1 the first battle, 0xff2 the Break, 0xff3..0xff5 an element
// taken / a status / a gene learned, 0xfa8 / 0xfa9 the Genes / Defend commands), once each: not
// while a script runs (an event battle's waits) or a talk is on
bool tutorial(App& app, int key) {
    if (!app.battle.active || app.game.flag(kFlag) || app.game.flag(key) || app.runner.running() || app.testNoTutorials)
        return false;
    const uint32_t at = app.events.returnEntry(2);
    if (!at || !app.runner.startAt(app.events, at, 0, app.game, app.area, app.mapNumber)) return false;
    app.runnerWhat = "battle tutorial";
    app.game.vars[1] = key;
    open(app);
    app.runner.setGate(kFlag);
    return true;
}
// A battle frame with the talk on: the script's step (its pages, portraits and pictures), the
// battle held. Once the script clears the flag an event battle's waits again; another ends.
void step(App& app) {
    if (!app.runner.running()) { app.game.setFlag(kFlag, false); return; }
    stepEvents(app);
    if (app.game.flag(kFlag)) return;
    if (!app.battle.eventBattle) app.runner.stop();
}
} // namespace battletalk

// MuCom_GetViegOlgarCheck: record 0 is Olgar - scenario 1, or scenario 0 once flag 0x468 is set;
// MuCom_GetOlgarDanteCheck: scenario 1 (record 1 is Dante there)
bool viegIsOlgar(const App& app) { return app.game.scenario == 1 || (app.game.scenario == 0 && app.game.flag(0x468)); }
bool olgarIsDante(const App& app) { return app.game.scenario == 1; }

// MuStr_GetCharNameId: a record's name in font block 7 (188 Vieg, 189 Olgar, 190 Vahti, 191 Dante)
int charNameId(const App& app, int c) {
    if (c == 0 && viegIsOlgar(app)) return 0xbd;
    if (c == 1 && olgarIsDante(app)) return 0xbf;
    return c + 0xbc;
}

// btl_prty_init: a record's battle model - j009a / j010 / j011 (record + 8), j009b for record 0
// while it is Olgar, j007 for the guest beside (slot 1, record 3)
int battleModelOf(const App& app, int c, int slot) {
    if (slot == 1 && c == 3) return 6;
    if (c == 0 && viegIsOlgar(app)) return 11;
    return std::clamp(c + 8, 0, cr::kNumBattleModels - 1);
}

// _btl_in_eff (5977): the sound a battle opens with, by its group - 0xa9 for the bosses' and story
// fights' pairs (9-10, 0x2a-0x2b, 0x48-0x49, 0x7a-0x7b, 0xac-0xad, 0x154-0x163, 0x168-0x16f, 0x179),
// 0xaa for 0x104-0x106, 0x118-0x11b, 0x12c-0x12e, 0x140-0x142, 0x170, 0x173, 0x176, 0x178, 0xab for
// 0x164-0x167, else 0xa8 (sound effects, not the btlvoice table's)
int battleInSound(int g) {
    for (int base : {9, 0x2a, 0x48, 0x7a, 0xac})
        if (g == base || g == base + 1) return 0xa9;
    if ((g >= 0x154 && g <= 0x163) || (g >= 0x168 && g <= 0x16f) || g == 0x179) return 0xa9;
    if ((g >= 0x104 && g <= 0x106) || (g >= 0x118 && g <= 0x11b) || (g >= 0x12c && g <= 0x12e) || (g >= 0x140 && g <= 0x142) ||
        g == 0x170 || g == 0x173 || g == 0x176 || g == 0x178)
        return 0xaa;
    if (g >= 0x164 && g <= 0x167) return 0xab;
    return 0xa8;
}

// read_btlcpl (6526-6529): a monster's name row (+0xaa) - its id - 1, but 0x106 (Dante's) for model
// 0xa5 (m010: monster rows 125 and 140) while MuCom_GetOlgarDanteCheck holds
int monsterNameRow(const App& app, const cr::MonsterDef& m) {
    return m.model == 0xa5 && olgarIsDante(app) ? 0x106 : m.id - 1;
}


void startBattle(App& app, const cr::BattleStart& start, int flags) {
    app.renderer.resetHistory(); // a cut: the upscaler starts over
    App::Battle& b = app.battle;
    const std::array<int, 3> ids = app.battleData.group(start.group);
    if (!app.battleData.loaded() || !ids[0] || start.stage < 0 || start.stage >= cr::kNumBattleStages) {
        app.unsupported.push_back("battle " + std::to_string(start.group) + " on map " + std::to_string(app.mapNumber));
        return;
    }
    app.sound.stopDialogueVoices();
    app.voiceCue = 0;
    b.active = true;
    b.start = start;
    b.flags = flags;
    b.eventBattle = false;
    b.breakFx = {};
    app.game.setFlag(0xffd, false), app.game.setFlag(0xffe, false); // btl_main 0: the defeat's and the story end's flags
    app.runner.setGate(battletalk::kFlag); // PrcEvnt in battle mode
    // CRView::prc's encounter frame (1283-1284): one get_random(2), then SetFade(white, 255, 255, 1 ms) -
    // solid white at once; btl_main 0 -> 10 -> 100 -> 199 -> 200 run under it, and 200 starts the
    // fade in (SetFade(white, 255, 0, 1000 ms): the Intro phase, kIntroWhite steps on)
    app.rng.get(2);
    app.fadeGrey = 1, app.fadeFrom = 1, app.fadeTo = 1, app.fadeFrames = 20, app.fadeT = 0;
    b.frames = 0;
    b.clock = 0;
    b.formation = std::max(0, app.battleData.formation(ids));
    if (SDL_getenv("CR_BATTLE_DEBUG"))
        std::printf("battle %d: monsters %d %d %d, formation %d\n", start.group, ids[0], ids[1], ids[2], b.formation);
    b.actors.clear();
    b.phase = App::Battle::Intro;
    b.phaseFrames = 0;
    b.introStep = 300;
    b.menu = App::Battle::Top;
    b.cursor = 0, b.topCursor = 0, b.cmdCursor[0] = b.cmdCursor[1] = 0; // (__menub_mainSet)
    b.member = 0;
    b.view = {};
    b.viewState = 0, b.slide = battleSlideDistance(app), b.slideHold = 0, b.menuGone = 100, b.roundWait = false, b.hudSlide = 0;
    b.analyze = {};
    b.turnOn = false, b.pairHeld = false;
    b.handover = 0, b.breakDue = false, b.tutorialDue = 0, b.nullHitDue = false;
    for (App::Battle::Popup& p : b.popups) p = App::Battle::Popup();
    b.banner.clear();
    b.bannerFrames = 0;
    b.bannerShown.clear();
    b.notices.clear();
    b.levelled.clear();
    b.settled = false;
    b.exit = 0;
    b.quake = {};
    b.voiceRng = uint32_t(SDL_GetTicks()) | 1u;
    for (int& v : b.voiceVariant) { b.voiceRng = b.voiceRng * 1103515245u + 12345u; v = int((b.voiceRng >> 16) % 3); } // reset_variants
    for (bool& c : b.commandSaid) c = false;
    b.voiceRound = -1, b.voiceLeader = 0, b.winWait = 0;
    b.flash = {};
    b.stageAction = 0;
    b.breakPop = 0;
    b.bossEnd = false, b.bossFighter = 2, b.bossKey = 0, b.bossModel = -1;
    if (start.group == 0x294) app.game.setFlag(0x596, false); // (read_btlcpl: set again as its boss falls)
    if (start.group == 0x2a5) app.game.setFlag(0x791, false);
    b.gaugeShown = 0;
    b.cam = -1, b.camActor = 0, b.camFrame = 0, b.camDur = 0, b.camHold = 25, b.camLive = false, b.camHeld = false;
    b.camEase = 1, b.camRoll = 0, b.camSnapped = true;
    b.camWantEye = b.camEye = {488, 12, 555}, b.camWantTarget = b.camTarget = {500, 4, 498};
    loadModelFile(app, cr::kBattleStageFile[start.stage], b.stage, false);
    readMdlHdRules(b.stage, start.stage);
    auto add = [&](int modelId, const short* place, float yaw, int fighter) -> App::Battle::Actor& {
        App::Battle::Actor a;
        a.modelId = modelId;
        a.model = cr::kBattleModelFile[modelId];
        if (SDL_getenv("CR_BATTLE_DEBUG")) std::printf("battle %d: fighter %d model %d %s\n", start.group, fighter, modelId, a.model.c_str());
        if (!b.models.count(a.model)) loadModelFile(app, a.model, b.models[a.model], true);
        if (fighter >= 0 && fighter < 5) battleSlotsLoaded(b.models[a.model], modelId, fighter < 2, fighter >= 2 && b.rules.f[size_t(fighter)].def && b.rules.f[size_t(fighter)].def->strongGrowth);
        a.pos = a.home = {float(place[0]), 0.0f, float(place[1])};
        a.yaw = a.homeYaw = a.wantYaw = yaw; // (+0xfb0 = 15: read_btlcpl 6598, btl_main 7270 / 7293)
        a.fighter = fighter;
        b.actors.push_back(a);
        playSlot(app, b.actors.back(), btlui::kSlotIdle);
        return b.actors.back();
    };
    // The rules: the pair from the party, the monsters grown to their level (the band: the
    // dungeon number, as the treasure boxes use it - the original keeps a per-route byte)
    const int route = std::clamp(app.playerChar / 2, 0, 3);
    cr::BattleRules& rules = b.rules;
    rules = cr::BattleRules();
    rules.useRandom(&app.rng); // the one stream, as the original's
    rules.strike = start.strike;
    rules.stage = start.stage;
    rules.group = start.group;
    rules.story = &app.game;
    rules.scenario = std::clamp(app.game.scenario, 0, cr::kScenarios - 1);
    rules.formation = b.formation;
    rules.allyAuto = rules.scenario == 0 && start.group == 695; // btlord_enemy_rand: ctx 0x2b6 - group 695, the guest - member 1 attacks on its own
    // __menub_mainPrc: Escape is dimmed and refused while a script runs (G+0x364 bit 0 - an
    // event battle), group 0x298 apart; the opcode's flags byte (0x8a53e) is never read
    rules.escapeBlocked = app.runner.running() && start.group != 0x298;
    (void)flags;
    rules.setupParty(app.game, app.itemTable, route);
    // (Options' Cheats. A battle the story has you lose - btl_main 0x4b0's 0x289 / 0x28e / 0x2a2 / 0x295: its script goes
    // on from the pair's fall - keeps them while it can be won, Infinite HP lapsing once no foe can fall (rules.infiniteHp);
    // btlord_chk's 0x28a / 0x2a3 end after their first round whatever the HP, so they need nothing)
    rules.storyLoss = start.group == 0x289 || start.group == 0x28e || start.group == 0x2a2 || start.group == 0x295;
    const bool cheatHp = app.cheats.hp;
    rules.cheatHp = cheatHp, rules.cheatMp = app.cheats.mp, rules.cheatOneHit = app.cheats.oneHit;
    rules.cheatBreak = app.cheats.fullBreak;
    if (rules.cheatBreak) rules.breakStarted = true, rules.breakTarget = rules.breakGauge = 8;
    rules.expScale = app.cheats.exp;
    for (int k = 0; k < 2; ++k) {
        if (cheatHp && rules.f[size_t(k)].present) rules.f[size_t(k)].hp = rules.f[size_t(k)].hpMax;
        if (app.cheats.mp && rules.f[size_t(k)].present) rules.f[size_t(k)].mp = rules.f[size_t(k)].mpMax;
    }
    // btl_enemy_init_lv: the scenario's level byte (the band the World Select chose)
    const int band = app.game.bands[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))];
    rules.setupSpecial(app.game, app.itemTable, start.group, route); // the story battles' line-ups
    rules.setupEnemies(app.battleData, ids, band);
    for (int k = 0; k < 2; ++k) // Uty_Btl_ParamUp01 / 02 called before this battle began
        if (app.paramUp[k]) {
            for (int st = cr::kStatusAttackUp; st <= cr::kStatusLuckUp; ++st) rules.f[size_t(k)].status[st] = 6;
            app.paramUp[k] = false;
        }
    for (int k = 0; k < 2; ++k) { // the pair (or who fights): j001..j008
        if (!rules.f[size_t(k)].present) continue;
        const int c = rules.f[size_t(k)].character;
        App::Battle::Actor& a = add(battleModelOf(app, c, k), cr::kBattlePosition[0][k], 3.14159265f, k);
        a.name = sysText(app, 58000 + charNameId(app, c)); // the name plates' character names
        a.level = rules.f[size_t(k)].level; // (btl_main's 0.5 at +0xf98 is the half turn, the yaw above)
    }
    for (int k = 0; k < 3; ++k) {
        const cr::MonsterDef* m = app.battleData.monster(ids[size_t(k)]);
        if (!m || m->model >= cr::kNumBattleModels || !rules.f[size_t(2 + k)].present) continue;
        App::Battle::Actor& a = add(m->model, cr::kBattlePosition[b.formation][k], 0.0f, 2 + k);
        if (m->model == 13 || m->model == 14 || m->model == 15 || m->model == 16) a.pos.z += 20; // read_btlcpl's nudges
        if (m->model == 32) a.pos.z -= 10;
        a.home = a.pos;
        a.scale = m->scaleXZ, a.scaleY = m->scaleY;
        a.enemy = true;
        a.monster = m->id;
        a.level = rules.f[size_t(2 + k)].level;
        a.hp = a.hpMax = uint32_t(rules.f[size_t(2 + k)].hpMax);
        a.name = sysText(app, 53000 + monsterNameRow(app, *m)); // font block 2: monster names
        // read_btlcpl (6560-6562): slot 0's frame get_random(btlanm_get_max_frm(slot 0)) - the
        // monsters' idles out of step
        a.time = float(app.rng.get(int(actorDuration(app, a) - 1.0f)));
    }
    if (start.group == 0x291 || start.group == 0x22c) {
        // read_btlcpl: these two bring btlcplname[28] (m127) in on model slot 4 at (500, 0, 465), no
        // fighter of its own (+0x23c set back to 0); btl_anm_load reads its action set 14 alone, which
        // btlact_440 plays - till then it stands at rest
        static const short kPropAt[2] = {500, 465};
        App::Battle::Actor& p = add(28, kPropAt, 0.0f, kPropFighter);
        p.action = -1, p.slot = 0, p.time = 0;
    }
    for (int i = 0; i < 5; ++i) {
        b.shownHp[i] = rules.f[size_t(i)].hp;
        b.shownMp[i] = rules.f[size_t(i)].mp;
        if (i < 2) b.rollHp[i] = 0, b.rollMp[i] = 0; // (btlpcparam_prty_init: shown +0x60 / +0x68 from 0 - the rows count up under the white, Part 3 1202.8 s)
    }
    setBattleCamera(app, std::max(0, b.formation - 2), -1); // (under the white; btl_main 200 sets it again)
    // btl_main 0: _btl_in_eff(group) - the battle's opening sound
    {
        const uint32_t id = uint32_t(battleInSound(start.group));
        app.sound.play(id);
        app.soundLog.push_back(id);
    }
    // btl_main 0: var 58 names the battle's track (_BGM_FILE index); 38 and over - a random
    // battle's 999 - play track 31 (bgm43); 997 leaves the field's music playing
    const auto v58 = app.game.vars.find(58);
    const int want = v58 != app.game.vars.end() ? v58->second : 999;
    const int track = want == 997 ? -1 : want >= 0 && want < cr::kBgmTracks ? want : 31;
    b.fieldTrack = app.lastFieldBgm; // (G+0x8a97c: the field's last play request, playing or not)
    if (track >= 0) playBgm(app, uint32_t(track));
    app.dialogue.clear();
    ++app.battlesFought;
    if (SDL_getenv("CR_BATTLE_DEBUG")) {
        for (size_t i = 0; i < b.stage.cpu.meshes.size(); ++i) {
            const int ap = b.stage.cpu.meshes[i].appearance;
            if (ap < 0 || ap >= int(b.stage.cpu.materials.size())) continue;
            const cr::Material& mt = b.stage.cpu.materials[size_t(ap)];
            std::printf("battle stage %s: mesh %zu draw order %d%s%s%s\n", cr::kBattleStageFile[start.stage], i, mt.drawOrder, mt.blended() ? " blended" : "",
                        mt.depthWrite() ? " depth-write" : "", b.stage.cpu.meshes[i].visible ? "" : " hidden");
        }
    }
    if (SDL_getenv("CR_BATTLE_DEBUG"))
        for (const auto& a : b.actors)
            std::printf("battle actor: fighter %d model %d %s (%zu meshes) at %.0f %.0f %.0f scale %.2f/%.2f\n", a.fighter, a.modelId,
                        a.model.c_str(), b.models[a.model].meshes.size(), a.pos.x, a.pos.y, a.pos.z, a.scale, a.scaleY);
    SDL_Log("battle: group %d on stage %s (formation %d, %s)", start.group, cr::kBattleStageFile[start.stage],
            b.formation, start.strike == 1 ? "first strike" : start.strike == 2 ? "ambush" : "normal");
    updateTitle(app);
}

// What the battle leaves in the save (once): MP back to the party, and when they won the
// experience (levels) and the Oz
void settleBattle(App& app) {
    App::Battle& b = app.battle;
    if (b.settled || !b.active) return;
    b.settled = true;
    // MbResultSet: the boards from where the members were, the takings, the items grouped
    App::Battle::Result& rs = b.result;
    rs = App::Battle::Result();
    for (int k = 0; k < 2; ++k) {
        const cr::Fighter& x = b.rules.f[size_t(k)];
        if (!x.present || x.guest) continue;
        const cr::PartyMember& pm = app.game.party[size_t(x.character)];
        rs.character[k] = x.character, rs.level[k] = pm.level, rs.exp[k] = float(pm.exp), rs.hp0[k] = x.hp;
    }
    b.levelled = b.rules.applyRewards(app.game);
    int most = 0;
    for (int k = 0; k < 2; ++k) {
        if (rs.character[k] < 0) continue;
        const cr::PartyMember& pm = app.game.party[size_t(rs.character[k])];
        rs.levelTo[k] = pm.level, rs.expTo[k] = float(pm.exp), rs.levels[k] = pm.level - rs.level[k];
        rs.expStep[k] = (rs.expTo[k] - rs.exp[k]) / float((rs.levels[k] + 1) * 60);
        most = std::max(most, rs.levels[k]);
        if (rs.levels[k] > 0) rs.ups.push_back({k, rs.levels[k]});
    }
    rs.count = (most + 1) * 60;
    rs.expTotal = b.rules.expGained * (b.rules.doubleExp ? 2 : 1) * std::max(1, b.rules.expScale); // (Cheats: EXP Multiplier)
    rs.ozTotal = b.rules.ozGained * (b.rules.doubleOz ? 2 : 1);
    rs.purseTo = app.game.oz;
    rs.shownPurse = float(std::max(0, rs.purseTo - (b.rules.outcome() == 2 ? rs.ozTotal : 0)));
    auto addItem = [&](int item) {
        for (auto& [it, n] : rs.items)
            if (it == item) { ++n; return; }
        rs.items.push_back({item, 1});
    };
    for (int item : b.rules.drops) addItem(item); // (a Steal Item catch is in the bag already and isn't listed)
}

// Back to the field: its music again, the encounter count restarted (fldencount_init), and the
// area's EvrStart 2 script (run after battles)
void endBattle(App& app, bool won) {
    app.renderer.resetHistory(); // a cut: the upscaler starts over
    app.sound.stopBattleLines(); // FUN_00092744 (btl_main 0x238b: after the results, as the party is written back)
    App::Battle& b = app.battle;
    settleBattle(app);
    std::string names;
    for (const auto& a : b.actors)
        if (a.enemy) names += (names.empty() ? "" : ", ") + (a.name.empty() ? "monster " + std::to_string(a.monster) : a.name);
    unloadLoaded(app, b.stage);
    for (auto& [name, lm] : b.models) unloadLoaded(app, lm);
    b.models.clear();
    for (auto& [id, lm] : app.effectModels) unloadLoaded(app, lm);
    app.effectModels.clear();
    b.effects.clear();
    app.fieldEffects.clear();
    b.actors.clear();
    b.active = false;
    app.renderer.destroy(b.gameOver);
    b.lostTap = false;
    { // fldread_prc's state -1 (the field read back): var 60 says what plays - 998 nothing (the
      // battle's music faded out at its end), 997 nothing new either (its music was never
      // faded: it plays on), under 100 that track, else the field's last
        const auto v60 = app.game.vars.find(60);
        const int after = v60 != app.game.vars.end() ? v60->second : 999;
        if (after == 998) playBgm(app, 1u << 29);
        else if (after == 997) {}
        else if (after >= 0 && after < 100) playBgm(app, uint32_t(after));
        else playBgm(app, b.fieldTrack >= 0 ? uint32_t(b.fieldTrack) : 1u << 29);
    }
    cr::EncounterTable table;
    const bool have = cr::encounterTable(app.events, app.mapNumber, table);
    app.encounters.enterMap(have ? &table : nullptr);
    resetFieldNpcs(app); // (the field read back: load flags | 0xd, npc_read_setup, then npc_setup)
    app.lastEvent = (won ? "won against " : "ran from ") + names;
    // Back on the field (fldread_prc): an event battle's script goes on from its Battle; after any
    // other the room's entry script (EvrStart 5) runs again, as the field reloads
    app.runner.setGate(-1);
    app.game.setFlag(battletalk::kFlag, false);
    // FieldScene::onLoading's SetMask2(1): the field revealed out of the battle's black over 10 frames
    app.fadeGrey = 0, app.fadeFrom = 1, app.fadeTo = 0, app.fadeFrames = 10, app.fadeT = 0;
    b.ending = false, b.endFrames = b.pendingExit = 0;
    if (b.eventBattle) {
        b.eventBattle = false;
    } else {
        app.runner.stop(); // (a battle tutorial cut short)
        app.pendingEntry = App::Entry::Room;
    }
    updateTitle(app);
}

void setBanner(App& app, const std::string& text, int frames = btlui::kBannerFrames) {
    app.battle.banner = text;
    app.battle.bannerFrames = text.empty() ? 0 : frames;
}


void battleMenu(App& app, App::Battle::Menu menu) {
    App::Battle& b = app.battle;
    b.menu = menu;
    ++b.page;
    b.cursor = 0;
    if (menu == App::Battle::Top) b.cursor = b.topCursor, b.cmdCursor[0] = b.cmdCursor[1] = 0;
    else if (menu == App::Battle::Commands) b.cursor = b.cmdCursor[b.rules.pair ? 0 : std::clamp(b.member, 0, 1)];
    b.pick = b.pickTop = 0;
    // the pages' own kept lines (the target page's 901, the escape question's 801) go with them; a
    // timed message stays until it runs out or the next replaces it - __menub_mainPrc clears slot 11
    // only there (Part 3 1319.36-1321.66 s: "Vieg's team attacks first!" stays over the first page
    // until "Run from battle?" replaces it)
    if (menu != App::Battle::Targets && b.bannerFrames > (1 << 19)) setBanner(app, "");
    // the camera: the field of battle for the first menu, the member (or the pair) choosing
    if (menu == App::Battle::Top || menu == App::Battle::EscapeAsk) setBattleCamera(app, b.formation + 12);
    else if (menu == App::Battle::Commands) {
        setBattleCamera(app, b.rules.pair ? 30 : 28 + b.member);
        btlvoice::command(app, b.rules.pair ? 0 : b.member); // __menub_mainPrc: the member's line
    }
}

// The fighters (or items) the menus offer
void battleOpenTargets(App& app, int action) {
    App::Battle& b = app.battle;
    const cr::BattleActionDef* a = app.battleActions.get(action);
    if (!a) return;
    b.picks = b.rules.targets(*a, b.member);
    if (b.picks.empty()) { menuSound(app, se::kBuzzer); return; }
    if (b.rules.isGroupScope(*a)) { // the whole side: no choosing (the plates show who)
    }
    b.menu = App::Battle::Targets;
    ++b.page;
    b.pick = 0;
    b.targetAction = action;
    cameraForTarget(app, b.picks[0], b.rules.isGroupScope(*a));
    menuSound(app, se::kChoose); // ("Target: ..." on the banner as the page comes: stepMenuView)
}

void battleStartRound(App& app) {
    App::Battle& b = app.battle;
    b.rules.beginRound(b.cmds, app.battleActions);
    app.sound.stopBattleLines(); // __menub_mainPrc 0x270e (the commands close): FUN_00092744, every line stopped
    for (int k = 0; k < 2; ++k) // defending holds the guard pose through the round
        if (b.rules.f[size_t(k)].guarding)
            if (App::Battle::Actor* a = battleActor(app, k)) playSlot(app, *a, btlui::kSlotGuard);
    b.phase = App::Battle::Acting;
    b.phaseFrames = 0;
    b.turnOn = false, b.pairHeld = false;
    b.roundWait = true; // (the menu goes first: menub_mainPrc 0x270e, then btl_main 500 / 600)
    // btl_main 500: the message queue's count (+0xae4) back to 0; the banner up stays - 500 / 600 /
    // 700 don't wait for it (only 799, between actions, does), and the first action's name replaces it
    // (Part 9 2972.18-2973.84 s: "Monster Assault!!" from the fade's start until the first action's
    // camera, 33 frames, with no blank frame before "Stress")
    b.notices.clear();
}

// A command is set: the second member's turn (solo), else the round
void battleCommandDone(App& app) {
    App::Battle& b = app.battle;
    menuSound(app, se::kChoose);
    if (!b.rules.pair && b.member == 0 && b.rules.f[1].alive() && !b.rules.allyAuto) {
        b.member = 1;
        battleMenu(app, App::Battle::Commands);
        return;
    }
    battleStartRound(app);
}

// __menub_mainPrc 3: the pair's places for the round - in pair mode side by side (_btlinitpos'
// second row), solo back where they began (the first; the second member only standing)
void placePair(App& app, bool pair) {
    App::Battle& b = app.battle;
    for (int k = 0; k < 2; ++k)
        if (App::Battle::Actor* a = battleActor(app, k); a && (pair || k == 0 || b.rules.f[size_t(k)].alive())) {
            const short* place = cr::kBattlePosition[pair ? 1 : 0][k];
            a->pos = a->home = {float(place[0]), 0.0f, float(place[1])};
        }
}

// The Vita's pad steps over what can't be chosen (FUN_810087fa's mask): Pair without a pair to
// make, Escape where it is barred; Items with no battle item held (0x15..0x2f)
bool battleTopBarred(const App& app, int i) {
    const App::Battle& b = app.battle;
    if (i == 1) return !b.rules.f[0].alive() || !b.rules.f[1].alive() || b.rules.allyAuto || b.rules.f[1].guest;
    return i == 2 && b.rules.escapeBlocked;
}
bool battleCommandBarred(const App& app, int i) {
    if (i != 2) return false;
    for (int n = 21; n <= 47; ++n)
        if (auto it = app.game.items.find(n); it != app.game.items.end() && it->second > 0) return false;
    return true;
}

void battleChooseTop(App& app, int i) {
    App::Battle& b = app.battle;
    b.topCursor = i;
    b.cmds[0] = b.cmds[1] = cr::BattleCommand();
    b.pendingItem = b.pendingGene = 0;
    switch (i) {
    case 0: // Solo
        b.rules.pair = false;
        placePair(app, false);
        if (!b.rules.f[0].alive() && b.rules.allyAuto) { battleStartRound(app); break; } // only the guest stands
        b.member = b.rules.f[0].alive() ? 0 : 1;
        menuSound(app, se::kChoose);
        battleMenu(app, App::Battle::Commands);
        break;
    case 1: // Pair: both standing (not with a guest)
        if (!b.rules.f[0].alive() || !b.rules.f[1].alive() || b.rules.allyAuto || b.rules.f[1].guest) { menuSound(app, se::kBuzzer); break; }
        b.rules.pair = true;
        placePair(app, true);
        b.member = 0;
        menuSound(app, se::kChoose);
        battleMenu(app, App::Battle::Commands);
        break;
    case 2: // Escape: asked first (57015)
        if (b.rules.escapeBlocked) { menuSound(app, se::kBuzzer); break; }
        menuSound(app, se::kChoose);
        battleMenu(app, App::Battle::EscapeAsk);
        b.confirm = 0;
        break;
    default: // Options (_SetOptionMenu): the Options screen over the battle, back here when it closes
        menuSound(app, se::kChoose);
        openOptions(app, 0);
        break;
    }
}

// The genes the member (in pair mode, either of the pair) can use, and the MP to pay with
std::vector<int> battleGeneList(const App& app) {
    const App::Battle& b = app.battle;
    std::vector<int> out = cr::usableAbilities(app.game, b.rules.f[size_t(b.rules.pair ? 0 : b.member)].character);
    if (b.rules.pair)
        for (int g : cr::usableAbilities(app.game, b.rules.f[1].character))
            if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
    std::sort(out.begin(), out.end());
    return out;
}
int battleGeneMp(const App& app) { // __GetBtlGeneMp: the lower of the pair's
    const App::Battle& b = app.battle;
    return b.rules.pair ? std::min(b.rules.f[0].mp, b.rules.f[1].mp) : b.rules.f[size_t(b.member)].mp;
}
int geneMpCost(const App& app, int ability) { // skill 132 halves it
    const cr::BattleActionDef* a = app.battleActions.get(cr::geneAction(ability));
    return a ? app.battle.rules.geneCost(*a, app.battle.rules.pair ? 0 : app.battle.member) : 0;
}
bool battleGenesSilenced(const App& app) {
    const App::Battle& b = app.battle;
    if (b.rules.pair) return b.rules.f[0].status[cr::kStatusSilence] || b.rules.f[1].status[cr::kStatusSilence];
    return b.rules.f[size_t(b.member)].status[cr::kStatusSilence] > 0;
}

void battleChooseGene(App& app, int ability) {
    App::Battle& b = app.battle;
    const cr::BattleActionDef* a = app.battleActions.get(cr::geneAction(ability));
    if (!a) return;
    if (battleGeneMp(app) < geneMpCost(app, ability)) { // 57081
        setBanner(app, sysText(app, 57081));
        menuSound(app, se::kBuzzer);
        return;
    }
    b.pendingGene = ability;
    cr::BattleCommand& c = b.cmds[b.rules.pair ? 0 : b.member];
    c = cr::BattleCommand();
    c.kind = cr::kCmdGene;
    c.action = a->id;
    if (b.rules.isGroupScope(*a) || a->scope == 1) {
        c.target = b.member;
        b.pendingGene = 0;
        battleCommandDone(app);
        return;
    }
    battleOpenTargets(app, a->id);
}

void battleChooseCommand(App& app, int i) {
    App::Battle& b = app.battle;
    b.cmdCursor[b.rules.pair ? 0 : std::clamp(b.member, 0, 1)] = i;
    cr::BattleCommand& c = b.cmds[b.rules.pair ? 0 : b.member];
    c = cr::BattleCommand();
    b.pendingItem = 0;
    switch (i) {
    case 0: battleOpenTargets(app, b.rules.attackAction(b.member)); break;
    case 1: // Genes: what the worn plates hold that's known
        b.picks = battleGeneList(app);
        if (b.picks.empty() || battleGenesSilenced(app)) { menuSound(app, se::kBuzzer); break; }
        menuSound(app, se::kChoose);
        b.menu = App::Battle::Genes;
        ++b.page;
        b.pick = b.pickTop = 0;
        battletalk::tutorial(app, 0xfa8); // the first time: what genes cost
        break;
    case 2: { // Items: what the bag holds of the battle items (21..47: get_btl_item_list's 0x15 .. 0x2f)
        b.picks.clear();
        for (int n = 21; n <= 47; ++n)
            if (auto it = app.game.items.find(n); it != app.game.items.end() && it->second > 0 && app.battleActions.get(n + 79))
                b.picks.push_back(n);
        if (b.picks.empty()) {
            setBanner(app, sysText(app, 57080));
            menuSound(app, se::kBuzzer);
            break;
        }
        menuSound(app, se::kChoose);
        b.menu = App::Battle::Items;
        ++b.page;
        b.pick = b.pickTop = 0;
        break;
    }
    case 3: // Defend: the guard pose at once (__menub_mainPrc: the loop motion starts as it is chosen);
        // the page goes, then 20 frames before the next (0x66 -> 199)
        c.kind = cr::kCmdDefend;
        if (App::Battle::Actor* a = battleActor(app, b.member)) playSlot(app, *a, btlui::kSlotGuard);
        if (b.viewState == 1 || b.viewState == 2) b.slideHold = 20;
        battletalk::tutorial(app, 0xfa9); // the first time: what it does
        battleCommandDone(app);
        break;
    default: break;
    }
}

void battleChooseItem(App& app, int item) {
    App::Battle& b = app.battle;
    const cr::BattleActionDef* a = app.battleActions.get(item + 79);
    if (!a) return;
    b.pendingItem = item;
    cr::BattleCommand& c = b.cmds[b.rules.pair ? 0 : b.member];
    c = cr::BattleCommand();
    c.kind = cr::kCmdItem;
    c.item = item;
    if (b.rules.isGroupScope(*a) || a->scope == 1) {
        c.target = b.member;
        battleCommandDone(app);
        return;
    }
    battleOpenTargets(app, item + 79);
}

void battleChooseTarget(App& app, int fighter) {
    App::Battle& b = app.battle;
    cr::BattleCommand& c = b.cmds[b.rules.pair ? 0 : b.member];
    if (b.pendingItem) {
        c.kind = cr::kCmdItem;
        c.item = b.pendingItem;
    } else if (b.pendingGene) {
        c.kind = cr::kCmdGene;
        c.action = cr::geneAction(b.pendingGene);
        b.pendingGene = 0;
    } else
        c.kind = cr::kCmdAttack;
    c.target = fighter;
    setBanner(app, "");
    battleCommandDone(app);
}

void battleBack(App& app) {
    App::Battle& b = app.battle;
    switch (b.menu) {
    case App::Battle::Commands:
        if (!b.rules.pair && b.member == 1 && b.rules.f[0].alive()) {
            b.member = 0;
            b.cursor = b.cmdCursor[0]; // (the first member's own cursor)
            ++b.page;
            setBattleCamera(app, 28);
        } else
            battleMenu(app, App::Battle::Top);
        break;
    case App::Battle::Targets: {
        const bool item = b.pendingItem != 0, gene = b.pendingGene != 0;
        const int chosen = item ? b.pendingItem : b.pendingGene;
        battleMenu(app, item ? App::Battle::Items : gene ? App::Battle::Genes : App::Battle::Commands);
        if (item) b.pendingItem = 0;
        if (gene) b.pendingGene = 0, b.picks = battleGeneList(app);
        if (item) {
            b.picks.clear();
            for (int n = 21; n <= 47; ++n)
                if (auto it = app.game.items.find(n); it != app.game.items.end() && it->second > 0 && app.battleActions.get(n + 79))
                    b.picks.push_back(n);
        }
        // (_MuScrList_BattleSetItem / RestorePos: the list again on the one just chosen)
        for (size_t k = 0; (item || gene) && k < b.picks.size(); ++k)
            if (b.picks[k] == chosen) {
                b.pick = int(k);
                b.pickTop = std::max(0, b.pick - btlui::kItemRows + 1);
                break;
            }
        break;
    }
    case App::Battle::Items: case App::Battle::Genes: battleMenu(app, App::Battle::Commands); break;
    case App::Battle::EscapeAsk: battleMenu(app, App::Battle::Top); break;
    default: return;
    }
    menuSound(app, se::kBack);
}

void battleEscapeAnswer(App& app, bool yes, bool cancelled) {
    App::Battle& b = app.battle;
    menuSound(app, cancelled ? se::kBack : se::kChoose); // (ComUty_YesNoChoiceProc: 0x93 for either; the Vita's × 0x94)
    if (!yes) { // 0x322: the page goes, then the first page comes in again (state 1)
        battleMenu(app, App::Battle::Top);
        return;
    }
    b.rules.pair = false;
    b.cmds[0] = b.cmds[1] = cr::BattleCommand();
    b.cmds[0].kind = cr::kCmdEscape;
    // the escape is the pair's one action this round (both members' commands become it)
    b.rules.pair = true;
    battleStartRound(app);
    b.rules.pair = false;
}

// ---- Playing the round (btlact_*) ----

// _btlact_attack_init's mdl_ang_y (12444-12698): the attacker's wanted yaw toward `target` - turned
// to at 15 degrees a frame by mdl_put, not snapped
void faceFighter(App& app, App::Battle::Actor& a, int target) {
    if (const App::Battle::Actor* t = battleActor(app, target))
        if (t != &a) a.wantYaw = std::atan2(t->pos.x - a.pos.x, t->pos.z - a.pos.z);
}

// ---- Screen quakes and flashes (Quake, SetFade_back) ----

// StartQuake(pixels, from %, to %, frames, hold): the offset's size runs from -> to over the
// frames; a held quake goes on at its end until EndQuake, and a passing one doesn't replace it
void startQuake(App::Battle::Quake& q, float amp, float from, float to, int frames, bool hold) {
    if (!hold && q.mode == 2) return;
    q.amp = std::fabs(amp);
    q.now = q.from = std::clamp(from, 0.0f, 100.0f);
    q.to = std::clamp(to, 0.0f, 100.0f);
    q.frame = 0;
    q.frames = std::max(1, frames);
    q.mode = hold ? 2 : 1;
}

void startQuake(App& app, float amp, float from, float to, int frames, bool hold) {
    startQuake(app.battle.quake, amp, from, to, frames, hold);
}

void endQuake(App::Battle::Quake& q, float amp, float from, float to, int frames) { // EndQuake: dies away from -> to
    startQuake(q, amp, from, to, frames, true);
    q.mode = 1;
}
void endQuake(App& app, float amp, float from, float to, int frames) { endQuake(app.battle.quake, amp, from, to, frames); }

// Quake::update, once a frame: each axis 75..100% of the size now, against its last direction
// (and turned again one time in 11)
void stepQuake(App::Battle::Quake& q) {
    if (!q.mode) {
        q.off[0] = q.off[1] = 0;
        return;
    }
    for (float& o : q.off) {
        q.rng = q.rng * 1103515245u + 12345u;
        const int r = int((q.rng >> 16) & 0x7fff);
        float v = float(r % 26 + 75) * 0.01f * q.amp * q.now * 0.01f;
        if (o * v >= 0) v = -v;
        if (r % 11 == 0) v = -v;
        o = v;
    }
    q.now = q.from + (q.to - q.from) * float(q.frame) / float(q.frames);
    if (++q.frame >= q.frames) {
        q.frame = q.frames;
        if (q.mode == 1 || std::fabs(q.now) < 1e-4f) q.mode = 0;
    }
}

void stepQuake(App& app) { stepQuake(app.battle.quake); }

// SetFade_back(colour, from, to, ms): white over the 3D, 50 a frame of the time (PutMask)
void setFlash(App& app, float from, float to, int ms) {
    App::Battle::Flash& f = app.battle.flash;
    f.on = true;
    f.from = from;
    f.to = to;
    f.at = 0;
    f.total = std::max(1, ms);
}

void stepFlash(App& app) {
    App::Battle::Flash& f = app.battle.flash;
    if (!f.on) return;
    f.alpha = f.from + (f.to - f.from) * float(f.at) / float(f.total);
    f.at += 50;
    if (f.at >= f.total) {
        f.at = f.total;
        if (f.alpha <= 0) f.on = false;
    }
}

// ---- The bosses' staged actions (btlact_main's kinds 10..22: btlact_430..443, 509) ----
// Each runs its own timeline instead of the gene's: the user's motion, a run of cameras (the
// next as the last one ends), effects on the user, the targets or a point, sounds, quakes and
// flashes at fixed frames, the hit (btlcalc_prc), then a wait. btlai_prc gives an action its
// kind (the set moves _btlque_special picks among them); the older five (430..436) hit and then
// wait, the later ones (437..) play the motion to its end - the hit somewhere in it - then go to
// the formation's camera. Effects and sounds are Omega's own numbers.
int stagedKind(int action) {
    switch (action) {
    case 430: case 503: return 430;
    case 432: case 505: return 432;
    case 434: case 507: return 434;
    case 435: case 241: return 435;
    case 436: return 436;
    case 437: return 437;
    case 439: case 440: case 441: case 442: case 443: return action;
    case 509: case 511: return 509;
    default: return 0;
    }
}
bool isStagedAction(int action) { return stagedKind(action) != 0; }

bool cameraDone(const App& app) { // btlcamera_prc clears +0xa88 when its frames are over
    return !app.battle.camLive;
}

std::vector<int> turnTargets(const cr::BattleTurn& t) { // the action's targets (+0x7a), each once
    std::vector<int> out; // (not the users' own silent losses - Risk Breaker's quarter, 0412's give-up)
    for (const cr::BattleHit& h : t.hits)
        if (!h.silent && std::find(out.begin(), out.end(), h.target) == out.end()) out.push_back(h.target);
    return out;
}
// que +0x8e: the action's first target - _btlact_attack_init's camera, approach and facing key on it,
// not on a user's own loss pushed first (Risk Breaker: Part 12 311.92 s, the pair runs in at the boss)
int firstTarget(const cr::BattleTurn& t) {
    for (const cr::BattleHit& h : t.hits)
        if (!h.silent) return h.target;
    return t.hits.empty() ? -1 : t.hits[0].target;
}

void startStaged(App& app, const cr::BattleActionDef* a) {
    App::Battle& b = app.battle;
    const cr::BattleTurn& t = b.turn;
    const int user = t.actors[0];
    App::Battle::Actor* u = battleActor(app, user);
    const int cls = a ? a->cls : 8;
    const int kind = stagedKind(t.action);
    b.stageAction = kind;
    b.stageCams = 0;
    b.stageWait = 40;
    b.stageEnd = 0;
    b.stageChain = 0;
    if (u && !t.hits.empty()) faceFighter(app, *u, firstTarget(t));
    // The later ones: the slot played once (mdl_set_act_motion_nextanm), its length the motion's
    // (btlanm_get_max_frm), the cameras chained from `base` while the next exists
    auto played = [&](int base, int after) {
        if (u) playSlot(app, *u, cls, btlui::kSlotIdle);
        const int len = u ? std::max(1, int(actorDuration(app, *u))) : 60;
        b.stageMotionEnd = len;
        b.stageEnd = len + after;
        b.stageChain = base;
        setBattleCamera(app, base);
    };
    switch (kind) {
    case 430: // the charge (effect 13 on the user), the shot, two flashes
        if (u) playSlot(app, *u, cls, btlui::kSlotIdle);
        b.hitFrame = 140;
        spawnEffect(app, 0xd, user);
        app.sound.play(0x5e);
        setBattleCamera(app, 0x207);
        break;
    case 432: { // the slot's loop, 45 frames past its start
        app.sound.play(0xb8);
        if (u) playSlot(app, *u, cls, -1);
        const cr::BattleAnim an = app.battleData.anim(u ? u->modelId : 0, cls);
        b.hitFrame = an.loopStart + 45;
        setBattleCamera(app, 0x1fe);
        break;
    }
    case 435: { // 435 itself: frames 103..113 looped, its cameras; 241: the slot's loop, a quick one
        const bool own = t.action == 435;
        if (own) app.sound.play(0xeb);
        app.sound.play(0xec);
        if (u) {
            playSlot(app, *u, cls, -1);
            const cr::BattleAnim an = app.battleData.anim(u->modelId, cls);
            u->loopFrom = own ? 103 : an.loopStart;
            u->loopTo = own ? 113 : an.loopEnd;
        }
        b.stageSkip = own ? 0 : 100; // (the counter starts at 100: the same frames, 100 sooner)
        b.hitFrame = 163 - b.stageSkip;
        b.stageWait = own ? 100 + 40 : 102 + 40; // (to 263 / 265 on the counter, then 40)
        if (own) setBattleCamera(app, 0x215);
        else setBattleCamera(app, b.formation + 12);
        break;
    }
    case 434:
        if (u) playSlot(app, *u, cls, btlui::kSlotIdle);
        b.hitFrame = 130;
        b.stageWait = 65;
        spawnEffectFixed(app, 0xe, user);
        app.sound.play(0x45);
        setBattleCamera(app, 0x20c);
        break;
    case 436:
        app.sound.play(0x66);
        app.sound.play(0xeb);
        if (u) playSlot(app, *u, cls, btlui::kSlotIdle);
        b.hitFrame = 61;
        b.stageWait = 50;
        setBattleCamera(app, 0x210);
        break;
    case 437: // the charge on the user (0x26); hit at 215
        spawnEffect(app, 0x26, user);
        played(0x229, 10);
        b.hitFrame = 0xd7;
        break;
    case 439: played(0x233, 40); b.hitFrame = 0x62; break;
    case 440: // btlact_440_init: groups 0x291 / 0x22c's extra model on to its slot 14 as the user's motion starts
        played(0x23d, 40);
        b.hitFrame = 0x97;
        b.fov = 50;
        if (App::Battle::Actor* prop = battleActor(app, kPropFighter)) playSlot(app, *prop, 14, -2); // (gac+0x429c: model slot 4)
        break;
    case 441: // its motion phase is 120 frames, whatever the slot's length
        spawnEffectFixed(app, 0x1e, user);
        played(0x247, 40);
        b.stageMotionEnd = 120, b.stageEnd = 120 + 40;
        b.hitFrame = 0x52;
        b.fov = 40;
        break;
    case 442: played(0x251, 40); b.hitFrame = 0x9b; b.fov = 40; break;
    case 443: { // the item cameras, the hit as the motion ends (btlact_433's way)
        b.camHold = 25; // (_btlact_443_init: +0xae8 = 0x19)
        if (user < 2) setBattleCamera(app, b.rules.pair ? 0x55 : user + 0x53);
        else if (monsterSize(app, user) == 1) setBattleCamera(app, b.rules.enemyPair ? 0x58 : user + 0x54, user);
        else setBattleCamera(app, monsterSize(app, user) + 0x57, user);
        if (u) playSlot(app, *u, cls, btlui::kSlotIdle);
        b.hitFrame = u ? std::max(1, int(actorDuration(app, *u))) : 60;
        break;
    }
    case 509:
        if (t.action == 509) { // 511: the cameras and the motion, the hit as it ends
            spawnEffectFixed(app, 0x19, user);
            played(0x25b, 40);
            b.hitFrame = 0x87;
            b.fov = 50;
        } else {
            played(0x25b, 40);
            b.hitFrame = b.stageMotionEnd;
            b.stageEnd = 0; // (hit, then the 40)
        }
        break;
    default: break;
    }
}

// One frame of it (b.turnFrame already counted)
void stepStaged(App& app) {
    App::Battle& b = app.battle;
    const cr::BattleTurn& t = b.turn;
    const int f = b.turnFrame + b.stageSkip, user = t.actors[0];
    auto nextCamera = [&](int most, int first) {
        if (b.stageCams < most && cameraDone(app)) setBattleCamera(app, first + ++b.stageCams);
    };
    auto onTargets = [&](int id) {
        for (int k : turnTargets(t)) spawnEffect(app, id, k);
    };
    if (b.stageChain && cameraDone(app) && b.turnFrame < b.stageMotionEnd &&
        app.battleData.camera(b.stageChain + b.stageCams + 1)) // (is_btlcamera)
        setBattleCamera(app, b.stageChain + ++b.stageCams);
    if (b.stageEnd && b.turnFrame == b.stageMotionEnd) { // the motion's over: the formation's view
        setBattleCamera(app, b.formation + 12);
        b.fov = 28.5f;
    }
    switch (b.stageAction) {
    case 430:
        nextCamera(3, 0x207);
        if (f == 70) {
            app.sound.play(0x5f);
            startQuake(app, 3, 0, 100, 2, true);
        }
        if (f == 133) endQuake(app, 3, 100, 0, 2);
        if (f == 79) setFlash(app, 0, 255, 150);
        if (f == 82) setFlash(app, 255, 0, 150);
        if (f == 110) setFlash(app, 0, 255, 500);
        if (f == 130) setFlash(app, 255, 0, 500);
        break;
    case 432:
        if (f == 14) app.sound.play(0xbd);
        if (f == 36) {
            if (const App::Battle::Actor* u = battleActor(app, user)) spawnEffectAt(app, 5, actorPoint(app, *u, 1206));
            onTargets(0x16);
            app.sound.play(0x60);
        }
        break;
    case 434:
        nextCamera(2, 0x20c);
        if (f == 84) app.sound.play(0xcf);
        if (f == 86) setFlash(app, 0, 255, 150);
        if (f == 89) setFlash(app, 255, 0, 150);
        if (f == 100) {
            onTargets(0x12);
            app.sound.play(0x50);
            startQuake(app, 5, 0, 100, 2, true);
        }
        if (f == 128) endQuake(app, 5, 100, 0, 2);
        break;
    case 435:
        if (f == 103) {
            spawnEffectAt(app, 0x11, {500, 0, 523});
            app.sound.play(0x48);
            setFlash(app, 0, 255, 150);
        }
        if (f == 106) setFlash(app, 255, 0, 150);
        if (f == 133) setFlash(app, 0, 255, 1500);
        if (f == 163) setFlash(app, 255, 0, 500);
        break;
    case 437:
        if (f == 1) app.sound.play(0x81);
        if (f == 0x7a) app.sound.stop(0x81), app.sound.play(0x82);
        if (f == 0xd6) app.sound.play(0x4a), app.sound.play(0x8f);
        if (f == 0x78) setFlash(app, 0, 255, 150);
        if (f == 0x7b) setFlash(app, 255, 0, 150);
        if (f == 0xfa) setFlash(app, 0, 255, 500);
        if (f == b.stageEnd) setFlash(app, 255, 0, 500); // (9999)
        break;
    case 439:
        if (f == 1) app.sound.play(0x110), app.sound.play(0x83);
        if (f == 0x3e) app.sound.play(0x84);
        if (f == 0x46) spawnEffectAt(app, 9, {500, 0, 430});
        if (f == 0x5d) {
            app.sound.play(0x114), app.sound.play(0xd4);
            startQuake(app, 3, 0, 100, 2, true);
            setFlash(app, 0, 255, 150);
        }
        if (f == 0x60) app.sound.play(0x111), setFlash(app, 255, 0, 150);
        if (f == 0x96) endQuake(app, 3, 100, 0, 2);
        break;
    case 440:
        if (f == 1) app.sound.play(0x85);
        if (f == 0x3d) spawnEffectAt(app, 0x1b, {500, 0, 465});
        if (f == 0x5a) startQuake(app, 5, 0, 100, 2, true);
        if (f == 0x68) endQuake(app, 5, 100, 0, 0x43);
        if (f == 0x74) app.sound.play(0x86), spawnEffectAt(app, 0x1c, {500, 0, 465});
        if (f == 0xa3) setFlash(app, 0, 255, 250);
        if (b.stageMotionEnd && f == b.stageMotionEnd + 5) setFlash(app, 255, 0, 150);
        break;
    case 441:
        if (f == 0x1f) startQuake(app, 2, 0, 100, 0x20, true), app.sound.play(0x8b);
        if (f == 0x24) app.sound.play(0x10d);
        if (f == 0x45) {
            spawnEffectFixed(app, 0x1f, user);
            app.sound.play(0x8c), app.sound.play(0xd4);
            endQuake(app, 5, 100, 0, 0x33);
        }
        break;
    case 442:
        if (f == 10) app.sound.play(0x89);
        if (f == 0x46) spawnEffectFixed(app, 0x20, user);
        if (f == 0x47) startQuake(app, 2, 0, 100, 0x2c, true);
        if (f == 0x6e) app.sound.play(0x8a);
        if (f == 0x74) startQuake(app, 3, 100, 100, 2, true);
        if (f == 0x92) endQuake(app, 5, 60, 100, 0x4a);
        if (f == 0xb9) setFlash(app, 0, 255, 1000);
        if (f == 0xd7) setFlash(app, 255, 0, 750);
        break;
    case 509:
        if (t.action != 509) break;
        if (f == 1) app.sound.play(0x87);
        if (f == 0x78) app.sound.play(0x88), setFlash(app, 255, 0, 150);
        if (f == 0x69) setFlash(app, 0, 255, 500);
        if (f == 0x10) startQuake(app, 2, 0, 100, 0x31, true);
        if (f == 0x4a) endQuake(app, 4, 100, 0, 0x2e);
        if (f == 0x87) endQuake(app, 5, 100, 0, 10);
        if (f == 0x88) spawnEffectFixed(app, 0x1a, user);
        if (f == 0xa3) setFlash(app, 0, 255, 250);
        if (b.stageMotionEnd && f == b.stageMotionEnd + 5) setFlash(app, 255, 0, 150);
        break;
    default: break;
    }
}

// Each frame of the whole action: the cameras that go on past the hit (432, 435, 436); 432's
// fifth looks at its target (0x203, 0x204) or the pair (0x205)
void stepStagedCameras(App& app) {
    App::Battle& b = app.battle;
    if (b.stageAction == 432 && b.stageCams < 5 && cameraDone(app)) {
        ++b.stageCams;
        const int target = b.turn.hits.empty() ? 0 : std::clamp(b.turn.hits[0].target, 0, 1);
        setBattleCamera(app, b.stageCams < 5 ? 0x1fe + b.stageCams : b.rules.pair ? 0x205 : target + 0x203);
    }
    if (b.stageAction == 435 && b.turn.action == 435 && b.stageCams < 5 && cameraDone(app))
        setBattleCamera(app, 0x215 + ++b.stageCams);
    if (b.stageAction == 436 && b.stageCams < 4 && cameraDone(app)) setBattleCamera(app, 0x210 + ++b.stageCams);
}

// The hit's own extras, after showHits
void stagedHit(App& app) {
    App::Battle& b = app.battle;
    App::Battle::Actor* u = battleActor(app, b.turn.actors[0]);
    switch (b.stageAction) {
    case 432:
        for (int k : turnTargets(b.turn)) spawnEffect(app, 0x17, k);
        app.sound.play(0x61);
        if (u) u->then = btlui::kSlotIdle; // mdl_set_act_nextanm: the loop plays out, then stands
        break;
    case 435:
        if (u) u->then = btlui::kSlotIdle;
        break;
    case 436: startQuake(app, 10, 100, 0, 10, false); break;
    case 443: setBattleCamera(app, b.formation + 12); break;
    case 509:
        if (b.turn.action != 509) setBattleCamera(app, b.formation + 12);
        break;
    default: break;
    }
}

// ---- Battle voices (the voiced releases: 2.2.0's _btlvoice_*, its voicebtl.bin) ----
// A line is the clip table[speaker x 1000 + situation - 1] (sys.evt section +0x40), played with
// tag speaker x 1000: that speaker's previous line stops first. Each fighter cycles through three
// variants of a situation. Nothing here plays without the table (the iPad's 1.0.1 has none).
namespace btlvoice {

bool on(const App& app) { return !app.battleVoiceTable.empty() && app.sound.voiceClips(true) > 0; }

int rnd(App& app, int n) {
    App::Battle& b = app.battle;
    b.voiceRng = b.voiceRng * 1103515245u + 12345u;
    return n > 1 ? int((b.voiceRng >> 16) % uint32_t(n)) : 0;
}

// Omega 3.1.0's speaker (FUN_000924ac): by the fighter's sys.evt row (+0xaa: the party record + 4
// - 5 for record 0 as Olgar, 0x106 for record 1 as Dante -, a monster its id - 1), then its battle
// model (+0xa2: j009a / j009b speak as 0, j010 as 1, model 0xa5 as 6)
int speaker(const App& app, int k) {
    const App::Battle& b = app.battle;
    if (k < 0 || k > 4) return -1;
    const cr::Fighter& f = b.rules.f[size_t(k)];
    if (!f.present) return -1;
    int row, model = -1;
    if (k < 2) {
        row = f.character + 4;
        if (f.character == 0 && viegIsOlgar(app)) row = 5;
        if (f.character == 1 && olgarIsDante(app)) row = 0x106;
        for (const App::Battle::Actor& a : b.actors)
            if (a.fighter == k) model = a.modelId;
    } else {
        row = f.def ? monsterNameRow(app, *f.def) : f.monster - 1;
        model = f.def ? f.def->model : -1;
    }
    int sp = -1;
    if (row < 0x7c) sp = row == 4 ? 0 : row == 5 ? 1 : row == 6 ? 2 : row == 7 ? 25 : -1;
    else {
        struct R { int row, sp; };
        static const R kRow[] = {{0x7c, 3},  {0x7f, 3},  {0x7d, 6},  {0x8c, 6},  {0x80, 7},  {0x82, 7},  {0x9d, 7},
                                 {0x81, 8},  {0x83, 10}, {0x9e, 10}, {0x84, 9},  {0x85, 11}, {0x86, 12}, {0x87, 13},
                                 {0x8a, 15}, {0x8d, 15}, {0x90, 15}, {0x93, 15}, {0xa1, 15}, {0x8b, 14}, {0x8e, 14},
                                 {0x91, 14}, {0x94, 14}, {0xa2, 14}, {0x8f, 4},  {0x92, 5},  {0x95, 16}, {0x96, 17},
                                 {0x97, 21}, {0x98, 22}, {0x99, 25}, {0x9a, 26}, {0x9b, 19}, {0x9c, 20}, {0x9f, 23},
                                 {0xa0, 24}, {0xa4, 18}};
        for (const R& e : kRow)
            if (e.row == row) sp = e.sp;
    }
    if (model == 0xa5) sp = 6;
    else if (model == 8 || model == 0xb) sp = 0;
    else if (model == 9) sp = 1;
    return sp;
}

int clipFor(const App& app, int sp, int situation) {
    const size_t i = size_t(sp * 1000 + situation - 1);
    return sp >= 0 && situation > 0 && i < app.battleVoiceTable.size() ? app.battleVoiceTable[i] : 0;
}

// _btlvoice_play: fighter k says the next of its three variants (-1: nothing)
void play(App& app, int k, int a, int b2, int c) {
    if (!on(app)) return;
    const int sp = speaker(app, k);
    if (sp < 0) return;
    int& v = app.battle.voiceVariant[size_t(k)];
    const int s = v == 0 ? a : v == 1 ? b2 : c;
    v = (v + 1) % 3;
    if (s == -1) return;
    const int clip = clipFor(app, sp, s);
    if (clip > 0) app.sound.playBattleLine(clip, sp * 1000);
}

bool living(const App& app, int k) { return k >= 0 && k < 5 && app.battle.rules.f[size_t(k)].alive(); }

// One of two at random among the living (the first if neither)
int eitherOf(App& app, int x, int y) {
    const bool lx = living(app, x), ly = living(app, y);
    if (lx && ly) return rnd(app, 2) ? y : x;
    return ly ? y : x;
}

// _btlvoice_choose_speaker: 99 either of the party; a side acting as a pair, either of it
int chooseSpeaker(App& app, int k) {
    const cr::BattleRules& r = app.battle.rules;
    if (k == 99) return r.f[1].present && rnd(app, 2) ? 1 : 0; // (FUN_00092d18: a coin flip whenever member 1 is there, down or not)
    if (k >= 2 && r.enemyPair && (k == 2 || k == 3)) return eitherOf(app, 2, 3);
    if (k < 2 && r.pair) return eitherOf(app, 0, 1);
    return k;
}

void playSide(App& app, int k, int a, int b2, int c) { play(app, chooseSpeaker(app, k), a, b2, c); }

// _btlvoice_pick_speakers, each round: who of each side speaks for it
void pickSpeakers(App& app) {
    App::Battle& b = app.battle;
    for (bool& s : b.voiceSpeaks) s = false;
    b.voiceSpeaks[size_t(eitherOf(app, 0, 1))] = true;
    std::vector<int> foes;
    for (int k = 2; k < 5; ++k)
        if (living(app, k)) foes.push_back(k);
    if (!foes.empty()) b.voiceSpeaks[size_t(foes[size_t(rnd(app, int(foes.size())))])] = true;
}

bool mayReact(const App& app, int k) { // a side acting as a pair, or hit by an all-target action (scope 3 / 5), reacts through its speaker only
    const App::Battle& b = app.battle;
    const bool pairSide = k < 2 ? b.rules.pair : b.rules.enemyPair;
    const cr::BattleActionDef* a = app.battleActions.get(b.turn.action);
    const bool allOf = a && (a->scope == 3 || a->scope == 5);
    return (!pairSide && !allOf) || b.voiceSpeaks[size_t(k)];
}

// _btlvoice_listed_battle (FUN_00092830): these battles' strong-growth monsters falling say 0x1f..0x21 instead
bool listedBattle(const App& app, int k) { // Omega: groups 669..672
    static const int kList[] = {669, 670, 671, 672};
    const cr::Fighter& f = app.battle.rules.f[size_t(std::clamp(k, 0, 4))];
    return f.def && f.def->strongGrowth && std::find(std::begin(kList), std::end(kList), app.battle.start.group) != std::end(kList);
}

// FUN_000928b0 (from btlcalc_chk_dead): fighter k took a blow of `damage` (0: a miss, a guard, no
// effect - btlcalc_dmg_hp's return, not the HP). Not for the kind-99 changes (status_prc's ticks,
// 0404's and 0412's costs in their users' HP); under action 437 only while the HP left equals the
// damage; a blow skill 189 answered before it landed (que +0xc5) gets the counter line 0xa8 instead
void hpChange(App& app, int k, int damage) {
    if (!on(app) || speaker(app, k) < 0 || !mayReact(app, k)) return;
    const App::Battle& b = app.battle;
    const cr::Fighter& f = b.rules.f[size_t(k)];
    if (b.turn.action == 437 && f.hp != damage) return;
    if (b.turn.counterSkill) play(app, k, 0xa8, 0xa8, 0xa8);
    else if (damage == 0) play(app, k, 0x22, 0x23, 0x24); // it came to nothing
    else if (f.hp <= 0) {                                   // down
        if (k >= 2 && listedBattle(app, k)) play(app, k, 0x1f, 0x20, 0x21);
        else play(app, k, 0x1c, 0x1d, 0x1e);
    } else if (f.hp > f.hpMax / 4) play(app, k, 0x16, 0x17, 0x18); // hit
    else play(app, k, 0x19, 0x1a, 0x1b);                             // hit, low already
}

void healed(App& app, int k) { // _btlvoice_healed: to full, or not
    if (!on(app) || speaker(app, k) < 0 || !mayReact(app, k)) return;
    const cr::Fighter& f = app.battle.rules.f[size_t(k)];
    if (f.hp >= f.hpMax) play(app, k, 0x28, 0x29, 0x2a);
    else play(app, k, 0x25, 0x26, 0x27);
}

// _btlvoice_attack: how the targets fared - 0 struck, 1 a critical, 2 one fell, 3 a miss or a
// guard, 4 a strong one fell
void attack(App& app, const cr::BattleTurn& t) {
    if (!on(app) || speaker(app, t.actors[0]) < 0) return;
    { // only when the action had no line of its own (its slot is -1); else the turn line was the voice -
      // FUN_00092f04 reads the party slot for a member, the monster slot for a monster (not by the speaker mask)
        const cr::BattleActionDef* a = app.battleActions.get(t.action);
        if (a && a->voice[t.actors[0] < 2 ? 0 : 3] != -1) return;
    }
    int how = 0;
    for (const cr::BattleHit& h : t.hits) {
        const cr::Fighter& f = app.battle.rules.f[size_t(std::clamp(h.target, 0, 4))];
        if (h.type == cr::kHitMiss || h.type == cr::kHitGuard || h.amount == 0) how = std::max(how, 3);
        if (f.hp <= 0 || h.fell) how = std::max(how, f.def && f.def->strongGrowth ? 4 : 2);
        else if (h.type == cr::kHitCritical && how == 0) how = 1;
    }
    static const int kLine[5] = {0x2e, 0x31, 0x34, 0x37, 0x3a};
    playSide(app, t.actors[0], kLine[how], kLine[how] + 1, kLine[how] + 2);
}

int halfOrLess(const App& app, int k) {
    const cr::Fighter& f = app.battle.rules.f[size_t(k)];
    return f.hp <= f.hpMax / 2;
}

// _btlvoice_command_hurt: the pair's command, one of it at half HP or less (10..12) or not (7..9)
void commandHurt(App& app, int member) {
    const bool hurt = (living(app, 0) && halfOrLess(app, 0)) || (living(app, 1) && halfOrLess(app, 1));
    playSide(app, member, hurt ? 10 : 7, hurt ? 11 : 8, hurt ? 12 : 9);
}

// _btlvoice_command: a member's commands open (once a round): 1..3, at half HP or less 4..6,
// the partner down 13..15 / 16..18; battles 356..359 the pair's lines instead
void command(App& app, int member) {
    App::Battle& b = app.battle;
    if (!on(app) || member < 0 || member > 1 || b.commandSaid[size_t(member)]) return;
    b.commandSaid[size_t(member)] = true;
    if (b.rules.pair) {
        b.commandSaid[0] = b.commandSaid[1] = true;
        commandHurt(app, member);
        return;
    }
    const int partner = member ^ 1;
    const int idx = halfOrLess(app, member) | ((b.rules.f[size_t(partner)].present && !living(app, partner)) ? 2 : 0);
    if (idx < 2 && b.start.group >= 0x164 && b.start.group <= 0x167) { commandHurt(app, member); return; }
    static const int kLine[4] = {1, 4, 13, 16};
    playSide(app, member, kLine[idx], kLine[idx] + 1, kLine[idx] + 2);
}

// _btlvoice_turn: as a round's order is made, its first actor's action lines (the record's
// +10.. for the party and the voiced characters, +16.. for the other monsters)
void turn(App& app, int k, int action) {
    if (!on(app)) return;
    const cr::BattleActionDef* a = app.battleActions.get(action);
    const int sp = speaker(app, k);
    if (!a || sp < 0) return;
    const bool own = (sp >= 0 && sp <= 2) || (sp >= 19 && sp <= 26) || k < 2; // (_btlvoice_turn's mask: speakers 0..2, 19..26)
    const int who = own ? chooseSpeaker(app, k) : (app.battle.rules.enemyPair && (k == 2 || k == 3) ? eitherOf(app, 2, 3) : k);
    const int* v = own ? a->voice : a->voice + 3;
    play(app, who, v[0], v[1], v[2]);
}

// btl +0xb06 (_read_btlcpl): a monster of the strong-growth kind is in this battle
bool strongInBattle(const App& app) {
    for (int k = 2; k < 5; ++k) {
        const cr::Fighter& f = app.battle.rules.f[size_t(k)];
        if (f.present && f.def && f.def->strongGrowth) return true;
    }
    return false;
}

// FUN_000926d8 (btl_main 799): in groups 651, 669, 671, 672 and 673 the victory waits while a battle voice still
// plays. The scene caps the wait at the longest clip (15.7 s in voicebtl.bin): a stream the device never drains
// (the headless tests) cannot hold the battle

bool winWaits(const App& app) {
    const int g = app.battle.start.group;
    return (g == 651 || g == 669 || g == 671 || g == 672 || g == 673) && app.sound.battleLinePlaying();
}

// _btlvoice_victory (FUN_0009327c): scripted battles' own lines (0xc93d0, by the pair), else 500..502 / with
// someone down or at a quarter HP or less 503..505; six on (506..511) with a strong-growth monster in the battle
void victory(App& app) {
    if (!on(app)) return;
    App::Battle& b = app.battle;
    // Omega's one list (0xe2b90 in the voiced release): the story battles' lines 600..620
    static const short kScripted[][2] = {{651, 600}, {652, 601}, {737, 602}, {656, 603}, {657, 604}, {658, 605}, {659, 606},
                                         {660, 607}, {663, 608}, {666, 609}, {667, 610}, {664, 611}, {665, 611}, {676, 613},
                                         {677, 614}, {669, 615}, {670, 616}, {671, 617}, {673, 618}, {672, 619}, {668, 612},
                                         {674, 620}};
    int line = 0;
    for (const auto& e : kScripted)
        if (e[0] == b.start.group) line = e[1];
    const int who = chooseSpeaker(app, b.voiceLeader); // (+0xaf2: the member who last headed the order)
    const int sp = speaker(app, who);
    if (sp < 0) return;
    if (!line) {
        // "hurt": the speaker down or at a quarter of its HP or less - both members only as a pair
        bool hurt = false;
        for (int k = 0; k < 2; ++k)
            if (b.rules.f[size_t(k)].present && (k == who || b.rules.pair))
                hurt |= !living(app, k) || b.rules.f[size_t(k)].hp <= b.rules.f[size_t(k)].hpMax / 4;
        int& v = b.voiceVariant[size_t(who)];
        line = (strongInBattle(app) ? 506 : 500) + (hurt ? 3 : 0) + v;
        v = (v + 1) % 3;
    }
    const int clip = clipFor(app, sp, line);
    if (clip > 0) app.sound.playBattleLine(clip, sp * 1000);
}

} // namespace btlvoice

// btlact_attack_init's motion for fighter k of the turn (0 the attacker, 1 its pair's second):
// - the attacker's slot 9 if it has more than 3 frames (btlanm_get_max_frm(.., 9) > 3): in place
//   (70 of Omega's 166 battle models load one - m225's blow, m101..m129, most m2xx; j006's and j007's
//   packages have one, but _btlanmtbl leaves it unloaded);
// - else slot 10, in place: a party member's attack of class 10 (action +0x17) or character 0x21's,
//   a monster of model 0xa0..0xa2, or of 0x4c..0x4f with class 10 (0x4f: always);
// - else slot 8 toward the target - 13 for models 0x9d..0x9f (m007a..c) while the party stands as a pair
//   (btl +0xa64, __menub_mainPrc's Pair) or enemies 2 and 3 act together (+0xa68, btlord_enemy_rand).
// The second of a pair: slot 8 toward the target, or 13 for a party model 7 / a monster of 0xa0..0xa2.
// (Each then through mdl_set_act_motion_nextanm: playSlot passes slots of 3 frames or less.)
void turnOver(App::Battle::Actor& a, float want, float frames);
// _btlact_attack 1000 -> 0x406 (3.1.0 decomp 12911-12990): the return slot (nextanm 3, then the idle) and
// btlmove_setpos home over that slot's own window; the wanted yaw the heading out again, turned to over it
void startAttackReturn(App& app, App::Battle::Actor& a) {
    playSlot(app, a, btlui::kSlotReturn, btlui::kSlotIdle);
    const cr::BattleAnim ra = app.battleData.anim(a.modelId, btlui::kSlotReturn);
    if (ra.hasMoveWindow()) {
        a.from = a.pos;
        a.to = a.home;
        a.moveFrom = ra.moveStart;
        a.moveTo = ra.moveEnd;
    } else
        a.moveFrom = a.moveTo = 0;
    if (a.approached) turnOver(a, a.approachYaw, float(ra.hasMoveWindow() ? ra.moveEnd - ra.moveStart : 1));
    a.turnHome = false;
}

// _btlanm_get_max_frm (3.1.0 0x47e44): a fighter's motion slot's frames less one (the raw slot, no skipping)
int battleMaxFrame(App& app, int fighter, int slot) {
    const App::Battle::Actor* u = battleActor(app, fighter);
    if (!u) return 0;
    const LoadedModel& lm = app.battle.models[u->model];
    const int i = battleAction(lm, slot);
    return i >= 0 ? int(lm.actions[size_t(i)].duration) - 1 : 0;
}

int attackSlot(App& app, const cr::BattleTurn& t, int k, int cls, bool& inPlace) {
    App::Battle& b = app.battle;
    const App::Battle::Actor* u = battleActor(app, t.actors[k]);
    inPlace = false;
    if (!u) return 8;
    const bool pair = b.rules.pair || b.rules.enemyPair;
    const int model = u->modelId, fighter = t.actors[k];
    if (k == 1) {
        inPlace = fighter < 2 ? model == 7 : model >= 0xa0 && model <= 0xa2;
        return inPlace ? 13 : 8;
    }
    const LoadedModel& lm = b.models[u->model];
    const int nine = battleAction(lm, 9);
    if (nine >= 0 && lm.actions[size_t(nine)].duration - 1 > 3) {
        inPlace = true;
        return 9;
    }
    const int character = fighter >= 0 && fighter < 5 ? b.rules.f[size_t(fighter)].character : -1;
    const bool ten = fighter < 2 ? character == 0x21 || cls == 10
                                 : (model >= 0xa0 && model <= 0xa2) || ((cls == 10 || model == 0x4f) && model >= 0x4c && model <= 0x4f);
    if (ten) {
        inPlace = true;
        return 10;
    }
    return model >= 0x9d && model <= 0x9f && pair ? 13 : 8;
}

// _btlact_attack_init's counts for _btlact_attack (3.1.0 decomp 12375-12702; the states 12862-13241): when
// the calc comes (que +0xb0), the frames _btlcalc_prc_0404_effect shows the blow's look without it (+0xb2,
// +0xb4: a target's flinch if it is hurt, the hit effect and its sound), how long the blow lasts (+8):
// - slot 9 in place (0x76c): the calc at the slot's hit, the look at its second hit; 0x406 after the slot's
//   max frame, waiting 10 (no way back), then 1999's 10;
// - slot 10 in place (0x6a4 for a party member / monsters 0xa0..0xa2: SE 0x2d at frame 13, at the hit - 2
//   effect 2 from the user with SE 0x2e, two frames to the target, the calc as it lands; 0x708 for monsters
//   0x4c..0x4f: at the hit - 5 effect 3, five frames to the target): 0x406 after the slot's max frame,
//   one frame, then 1999's 10;
// - else 1000 (the approach): the calc at the user's hit, the look at its second hit and, in a pair, at the
//   second's hit (+0xb4; the variant-2 camera at the second's move end, +0x10). A pair's second of party
//   model 1 / monsters 0x8e..0x90 times the blow: the calc at its hit, the look at the first's, +8 its max
//   frame; one standing (party model 7 / monsters 0xa0..0xa2, slot 13: que +0x14) the calc and +8 at its
//   slot-13 hit, the look at the first's slot-13 hits, SE 0x2d at frame 13. (Omega's members are models
//   6 and 8..11: their pairs never take the model 1 / 7 forms)
void attackCounts(App& app, const cr::BattleTurn& t) {
    App::Battle& b = app.battle;
    b.attackFxAt[0] = b.attackFxAt[1] = -1;
    b.partnerStands = false;
    b.shot = {};
    const int user = t.actors[0];
    const App::Battle::Actor* u = battleActor(app, user);
    if (!u) return;
    const cr::BattleActionDef* a = app.battleActions.get(t.action);
    bool inPlace = false;
    const int slot = attackSlot(app, t, 0, a ? a->cls : 0, inPlace), model = u->modelId;
    const cr::BattleAnim an = app.battleData.anim(model, slot);
    if (an.hit > 0) b.hitFrame = an.hit;
    if (slot == 9 && inPlace) { // 0x76c (12375-12405; 13164-13241)
        b.attackFxAt[0] = an.hit2;
        b.attackEndAt = battleMaxFrame(app, user, 9) + 10 + 10;
        return;
    }
    if (slot == 10 && inPlace) { // 0x6a4 / 0x708 (12413-12447, 12687-12702; 13018-13162)
        const bool ball = user >= 2 && model >= 0x4c && model <= 0x4f;
        b.shot.effect = ball ? 3 : 2;
        b.shot.steps = ball ? 5 : 2;
        b.shot.at = an.hit - b.shot.steps;
        b.shot.from = ball ? 1210 : 1204;
        b.shot.target = firstTarget(t);
        b.attackEndAt = battleMaxFrame(app, user, 10) + 1 + 10;
        return;
    }
    b.attackFxAt[0] = an.hit2; // (12465-12471)
    b.attackBackAt = battleMaxFrame(app, user, slot);
    const App::Battle::Actor* v = t.actors[1] >= 0 ? battleActor(app, t.actors[1]) : nullptr;
    const bool members = user < 2;
    if (!v || !(members ? b.rules.pair : b.rules.enemyPair)) return;
    const int vm = v->modelId;
    if (members ? vm == 7 : vm >= 0xa0 && vm <= 0xa2) { // (12551-12570 / 12617-12636)
        const cr::BattleAnim first = app.battleData.anim(model, 13);
        const int hit = app.battleData.anim(vm, 13).hit;
        if (hit > 0) b.attackBackAt = b.hitFrame = hit;
        b.attackFxAt[0] = first.hit, b.attackFxAt[1] = first.hit2;
        b.partnerStands = true;
        return;
    }
    const cr::BattleAnim second = app.battleData.anim(vm, 8);
    b.attackFxAt[1] = second.hit; // (12581 / 12644)
    if (members) b.attackCam2At = second.moveEnd; // (12572: que +0x10, the second's)
    if (members ? vm == 1 : vm >= 0x8e && vm <= 0x90) { // (12597-12606 / 12669-12682)
        b.attackBackAt = battleMaxFrame(app, t.actors[1], 8);
        if (second.hit > 0) b.hitFrame = second.hit;
        b.attackFxAt[0] = app.battleData.anim(model, 8).hit;
    }
}

void startTurn(App& app) {
    App::Battle& b = app.battle;
    const cr::BattleTurn& t = b.turn;
    if (!t.repeat) { // btlque_make_que (once an action): the actor's action line, the sides' speakers picked afresh
        b.voiceRound = b.rules.round;
        if (t.actors[0] >= 0 && t.actors[0] < 2) b.voiceLeader = t.actors[0];
        // (no line for a counter's go - its "Counter!" instead - nor for group 657's 437 in its first two rounds)
        if (!t.counter && !(t.action == 437 && b.start.group == 657 && b.rules.round <= 2)) btlvoice::turn(app, t.actors[0], t.action);
        btlvoice::pickSpeakers(app);
    }
    if (t.kind == cr::kCmdEscape && t.escaped) btlvoice::playSide(app, 99, 0x13, 0x14, 0x15); // btlact_escape_init (either member)
    else if (t.kind == cr::kCmdAttack) btlvoice::attack(app, t);                 // btlact_attack_init
    if (t.kind == cr::kCmdAttack) { // btlact_attack_init's __effgo: actions 0x12f / 0x129, 0x12e, each attacker's swing
        if (t.action == 0x12f) app.sound.play(0xb8);
        else if (t.action == 0x129 || t.action == 0x12e) app.sound.play(0xb6);
        for (int k : t.actors) {
            const App::Battle::Actor* u = k >= 0 ? battleActor(app, k) : nullptr;
            if (!u || u->modelId == 7) continue;
            const int m = u->modelId; // (G+0xa2: kBattleModelFile's index)
            const auto tbl = [&](int add) { return m < cr::kNumBattleModels ? cr::kBattleAnimTable[m][15] + add : -1; };
            int id = -1;
            if (m == 0xb || m == 8 || m == 0xa4 || m == 0xa3) id = 0x3a;
            else if (m == 0xa5 || m == 9) id = 0x3c;
            else if (m == 10) id = 0x3b;
            else if (m >= 0x19 && m <= 0x1a) id = 0x3d;
            else if (m >= 0x17 && m <= 0x18) id = 0x3e;
            else if (m == 0x1b) id = 0x7d;
            else if (m == 0x1c || m == 0x1d) id = 0x7e;
            else if (m >= 0x1e && m <= 0x1f) id = 0x7f;
            else if (m >= 0x13 && m <= 0x14) id = 0x3f;
            else if (m >= 0x15 && m <= 0x16) id = 0x40;
            else if (m >= 0x2e && m <= 0x31) id = 0x6a;
            else if (m >= 0x32 && m <= 0x35) id = 0x69;
            else if (m >= 0x21 && m <= 0x84) id = tbl(0x67); // (its species' swing, _btlanmtbl [15])
            else if (m >= 0x85 && m <= 0x87) id = 0x79;
            else if (m >= 0x88 && m <= 0x8a) id = 0x7a;
            else if ((m >= 0x8b && m <= 0xa2) || m <= 7) id = tbl(0x26);
            if (id >= 0) app.sound.play(uint32_t(id));
        }
        const App::Battle::Actor* u = battleActor(app, t.actors[0]);
        if (u && u->modelId == 0x20 && t.actors[0] >= 2) { // m225's in-place blow (btlact_attack_init's slot-9
            // path, 3.1.0 0x187a8..0x18800): SE 0x90 and effect 0x18 on it (eff3d_set_btl -> eff3d_set ->
            // eff3d_set_with_pos: at its node, angles 0), its camera-facing byte then cleared
            app.sound.play(0x90);
            const size_t before = app.battle.effects.size();
            spawnEffect(app, 0x18, t.actors[0]);
            if (app.battle.effects.size() > before) app.battle.effects.back().keepAngles = true;
        }
    }
    b.turnFrame = 0;
    b.attackBackAt = b.attackEndAt = -1;
    b.attackFxAt[0] = b.attackFxAt[1] = -1;
    b.partnerStands = false;
    b.shot = {};
    b.nodeFx = 0;
    b.hitsShown = false;
    b.hitFrame = 20;
    b.bounceAt = -1;
    b.idleSince = -1;
    if (t.cancelled) { // btlact_none: its message for 40 frames in place of the action, 60 frames on the
        // user's camera (0x53 + member, a pair's 0x55, a monster's 0x54 + its slot)
        setBanner(app, sysText(app, t.message), 40);
        b.hitFrame = 0; // (sixty frames, the Acting step's rule for a cancelled one)
        const int u = t.actors[0];
        b.camHold = 25; // (_btlact_none_init: +0xae8 = 0x19)
        if (u >= 0) setBattleCamera(app, u < 2 ? (b.rules.pair ? 0x55 : 0x53 + u) : 0x54 + (u - 2));
        return;
    }
    if (t.kind == cr::kCmdEscape) {
        setBanner(app, sysText(app, t.escaped ? 57000 : 57001));
        b.hitFrame = 0; // btlact_escape: away - 30 frames (2.2.0; 1.0.1 10, no line to wait for)
        return;          // then 40; stayed - 40
    }
    const cr::BattleActionDef* a = app.battleActions.get(t.action);
    // btlque_make_que: a counter (+0x608, taken from +0x564) is announced as action 0xdc's name, "Counter",
    // whatever blow it is (font_set_utf16(11, 3, 0xdb, 40); Part 5 131.5 s: "Counter" over Huotou's Chomp). An
    // entry is announced once (its +1000): a repeated go of the same action - Asura Lotus's strikes, 234 / 239 /
    // 243 - leaves the banner alone (Part 12 193.5-200 s: "Asura Lotus" over the first strike only)
    if (!t.repeat || t.counter) setBanner(app, sysText(app, 54000 + (t.counter ? 0xdb : t.action - 1)));
    for (int k : t.actors) // the gene's MP leaves as it's cast
        if (k >= 0) b.shownMp[k] = b.rules.f[size_t(k)].mp;
    b.stageAction = 0;
    if (isStagedAction(t.action) && t.actors[0] >= 2) { // btlact_430_init.. instead of btlact_gene_init
        startStaged(app, a);
        return;
    }
    cameraForTurn(app, t);
    if (t.kind == cr::kCmdGene || t.kind == cr::kCmdItem) // btlact_gene_init / item_init: the casting glow
        for (int k : t.actors)
            if (k >= 0) spawnEffect(app, effects::kCast, k);
    if (t.kind == cr::kCmdGene) { // btlact_gene_init: __effgo 0x37 (m103a's own 0xb6 first)
        const App::Battle::Actor* u = battleActor(app, t.actors[0]);
        if (u && u->modelId == 0xf) app.sound.play(0xb6);
        app.sound.play(0x37);
    } else if (t.kind == cr::kCmdItem) app.sound.play(0x38); // btlact_item_init
    if (t.kind == cr::kCmdItem && t.item > 0) { // the bag gives one up
        auto it = app.game.items.find(t.item);
        if (it != app.game.items.end() && --it->second <= 0) app.game.items.erase(it);
    }
    const int target = !t.barriers.empty() ? t.barriers[0] : firstTarget(t); // (reflected: at the barrier)
    const bool opposingTarget = target >= 0 && (target < 2) != (t.actors[0] < 2);
    for (int k = 0; k < 2; ++k) {
        App::Battle::Actor* u = t.actors[k] >= 0 ? battleActor(app, t.actors[k]) : nullptr;
        if (!u) continue;
        int slot = a && a->cls >= 8 && a->cls < cr::kBattleAnimSlots ? a->cls : 8;
        bool inPlace = false;
        if (t.kind == cr::kCmdAttack) slot = attackSlot(app, t, k, a ? a->cls : 0, inPlace);
        if (t.kind == cr::kCmdAttack && SDL_getenv("CR_BATTLE_DEBUG"))
            std::printf("attack: fighter %d (model %d %s) slot %d%s, cls %d\n", t.actors[k], u->modelId, u->model.c_str(), slot,
                        inPlace ? " in place" : "", a ? a->cls : -1);
        if (target >= 0 && t.kind == cr::kCmdAttack) faceFighter(app, *u, target); // (gene_init / item_init turn no one)
        const cr::BattleAnim an = app.battleData.anim(u->modelId, slot);
        // btlact_attack_init: btlmove_setpos_angy with the model's own move_cnt / start / end for the
        // slot - a model with no window acts in place, as do slots 9 and 10 (no btlmove at all)
        const bool approach = !inPlace && cr::battleApproachEnabled(a ? a->motion : 0, opposingTarget, an);
        u->turnHome = false, u->approached = false;
        if ((t.kind == cr::kCmdGene || t.kind == cr::kCmdItem) && !approach) {
            // _btlact_gene_init / _btlact_item_init (42018 / 42756): _mdl_set_act_motion_loop(slot, 2,
            // loop start, loop end) - played to the loop's end, then its loop over and over (the whole
            // slot when it has none) till the hit's next frame (state 2000's nextanm 0: see the
            // hitsShown step - the clip plays out, then the idle) or another motion (a blow)
            // (the loop's range from the class slot's row - btlanm_get_loop_start / _end(.., class), 3.1.0
            // decomp 42014-42019 - whichever clip mdl_set_act_motion_loop then moves on to)
            playSlot(app, *u, slot, -1);
            if (an.loopEnd > an.loopStart) u->loopFrom = an.loopStart, u->loopTo = an.loopEnd;
            if (SDL_getenv("CR_BATTLE_DEBUG"))
                std::printf("cast: fighter %d (model %d %s) class slot %d -> slot %d, loop %d..%d, clip %.2f\n", t.actors[k], u->modelId,
                            u->model.c_str(), slot, u->slot, an.loopStart, an.loopEnd, actorDuration(app, *u));
        } else
            playSlot(app, *u, slot, approach ? btlui::kSlotReturn : btlui::kSlotIdle);
        // (_btlact_gene_init / _btlact_item_init: the hit at the cast slot's loop start + 30)
        if (k == 0) b.hitFrame = t.kind == cr::kCmdGene || t.kind == cr::kCmdItem ? an.loopStart + 30 : an.hit > 0 ? an.hit : t.kind == cr::kCmdAttack ? 18 : 30;
        u->moveFrom = u->moveTo = 0;
        cr::Vec3 spot;
        if (approach && t.kind == cr::kCmdAttack && opposingTarget && attackSpot(app, t.actors[k], k, target, spot)) {
            // _btlact_attack_init: btlmove_setpos_angy to the formation table's spot, facing along the move
            u->from = u->pos;
            u->to = {spot.x, u->pos.y, spot.z};
            if (k == 0) b.attackCam2At = an.moveEnd; // (que +0x10 = FUN_00047d8c: the slot's move end)
            // (12536-12539: mdl_ang_y(from, to) into both the wanted and the shown yaw - kept for the way
            // back (DAT_000dfb58) - and the setpos_angy move turns it home once it starts)
            if (u->to.x != u->from.x || u->to.z != u->from.z) u->yaw = u->wantYaw = std::atan2(u->to.x - u->from.x, u->to.z - u->from.z);
            u->approachYaw = u->wantYaw, u->approached = true, u->turnHome = true;
            u->moveFrom = an.moveStart;
            u->moveTo = an.moveEnd;
        } else if (approach) { // (a gene's move) btlmove_setpos: up to the target, a step short of it
            const App::Battle::Actor* tv = battleActor(app, target);
            if (tv) {
                const LoadedModel &lu = b.models[u->model], &lt = b.models[tv->model];
                cr::Vec3 d = u->home - tv->home;
                const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.z * d.z));
                const float reach = 0.6f * (lu.radius * u->scale + lt.radius * tv->scale) + (k ? 18.0f : 0.0f);
                u->from = u->pos;
                u->to = tv->home + cr::Vec3{d.x / len * reach, 0, d.z / len * reach};
                if (k == 1) u->to = u->to + cr::Vec3{d.z / len * 18, 0, -d.x / len * 18}; // beside the first
                u->moveFrom = an.moveStart;
                u->moveTo = an.moveEnd;
            }
        }
    }
    if (t.kind == cr::kCmdAttack) attackCounts(app, t);
}

// _btlact_attack_init (12480-12538, 12575-12650): where an attacker steps to - the formation table
// (0xdec80, kBattlePosition): a member's blow at monster t the formation's row, entry 3 + (t - 2)
// (the pair's 6 + 2(t - 2), its second 7 + 2(t - 2)); a monster's at member t row 0 (the pair's row 1),
// entry 3 + t (an enemy pair's 6 + 2t, its second 7 + 2t), z - 10 for a size-3 monster and + 5 for
// size 2 (the first only). Against the pair a lone monster's x and z are both entry 3 + t's x (the
// code reads +0x3c twice)
bool attackSpot(const App& app, int user, int k, int target, cr::Vec3& out) {
    const App::Battle& b = app.battle;
    const int formation = std::clamp(b.formation, 0, cr::kNumFormations - 1);
    auto at = [](int row, int entry) { return cr::Vec3{float(cr::kBattlePosition[row][entry][0]), 0, float(cr::kBattlePosition[row][entry][1])}; };
    if (user < 2) {
        if (target < 2 || target > 4) return false;
        const int t = target - 2;
        out = !b.rules.pair ? at(formation, 3 + t) : at(formation, 6 + 2 * t + (k == 1 ? 1 : 0));
        return true;
    }
    if (target < 0 || target > 1) return false;
    const int row = b.rules.pair ? 1 : 0;
    if (k == 1) { out = at(row, 7 + 2 * target); return true; }
    if (b.rules.enemyPair) out = at(row, 6 + 2 * target);
    else if (b.rules.pair) out = at(1, 3 + target), out.z = out.x;
    else out = at(0, 3 + target);
    const int size = monsterSize(app, user);
    if (size == 3) out.z -= 10;
    else if (size == 2) out.z += 5;
    return true;
}

// Analyze (btlact_gene 0xc1c..0xc9e): for each target (the first, then a second), its name
// (font_set_utf16(11, 2, +0xaa)), 57031 "Remaining HP" (var 0 its HP), 57032 "Enemy weakness"
// for each element it's weak to and 57033 "Enemy characteristic" for each it stands up to
// (_SetVar(0, the element's name, 57040 + k); 57045 "No results" for an empty list) - each held
// on the banner until a tap (_pad_chk_trg_up); 40 frames after the last the action ends (2000)
void startAnalyze(App& app) {
    App::Battle& b = app.battle;
    auto line = [&](int id, int value, const std::string& text) {
        std::vector<std::vector<std::string>> pages;
        if (!app.systemText.message(id, textLanguage(app), [&](int) { return value; }, pages, nullptr,
                                    [&](int) { return text; }) || pages.empty() || pages[0].empty())
            return std::string();
        std::string s = pages[0][0];
        for (size_t i = 1; i < pages[0].size(); ++i) s += " " + pages[0][i];
        return s;
    };
    App::Battle::Analyze& an = b.analyze;
    an = {};
    std::vector<int> targets;
    for (const cr::BattleHit& h : b.turn.hits)
        if (h.target >= 0 && targets.size() < 2 && std::find(targets.begin(), targets.end(), h.target) == targets.end())
            targets.push_back(h.target);
    for (int t : targets) {
        const cr::Fighter& f = b.rules.f[size_t(t)];
        const App::Battle::Actor* u = battleActor(app, t);
        an.pages.push_back(u ? u->name : std::string());
        an.pages.push_back(line(57031, f.hp, std::string()));
        for (const bool weak : {true, false}) {
            const std::vector<int> list = cr::analyzeElements(f, weak);
            if (list.empty()) an.pages.push_back(line(weak ? 57032 : 57033, 0, sysText(app, 57045)));
            for (int k : list) an.pages.push_back(line(weak ? 57032 : 57033, 0, sysText(app, 57040 + k)));
        }
    }
    if (an.pages.empty()) return;
    an.on = true;
    setBanner(app, an.pages[0], 1 << 20);
}

// A tap on Analyze's line: the next, or (after the last) the line stays its 40 frames and so
// does the action
void analyzeTap(App& app) {
    App::Battle& b = app.battle;
    App::Battle::Analyze& an = b.analyze;
    if (!an.on) return;
    if (++an.at < an.pages.size()) {
        setBanner(app, an.pages[an.at], 1 << 20);
        return;
    }
    an.on = false;
    an.endAt = b.turnFrame + 40;
    b.bannerFrames = std::min(b.bannerFrames, 40); // (2000 ms left on it)
}

void releasePair(App::Battle& b) { // the calc's pair flags, as its hit comes (see pairHeld)
    if (b.pairHeld) b.rules.pair = b.pairAfterHit, b.rules.enemyPair = b.enemyPairAfterHit;
    b.pairHeld = false;
}

// _btlcalc_prc_0404_effect (3.1.0 decomp 26526-26600): a blow's look without its calc - on each target a
// hurt one (que +0x44 > 0) not guarding flinches (slot 5, then the idle), and but for one fended off (type
// 6) the hit's effect and sound: a critical's 5 and 0x32, the user's element's 0x2a + 3e and 0x57 + e, else
// 4 and 0x2f + random(3)
void attackLook(App& app) {
    App::Battle& b = app.battle;
    const int user = b.turn.actors[0];
    const int ue = user >= 0 && user < 5 ? b.rules.f[size_t(user)].element : 0;
    for (const cr::BattleHit& h : b.turn.hits) {
        App::Battle::Actor* a = battleActor(app, h.target);
        if (!a || h.mp || h.type == cr::kHitQuiet) continue;
        if (h.amount > 0 && a->slot != btlui::kSlotGuard) playSlot(app, *a, btlui::kSlotHit, btlui::kSlotIdle);
        if (h.type == cr::kHitGuard) continue;
        if (h.type == cr::kHitCritical) spawnEffect(app, effects::kCritical, h.target), app.sound.play(0x32);
        else if (ue > 0) spawnEffect(app, 0x2a + 3 * ue, h.target), app.sound.play(uint32_t(0x57 + ue));
        else spawnEffect(app, effects::kHit, h.target), app.sound.play(0x2f + uint32_t(b.rules.random(3)));
    }
}

// The hits land: the numbers, the targets' reactions, the HP the HUD shows
void showHits(App& app) {
    App::Battle& b = app.battle;
    b.hitsShown = true;
    releasePair(b);
    if (b.turn.message > 0) { // stolen Oz, a monster running, the barrier, the Apples
        std::string name = b.turn.message == 57005 || b.turn.message == 57049 || b.turn.message == 57050
                               ? itemName(app, b.turn.messageValue)
                               : std::string();
        if (b.turn.message == 57000 || b.turn.message == 57001)
            if (App::Battle::Actor* u = battleActor(app, b.turn.actors[0])) name = u->name;
        std::vector<std::vector<std::string>> pages;
        const int value = b.turn.messageValue;
        if (app.systemText.message(b.turn.message, textLanguage(app), [&](int) { return value; }, pages, nullptr,
                                   [&](int) { return name; }) &&
            !pages.empty() && !pages[0].empty())
            setBanner(app, pages[0][0], btlui::kMessageFrames);
    }
    if (b.turn.kind == cr::kCmdGene && b.turn.action == 0xe9) startAnalyze(app); // btlact_gene 2000: 0xe9 -> 0xc1c
    for (const cr::BattleNotice& n : b.turn.notices) // btl_add_msgque: one waiting per fighter
        if (std::none_of(b.notices.begin(), b.notices.end(), [&](const cr::BattleNotice& q) { return q.fighter == n.fighter; }) &&
            b.notices.size() < 5)
            b.notices.push_back(n);
    if (b.turn.gone) // the monster leaves
        if (App::Battle::Actor* u = battleActor(app, b.turn.actors[0])) u->fading = true;
    { // the calcs' own lines (3.1.0's voice calls in the calcs, as the blow is shown)
        const cr::BattleActionDef* act = app.battleActions.get(b.turn.action);
        if (b.turn.reflected && !b.turn.barriers.empty()) // _btlact_gene: the barrier's holder, as it turns the gene back
            btlvoice::playSide(app, b.turn.barriers.front(), 0xa5, 0xa5, 0xa5);
        if (act && act->calc == 10 && !b.turn.hits.empty()) { // _btlcalc_0415 (Steal Oz): the robbed one - hit, or nothing taken
            const bool took = b.turn.message == 57039;
            btlvoice::playSide(app, b.turn.hits.front().target, took ? 0x16 : 0x22, took ? 0x17 : 0x23, took ? 0x18 : 0x24);
        }
        if (act && act->id == 223) { // _btlcalc_0504 (Steal Item): the thief - a catch 0x96, a failure 0x97, nothing left silent
            const bool got = std::any_of(b.turn.notices.begin(), b.turn.notices.end(),
                                         [](const cr::BattleNotice& n) { return n.fighter < 0 && n.entry == 5; });
            if (got) btlvoice::playSide(app, b.turn.actors[0], 0x96, 0x96, 0x96);
            else if (b.turn.message == 57038) btlvoice::playSide(app, b.turn.actors[0], 0x97, 0x97, 0x97);
        }
        if (b.turn.message == 57049 || b.turn.message == 57050) // _btlcalc_0506: a gene or skill taken from the fallen
            btlvoice::playSide(app, b.turn.actors[0], 0xa9, 0xaa, 0xab);
    }
    for (const cr::BattleHit& h : b.turn.hits) {
        App::Battle::Actor* a = battleActor(app, h.target);
        const cr::Fighter& f = b.rules.f[size_t(h.target)];
        if (h.revived) btlvoice::playSide(app, h.target, 0x2b, 0x2c, 0x2d); // btlcalc_0424
        else if (!h.mp && !h.silent && h.type != cr::kHitQuiet) {
            const cr::BattleActionDef* act = app.battleActions.get(b.turn.action);
            if (act && act->calc == 40 && app.game.flag(0x584) && b.voiceSpeaks[size_t(h.target)]) // _btlcalc_0432's 1000-damage form: the side's speaker
                btlvoice::playSide(app, h.target, 0xa7, 0xa7, 0xa7);
            if (h.amount < 0) btlvoice::healed(app, h.target);   // btlcalc_dmg_hp
            else btlvoice::hpChange(app, h.target, h.amount);     // btlcalc_chk_dead (a miss too)
        }
        if (!h.mp && h.type != cr::kHitQuiet && h.target >= 2) { // btlcalc_chk_dead on a monster: the achievements' flags
            if (h.type != cr::kHitKill) battleConfFlag(app, cr::achievements::blowFlag(h.amount)); // a blow of 5,000 / 10,000 / 30,000
            if (h.fell) {
                int& kills = app.game.vars[cr::achievements::kKillVar]; // var 0x17 (to 9999); the 500th: 0x35
                if (kills < 9999 && ++kills >= 500) battleConfFlag(app, cr::achievements::kKills500);
                // a monster of battle models 33..138 leaves its gene plate when the bag has none (steal_item_add): 0x3e
                if (f.def && cr::plateDrop(f.def->model) > 0 && ownedCount(app, cr::plateDrop(f.def->model)) == 0)
                    battleConfFlag(app, cr::achievements::kPlateFound);
            }
        }
        b.shownHp[h.target] = f.hp;
        b.shownMp[h.target] = f.mp;
        if (!a || h.type == cr::kHitQuiet) continue;
        for (App::Battle::Popup& p : b.popups) { // (__dmg_set: the first free of five, else none)
            if (p.on) continue;
            p.on = true;
            p.at = actorHead(app, *a);
            p.amount = h.amount;
            p.type = h.type;
            p.target = h.target;
            p.mp = h.mp;
            p.timer = btlui::kPopupTime;
            break;
        }
        // _btlcalc_chk_dead (27526-27620) / _btlcalc_0424: a revive rises (7, then 0); a fall - a member
        // hit, then down looping (5, 6 in its loop), a monster hit, then standing as it fades (5, 0);
        // a hit standing - 5 then 0, unless no damage (a miss, fended off: nothing) or the model is
        // guarding (its slot 4 goes on)
        if (h.revived) playSlot(app, *a, btlui::kSlotRise, btlui::kSlotIdle);
        else if (h.fell) {
            const cr::MonsterDef* def = a->enemy ? f.def : nullptr;
            const LoadedModel& lm = b.models[a->model];
            const int down = battleAction(lm, btlui::kSlotDown); // (is_BTLANM_D02: a down motion of 3 frames or more)
            const bool hasDown = down >= 0 && down < int(lm.actions.size()) && lm.actions[size_t(down)].duration >= 3.0f;
            if (def && def->strongGrowth && hasDown) { // btlcalc_chk_dead: a boss
                const int g = b.start.group;
                if (g == 0x294) app.game.setFlag(0x596, true);
                if (g == 0x2a5) app.game.setFlag(0x791, true);
                int standing = 0; // (get_boss_cnt: it counts until marked fallen)
                for (int k = 2; k < 5; ++k)
                    if (k != h.target && b.rules.f[size_t(k)].alive()) ++standing;
                if (def->size == 1 || g == 0x292 || g == 0x2a2) { // down for good, and no end of its own
                    playSlot(app, *a, btlui::kSlotHit, btlui::kSlotDown);
                    b.bossEnd = false;
                } else if (standing == 0) { // the last: hit, standing, its end to come
                    playSlot(app, *a, btlui::kSlotHit, btlui::kSlotIdle);
                    b.bossFighter = h.target;
                    b.bossKey = a->modelId >= 0 && a->modelId < cr::kNumBattleModels ? cr::kBattleAnimTable[a->modelId][16] : 0;
                    b.bossEnd = true;
                } else { // others still up: gone at once (effect 7, sound 0x42)
                    a->fading = true;
                    playSlot(app, *a, btlui::kSlotHit, -2);
                    spawnEffect(app, 7, h.target);
                    app.sound.play(0x42);
                    b.bossEnd = true;
                }
            } else {
                if (a->enemy) a->fading = true; // monsters fade out over 20 frames
                playSlot(app, *a, btlui::kSlotHit, a->enemy ? btlui::kSlotIdle : btlui::kSlotDown);
            }
        } else if (h.amount > 0 && a->slot != btlui::kSlotGuard)
            playSlot(app, *a, btlui::kSlotHit, btlui::kSlotIdle);
    }
    // The calcs' sounds (Omega's __effgo in btlcalc_*, a target at a time; each heard once an action)
    std::vector<uint32_t> sounds;
    auto sound = [&](uint32_t id) {
        if (std::find(sounds.begin(), sounds.end(), id) == sounds.end()) sounds.push_back(id);
    };
    bool areaSounded = false;
    auto calcSound = [&](int calc, const cr::BattleHit& h, int user, int ue, bool area, int element) {
        const int t = h.target;
        switch (calc) {
        case 0: // 0404: fended off 0x39; a critical 0x32, the weapon's element 0x57 + it, else 0x2f..0x31
            // (actions 0x1fe / 0x1fc 0x50, 0xec 0x8d, 0x1b8 / 0x191 none) - a miss, no damage or the
            // invincible's 0 too (24747-24810: only type 6 is left out)
            if (h.type == cr::kHitGuard) sound(0x39);
            else {
                const int a = b.turn.action;
                if (a == 0x1fe || a == 0x1fc) sound(0x50);
                else if (a == 0xec) sound(0x8d);
                else if (a != 0x1b8 && a != 0x191)
                    sound(h.type == cr::kHitCritical ? 0x32 : ue > 0 ? 0x57 + uint32_t(ue) : 0x2f + uint32_t(b.rules.random(3)));
            }
            break;
        case 1: // 0406 (24952-24990): whenever the damage is 0 or more - an element's gene 0x47 + 2e a
            // target, its burst over a side 0x48 + 2e once; 0xf0 0x8e; 0x1b8, 0x1b2 / 0x1b3 none
            if (b.turn.action == 0xf0 && h.amount >= 0) sound(0x8e);
            else if (b.turn.action == 0x1b8 || b.turn.action == 0x1b2 || b.turn.action == 0x1b3) break;
            else if (element > 0 && h.amount >= 0) {
                if (!area) sound(0x47 + 2 * uint32_t(element));
                else if (!areaSounded) sound(0x48 + 2 * uint32_t(element)), areaSounded = true;
            }
            break;
        case 2: case 3: case 4: case 5: case 6: sound(0x43); break; // 0407 .. 0411
        case 7: if (t != user) sound(0x50); break;                   // 0412
        case 8: if ((t < 2) != (user < 2)) sound(0x52); break;       // 0413: the one drained
        case 12: sound(0x53); break;                                 // 0417
        case 13: case 16: case 18: case 22: sound(0x4f); break;      // 0418, 0421, 0423, 042700
        case 14: sound(0x35); break;                                 // 0419
        case 19: sound(0x54); break;                                 // 0424
        case 23: sound(b.turn.action == 0xe8 ? 0x53 : 0x4f); break;  // 042701
        case 24: sound(0x5c); break;                                 // 0428
        case 26: sound(0x5b); break;                                 // 030205
        case 27: case 28: case 29: case 30: case 32: case 33: case 34: case 35: case 36: case 38: case 39: case 43:
            sound(0x51); break;                                      // 030206 .. 0602, 0430, 0431, 0435
        case 31: case 42: sound(0x52); break;                        // 030210, 0434
        case 37: sound(0x8d); break;                                 // 0429
        case 40: sound(0xa3); break;                                 // 0432
        default: break;
        }
    };
    // The calcs' effects (eff3d_set_btl in btlcalc_*)
    const cr::BattleActionDef* calcAct = app.battleActions.get(b.turn.action);
    if (calcAct && calcAct->calc == 10) sound(0x2f + uint32_t(b.rules.random(3))); // 0415: once, after its effects
    if (const cr::BattleActionDef* act = calcAct) {
        const int user = b.turn.actors[0];
        const int ue = user >= 0 ? b.rules.f[size_t(user)].element : 0;
        bool area = false;
        for (const cr::BattleHit& h : b.turn.hits) {
            using namespace effects;
            const int t = h.target;
            int id = -1;
            switch (act->calc) {
            case 0: // btlcalc_0404: the element's hit, or the plain / critical one - all but the fended off
                // (type 6: its sound only); a miss, no damage or the invincible still jolt and show it
                if (h.type == cr::kHitGuard) break;
                id = h.type == cr::kHitCritical ? kCritical : ue > 0 ? 0x2a + 3 * ue : kHit; // (Omega's element hits 0x2a + 3e, CR1's 0x25)
                if (b.turn.action == 0x1fe || b.turn.action == 0x1fc) id = 0x12; // (Omega's own for these)
                else if (b.turn.action == 0xec) id = 0x28;
                else if (b.turn.action == 0x1b8 || b.turn.action == 0x191) id = -1;
                startQuake(app, 3, 100, 0, 5, false); // and the screen jolts (StartQuake(3, 100, 0, 5))
                break;
            case 1: // 0406: elemental genes - one burst over a whole side, else on each target (all of it
                // whenever the damage is 0 or more; 0xf0 its own 0xb; 0x1b8, 0x1b2 / 0x1b3 the jolt only)
                if (h.amount < 0) break;
                startQuake(app, 3, 100, 0, 5, false);
                if (b.turn.action == 0x1b8 || b.turn.action == 0x1b2 || b.turn.action == 0x1b3) break;
                if (b.turn.action == 0xf0) { id = 0xb; break; }
                if (act->element <= 0) break;
                if (act->scope == 5 || act->scope == 3) {
                    if (!area) spawnEffectAt(app, 0x29 + 3 * act->element, {500, 0, t < 2 ? 523.0f : 472.0f}); // (CR1's 0x24 + 5)
                    area = true;
                } else
                    id = kGene + 3 * act->element;
                break;
            case 2: case 3: case 4: case 5: case 6: case 9: case 10: id = kFixed; break;
            case 7: id = t == user ? -1 : kSacrifice; break;
            case 8: id = (t < 2) == (user < 2) ? kHeal : kDrain; break;
            case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 22: case 23: case 35: case 36: id = kHeal; break;
            case 19: id = kRevive; break;
            case 24: id = kAnalyze; break;
            case 25: id = act->element > 0 ? 0x36 + act->element : -1; break; // (CR1's 0x31 + 5)
            case 26: id = kBuff; break;
            case 27: case 28: case 29: case 30: case 32: case 33: case 34: id = kStatus; break;
            case 31: id = kDrain; break;
            case 38: id = kDrain; break; // (_btlcalc_0430: eff3d 0x2a on the target, immune or not - Part 8 3059.5 s)
            default: break;
            }
            if (id >= 0) spawnEffect(app, id, t);
            calcSound(act->calc, h, user, ue, act->scope == 5 || act->scope == 3, act->element);
            if (h.fell && t >= 2) { // btlcalc_chk_dead: monsters without strong growth burst
                const cr::Fighter& f = b.rules.f[size_t(t)];
                const App::Battle::Actor* fa = battleActor(app, t);
                const LoadedModel* flm = fa ? &b.models[fa->model] : nullptr;
                const int down = flm ? battleAction(*flm, btlui::kSlotDown) : -1;
                const bool boss = f.def && f.def->strongGrowth && down >= 0 && down < int(flm->actions.size()) &&
                                  flm->actions[size_t(down)].duration >= 3.0f; // (is_BTLANM_D02)
                if (f.def && !boss) spawnEffect(app, kFall, t), app.sound.play(0x41);
            }
        }
    }
    for (uint32_t id : sounds) app.sound.play(id);
    if (b.rules.breakGauge != b.gaugeShown && b.rules.breakGauge != 0 && (b.gaugeShown == 0 || (b.gaugeShown > 0) != (b.rules.breakGauge > 0)))
        b.breakPop = 5.0f; // btlbreak_set: the label pops when a side takes the Break
}

// btlbreak_effect_init / _prc: two white stripes at 30 degrees (alpha up to 152), "Player" and
// "Enemy" sliding along them (window_hd (704, 328) / (864, 328), 160 x 104), "BREAK!!" (512, 432,
// 512 x 72) and "The battle situation changed" (512, 504, 512 x 32) sliding in from opposite
// sides on 480 sin(t): 5 frames in, 30 held (at 15 the other side takes the Break, sound 0x7a),
// 5 out (sound 0x24, or 0x25 when the enemies hold it). Sound 0x23 as it starts (Omega's ids).
void startBreakFx(App& app) {
    App::Battle::BreakFx& fx = app.battle.breakFx;
    fx = App::Battle::BreakFx();
    fx.on = true;
    app.sound.play(0x23); // (Omega's btlbreak_effect_init; CR1's 0x47)
}
bool stepBreakFx(App& app) {
    App::Battle& b = app.battle;
    App::Battle::BreakFx& fx = b.breakFx;
    const float deg = 3.14159265f / 180.0f;
    if (fx.state == 0) {
        fx.t += 18;
        fx.slide = int(480.0f * std::sin(float(fx.t) * deg));
        fx.alpha = std::min(152, fx.alpha + 64);
        fx.pos += 96;
        if (fx.t >= fx.limit) fx.t = 0, fx.limit = 30, fx.state = 10;
    } else if (fx.state == 10) {
        ++fx.t;
        if (fx.t == fx.limit / 2) {
            b.rules.takeBreak();
            b.breakPop = 5.0f;
            app.sound.play(0x5d); // (CR1's 0x7a)
        }
        if (fx.t >= fx.limit) {
            fx.t = 0, fx.limit = 90, fx.state = 0x3e6;
            app.sound.play(b.rules.breakGauge < 1 ? 0x25 : 0x24); // (CR1's 0x49 / 0x48)
        }
    } else if (fx.state == 0x3e6) {
        fx.t += 18;
        fx.slide = int(480.0f * std::sin(float(fx.t) * deg) + 480.0f);
        fx.alpha = std::max(0, fx.alpha - 64);
        fx.pos += 96;
        if (fx.t >= fx.limit) fx.state = 999;
    } else
        return true;
    return false;
}
void drawBreakFx(App& app, cr::MenuPainter& m) {
    const App::Battle::BreakFx& fx = app.battle.breakFx;
    if (!fx.on) return;
    const float a = float(fx.alpha) / 255.0f, white[4] = {1, 1, 1, a};
    if (app.renderer.phoneLayout()) { // 2.2.0 (the Retina path): the stripes into the iPhone screen's
        // corners ((0, 160)-(280, 0)-(0, 216)-(376, 0), (160, 320)-(480, 140)-(256, 320)-(480, 192)),
        // "Player" at (pos cos 30 + 130, 10 - pos sin 30), "Enemy" at (234 - pos cos 30, pos sin 30
        // + 208), "BREAK!!" (528, 432, 480 x 72) at (slide - 480, 120), the line (528, 504, 480 x 32)
        // at (480 - slide, 192). Android 1.1.6's btlbreak_effect_prc: the first stripe and "Player"
        // (- ex, - ey) into the phone's top left, the second and "Enemy" (+ ex, + ey) into its
        // bottom right; "BREAK!!" and the line stay centred
        const float ex = androidEdgeX(app), ey = androidEdgeY(app);
        for (float x = 0; x < 480; x += 2) {
            const float top1 = 160 - 160 * x / 280, bot1 = 216 - 216 * x / 376;
            if (bot1 > top1 && bot1 > 0) m.quad(x + 16 - ex, std::max(top1, 0.0f) + 32 - ey, 2, bot1 - std::max(top1, 0.0f), white, white);
            if (x >= 160) {
                const float top2 = std::max(140.0f, 320 - 180 * (x - 160) / 320);
                const float bot2 = x < 256 ? 320 : std::max(192.0f, 320 - 128 * (x - 256) / 224);
                if (bot2 > top2) m.quad(x + 16 + ex, top2 + 32 + ey, 2, bot2 - top2, white, white);
            }
        }
        const float c = std::cos(30 * 3.14159265f / 180), s = std::sin(30 * 3.14159265f / 180), pos = float(fx.pos);
        m.piece(pos * c + 130 + 16 - ex, 10 - pos * s + 32 - ey, 704, 328, 160, 104);
        m.piece(234 - pos * c + 16 + ex, pos * s + 208 + 32 + ey, 864, 328, 160, 104);
        m.piece(float(fx.slide) - 480 + 16, 120 + 32, 528, 432, 480, 72);
        m.piece(480 - float(fx.slide) + 16, 192 + 32, 528, 504, 480, 32);
        return;
    }
    // tex2d -> menu units: + (16, 32). The stripes as 2-unit columns between their edges
    for (float x = -16; x < 496; x += 2) {
        const float top1 = 128 - 160 * (x + 16) / 280, bot1 = 184 - 216 * (x + 16) / 376;
        if (bot1 > top1 && bot1 > -32) m.quad(x + 16, std::max(top1, -32.0f) + 32, 2, bot1 - std::max(top1, -32.0f), white, white);
        if (x >= 176) {
            const float top2 = std::max(172.0f, 352 - 180 * (x - 176) / 320);
            const float bot2 = x < 272 ? 352 : std::max(224.0f, 352 - 128 * (x - 272) / 224);
            if (bot2 > top2) m.quad(x + 16, top2 + 32, 2, bot2 - top2, white, white);
        }
    }
    const float c = std::cos(30 * 3.14159265f / 180), s = std::sin(30 * 3.14159265f / 180), pos = float(fx.pos);
    m.piece(pos * c + 130 - 16 + 16, 10 - pos * s - 32 + 32, 704, 328, 160, 104);
    m.piece(234 - pos * c + 16 + 16, pos * s + 208 + 32 + 32, 864, 328, 160, 104);
    m.piece(float(fx.slide) - 496 + 16, 114 + 32, 512, 432, 512, 72);
    m.piece(464 - float(fx.slide) + 16, 186 + 32, 512, 504, 512, 32);
}

// The enemies are down (btl_main 1000): the victory pose and tune, then the results
// btl_main 1000: ten frames while the battle's music fades out (unless the tune is the battle's own
// or 997), then 0x3e9 - the victory (victoryShown)
void battleWon(App& app) {
    App::Battle& b = app.battle;
    b.phase = App::Battle::Won;
    b.phaseFrames = 0;
    b.wonTap = false;
    const auto v58 = app.game.vars.find(58), v59 = app.game.vars.find(59);
    const int battleTune = v58 != app.game.vars.end() ? v58->second : 999, tune = v59 != app.game.vars.end() ? v59->second : 10;
    if (tune != 997 && battleTune != tune) playBgm(app, 1u << 26); // (__bgmgo(0x4000000): out over 10 frames)
}
void victoryShown(App& app) {
    App::Battle& b = app.battle;
    btlvoice::victory(app); // btl_main 0x3e9
    int& bank = app.game.walkExp[size_t(fieldRoute(app))]; // skill 174's walking EXP joins the battle's
    if (!b.settled) { // (btl_main 0x44b: the second scenario's EXP a fifth more, then the walked bank, then the Apples)
        if (app.game.scenario == 1) b.rules.expGained += int(unsigned(b.rules.expGained) * 2 / 10);
        b.rules.expGained += bank, bank = 0;
    }
    settleBattle(app);
    { // var 59: the victory tune (10 for random encounters) - none when it's the battle's own (var 58),
        // or 997 / 998, the battle's music played on (btl_main 0x3e9)
        const auto v58 = app.game.vars.find(58), v59 = app.game.vars.find(59);
        const int battleTune = v58 != app.game.vars.end() ? v58->second : 999, tune = v59 != app.game.vars.end() ? v59->second : 10;
        if (battleTune != tune && tune != 998 && tune != 997) playBgm(app, uint32_t(tune >= 0 && tune < 0x26 ? tune : 10));
    }
    if (b.start.group == 0x295) app.game.setFlag(0x644, true);
    for (int k = 0; k < 2; ++k)
        if (App::Battle::Actor* a = battleActor(app, k); a && b.rules.f[size_t(k)].alive()) {
            a->yaw = a->wantYaw = a->homeYaw;
            playSlot(app, *a, btlui::kSlotWin);
        }
    for (App::Battle::Actor& a : b.actors)
        if (a.fighter >= 0 && a.fighter < 2) a.hidden = false;
    setBattleCamera(app, b.rules.pair ? 0x47 : b.voiceLeader + 0x45); // (btl_main 1099: +0xaf2, the order's last leader)
    setBanner(app, sysText(app, 57007), 60);                                    // font_set_utf16(11, 6, 7, 60)
}

// btl_main 8000 -> 1000: a battle whose last boss fell standing (G+0xa67), won in any formation but
// 2, shows its end first - the camera on it (the formation's 0x34th for model 0, 0x35th for a lone
// boss, else 0x67th), the fallen monsters' down motion (btlanm_boss_dead_anim), the pair out of
// sight. Models 0xd .. 0x12, 0, 0x1b, 0x1d .. 0x20 (0x3df .. 0x3e6): a quake and a white-out, the
// boss gone under it, the pair back with its win pose; any other (0x3de): its down motion played
// (sound 0xc6 at its frame), then it fades (effect 7, sound 0x42) and 40 frames pass. Then 1000's
// 10 frames, and the victory (battleWon)
void battleVictory(App& app) {
    App::Battle& b = app.battle;
    App::Battle::Actor* boss = battleActor(app, b.bossFighter);
    if (!b.bossEnd || b.formation == 2 || !boss) return battleWon(app);
    b.phase = App::Battle::BossDown;
    b.phaseFrames = 0;
    b.bossModel = boss->modelId;
    int bosses = 0; // (G+0xa79)
    for (int k = 2; k < 5; ++k)
        if (const cr::Fighter& f = b.rules.f[size_t(k)]; f.present && f.def && f.def->strongGrowth) ++bosses;
    for (App::Battle::Actor& a : b.actors) {
        if (a.fighter >= 2 && !b.rules.f[size_t(a.fighter)].alive() && a.slot != btlui::kSlotDown) playSlot(app, a, btlui::kSlotDown, -3);
        if (a.fighter >= 0 && a.fighter < 2 && b.rules.f[size_t(a.fighter)].alive()) a.hidden = true;
    }
    b.bossWait = int(actorDuration(app, *boss)) - 1 + 20; // (btlanm_get_max_frm(.., 6) + 20)
    b.camHold = b.bossWait; // (+0xae8 = the same: cameras 0x40 / 0x43 / 0x44's 10000 turn over it)
    setBattleCamera(app, b.formation + (b.bossModel == 0 ? 0x34 : bosses == 1 ? 0x35 : 0x67));
}

void stepBossDown(App& app) {
    App::Battle& b = app.battle;
    const int f = b.phaseFrames, m = b.bossModel;
    App::Battle::Actor* boss = battleActor(app, b.bossFighter);
    // the white-outs (0x3e0 .. 0x3e6, 0x3df): sound 0xb9 with 0x45 (0x3e3: 0xb9, 0x3df: 0x45), the white's
    // frame and time in, when the boss goes and when the white goes (sound 0xe6) and the pair's back
    struct WhiteOut { int models[2], white, whiteMs, gone, back, backMs; bool b9, s45; };
    static const WhiteOut kWhite[] = {
        {{0xd, 0xe}, 60, 1000, 90, 100, 2000, true, true},     {{0xf, 0x10}, 66, 700, 90, 100, 2000, true, true},
        {{0x11, 0x12}, 104, 1000, 130, 140, 2000, true, true}, {{0, -1}, 60, 3500, 135, 140, 2000, true, false},
        {{0x1b, -1}, 53, 1000, 73, 80, 2000, true, true},      {{0x1d, -1}, 100, 1000, 120, 140, 1500, true, true},
        {{0x1e, 0x1f}, 50, 1000, 70, 90, 2000, true, true},    {{0x20, -1}, 104, 1000, 124, 140, 2000, false, true},
    };
    const WhiteOut* w = nullptr;
    for (const WhiteOut& x : kWhite)
        if (x.models[0] == m || x.models[1] == m) w = &x;
    if (!w) { // 0x3de, then 999 (+0xae8 40, on while above 1: 39 frames) and 1000 - the Won phase's own 10
        if (b.bossWait > 0) {
            if (boss && b.bossKey != 0 && int(boss->time) == b.bossKey) app.sound.play(0xc6);
            if (--b.bossWait < 2) {
                b.bossWait = -40; // (Part 6 955.44 -> 957.88 s: the fade's effect to the victory, 49 frames)
                if (boss) boss->fading = true;
                // effect 7: models 0x6b .. 0x6f 10 up and 0x5e .. 0x61 5 up from where they stand, at
                // 300%; any other on its own (eff3d_set_btl)
                const int bm = boss ? boss->modelId : -1;
                if (boss && ((bm >= 0x6b && bm <= 0x6f) || (bm >= 0x5e && bm <= 0x61)))
                    startEffect(app, b.effects, 7, boss->pos + cr::Vec3{0, bm >= 0x6b ? 10.0f : 5.0f, 0}, false, 3.0f, 0.0f);
                else spawnEffect(app, 7, b.bossFighter);
                app.sound.play(0x42);
            }
        } else if (++b.bossWait == 0) battleWon(app);
        return;
    }
    if (f == 1) {
        if (w->b9) app.sound.play(0xb9);
        if (w->s45) app.sound.play(0x45);
        startQuake(app, 3, 0, 100, 2, true);
    }
    if (f == w->white) setFlash(app, 0, 255, w->whiteMs);
    if (f == w->gone) {
        if (App::Battle::Actor* m2 = battleActor(app, 2)) m2->hidden = true; // (G+0x188's model)
        endQuake(app, 3, 100, 0, 2);
    }
    if (f == w->back) {
        app.sound.play(0xe6);
        setFlash(app, 255, 0, w->backMs);
        setBattleCamera(app, b.rules.pair ? 0x47 : b.voiceLeader + 0x45, -100); // (btl_main 0x3df: one step of its turn)
        for (App::Battle::Actor& a : b.actors)
            if (a.fighter >= 0 && a.fighter < 2 && b.rules.f[size_t(a.fighter)].alive()) {
                a.hidden = false;
                a.yaw = a.wantYaw = a.homeYaw;
                playSlot(app, a, btlui::kSlotWin); // (btl_win_anm)
            }
    }
    if (f == w->back) battleWon(app); // (0x3df / 0x3e0 ..: straight to 1000, whose 10 frames are the Won phase's)
}

// chk_cpl657_mapchange: group 0x291 in the first scenario, the boss (fighter 2) at 80% / 60% / 20%
// of its HP for the first time (flags 0x591 / 0x592 / 0x593) - the battle's stage 0x15 / 0x18 / 0x19
int stageChange657(App& app) {
    App::Battle& b = app.battle;
    if (app.game.scenario != 0 || b.start.group != 0x291) return -1;
    const int hp = b.rules.f[2].hp, most = b.rules.f[2].hpMax;
    if (hp <= most / 5 && !app.game.flag(0x593)) return app.game.setFlag(0x593, true), 0x19;
    if (hp <= most * 60 / 100 && !app.game.flag(0x592)) return app.game.setFlag(0x592, true), 0x18;
    if (hp <= most * 80 / 100 && !app.game.flag(0x591)) return app.game.setFlag(0x591, true), 0x15;
    return -1;
}

// btl_main 0x366: group 0x294's end - from frame 40 white over 3 s (sounds 0x41, 0xcb, 0x48, 0x128, the
// music fading), the models gone at 129, the white gone at 130 (150 ms), and at 163 the quake and the
// rumble stopped and the battle left for the field, where its script goes on. 0x37a: white over 1 s
// (sound 0xcb), at 22 the new stage and the white going, at 44 the commands again (and with stage
// 0x15 the script's talk)
void stepSetPiece(App& app) {
    App::Battle& b = app.battle;
    const int f = b.phaseFrames;
    if (b.setPiece == 0x366) {
        if (f == 40) {
            setFlash(app, 0, 255, 3000);
            for (uint32_t id : {0x41u, 0xcbu, 0x48u, 0x128u}) app.sound.play(id);
            playBgm(app, 0x2000000);
        }
        if (f == 129)
            for (App::Battle::Actor& a : b.actors) a.hidden = true;
        if (f == 130) setFlash(app, 255, 0, 150);
        if (f == 163) {
            endQuake(app, 2, 0, 0, 1);
            app.sound.play(0x20000011);
            b.exit = 1;
        }
        return;
    }
    if (f == 1) setFlash(app, 0, 255, 1000), app.sound.play(0xcb);
    if (f == 22 && b.setPieceStage >= 0 && b.setPieceStage < cr::kNumBattleStages) {
        unloadLoaded(app, b.stage);
        loadModelFile(app, cr::kBattleStageFile[b.setPieceStage], b.stage, false);
        readMdlHdRules(b.stage, b.setPieceStage);
        b.start.stage = b.rules.stage = b.setPieceStage; // (G+0x8a53d)
        setFlash(app, 255, 0, 1000);
    }
    if (f == 44) {
        b.phase = App::Battle::Command;
        b.phaseFrames = 0;
        b.member = 0;
        b.commandSaid[0] = b.commandSaid[1] = false;
        battleMenu(app, App::Battle::Top);
        battletalk::between(app);
        if (b.setPieceStage == 0x15) battletalk::open(app);
    }
}

void endRound(App& app) {
    App::Battle& b = app.battle;
    if (b.rules.outcome() == 1) { // _btlact_escape returns 99: btl_main goes to 9000 at once - no round's end
        b.phase = App::Battle::Fled; // (statuses, poison) and no wait before the black (3.1.0 48771-48900)
        b.phaseFrames = 0;
        settleBattle(app);
        b.exit = 1;
        return;
    }
    battletalk::rules(app); // (status_prc's boss rules as the round ends)
    b.rules.endRound();
    for (int k = 0; k < 2; ++k) // the guard pose ends with the round
        if (App::Battle::Actor* a = battleActor(app, k); a && a->slot == btlui::kSlotGuard) playSlot(app, *a, btlui::kSlotIdle);
    for (int i = 0; i < 5; ++i) b.shownHp[i] = b.rules.f[size_t(i)].hp, b.shownMp[i] = b.rules.f[size_t(i)].mp;
    { // btlord_chk's story ends, before the round's outcome is read: scenario 0's group 0x28e once enemy 3 is
      // down (no enemy there counts as down), 0x28a - the opening, j001_t01's Agent - and scenario 1's 0x2a3
      // - c101_t01's, its twin - from the first round: flag 0xffe; with it set (btl_main 799) the battle ends
      // there (9000), the pair standing or not: its script goes on. The code reads "round > 1" / ">= 2" for
      // these two, but the reference video shows the opening's FIRST round end that way: Part 1 1581-1594 s,
      // one command page, the pair's Snow Tiger Lunar Lion, then the Particle Cannon (_btlque_special's
      // "round 2" action) for 2848 / 3031 on Vieg 803 / Olgar 1153, both HP rolled to 0, and the scene goes
      // on ("Gah! This is lookin' bad...") - no defeat. (Why the count reads one more there is not traced.)
      // The port's first round lost was the game over (CRO-U #3: both battles, the Agent's passed only
      // with Infinite HP)
        const int g = b.start.group, s = app.game.scenario;
        if ((s == 0 && g == 0x28e && !b.rules.f[3].alive()) || (s == 0 && g == 0x28a) || (s == 1 && g == 0x2a3))
            app.game.setFlag(0xffe, true);
        if (app.game.flag(0xffe)) {
            b.phase = App::Battle::StoryEnd;
            b.phaseFrames = 0;
            return;
        }
    }
    switch (b.rules.outcome()) {
    case 2: // won: the victory pose and tune, then the results (btl_main 0x44b) - a story boss's
        // last word first (8000)
        if (battletalk::end(app)) b.phase = App::Battle::WinTalk, b.phaseFrames = 0;
        else battleVictory(app);
        break;
    case 3: { // lost (0x4b1): the defeat tune and "... lost the battle"
        // btl_main 0x4b0: groups 0x289, 0x28e, 0x2a2 and 0x295 are lost as the story has it - straight to
        // the battle's end (9000), no defeat (flag 0xffd stays clear, so no btl_lose): its script goes on
        if (b.start.group == 0x289 || b.start.group == 0x28e || b.start.group == 0x2a2 || b.start.group == 0x295) {
            b.phase = App::Battle::StoryEnd;
            b.phaseFrames = 0;
            break;
        }
        b.phase = App::Battle::Lost;
        b.phaseFrames = 0;
        b.lostTap = false;
        playBgm(app, 1u << 26); // btl_main 0x4b0: the battle's music out over 10 frames; 0x4b1 then plays the defeat's (0x1a) with its text
        break;
    }
    case 1:
        b.phase = App::Battle::Fled;
        b.phaseFrames = 0;
        settleBattle(app);
        break;
    default: {
        // btl_main 0x320 (7444-7461): the pair standing (slot 7, the getting up, apart) - and formation 2's
        // first two monsters - back to their idles (mdl_set_act_motion_nextanm 0): a cast's loop ends here
        for (int k = 0; k < (b.formation == 2 ? 4 : 2); ++k)
            if (App::Battle::Actor* a = battleActor(app, k); a && b.rules.f[size_t(k)].alive() && a->slot != btlui::kSlotRise && a->slot != btlui::kSlotIdle)
                playSlot(app, *a, btlui::kSlotIdle);
        // btl_main 0x352's end: group 0x294's tenth round (chk_cpl660) or group 0x291's stage change
        // (chk_cpl657_mapchange) in place of the commands
        const int piece = app.game.scenario == 0 && b.start.group == 0x294 && b.rules.round == 10 ? 0x366 : 0;
        const int stage = piece ? -1 : stageChange657(app);
        if (piece || stage >= 0) {
            b.phase = App::Battle::SetPiece;
            b.phaseFrames = 0;
            b.setPiece = piece ? piece : 0x37a;
            b.setPieceStage = stage;
        } else { // btl_main 800 -> 0x352: thirty frames on the formation's camera, then the commands (400)
            b.phase = App::Battle::RoundEnd;
            b.phaseFrames = 0;
            b.member = 0;
            b.commandSaid[0] = b.commandSaid[1] = false; // +0xafc / +0xafd
            setBattleCamera(app, std::max(0, b.formation - 2));
        }
        if (b.start.group == 0x294 && app.game.scenario == 0) { // btl_main 800: group 0x294's rounds - the quake
            // dying away over 60 frames under a line, the rumble (0x11) fading over 40; the 10th held, looping
            struct Rumble { int round, message; float amp; };
            static const Rumble kRumbles[] = {{1, 57109, 8}, {2, 57095, 5}, {3, 57096, 5}, {4, 57096, 15},
                                              {6, 57097, 10}, {7, 57110, 30}, {8, 57098, 20}};
            for (const Rumble& r : kRumbles)
                if (b.rules.round == r.round) {
                    setBanner(app, sysText(app, r.message), 60);
                    endQuake(app, r.amp, 100, 0, 60);
                    app.sound.play(0x11);
                    app.sound.play(0x2000011);
                }
            if (b.rules.round == 10) {
                setBanner(app, sysText(app, 57099), 60);
                endQuake(app, 20, 100, 100, 260);
                app.sound.play(0x800011);
            }
        }
        if (b.phase == App::Battle::SetPiece) break;
        battletalk::between(app); // btl_main 0x352
        if (app.game.flag(0xffe)) { // (0x28d at half HP) btl_main 400: the flag set, the battle ends (9000)
            b.phase = App::Battle::StoryEnd;
            b.phaseFrames = 0;
            break;
        }
        if (!app.game.flag(battletalk::kFlag)) battletalk::betweenStart(app); // btl_main 0
        break;
    }
    }
}



// FUN_00036c80 / 36d54 (from __menub_mainPut, every frame): the party's shown HP / MP step toward the
// value by a fifth of the gap, or by 1 within 4
void rollPartyNumbers(App::Battle& b) {
    auto roll = [](int& shown, int want) {
        const int d = want - shown;
        if (d == 0) return;
        shown += std::abs(d) <= 4 ? (d > 0 ? 1 : -1) : d / 5;
    };
    for (int k = 0; k < 2; ++k) roll(b.rollHp[k], b.shownHp[k]), roll(b.rollMp[k], b.shownMp[k]);
}

// How far a page slides: 320 in iOS 3.1.0 (80 a frame); Android 1.1.6's _menub_mainPrc takes 480 on
// a Retina or 720 x 480 screen (320 below) and moves a quarter of it a frame - its iPhone screen is
// centred on a wider phone, where 320 would leave the plates in sight at the side. The Android
// layout is a 720-tall phone's (Retina): 480, 120 a frame (Part 2 1248.76 s: the plate 240 px right)
int battleSlideDistance(const App& app) { return app.renderer.androidScreen() ? 480 : 320; }

// menub_mainPrc's slides: the page shown follows the logic's - it goes out (+80 a frame to 320),
// is held out a while (Defend's 20 frames), and the next comes in (-80 a frame to 0: state 1's
// sound 0x22 for the first page); with none to show (a talk, the round, Options) it stays out.
// The left HUD (+0x3c) goes 480 to the left, 120 a frame, while a talk runs and comes back after
void stepMenuView(App& app) {
    App::Battle& b = app.battle;
    App::Battle::MenuView& v = b.view;
    const bool talk = battletalk::on(app);
    // (and as the results open: btl_main 1099's end - FUN_0001dd08 sends the two windows to x -480 over 4 frames,
    // Part 3 979.44-979.56 s)
    const bool hudOut = talk || b.phase == App::Battle::Results;
    b.hudSlide = hudOut ? std::min(480, b.hudSlide + 120) : std::max(0, b.hudSlide - 120);
    const int want = b.phase == App::Battle::Command && !talk && !app.optionsOpen ? b.page : -1;
    auto live = [&]() { // the page as the logic has it
        v.menu = b.menu, v.cursor = b.cursor, v.member = b.member, v.pick = b.pick, v.pickTop = b.pickTop;
        v.confirm = b.confirm, v.targetAction = b.targetAction, v.pair = b.rules.pair, v.picks = b.picks;
    };
    if ((b.viewState == 1 || b.viewState == 2) && v.page != want) b.viewState = 3;
    switch (b.viewState) {
    case 1: // coming in
        live();
        b.slide = std::max(0, b.slide - battleSlideDistance(app) / 4);
        if (b.slide == 0) {
            b.viewState = 2;
            if (v.menu == App::Battle::Top) app.sound.play(0x22); // (__menub_mainPrc 1 -> 2: the top menu in place)
        }
        return;
    case 2: live(); return;
    case 3: // going out
        b.slide = std::min(battleSlideDistance(app), b.slide + battleSlideDistance(app) / 4);
        if (b.slide < battleSlideDistance(app)) return;
        b.viewState = b.slideHold > 0 ? 4 : 0;
        if (b.viewState == 4) return;
        break;
    case 4: // held out
        if (--b.slideHold > 0) return;
        b.viewState = 0;
        break;
    default:
        if (v.page == want || want < 0) { // nothing new: out
            v.page = -1;
            ++b.menuGone;
            return;
        }
        break;
    }
    // out: the next page (or none)
    b.slide = battleSlideDistance(app);
    b.slideHold = 0;
    b.menuGone = 0;
    v.page = want;
    if (want < 0) return;
    live();
    b.viewState = 1;
    if (v.menu == App::Battle::EscapeAsk) setBanner(app, sysText(app, 57015), 1 << 20); // 800: font_set_utf16(11, 6, 15), kept (801)
    if (v.menu == App::Battle::Targets) // 900: btlord_make_select's font_set_utf16(11, 6, 0x42 + scope), kept (901)
        if (const cr::BattleActionDef* a = app.battleActions.get(v.targetAction)) setBanner(app, sysText(app, 57066 + a->scope), 1 << 20);
}

// The command pages' cursor plate lit: always for keys and pads; after a touch or click only as the
// page goes (the plate just tapped) - 3.1.0 is touch-only and lights none till a tap
// (touch play: none - the held plate lights instead, and goes dark as the page leaves: Part 2
// 1249.18-1249.26 s, Pair lit while held, dark from the slide's first frame)
bool battleCursorLit(const App& app) { return !app.pointerLast; }
// A touch held inside a battle box (battleClick's units: rx from the right-hand group, my), x / y / w / h
bool battlePressIn(const App& app, float x, float y, float w, float h) {
    if (!app.press.down || app.press.screen != kPressBattle) return false;
    float lx, ly;
    app.renderer.toLogical(app.press.px, app.press.py, lx, ly);
    const float uiX = app.renderer.uiX(), rightX = (app.renderer.phoneLayout() ? uiX : 2 * uiX) + 2 * vitaEdge43(app); // (the Vita's menus 43 right)
    const float rx = (lx - rightX) / 2, my = ly / 2;
    return rx >= x && rx < x + w && my >= y && my < y + h;
}

// Taps are taken once the page is in (and the logic's page is the one shown)
bool battleMenuReady(const App& app) {
    const App::Battle& b = app.battle;
    return b.viewState == 2 && b.view.page == b.page;
}

// _mdl_put (11752-11761): each model's shown yaw toward its wanted one, by +0xfb0 degrees a frame
// (GetNewMdlDir: the shorter way round, wrapped), the step back to 15 once it's there
void turnActors(App& app) {
    constexpr float kTurn = 2.0f * 3.14159265f;
    for (App::Battle::Actor& a : app.battle.actors) {
        float d = std::remainder(a.wantYaw - a.yaw, kTurn);
        const float step = a.turnStep / 360.0f * kTurn;
        if (std::fabs(d) <= step) {
            a.yaw = a.wantYaw;
            a.turnStep = 15;
        } else
            a.yaw = std::remainder(a.yaw + (d > 0 ? step : -step), kTurn);
    }
}

// A turn timed over `frames` (btlmove_prc / the attack's return and end: the gap x 360 / frames)
void turnOver(App::Battle::Actor& a, float want, float frames) {
    const float d = std::fabs(std::remainder(want - a.wantYaw, 2.0f * 3.14159265f));
    a.wantYaw = want;
    a.turnStep = d * 180.0f / 3.14159265f / std::max(1.0f, frames);
}

void stepBattle(App& app) {
    rollPartyNumbers(app.battle);
    App::Battle& b = app.battle;
    if (SDL_getenv("CR_BATTLE_BANNERLOG")) { // the top banner's changes, by battle frame (the last frame's state)
        static std::string shown;
        static int shownPhase = -1;
        static bool shownHits = false;
        if (b.banner != shown || int(b.phase) != shownPhase)
            std::printf("battle frame %d: phase %d, banner '%s' (%d left), camera %d\n", b.frames, int(b.phase), b.banner.c_str(), b.bannerFrames, b.cam);
        const bool hits = b.turnOn && b.hitsShown;
        if (hits && !shownHits) std::printf("battle frame %d: action %d's hits shown\n", b.frames, b.turn.action);
        shown = b.banner, shownPhase = int(b.phase), shownHits = hits;
    }
    b.bannerShown = b.bannerFrames > 0 ? b.banner : std::string(); // (what the last frame set shows from this one)
    ++b.frames;
    ++b.phaseFrames;
    stepEffects(app);
    stepSceneEffects(app);
    for (App::Battle::Actor& a : b.actors) {
        const float dur = actorDuration(app, a);
        const cr::BattleAnim an = app.battleData.anim(a.modelId, a.slot);
        // (a new slot: its first frame held through the cross-fade - the model's frame lags the change
        // by it, the move along its window doesn't: btlmove_prc counts on its own)
        if (!a.blend.step(a.action, a.time, 1.0f, b.models[a.model].actions)) a.time += 1.0f;
        if (a.then == -2) { // held on its last frame (a fallen monster)
            a.time = std::max(dur - 1, 0.0f);
        } else if (a.then == -3) { // played once, then held (mdl_set_act_motion_loop mode 2: a boss's end)
            a.time = std::min(a.time, std::max(dur - 1, 0.0f));
        } else if (a.then >= 0) {
            if (a.time >= std::max(dur, 1.0f)) {
                const int next = a.then;
                const bool back = next == btlui::kSlotReturn;
                if (back) startAttackReturn(app, a); // and walks back to its place meanwhile (a blow not on _btlact_attack's count)
                else playSlot(app, a, next, -1);
                if (!back && next == btlui::kSlotIdle) { // (the yaw is left: the attack's end turns it home)
                    a.pos = a.home;
                    a.moveFrom = a.moveTo = 0;
                }
            }
        } else if (a.loopTo > a.loopFrom && a.time >= float(a.loopTo))
            a.time = float(a.loopFrom) + (a.time - float(a.loopTo));
        else if (a.loopTo <= a.loopFrom && an.loopEnd > an.loopStart && a.time >= float(an.loopEnd))
            a.time = float(an.loopStart) + (a.time - float(an.loopEnd));
        else if (dur > 0 && a.time >= dur)
            a.time = std::fmod(a.time, dur);
        if (a.moveTo > a.moveFrom) {
            const float t = std::clamp((a.time + a.blend.lag() - float(a.moveFrom)) / float(a.moveTo - a.moveFrom), 0.0f, 1.0f);
            a.pos = a.from + (a.to - a.from) * t;
            // btlmove_prc: a setpos_angy move, its wait over, turns the wanted yaw home (0 / 0.5) over the move
            if (a.turnHome && a.time + a.blend.lag() >= float(a.moveFrom)) {
                turnOver(a, a.homeYaw, float(a.moveTo - a.moveFrom));
                a.turnHome = false;
            }
        }
        if (a.fading) a.alpha = std::max(0.0f, a.alpha - 1.0f / 20.0f);
    }
    // The badge's gauge follows the rules' a step every other frame (menub_mainPut: only while
    // prc's frame count +0x918c4 is even; Part 3 1117.02-1117.38 s: a tick each 0.1 s), the pop shrinks
    if (b.gaugeShown != b.rules.breakGauge && (b.steps & 1) == 0) b.gaugeShown += b.gaugeShown < b.rules.breakGauge ? 1 : -1;
    ++b.steps;
    if (b.breakPop >= 1.0f) b.breakPop -= 4.0f / 7.0f;
    if (b.bannerFrames > 0) --b.bannerFrames;
    if (b.bannerFrames == 0 && !b.notices.empty()) { // menub_mainPut: the next queued message
        const cr::BattleNotice n = b.notices.front();
        b.notices.erase(b.notices.begin());
        std::string name; // var 9 the item (Steal Item), else var 0 the fighter
        if (n.fighter < 0) name = itemName(app, -n.fighter);
        else if (const App::Battle::Actor* u = battleActor(app, n.fighter)) name = u->name;
        setBanner(app, sysText(app, 57000 + n.entry, name), btlui::kMessageFrames);
    }
    for (App::Battle::Popup& p : b.popups)
        if (p.on && (p.timer -= 16) < 0) p.on = false;

    battletalk::rules(app);
    if (app.game.flag(battletalk::kFlag)) { // a talk: the script runs (its fades with it), the battle waits
        battletalk::step(app);
        stepMenuView(app);
        turnActors(app);
        stepBattleCamera(app);
        stepQuake(app);
        stepFlash(app);
        return;
    }
    if (app.fadeT < app.fadeFrames) ++app.fadeT; // the screen's fade (stepEvents' while a talk runs)
    if (app.fadeBackT < app.fadeBackFrames) ++app.fadeBackT;
    switch (b.phase) {
    case App::Battle::Intro: // btl_main 10 / 100 / 199 under the encounter's white, then 200 and 300
        if (b.phaseFrames == kIntroWhite) {
            // 200: the fade in over 20 frames, the formation's camera held still (actor -1), the 300
            // counter (+0xae8: 30, a boss's formation 2 60), a strike's or ambush's announcement (sys
            // texts 0x36 / 0x37 for 60 frames)
            app.fadeGrey = 1, app.fadeFrom = 1, app.fadeTo = 0, app.fadeFrames = 20, app.fadeT = 0;
            setBattleCamera(app, std::max(0, b.formation - 2), -1);
            if (b.start.strike == 1 || b.start.strike == 2) { // (var 0: the lead's name, +0xaa - "Vieg's team attacks first!")
                const App::Battle::Actor* lead = battleActor(app, 0);
                setBanner(app, sysText(app, 57053 + b.start.strike, lead ? lead->name : std::string()), 60);
            }
        }
        // 300's first frame (7348-7358): the counter at 29 sets the formation's camera again with actor 0 -
        // cameras 0..0xd turn (raw[14] -20 over raw[15] 15); formation 2's at 59 camera 0x21b (-25 over 30)
        // instead. The turn's first frame cuts to it (count 1: _Clr_evcv), the rest ease in by a fifth a
        // frame - the move is made under the fade and settles before the commands
        if (b.phaseFrames == kIntroWhite + 1) setBattleCamera(app, b.formation == 2 ? 0x21b : std::max(0, b.formation - 2), 0);
        if (b.introStep == 300 && b.phaseFrames >= kIntroWhite + (b.formation == 2 ? 60 : 30)) {
            // the counter below 1 (and the fade done): 400 next; _chk_karaok_frist - a story battle's talk
            // (flag 0xfff) holds 400 till it ends
            b.introStep = 400;
            battletalk::first(app);
        } else if (b.introStep == 400) {
            // 400 (7366-7400): ambushed (+0x8a910 2) - 500, the enemies' round, no menu and no camera of
            // its own (the turned one stays until the first action's); else 0x364 |= 0x200 and 499
            if (b.start.strike == 2) battleStartRound(app);
            else b.introStep = 499;
        } else if (b.introStep == 499) {
            // __menub_mainPrc 0 the frame after (PrcMenu runs before btl_prc): the top menu, sliding in from
            // 320, and its camera (formation + 0xc: a still one, cut to)
            b.phase = App::Battle::Command;
            b.phaseFrames = 0;
            battleMenu(app, App::Battle::Top);
        }
        break;
    case App::Battle::WinTalk: // btl_main 8000: the last word said, the victory
        battleVictory(app);
        break;
    case App::Battle::BossDown: stepBossDown(app); break;
    case App::Battle::SetPiece: stepSetPiece(app); break;
    case App::Battle::Command: // btl_main 400: group 695's guest (+0xa6b) with its leader down - no orders
        if (b.rules.f[1].guest && !b.rules.allyAuto && !b.rules.f[0].alive() && b.rules.f[1].alive() &&
            !battletalk::on(app) && b.menu == App::Battle::Top)
            battleStartRound(app);
        break;
    case App::Battle::Acting:
        if (b.breakFx.on) { // the battle waits for it; then the Break's tutorial (0xff2)
            if (stepBreakFx(app)) b.breakFx.on = false, battletalk::tutorial(app, 0xff2);
            break;
        }
        if (!b.turnOn) {
            // btl_main after an action's last state (its 9999, set the frame the port's turn ends): 700
            // sees btlact_main's 1 the next frame and goes to 799; 799's checks run the frame after
            // (3.1.0 decomp 7117-7124, 7406-7440) and 600 / 800 / 1000 act on the third - so the next
            // action's camera comes 43 frames after a gene's hit, not 41 (Part 12 756.16 -> 758.36 s:
            // 43 images). The waits below read the frame before (the banner's count runs one behind
            // menub's), so they stand for 799 and their pass for 600's frame
            if (b.handover > 0) {
                if (b.handover == 2 && b.tutorialDue) battletalk::tutorial(app, b.tutorialDue); // (700)
                if (b.handover == 1 && b.nullHitDue) { // (799: 0x2b5's blows don't land - the script's scene,
                    app.game.setFlag(0xfaa, true);      // its story battle's, after Battle: flag 4010)
                    battletalk::open(app);
                }
                if (b.handover == 2) b.tutorialDue = 0;
                if (b.handover == 1) b.nullHitDue = false;
                --b.handover;
                break;
            }
            // btl_main 799: no next action while a message is up or waiting (the round's first comes
            // from 500 / 600 straight)
            if (!b.roundWait && (!b.notices.empty() || (b.bannerFrames > 0 && b.bannerFrames <= btlui::kMessageFrames))) break;
            // btlord_chk in 799: a side lost the Break - its effect (0x5dc: init in 799, the effect's
            // frames from the next), then 600 / 800
            if (b.breakDue) {
                b.breakDue = false;
                startBreakFx(app);
                if (stepBreakFx(app)) b.breakFx.on = false, battletalk::tutorial(app, 0xff2);
                break;
            }
            // the menu goes first (0x270e, then two frames: __menub_mainPrc runs before btl_prc in the
            // original's frame, the port's view after the logic - so the logic sees the view a frame on)
            if (b.roundWait && (b.viewState != 0 || b.menuGone < 1)) break;
            b.roundWait = false;
            const bool pairWas = b.rules.pair, enemyPairWas = b.rules.enemyPair;
            if (b.rules.nextTurn(app.battleActions, b.turn)) {
                // the calc ran ahead of the scene: its chk_dead may have ended pair mode (3.1.0 _btlcalc_chk_dead
                // 27581 clears +0xaf0) - shown at the hit (showHits), as the original's _btlact_attack calls
                // _btlcalc_prc there (12904), after _btlact_attack_init read +0xaf0 for its camera (12271)
                b.pairHeld = true, b.pairAfterHit = b.rules.pair, b.enemyPairAfterHit = b.rules.enemyPair;
                b.rules.pair = pairWas, b.rules.enemyPair = enemyPairWas;
                b.turnOn = true;
                startTurn(app);
            } else if (std::none_of(std::begin(b.popups), std::end(b.popups), [](const App::Battle::Popup& p) { return p.on; })) {
                if (b.rules.outcome() == 2 && btlvoice::winWaits(app) && b.winWait++ < btlvoice::kWinWaitMax) break; // btl_main 799: the win holds for a line still playing
                endRound(app);
            }
            break;
        }
        ++b.turnFrame;
        if (b.stageAction) {
            stepStagedCameras(app);
            stepStaged(app);
        } else if (b.turn.kind == cr::kCmdAttack && b.turn.actors[0] >= 2) { // btlact_attack 0x76c: the bosses' blows
            const App::Battle::Actor* u = battleActor(app, b.turn.actors[0]);
            const int m = u ? u->modelId : -1, f = b.turnFrame;
            if (m == 0xc && f == 22) app.sound.play(0x62);
            if ((m == 0xd || m == 0xe) && f == 7) app.sound.play(0x63);
            if ((m == 0xf || m == 0x10) && f == 30) app.sound.play(0xc7);
            // (13185-13208: m103a / b's frame 0x12 sets effect 0x3f at its node 1204 - eff3d_set_with_pos(.., 50),
            // its own angles - and keeps it on that node while it plays)
            Effect* fx = nullptr;
            for (Effect& e : b.effects)
                if (b.nodeFx && e.handle == b.nodeFx && e.id == 0x3f) fx = &e;
            if (u && (m == 0xf || m == 0x10) && f == 0x12) {
                b.nodeFx = startEffect(app, b.effects, 0x3f, actorPoint(app, *u, 1204), false, 0.5f, 0.0f);
                if (b.nodeFx && !b.effects.empty()) b.effects.back().keepAngles = true;
            } else if (u && (m == 0xf || m == 0x10) && f > 0x12 && fx)
                fx->pos = actorPoint(app, *u, 1204);
        } else if (b.turn.kind == cr::kCmdGene && !b.hitsShown) { // btlact_gene 1000: the bosses' casting sounds
            const App::Battle::Actor* u = battleActor(app, b.turn.actors[0]);
            const int m = u ? u->modelId : -1, f = b.turnFrame;
            if ((m == 0xd || m == 0xe) && f == 18) app.sound.play(0xb8);
            if ((m == 0xf || m == 0x10) && f == 30) app.sound.play(0xd8);
            if ((m == 0x11 || m == 0x12) && f == (b.turn.action == 433 ? 36 : 44)) app.sound.play(0xcf);
            if (m == 0 && f == 28) app.sound.play(0xec); // (j001's, on either side)
        }
        // _btlact_attack 1000: a member's blow drawn variant 2 cuts, as the approach ends, to the target's
        // fourth camera (slot x 4 + 0x7b / 0xe3, the pair's 0x14b)
        if (!b.stageAction && b.turn.kind == cr::kCmdAttack && b.turn.actors[0] >= 0 && b.turn.actors[0] < 2 && b.attackVariant == 2 &&
            b.attackCam2At > 0 && b.turnFrame == b.attackCam2At && !b.turn.hits.empty()) {
            const int slot = enemyCameraSlot(app, b.turn.hits[0].target);
            setBattleCamera(app, slot * 4 + (b.rules.pair ? 0x14b : b.turn.actors[0] == 0 ? 0x7b : 0xe3), 0);
        }
        if (!b.stageAction && b.turn.kind == cr::kCmdAttack) {
            // _btlact_attack 1000 with a standing second (que +0x14) and 0x6a4: SE 0x2d at frame 13 (12886, 13140)
            if (b.turnFrame == 13 && (b.partnerStands || b.shot.effect == 2)) app.sound.play(0x2d);
            // 0x6a4 / 0x708 at the hit - 2 / - 5 (13134-13158 / 13061-13077): effect 2 / 3 on the user (eff3d_set_btl:
            // at its table node), 0x6a4's SE 0x2e; then 0x6ae / 0x712 move it each frame from the user's node 1204
            // / 1210 toward the first target's 1102 - k / 2, k / 5 of the way -, the calc as it gets there
            App::Battle::Shot& sh = b.shot;
            if (sh.effect && b.turnFrame == sh.at) {
                const size_t before = b.effects.size();
                spawnEffect(app, sh.effect, b.turn.actors[0]);
                if (b.effects.size() > before) b.effects.back().handle = sh.handle = ++app.effectHandles;
                if (sh.effect == 2) app.sound.play(0x2e);
            } else if (sh.effect && b.turnFrame > sh.at && ++sh.k <= sh.steps) {
                const App::Battle::Actor* u = battleActor(app, b.turn.actors[0]);
                const App::Battle::Actor* v = sh.target >= 0 ? battleActor(app, sh.target) : nullptr;
                for (Effect& e : b.effects)
                    if (u && v && sh.handle && e.handle == sh.handle) {
                        const cr::Vec3 p = actorPoint(app, *u, sh.from), q = actorPoint(app, *v, 1102);
                        const float w = float(sh.k) / float(sh.steps);
                        e.pos = p + (q - p) * w;
                    }
            }
            // _btlcalc_prc_0404_effect at que +0xb2 / +0xb4 when they aren't the calc's frame (12891-12902, 13230-13235)
            for (int k = 0; k < 2; ++k)
                if (b.attackFxAt[k] > 0 && b.attackFxAt[k] != b.hitFrame && b.turnFrame == b.attackFxAt[k]) attackLook(app);
        }
        if (b.attackBackAt > 0 && b.turnFrame == b.attackBackAt && b.attackBackAt >= b.hitFrame && !b.stageAction) { // (a count that ends before the hit: the clips time it)
            // _btlact_attack 1000 -> 0x406 (3.1.0 decomp 12862-12911) once its count reaches que +8, whatever the
            // clip: the user back over its return slot (12916-12930) - a pair's / an enemy pair's second too
            // (12952 / 12976), unless it stood in place (que +0x14: model 7 / 0xa0..0xa2), which adds five frames
            // instead (12992) - for the user's return slot's max frame; then 1999's ten (13004-13007) and 9999
            // (12818-12828). (+0xaf0 / +0xaf4 as the hit left them)
            const bool members = b.turn.actors[0] < 2, paired = members ? b.rules.pair : b.rules.enemyPair;
            const App::Battle::Actor* v = b.turn.actors[1] >= 0 ? battleActor(app, b.turn.actors[1]) : nullptr;
            const bool standing = v && (members ? v->modelId == 7 : v->modelId >= 0xa0 && v->modelId <= 0xa2);
            for (int k = 0; k < (paired && !standing ? 2 : 1); ++k) // (one that never stepped out keeps its own clip)
                if (App::Battle::Actor* u = b.turn.actors[k] >= 0 ? battleActor(app, b.turn.actors[k]) : nullptr; u && u->then == btlui::kSlotReturn)
                    startAttackReturn(app, *u);
            b.attackEndAt = b.turnFrame + battleMaxFrame(app, b.turn.actors[0], btlui::kSlotReturn) + (paired && standing ? 5 : 0) + 10;
        }
        if (!b.hitsShown && b.turnFrame >= b.hitFrame) {
            if (!b.turn.barriers.empty() && b.bounceAt < 0) {
                // btlact_gene / btlact_item 0x1004: the barriers light up, the users stop; 40 frames
                // on (0x100e) the barrier's side sends it back, and it lands on them
                for (int k : b.turn.barriers) spawnEffect(app, effects::kBuff, k);
                app.sound.play(0x5b);
                for (int k : b.turn.actors)
                    if (App::Battle::Actor* u = k >= 0 ? battleActor(app, k) : nullptr) playSlot(app, *u, btlui::kSlotIdle, btlui::kSlotIdle);
                b.bounceAt = b.turnFrame + 40;
                b.hitFrame = b.bounceAt + 1; // (0x1004 counts 40, swaps the targets; 0x100e the next frame sets the camera
                                             // and calculates - no glow, no wait: Part 8 2677.44 s, the hit at the box + 40.8)
                hitCamera(app, b.turn, b.turn.barriers[0], false); // (state 1000's camera, on the target, before the barrier check)
            } else {
                const bool genelike = (b.turn.kind == cr::kCmdGene || b.turn.kind == cr::kCmdItem) && !b.stageAction && !b.turn.cancelled;
                if (genelike) { // state 1000 (0x100e back on the users after a barrier)
                    const std::vector<int> ts = turnTargets(b.turn);
                    hitCamera(app, b.turn, b.turn.barriers.empty() ? (ts.empty() ? -1 : ts[0]) : b.turn.actors[0], !b.turn.barriers.empty());
                }
                showHits(app);
                if (b.stageAction) stagedHit(app);
            }
        }
        if (b.hitsShown) {
            // _btlact_gene / _btlact_item 2000's first frame (3.1.0 decomp 42350-42363 / 42803-42815): each
            // user (que +0x98, make_que's copy of the users +0x84 - 34895; the item's +0x84 itself), alive
            // (the gene's _IsDead) and not guarding (_btlact_autoguard), gets _mdl_set_act_nextanm(0): its
            // cast slot's loop cleared, next 0 - the clip plays on to its end, then the idle. (A boss's
            // gene - groups 676 / 677's 508 / 510, Part 14 863.7 s - stands at rest after the white.)
            if ((b.turn.kind == cr::kCmdGene || b.turn.kind == cr::kCmdItem) && !b.stageAction && !b.turn.cancelled &&
                b.turnFrame == b.hitFrame + 1)
                for (int k = 0; k < 2; ++k) {
                    const int who = b.turn.actors[k];
                    App::Battle::Actor* u = who >= 0 ? battleActor(app, who) : nullptr;
                    if (!u || u->then >= 0 || u->slot == btlui::kSlotIdle) continue; // (a mover's own way; nextanm(0) on slot 0 returns)
                    if (who < int(b.rules.f.size()) && (b.rules.f[size_t(who)].guarding || (b.turn.kind == cr::kCmdGene && !b.rules.f[size_t(who)].alive())))
                        continue;
                    u->then = btlui::kSlotIdle, u->castRest = true;
                    u->loopFrom = u->loopTo = -1;
                    if (SDL_getenv("CR_BATTLE_DEBUG"))
                        std::printf("cast rest: fighter %d slot %d at frame %.0f of %.0f, then the idle (turn frame %d)\n", who, u->slot, u->time,
                                    actorDuration(app, *u), b.turnFrame);
                }
            bool busy = false;
            for (int k = 0; k < 2; ++k)
                if (const App::Battle::Actor* u = b.turn.actors[k] >= 0 ? battleActor(app, b.turn.actors[k]) : nullptr)
                    busy |= u->then >= 0 && u->slot != btlui::kSlotHit && !u->castRest;
            // (the tail after the action: an attack's ten frames once its actors are back (btlact_attack
            // 1999), a gene's or item's forty after its hit (btlact_gene / item 2000); Analyze's forty
            // after its last line's tap)
            if (!busy && b.turnFrame >= b.hitFrame && b.idleSince < 0) b.idleSince = b.turnFrame;
            const bool attackLike = b.turn.kind == cr::kCmdAttack || b.turn.cancelled;
            const bool over = b.analyze.on ? false
                              : b.analyze.endAt >= 0 ? b.turnFrame >= b.analyze.endAt
                              : b.turn.kind == cr::kCmdEscape ? b.turnFrame >= (b.turn.escaped ? 10 + 40 : 40) // (btlact_escape: 0x3f2 ten frames, then the 40)
                              : b.stageAction && b.stageEnd ? b.turnFrame >= std::max(b.stageEnd, b.hitFrame + 1)
                              : b.stageAction ? b.turnFrame >= b.hitFrame + b.stageWait // the staged ones' wait
                              : b.turn.cancelled ? b.turnFrame >= 60
                              : attackLike && b.attackEndAt >= 0 ? b.turnFrame >= b.attackEndAt // (_btlact_attack's count)
                              : attackLike && b.attackBackAt > 0 && b.attackBackAt >= b.hitFrame && b.turnFrame <= b.attackBackAt ? false // (... still to come)
                              : attackLike ? (b.idleSince >= 0 && b.turnFrame >= b.idleSince + 10) || b.turnFrame > 400
                                           : (!busy && b.turnFrame >= b.hitFrame + 40) || b.turnFrame > 400;
            if (over) {
                b.analyze = {};
                b.stageAction = 0;
                b.stageSkip = 0;
                b.fov = 28.5f;
                b.turnOn = false;
                releasePair(b); // (a turn without a hit shown)
                b.handover = b.turn.kind == cr::kCmdEscape && b.turn.escaped ? 0 : 2; // (an escape's 99 goes to 9000 from 700)
                // btlord_chk: a side lost the Break - its effect first (btl_main 0x5dc), from 799
                b.breakDue = b.rules.breakChanging();
                // btl_main 700 (the next frame, as it sees btlact's 1): the action's tutorials - an element taken,
                // a status, a gene learned; 799 (the frame after): 0x2b5's scene - both from the hand-over below
                b.tutorialDue = b.rules.elementTaken ? 0xff3 : b.rules.statusTaken ? 0xff4 : b.rules.learnedNew ? 0xff5 : 0;
                b.nullHitDue = !b.tutorialDue && b.start.group == 693 && b.rules.nullHit && !app.game.flag(0xfaa) && b.eventBattle;
                // (btlact_attack 1999's end: each attacker's wanted yaw home (0 / 0.5), turned over 3
                // frames - a size-3 monster's 7; the other actions don't turn their actors)
                for (int k = 0; k < 2; ++k)
                    if (App::Battle::Actor* u = b.turn.actors[k] >= 0 ? battleActor(app, b.turn.actors[k]) : nullptr) {
                        u->pos = u->home;
                        u->moveFrom = u->moveTo = 0;
                        u->turnHome = false, u->approached = false;
                        if (u->slot != btlui::kSlotGuard)
                            turnOver(*u, u->homeYaw, b.turn.kind == cr::kCmdAttack && b.turn.actors[k] >= 2 && monsterSize(app, b.turn.actors[k]) == 3 ? 7.0f : 3.0f);
                    }
            }
        }
        break;
    case App::Battle::RoundEnd: // (800 -> 0x352: thirty frames, then the menu slides in)
        if (b.phaseFrames >= 30) {
            b.phase = App::Battle::Command;
            b.phaseFrames = 0;
            battleMenu(app, App::Battle::Top);
        }
        break;
    case App::Battle::Won: // (1000: ten frames of the music's fade; 0x3e9 / 1099: the victory held 140 frames or a tap)
        if (b.phaseFrames == 10) victoryShown(app);
        if (b.phaseFrames < 10) b.wonTap = false;
        if (b.phaseFrames >= 150 || b.wonTap) {
            b.wonTap = false;
            b.phase = App::Battle::Results;
            b.phaseFrames = 0;
            b.result.stage = 0, b.result.frames = 0, b.result.message = 57007;
        }
        break;
    case App::Battle::Results: stepResults(app); break;
    case App::Battle::Lost: // 0x4b0 ten frames (the music fading; an event battle's gameover.mct read), 0x4b1 the
        // defeat's tune and text (60 frames), then 0x513: a tap - after 40 frames of the game over picture,
        // which fades in over 20 - and (9000) btl_lose: a story battle starts over from the title, any
        // other goes back to the pair's Ark (drawFrame does either, between frames)
        if (b.phaseFrames == 1 && b.eventBattle && !b.gameOver.id) {
            cr::Image img;
            if (loadImage(app, app.renderer.phoneLayout() ? "gameover.mct" : "gameover_hd.mct", img) || loadImage(app, "gameover.mct", img))
                b.gameOver = app.renderer.upload(img);
        }
        if (b.phaseFrames == 10) {
            // 0x4b0 -> 0x4b1: the defeat voice only for a battle a script started (+0xaf6, G+0x364 bit 0 - as
            // for a pair); then the camera 0x48 and the monsters still standing out of sight (+0xfb4)
            if (b.eventBattle) btlvoice::playSide(app, 99, 0x1f, 0x20, 0x21);
            setBattleCamera(app, 0x48);
            for (App::Battle::Actor& a : b.actors)
                if (a.fighter >= 2 && b.rules.f[size_t(a.fighter)].alive()) a.hidden = true;
            playBgm(app, 0x1a);
            const App::Battle::Actor* lead = battleActor(app, 0);
            setBanner(app, sysText(app, 57016, lead ? lead->name : std::string()), 60);
        }
        if (b.phaseFrames >= (b.eventBattle ? 50 : 10) && b.lostTap) b.exit = b.eventBattle ? 2 : 3;
        b.lostTap = false;
        break;
    case App::Battle::StoryEnd: // 9000 -> 0x238b: the way out (ending: the black mask, twelve frames) back to the field, an event battle's script on
        b.exit = 1;
        break;
    case App::Battle::Fled:
        if (b.phaseFrames >= 30) b.exit = 1;
        break;
    default: break;
    }
    // btl_main 9000 -> 0x238b, whichever way the battle ends: SetMask(0) - black over 10 frames -,
    // the music out over 10 (unless var 60 is 997), twelve frames, then the field (or the title)
    if (b.exit && !b.ending) {
        b.pendingExit = b.exit, b.exit = 0, b.ending = true, b.endFrames = 0;
        app.fadeGrey = 0, app.fadeFrom = 0, app.fadeTo = 1, app.fadeFrames = 10, app.fadeT = 0;
        const auto v60 = app.game.vars.find(60);
        if (v60 == app.game.vars.end() || v60->second != 997) playBgm(app, 1u << 26);
    } else if (b.ending && ++b.endFrames >= 12) b.exit = b.pendingExit, b.ending = false;
    // the pages after the frame's logic: a page set this frame starts at its distance with its camera's
    // cut (Part 2 1248.62-1248.82 s: the cut, the plates partly in 3 game frames after it, settled at 4 -
    // as CR1's video 02 1634.70-1634.90 s)
    stepMenuView(app);
    turnActors(app); // (btl_prc's mdl_put)
    stepBattleCamera(app); // (btl_main's end: btlcamera_prc; btl_prc: the eye, the easing)
    stepQuake(app); // gameapp_prc: UpdateQuake and PutMask after the battle's frame
    stepFlash(app);
    if (SDL_getenv("CR_BATTLE_CAMLOG")) // debugging: the shown camera each battle frame
        std::printf("camlog %d phase %d/%d cam %#x actor %d count %d/%d ease %.0f fade %d/%d eye %.3f %.3f %.3f target %.3f %.3f %.3f\n", b.frames,
                    int(b.phase), b.phaseFrames, b.cam, b.camActor, b.camFrame, b.camDur, b.camEase, app.fadeT, app.fadeFrames, b.camEye.x,
                    b.camEye.y, b.camEye.z, b.camTarget.x, b.camTarget.y, b.camTarget.z);
}

// btl_prc (8040-8042): _mceCamera_setPerspectiveFov_op(cam, aspect, fov / 360, 1.0, 3500.0)
constexpr float kBattleNear = 1.0f, kBattleFar = 3500.0f;

// _btl_shadow (5929-5969, after mdl_put): a 32-triangle black ellipse fan under each fighter, into
// tex2d slot 0x5e (untextured, depth tested, not written, draw order 0x3de - after the models'): centred
// on the x / z of the model's node 1102 at its y + 0.1, radii its row's +0x42 (x) and +0x44 (z) turned
// by its shown yaw (+0xfac). The pair's (FUN_0001628c) at 80% of 255 from their rows (record + 4), the
// monsters' (FUN_00016008) at 80% of the model's colour alpha (+0xfb7: the death fade), none when
// +0x42 is 0; none while a model is hidden (+0xfb4 = 0)
void addBattleShadow(App& app, const App::Battle::Actor& a) {
    App::Battle& b = app.battle;
    if (a.fighter < 0 || a.fighter > 4 || a.hidden) return;
    const cr::Fighter& f = b.rules.f[size_t(a.fighter)];
    const cr::MonsterDef* row = a.fighter < 2 ? app.battleData.monster(f.character + 5) : f.def; // (1-based: row c + 4)
    if (!row) return;
    const int rx = int16_t(row->raw[0x42] | row->raw[0x43] << 8), rz = int16_t(row->raw[0x44] | row->raw[0x45] << 8);
    if (a.fighter >= 2 && rx == 0) return;
    App::Battle::Shadow sh;
    sh.fighter = a.fighter;
    // (80 x alpha / 100 in whole numbers: the pair's 204, a monster's from its colour's alpha byte)
    sh.alpha = float(a.fighter < 2 ? 80 * 255 / 100 : 80 * int(std::lround(std::clamp(a.alpha, 0.0f, 1.0f) * 255.0f)) / 100) / 255.0f;
    const cr::Vec3 c = actorPoint(app, a, 1102); // (btlcamera_getpos2: node 1102)
    sh.at = {c.x, a.pos.y + 0.1f, c.z};
    sh.rx = float(rx), sh.rz = float(rz);
    addShadowFan(b.shadowTris, b.shadowAlpha, sh.at, a.yaw, sh.rx, sh.rz, sh.alpha); // (the field's fan)
    b.shadows.push_back(sh);
}

void drawBattleShadows(App& app, const cr::Mat4& vp) {
    drawShadowFans(app, app.battleShadows, app.battle.shadowTris, app.battle.shadowAlpha, vp);
}

// read_mdl_hd (3.1.0 decomp 2958-2971, the stage read as model 0): its place zeroed, then t0151bt's z
// 100 less and t0155bt's 10 less - mdl_put draws model 0 there. t0155bt, the ship's deck: the turret's
// platform 10 back, so a blow at the turret (the pair's spot z 494, the lunge's hips 8 on) lands on the
// deck in front of its wall, not in it (#6; the Vita and Android releases at the hit)
cr::Mat4 battleStageWorld(int stage) {
    cr::Mat4 m;
    if (stage < 0 || stage >= cr::kNumBattleStages) return m;
    const std::string file = cr::kBattleStageFile[stage];
    if (file == "t0151bt.bin") m.m[14] = -100.0f;
    else if (file == "t0155bt.bin") m.m[14] = -10.0f;
    return m;
}

// btl_prc: the whole screen through the formation's camera (btl_main: camera formation - 2;
// perspective 0.0792 turns = 28.5 degrees vertical, 4:3)
void drawBattle(App& app, float dt) {
    App::Battle& b = app.battle;
    auto& r = app.renderer;
    const float W = r.screenW(), H = cr::Renderer::kLogicalH; // 3D across the whole screen
    auto record = [&](int side) { // (the step's start or end: Battle::Between)
        b.between.actors[side] = b.actors;
        battleCameraView(app, b.between.eye[side], b.between.target[side], &b.between.roll[side]);
        b.between.cam[side] = b.cam;
        b.between.camSets[side] = b.camSets;
        if (side == 1) b.between.camCut = b.camSnapped && b.between.camSets[0] != b.camSets;
        b.between.valid = side == 1;
    };
    for (b.clock += dt; b.clock >= 1.0f / kFramesPerSecond && b.active; b.clock -= 1.0f / kFramesPerSecond) {
        record(0);
        stepBattle(app);
        record(1);
    }
    b.between.alpha = std::clamp(b.clock * kFramesPerSecond, 0.0f, 1.0f);
    if (!b.active) return;
    const App::Battle::Between& bt = b.between;
    const float bk = bt.valid ? bt.alpha : 1.0f;
    r.fillRect(0, 0, W, H, 0, 0, 0, 1);
    drawSpaceBackdrop(app, true); // ScnEffectProc runs in battles too: the Space backdrop at the back
    r.setOrigin(0);
    cr::Vec3 eye{488, 12, 555}, target{500, 4, 498};
    float roll = 0;
    battleCameraView(app, eye, target, &roll);
    // (between steps: where the camera is by then, unless the step cut to another)
    const bool camBetween = bt.valid && !bt.camCut && bt.camSets[1] == b.camSets && same(eye, bt.eye[1]) && same(target, bt.target[1]);
    if (camBetween) {
        eye = lerp(bt.eye[0], bt.eye[1], bk);
        target = lerp(bt.target[0], bt.target[1], bk);
        roll = bt.roll[0] + (bt.roll[1] - bt.roll[0]) * bk;
    }
    if (SDL_getenv("CR_BETWEEN_DEBUG"))
        std::printf("battle frame: valid %d alpha %.2f camera %s (cam %d %d now %d)\n", int(bt.valid), bt.alpha, camBetween ? "between" : "as is",
                    bt.cam[0], bt.cam[1], b.cam);
    cr::Mat4 proj = cr::Mat4::perspective(b.fov * 3.14159265f / 180.0f, W / H, kBattleNear, kBattleFar);
    // btl_prc: the quake shifts the frustum by its offset in the 1024 x 768 screen's pixels
    proj.m[8] -= 2.0f * b.quake.off[0] / W;
    proj.m[9] -= 2.0f * b.quake.off[1] / H;
    // (the battle's gluLookAt under flag 0x100: the camera's frame turned raw[6] degrees about its own
    // z - the world turned the other way in the view)
    cr::Mat4 view = cr::Mat4::lookAt(eye, target, {0, 1, 0});
    if (roll != 0) {
        const float a = -roll * 3.14159265f / 180.0f;
        cr::Mat4 rz;
        rz.m[0] = std::cos(a), rz.m[1] = std::sin(a), rz.m[4] = -std::sin(a), rz.m[5] = std::cos(a);
        view = rz * view;
    }
    const cr::Mat4 vp = proj * view;
    b.vp = vp;
    b.proj = proj;
    r.setCamera(kBattleNear, kBattleFar, app.battle.fov * 3.14159265f / 180.0f);
    if (r.vitaScreen()) r.begin3D(); // (CRView: the Vita's whole screen)
    else r.begin3DRect(0, 0, W, H);
    // _mdl_put (11800): the stage (model 0) drawn at once - but stage 0xf's (d0555bt) goes into the
    // draw-order queue with the fighters: its meshes of order -300..-100 before them, 100..300 after
    // (the shadows' 0x3de after those)
    const bool stageQueued = b.start.stage == 0xf;
    const cr::Mat4 stageVp = vp * battleStageWorld(b.start.stage);
    drawLoaded(app, b.stage, stageVp, nullptr, 1.0f, -1.0f, stageQueued ? 1 : 0);
    b.shadows.clear(), b.shadowTris.clear(), b.shadowAlpha.clear();
    for (size_t ai = 0; ai < b.actors.size(); ++ai) {
        // the actor as a frame between steps shows it: moved, turned, faded and its animation
        // played on by then (an animation that looped, changed or jumped is shown as it is)
        App::Battle::Actor a = b.actors[ai];
        if (bt.valid && ai < bt.actors[0].size() && ai < bt.actors[1].size()) {
            const App::Battle::Actor& p0 = bt.actors[0][ai], &p1 = bt.actors[1][ai];
            if (p0.model == a.model && p1.model == a.model && same(a.pos, p1.pos) && a.yaw == p1.yaw && a.time == p1.time &&
                a.action == p1.action) {
                const cr::Vec3 d = p1.pos - p0.pos;
                if (cr::dot(d, d) < 40.0f * 40.0f) a.pos = lerp(p0.pos, p1.pos, bk), a.yaw = lerpAngle(p0.yaw, p1.yaw, bk);
                if (a.alpha == p1.alpha) a.alpha = p0.alpha + (p1.alpha - p0.alpha) * bk;
                if (p0.action == p1.action && p1.time >= p0.time && p1.time - p0.time <= 1.0f) a.time = p0.time + (p1.time - p0.time) * bk;
            }
        }
        if (a.alpha <= 0.001f || a.hidden) continue;
        LoadedModel& lm = b.models[a.model];
        const cr::Model& m = lm.cpu;
        if (!m.skeleton.nodes.empty() && lm.meshes.size() == m.meshes.size()) { // skin this instance
            const cr::Action* act = a.action >= 0 && a.action < int(lm.actions.size()) ? &lm.actions[size_t(a.action)] : nullptr;
            poseWorld(m.skeleton, lm.actions, act, a.time, a.blend, lm.world);
            for (size_t i = 0; i < m.meshes.size(); ++i) {
                cr::skinMesh(m, m.meshes[i], lm.world, lm.skinned);
                const bool spins = spinsTexture(m, i);
                if (spins) lm.uvScratch = m.meshes[i].uvs, spinUvs(lm.uvScratch, app.texSpin);
                r.updatePositions(lm.meshes[i], lm.skinned, spins ? lm.uvScratch : m.meshes[i].uvs, m.meshes[i].colors);
            }
        }
        modelDrawn(app);
        cr::Mat4 world;
        const float c = std::cos(a.yaw), sn = std::sin(a.yaw);
        world.m[0] = c * a.scale; world.m[2] = -sn * a.scale; world.m[5] = a.scaleY;
        world.m[8] = sn * a.scale; world.m[10] = c * a.scale;
        world.m[12] = a.pos.x; world.m[13] = a.pos.y; world.m[14] = a.pos.z;
        drawLoaded(app, lm, vp * world, nullptr, a.alpha);
        addBattleShadow(app, a);
    }
    if (stageQueued) drawLoaded(app, b.stage, stageVp, nullptr, 1.0f, -1.0f, 2);
    drawBattleShadows(app, vp); // (_btl_shadow after mdl_put: draw order 0x3de)
    drawEffects(app, vp, eye, target);
    r.end3D();
    drawSceneEffects(app, true); // the field's weather goes on over the battle
    r.setOrigin(0);
    if (b.flash.on && b.flash.alpha > 0) r.fillRect(0, 0, W, H, 1, 1, 1, b.flash.alpha / 255.0f); // PutMask
}

// ---- The HUD (menub_mainPut, __dmg_prc, MbResultPut) ----

// __dmg_prc's placing of slot `slot` (x, y in the 480 x 320 layout's units, projected): the clamps,
// then the slots' shifts; +0x9a0 / +0x9a4 keep where it went (slot 2's 30 up is the drawing's only)
void placePopup(App::Battle& b, int slot, float& x, float& y, int shown) {
    App::Battle::Popup& p = b.popups[slot];
    y = std::clamp(y, 32.0f, 272.0f);
    x = std::max(x, 64.0f);
    if (slot == 0 && p.target == 0 && shown > 1) y += 48;
    if (slot == 3 && b.popups[0].on) y += p.amount < 0 ? 28 : 48;
    int right = 460;
    for (int v = std::abs(p.amount); v > 9; v /= 10) right -= 20;
    x = std::min(x, float(right));
    p.x = x, p.y = y;
    if (slot == 2 && p.amount < 0 && y >= b.popups[0].y - 20 && y <= b.popups[0].y + 20) y -= 30;
}

// cmd_number: `digits` places 10 apart, right-aligned; the HP's red under a quarter, the
// level's orange style (width 3)
void drawBattleNumber(cr::MenuPainter& m, int value, float x, float y, int digits, bool low, float a, bool level = false) {
    const std::string t = std::to_string(std::max(0, value));
    const int n = int(t.size());
    for (int k = 0; k < n; ++k) {
        const int d = t[size_t(k)] - '0';
        const float u = level ? 588 + 14.0f * d : 14.0f * d + (low ? 140 : 0);
        m.piece(x + 10.0f * float(digits - n + k), y, u, 0, 14, 14, a);
    }
}

// cmd_bottom: a battle button (`x` and `y` its corner): base, face (lit when selected), icon, word
void drawBattleButton(cr::MenuPainter& m, float x, float y, int icon, int word, bool selected, bool dimmed, float a) {
    const float c = dimmed ? 0.25f : 1.0f;
    m.piece(x, y, 0, 184, 24, 40, a, c, c, c);
    m.pieceTo(x + 24, y, 84, 40, 28, 184, 4, 40, a, c, c, c);
    m.piece(x + 108, y, 28, 184, 24, 40, a, c, c, c);
    const float face = selected && !dimmed ? 96 : 52;
    m.piece(x + 28, y, face, 184, 24, 40, a, c, c, c);
    m.pieceTo(x + 52, y, 68, 40, face + 24, 184, 4, 40, a, c, c, c);
    m.piece(x + 120, y, face + 28, 184, 16, 40, a, c, c, c);
    m.piece(x + 8, y + 4, float(icon % 16 * 32), float(icon / 16 * 32 + 376), 32, 32, a, c, c, c);
    m.piece(x + 40, y + 8, float(word % 5 * 88), float(word / 5 * 24 + 512), 88, 24, a, c, c, c);
}

// The red -> orange HP bar of the plates and rows (_cmd_hp_bar / _cmd_mp_bar: tex2d_add4 on slot 45),
// `w` long when full: a parallelogram 3 high leaning right by 3 - the top (x + 3, y)..(x + 3 + fill),
// the bottom (x, y + 3)..(x + fill) - fill = w x value / most (at least 1, none at 0), its right end
// coloured that far to the full colour; under it the black one (value 1 of 1) 1 right and 1 down
void drawBattleHpBar(cr::MenuPainter& m, float x, float y, float w, int value, int most, bool mp, float a) {
    const float black[4] = {0, 0, 0, a};
    m.slant(x + 1, y + 1, w, 3, 3, black, black);
    if (value <= 0 || most <= 0) return;
    const float f = std::clamp(float(value) / float(most), 0.0f, 1.0f);
    const float fill = float(std::max(1, int(w) * std::min(value, most) / most));
    const float left[4] = {mp ? 0x07 / 255.f : 0xdd / 255.f, mp ? 0xaf / 255.f : 0x02 / 255.f, mp ? 0x73 / 255.f : 0, a};
    const float right[4] = {left[0] + (1 - left[0]) * f, left[1] + (0xc3 / 255.f - left[1]) * f,
                            left[2] + (0x73 / 255.f - left[2]) * f, a};
    m.slant(x, y, fill, 3, 3, left, right);
}

// menub_param_put: the element, then each status on, 20 apart
void drawBattleStatus(cr::MenuPainter& m, const cr::Fighter& f, float x, float y, float a) {
    m.piece(x, y, float(f.element * 20), 924, 20, 20, a);
    float at = x + 20;
    for (int k = 0; k < cr::kStatusCount; ++k)
        if (f.status[k] > 0 && k != cr::kStatusUnused92) {
            // menub_param_put: the sheet's row at 924, 20 a status - Omega's own Down (+0x96) at u 120 and Ward (+0x97) at 100
            const float u = k == cr::kStatusDown ? 120.0f : k == cr::kStatusWard ? 100.0f : float(140 + 20 * k);
            m.piece(at, y, u, 924, 20, 20, a);
            at += 20;
        }
}

// cmd_bottom_teki: a target's plate at (x, y): its level, name and HP bar
void drawBattlePlate(App& app, cr::MenuPainter& m, float x, float y, int fighter, bool selected, float a) {
    const App::Battle& b = app.battle;
    const cr::Fighter& f = b.rules.f[size_t(fighter)];
    m.piece(x, y, 0, 184, 24, 40, a);
    m.pieceTo(x + 24, y, 200, 40, 28, 184, 4, 40, a);
    m.piece(x + 224, y, 28, 184, 24, 40, a);
    const float face = selected ? 96 : 52;
    m.piece(x + 36, y, face, 184, 24, 40, a);
    m.pieceTo(x + 60, y, 180, 40, face + 24, 184, 4, 40, a);
    m.piece(x + 240, y, face + 28, 184, 16, 40, a);
    m.piece(x + 6, y + 25, 116, 14, 38, 10, a); // "Lv"
    drawBattleNumber(m, f.level, x + 6, y + 12, 3, false, a, true);
    drawBattleHpBar(m, x + 96, y + 25, 96, b.shownHp[fighter], f.hpMax, false, a); // (_cmd_bottom_teki: 0x60 wide)
    std::string name;
    for (const App::Battle::Actor& ac : b.actors)
        if (ac.fighter == fighter) name = ac.name;
    // menub_mainPut: _drawfont_center(i, x + 0x92, y + 10) - the 16-pixel name btlord_make_select set, white
    m.text(name, x + 146 - m.textWidth(name, 16) / 2, y + 10, false, a, 16);
    drawBattleStatus(m, f, x + 56, y - 8, a);
}

// A member's row: portrait, HP and MP numbers and bars, statuses (actor 1 at the bottom, actor
// 0 40 to the right and 24 up)
void drawBattleRow(App& app, cr::MenuPainter& m, int k, bool dimmed, float a) {
    const App::Battle& b = app.battle;
    const cr::Fighter& f = b.rules.f[size_t(k)];
    if (!f.present) return;
    const bool phone = app.renderer.phoneLayout(); // 2.2.0: (+16, -32), into the iPhone's corner
    const float dx = (k == 0 ? 40 : 0) + (phone ? 16 : 0), dy = (k == 0 ? -24 : 0) + (phone ? -32 : 0);
    const float c = dimmed ? 0.25f : 1.0f;
    const int face = f.character == 0 && viegIsOlgar(app) ? 4 : f.character; // __menub_mainPut: record 0 as Olgar is face 4
    m.piece((k == 0 ? 52 : 0) + (phone ? 16 : 0), 328 + dy, float(face * 48), 440, 48, 48, a, c, c, c);
    m.piece(2 + dx, 370 + dy, 0, 14, 22, 10, a); // "HP"
    m.piece(86 + dx, 370 + dy, 22, 14, 22, 10, a); // "MP"
    drawBattleNumber(m, b.rollHp[k], 24 + dx, 364 + dy, 5, b.rollHp[k] <= f.hpMax / 4, a);
    drawBattleNumber(m, b.rollMp[k], 108 + dx, 364 + dy, 4, b.rollMp[k] <= 0, a); // (cmd_number(mp, mp, 4): red at 0)
    drawBattleHpBar(m, 20 + dx, 376 + dy, 59, b.rollHp[k], f.hpMax, false, a);
    drawBattleHpBar(m, 100 + dx, 376 + dy, 49, b.rollMp[k], f.mpMax, true, a);
    drawBattleStatus(m, f, 152 + dx, 364 + dy, a);
}

// The results (__menuu_MbResultSet / Prc / Put). Prc 3: for (most levels + 1) x 60 frames the
// boards' EXP climbs (each member's gain over its own (levels + 1) x 60, the chime 0xa2 at a level)
// and HP fills over 60 frames, EXP Gained, OZ Earned and the purse count up a sixtieth a frame,
// a tick (0xa4) every second frame while anything came; a tap (0x93) puts everything at its end.
// Then a tap at a time: each member's level-up line (0xa2: 57008, or 57034 for several), each
// item's (0xa0: 57010, or 57035 with how many), and 0x93 as the windows go (4 frames)
void resultsNext(App& app) {
    App::Battle::Result& rs = app.battle.result;
    rs.frames = 0;
    if (rs.upNext < rs.ups.size()) {
        const auto [k, n] = rs.ups[rs.upNext++];
        menuSound(app, se::kMenuBase + 0x11);
        rs.stage = 1, rs.message = n == 1 ? 57008 : 57034, rs.value = n;
        const App::Battle::Actor* u = battleActor(app, k);
        rs.name = u ? u->name : std::string();
        rs.level[k] = rs.levelTo[k], rs.exp[k] = rs.expTo[k]; // (pcparam_change_lv)
        return;
    }
    if (rs.itemNext < rs.items.size()) {
        const auto [item, n] = rs.items[rs.itemNext++];
        menuSound(app, se::kMenuBase + 0xf);
        rs.stage = 2, rs.message = n == 1 ? 57010 : 57035, rs.value = n, rs.name = itemName(app, item);
        return;
    }
    menuSound(app, se::kChoose);
    rs.stage = 3, rs.count = 4;
}

void resultsSettle(App::Battle::Result& rs) { // __RestoreParam
    for (int k = 0; k < 2; ++k) rs.level[k] = rs.levelTo[k], rs.exp[k] = rs.expTo[k];
    rs.shownExp = float(rs.expTotal), rs.shownOz = float(rs.ozTotal), rs.shownPurse = float(rs.purseTo);
    rs.count = 0;
}

void resultsTap(App& app) {
    App::Battle::Result& rs = app.battle.result;
    if (rs.frames < 4 || rs.stage == 3) return; // (the windows coming in, or going)
    if (rs.stage == 0 && rs.count > 0) {
        menuSound(app, se::kChoose);
        resultsSettle(rs);
        return;
    }
    resultsNext(app);
}

void stepResults(App& app) {
    App::Battle& b = app.battle;
    App::Battle::Result& rs = b.result;
    ++rs.frames;
    if (rs.stage == 3) {
        if (--rs.count < 0) b.exit = 4;
        return;
    }
    if (rs.stage != 0 || rs.frames < 4 || rs.count <= 0) return;
    for (int k = 0; k < 2; ++k) {
        if (rs.character[k] < 0 || rs.exp[k] >= rs.expTo[k]) continue;
        rs.exp[k] = std::min(rs.expTo[k], rs.exp[k] + rs.expStep[k]);
        if (rs.level[k] < rs.levelTo[k] && rs.exp[k] >= float(cr::expForLevel(rs.level[k] + 1)))
            ++rs.level[k], menuSound(app, se::kMenuBase + 0x11);
    }
    auto climb = [](float& v, float to, float step) { if (v < to) v = std::min(to, v + step); };
    climb(rs.shownExp, float(rs.expTotal), float(rs.expTotal / 60)); // (MbResultPrc's own counters: never drawn)
    climb(rs.shownOz, float(rs.ozTotal), float(rs.ozTotal / 60));
    // the purse climbs by the earned OZ / 60 only while it is below the OZ earned (MbResultPrc compares
    // DAT_ec528 with DAT_ec49c, not the new purse): a fuller purse holds until __RestoreParam sets it
    // (Part 3 979.6-980.7 s: 6967 held, 10287 at the tap)
    if (rs.shownPurse < float(rs.ozTotal)) rs.shownPurse = std::min(9999999.0f, rs.shownPurse + float(rs.ozTotal / 60));
    --rs.count;
    if (rs.count % 3 == 0 && (rs.expTotal > 0 || rs.ozTotal > 0)) app.sound.play(se::kMenuBase + 0x13); // (0xa4 every third)
    if (rs.count < 1) resultsSettle(rs);
}

// MbResultPut: "Results", the pair's boards (ComUty_FaceBoardDraw, 88 apart), EXP Gained and OZ
// Earned (ComUty_ExpAndOzDraw at (288, 76)), the play time and the purse, the message board at
// (0, 220) with its one line; windows 1 / 2 come from the left / right, 3 / 4 up from below
void drawBattleResults(App& app, cr::MenuPainter& m, float a) {
    App::Battle& b = app.battle;
    const App::Battle::Result& rs = b.result;
    const PhoneMenu phoneMenu(app, m); // MbResultPut: the iPhone screen's menu coordinates
    cr::Renderer& r = app.renderer;
    const float in = rs.stage == 3 ? std::max(0.0f, float(rs.count) / 4) : std::min(1.0f, float(rs.frames) / 4);
    const float away = 1 - in;
    r.setOrigin(wideLeft(app)); // (widescreen: the heading and boards left, the takings right)
    m.picture(19, 8, 4, a * in); // "Results"
    for (int k = 0; k < 2; ++k) {
        const cr::Fighter& f = b.rules.f[size_t(k)];
        if (!f.present) continue;
        const float y = 24 + 88.0f * float(k);
        // the board as it counts: its level and EXP, HP filling over 60 frames
        cr::PartyMember& pm = app.game.party[size_t(f.character)];
        const int keepLevel = pm.level, keepExp = pm.exp;
        if (rs.character[k] >= 0) pm.level = rs.level[k], pm.exp = int(rs.exp[k]);
        const float fill = rs.stage == 0 && rs.count > 0 ? std::min(1.0f, float(rs.frames) / 60) : 1;
        const int hp = rs.character[k] >= 0 ? rs.hp0[k] + int(float(f.hpMax - rs.hp0[k]) * fill) : f.hp;
        drawFaceBoard(app, m, -320 * away, y, f.character, hp, f.hpMax, f.mp, f.mpMax, a);
        pm.level = keepLevel, pm.exp = keepExp;
    }
    r.setOrigin(wideRight(app));
    const float rx = 320 * away;
    m.picture(67, 304 + rx, 48, a);
    m.picture(68, 288 + rx, 76, a); // EXP Gained
    m.picture(70, 340 + rx, 96, a);
    drawShopNumber(m, rs.expTotal, 358 + rx, 98, 6, 12, a); // (MbResultPut: the totals, DAT_ec498 / 49c - not the counters)
    m.picture(69, 288 + rx, 132, a); // OZ Earned
    m.picture(70, 340 + rx, 152, a);
    drawShopNumber(m, rs.ozTotal, 358 + rx, 154, 6, 12, a);
    const int keepOz = app.game.oz;
    app.game.oz = int(rs.shownPurse); // (ComUty_BattryAndRealTimeAndOzDraw: the purse as it counts)
    drawClockAndOz(app, m, a * in);
    app.game.oz = keepOz;
    r.setOrigin(r.uiX());
    const float by = 160 * away;
    // __menuu_MbResultPut -> ComUty_MsgBoardDraw(.., 3, .., 0, 0xdc): picture 12, the mark (picture 13 out and
    // back) at (458, y + 78), the lines only while the window stands still (Part 3 979.59-979.70 s: the board
    // comes up empty; the line and the mark together)
    m.picture(12, 0, 220 + by, a);
    const short* mk = cr::kMenuPicture[13];
    m.piece(458, 298 + by, float(mk[0] + 20 * pictAnimFrame(rs.frames, 1, 4)), mk[1], mk[2], mk[3], a);
    std::vector<std::vector<std::string>> pages;
    const int value = rs.value;
    if (away <= 0.0f &&
        app.systemText.message(rs.message, textLanguage(app), [&](int) { return value; }, pages, nullptr,
                               [&](int) { return rs.name; }) &&
        !pages.empty())
        for (size_t i = 0; i < pages[0].size() && i < 4; ++i) m.text(pages[0][i], 24, 234 + by + 16.0f * float(i), false, a, 14);
    r.setOrigin(wideLeft(app));
    if (keyHints(app)) // (our key hints)
        m.text("Enter: continue", 20, 372, false, a * 0.8f * in, 9);
    r.setOrigin(r.uiX());
}

void drawBattleHud(App& app, float W, float H) {
    (void)W;
    (void)H;
    using namespace btlui;
    App::Battle& b = app.battle;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    // a talk: the portraits and the message box instead - the left HUD going out (+0x3c) first
    const bool talk = battletalk::on(app);
    if (talk && b.hudSlide >= 480) return;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    if (b.phase == App::Battle::Results && !talk) {
        drawBattleResults(app, m, std::min(1.0f, float(b.phaseFrames + 1) / 8.0f));
        r.setOrigin(r.uiX());
        if (b.hudSlide >= 480) return; // (the badge and the rows still on their way out)
    }
    if (b.phase == App::Battle::Lost && b.eventBattle && b.gameOver.id) { // the game over picture: tex2d (112, 32), 20 frames in
        r.setOrigin(r.uiX());
        const float ga = std::min(1.0f, float(b.phaseFrames) / 20.0f);
        const float gw = float(b.gameOver.width), gh = float(b.gameOver.height);
        r.drawQuadUV(b.gameOver, 2.0f * 112 + 32, 2.0f * 32 + 64, gw, gh, 0, 0, gw, gh, 1, 1, 1, ga);
        return;
    }
    const float a = std::min(1.0f, float(b.frames) / 10.0f);
    // The iPhone layout (2.2.0's __menub_mainPut): the badge and the rows into its screen's
    // corners - (+16, +32) / (+16, -32) - the banner 32 lower, the menus where they are. Android
    // 1.1.6's _menub_mainPut puts them on the phone's edges: the badge (- ex, - ey) (top left),
    // the rows (- ex, + ey) (bottom left), the banner - ey (top); the menus stay centred
    const bool phone = r.phoneLayout();
    const float edge = wideLeft(app); // the left-hand pieces' origin (the screen's edge inside the safe area)
    // (the Vita's __menub_mainPut: the badge and the rows at (int)(x - 43.352966), the command plates, targets and
    // Back at (int)(x + 43.352966) - vita decomp 62034 / 62163 / 62263: 43 whole units)
    const float ex = androidEdgeX(app) + vitaEdge43(app), ey = androidEdgeY(app);

    // Damage numbers (__dmg_prc): over their targets, projected from the 3D across the screen, put in
    // the 480 x 320 layout's units (tex2d's: the 1024 layout's (32, 64) + 2x): y 32..272, x 64 and over
    // and at most 460 less 20 a digit past the first; slot 0 48 lower while two or more show (its
    // target member 0), slot 3 28 (a heal) / 48 lower while slot 0 shows, slot 2's heal within 20 of
    // slot 0's put 30 higher. Drawn opaque (tex2d_add's own colour, not the HUD's fade)
    r.setOrigin(0);
    const int shown = int(std::count_if(std::begin(b.popups), std::end(b.popups), [](const App::Battle::Popup& p) { return p.on; }));
    r.setPictureLayer(true); // (over their targets in the 3D: the picture's place, unshrunk by a camera cutout - interfaceRect; CR1-U 9b36e2c)
    for (int slot = 0; slot < 5; ++slot) {
        App::Battle::Popup& p = b.popups[slot];
        if (talk || b.phase == App::Battle::Results) break;
        if (!p.on) continue;
        const float* v = b.vp.m;
        const float cx = v[0] * p.at.x + v[4] * p.at.y + v[8] * p.at.z + v[12];
        const float cy = v[1] * p.at.x + v[5] * p.at.y + v[9] * p.at.z + v[13];
        float cw = v[3] * p.at.x + v[7] * p.at.y + v[11] * p.at.z + v[15];
        if (std::fabs(cw) < 0.01f) cw = 0.01f;
        const std::string t = std::to_string(std::abs(p.amount));
        float px = (cx / cw * 0.5f + 0.5f) * r.screenW(), py = (1 - (cy / cw * 0.5f + 0.5f)) * cr::Renderer::kLogicalH;
        r.screenToLayout(px, py); // (the iPhone layout draws 2D scaled; the projection is the screen's)
        float tx = (px - r.uiX() - 32) / 2, ty = (py - 64) / 2;
        placePopup(b, slot, tx, ty, shown);
        const float x = tx + 16 + r.uiX() / 2, y = ty + 32, a = 1;
        const int step = (kPopupTime - p.timer) >> 4;
        if (p.type >= 1 && p.type <= 7) { // the label: Blowout! Critical! Weak! / No Effect! Guard! Miss! Block!
            float lx = x - 72, ly = y - 24, la = 1;
            const int idx = p.type < 4 ? p.type - 1 : p.type; // (__dmg_prc: idx 4 No Effect, 5 Guard, 6 Miss, 7 Block)
            if (p.type < 4) { // __dmg_prc: Blowout / Critical / Weak slide in from the right and out to the left (x += 18k in, -= 18k out)
                if (p.timer >= 320) { const int k = 4 - (step); lx += 18.0f * float(std::max(0, k)); la = 1 - 0.25f * float(std::max(0, k)); }
                else if (p.timer < 64) { const int k = 4 - (p.timer >> 4); lx -= 18.0f * float(k); la = 1 - 0.25f * float(k); }
            } else if (step < 8)
                ly -= float(kLabelHop[step]);
            m.piece(lx, ly, float(idx % 4 * 144), float(idx / 4 * 24 + 72), 144, 24, a * la);
        }
        const bool number = p.type == cr::kHitNormal || p.type == cr::kHitCritical || p.type == cr::kHitWeak ||
                            p.type == cr::kHitResist;
        if (!number) continue;
        const int style = p.amount < 0 ? 3 : p.type == cr::kHitResist ? 2 : p.type == cr::kHitWeak ? 1 : 0;
        float dx = x - 10.0f * float(t.size());
        const float dy = y - (p.amount < 0 ? 24 : 40);
        for (size_t i = 0; i < t.size(); ++i, dx += 19) {
            const int s = step - int(i);
            const float hop = s >= 0 && s < 8 ? float(kDigitHop[s]) : 0;
            m.piece(dx, dy - hop, float((t[i] - '0') * 24 + style % 2 * 240), float(style / 2 * 24 + 24), 24, 24, a);
        }
    }
    r.setPictureLayer(false); // (the HUD from here on: the interface's)

    // Top left: the Break badge - the ring, a piece per point, the holder's name
    const int g = b.gaugeShown;
    const float hud = -float(b.hudSlide); // (menub_mainPut: the badge and the rows at x - +0x3c)
    r.setOrigin(edge);
    m.setOffset((phone ? 16.0f : 0.0f) + hud - ex, (phone ? 32.0f : 0.0f) - ey);
    m.piece(0, 0, 0, 120, 64, 64, a);
    for (int i = 0; i < std::min(8, std::abs(g)); ++i) {
        const short* t = kBreakTick[i];
        if (g > 0) m.piece(t[0], t[1], t[2], t[3], 32, 32, a, 0x38 / 255.f, 1, 0xe3 / 255.f);
        else m.piece(t[0], t[1], t[2], t[3], 32, 32, a, 1, 0x14 / 255.f, 0x23 / 255.f);
    }
    if (g != 0 && b.breakPop > 1.0f) { // the label pops in large: tex2d_add_sca at (32 (s - 1), 10 (s - 1)), x s about its centre, opaque
        const float s = b.breakPop, sz = 64 * s, cx = 32 + (s - 1) * 32, cy = 32 + (s - 1) * 10;
        m.pieceTo(cx - sz / 2, cy - sz / 2, sz, sz, g < 0 ? 512 : 384, 240, 128, 128, 1);
    } else
        m.piece(0, 0, g == 0 ? 320 : g < 0 ? 256 : 192, 120, 64, 64, a);
    m.setOffset(0, 0);

    r.setOrigin(r.uiX());
    if (!talk) drawBreakFx(app, m); // btl_main 0x5dc
    r.setOrigin(edge);
    // Bottom left: the rows' backing (tex2d_add4 on slot 47): a 0x98 black box, solid for 44 then
    // fading over 120, from 262 to 320 (the iPad's 32 down), a 2-pixel 0x98 white rule over it
    m.setOffset(hud - ex, ey);
    {
        const float bx = phone ? 16.0f : 0.0f, by = phone ? 294.0f : 326.0f, k = 0x98 / 255.0f;
        const float black[4] = {0, 0, 0, k * a}, clear[4] = {0, 0, 0, 0}, white[4] = {1, 1, 1, k * a}, gone[4] = {1, 1, 1, 0};
        m.quad(bx, by, 44, 58, black, black);
        m.quad(bx + 44, by, 120, 58, black, clear);
        m.quad(bx, by - 2, 44, 2, white, white);
        m.quad(bx + 44, by - 2, 120, 2, white, gone);
    }
    // the pair's rows (the one not choosing dimmed)
    const bool choosing = b.phase == App::Battle::Command && b.menu != App::Battle::Top && b.menu != App::Battle::EscapeAsk;
    for (int k = 1; k >= 0; --k)
        drawBattleRow(app, m, k, choosing && !b.rules.pair && b.member != k, a);
    m.setOffset(0, 0);
    if (talk) {
        r.setOrigin(r.uiX());
        return;
    }

    // The banner: the action's name, the target prompt, the messages
    r.setOrigin(r.uiX());
    if (!b.bannerShown.empty()) {
        const float clear[4] = {0, 0, 0, 0}, dark[4] = {0, 0, 0, 0x98 / 255.f};
        const float by = phone ? 32.0f - ey : 0.0f;
        // (the Vita's: 28.647..514.353 tex2d - 3.1.0's 72..471 widened 43.353 each side, vita decomp 62463-62468)
        const float vx = vitaEdgeX(app);
        m.quad(88 - vx, 4 + by, 24, 24, clear, dark);
        m.quad(112 - vx, 4 + by, 351 + 2 * vx, 24, dark, dark);
        m.quad(463 + vx, 4 + by, 24, 24, dark, clear);
        m.setOutline(false); // (drawfont_btl: the banner's text plain)
        m.textCentred(b.bannerShown, 287 - m.textWidth(b.bannerShown, 14) / 2, 16 + by, false, 1, 14);
        m.setOutline(true);
    }

    // Right: the menus - the page as shown (b.view), slid right by b.slide (menub_mainPut: x + +0x38)
    // (the Vita: 43 further right - its plates, targets, Back and lists; Escape's Yes / No stay, an action tree
    // drawn at the window's own x - vita decomp 62405)
    r.setOrigin(vitaRight(app));
    const App::Battle::MenuView& v = b.view;
    if (v.page >= 0 && b.slide < battleSlideDistance(app)) {
        const int keepMember = b.member; // (the lists' helpers read the member choosing)
        const bool keepPair = b.rules.pair;
        b.member = v.member, b.rules.pair = v.pair;
        m.setOffset(float(b.slide), 0);
        // the HELP board across the top (ComUty_HelpWindowDraw): the item's or gene's target and effect
        auto helpBoard = [&](int item) { drawHelpWindow(app, m, item, a, 16, 32); }; // (Part 3 877 s: the Camp's panel and place)
        // The lit plate: 3.1.0 is touch-only - __menub_mainPrc's state 2 clears +0x92 every frame, so a
        // page comes in with none lit and the tapped one lights as it goes (Part 2 1248.82-1249.22 s:
        // Solo and Pair dark, Pair lit when tapped). The cursor's plate is the port's for keys and pads:
        // after a touch or click it shows only as the page leaves (the plate just chosen)
        const bool cursorLit = battleCursorLit(app);
        switch (v.menu) {
        case App::Battle::Top:
            for (int i = 0; i < 4; ++i) {
                const bool dim = (i == 1 && (!(b.rules.f[0].alive() && b.rules.f[1].alive()) || b.rules.allyAuto)) ||
                                 (i == 2 && b.rules.escapeBlocked);
                const bool held = !dim && v.menu == b.menu && battlePressIn(app, kButtonX, kButtonY + kButtonStep * float(i), 136, 40);
                drawBattleButton(m, kButtonX, kButtonY + kButtonStep * float(i), kTopIcon[i], kTopWord[i], (v.cursor == i && cursorLit) || held, dim, a);
            }
            break;
        case App::Battle::Commands:
            for (int i = 0; i < 4; ++i) {
                const bool dim = i == 1 && (battleGeneList(app).empty() || battleGenesSilenced(app));
                const bool held = !dim && v.menu == b.menu && battlePressIn(app, kButtonX, kButtonY + kButtonStep * float(i), 136, 40);
                drawBattleButton(m, kButtonX, kButtonY + kButtonStep * float(i), kCmdIcon[i], kCmdWord[i], (v.cursor == i && cursorLit) || held, dim, a);
            }
            m.backBar(kBackX, kBackY, v.menu == b.menu && battlePressIn(app, kBackX, kBackY, 232, 24), a); // (_cmd_back: lit while held)
            break;
        case App::Battle::Targets: {
            const int n = int(v.picks.size());
            const cr::BattleActionDef* act = app.battleActions.get(v.targetAction);
            const bool all = act && b.rules.isGroupScope(*act);
            for (int i = 0; i < n; ++i)
                drawBattlePlate(app, m, kPlateX, kPlateBottom - 48.0f * float(n - i), v.picks[size_t(i)], all || v.pick == i, a);
            m.backBar(kBackX, kBackY, v.menu == b.menu && battlePressIn(app, kBackX, kBackY, 232, 24), a);
            break;
        }
        case App::Battle::Items: { // MuScrList: the battle items in the bag, how many of each
            const int n = int(v.picks.size());
            for (int k = 0; k < kItemRows && v.pickTop + k < n; ++k) {
                const int index = v.pickTop + k, item = v.picks[size_t(index)];
                const float y = 112 + btlui::kItemPitch * float(k);
                // ComUty_ComPlate_Item: the plate (8 / 9 picked), icon, name, how many at (222, 30)
                const auto it = app.game.items.find(item);
                drawCampRow(app, m, 232, y, item, it == app.game.items.end() ? 0 : it->second, index == v.pick, a);
            }
            m.backBar(kBackX, kBackY, v.menu == b.menu && battlePressIn(app, kBackX, kBackY, 232, 24), a);
            if (v.pick >= 0 && v.pick < n) helpBoard(v.picks[size_t(v.pick)]); // (MuScrList_SetIdInfo's help: the items' too)
            break;
        }
        case App::Battle::Genes: { // the gene list: name, MP / cost (dim when the MP won't pay)
            const int n = int(v.picks.size()), have = battleGeneMp(app);
            for (int k = 0; k < kItemRows && v.pickTop + k < n; ++k) {
                const int index = v.pickTop + k, g = v.picks[size_t(index)], cost = geneMpCost(app, g);
                const float y = 112 + btlui::kItemPitch * float(k), ra = cost <= have ? a : a * 0.5f;
                // ComUty_ComPlate_Gene: the plate, icon, name, "MP/" (picture 27 at (188, 34)) and the cost
                // (three digits to (218, 30)), dim when the MP won't pay
                drawCampRow(app, m, 232, y, g, 0, index == v.pick, ra);
                m.picture(27, 232 + 188, y + 34, ra);
                drawShopNumber(m, cost, 232 + 218, y + 30, 3, 10, ra, 2); // (ComUty_PictNumDraw: the field from 218, right-aligned)
            }
            m.backBar(kBackX, kBackY, v.menu == b.menu && battlePressIn(app, kBackX, kBackY, 232, 24), a);
            if (v.pick >= 0 && v.pick < n) helpBoard(v.picks[size_t(v.pick)]); // the gene's target and effect
            break;
        }
        case App::Battle::EscapeAsk: // 800: "Run from battle?" on the banner; ComUty_YesNoChoiceDraw's
            // plates in window 6 (its x the page's slide): Yes at (336, 104), No at (336, 160)
        { // (__ComUty_YesNoChoiceProc: a held plate takes the cursor - the lit one follows the finger)
            const bool live = v.menu == b.menu;
            const float yx = kYesNoX - vitaEdge43(app); // (the Vita: not moved with the menus)
            const bool hy = live && battlePressIn(app, yx, kYesY, 136, 40), hn = live && battlePressIn(app, yx, kNoY, 136, 40);
            drawBattleButton(m, yx, kYesY, 0, 0, hy || (!hn && v.confirm == 0), false, a);
            drawBattleButton(m, yx, kNoY, 1, 1, hn || (!hy && v.confirm == 1), false, a);
        }
            break;
        }
        m.setOffset(0, 0);
        b.member = keepMember, b.rules.pair = keepPair;
    }
    r.setOrigin(r.uiX());
    const char* help = b.phase != App::Battle::Command ? ""
                       : b.menu == App::Battle::Top ? "Up/Down  Enter"
                       : b.menu == App::Battle::EscapeAsk ? "Left/Right  Enter  Esc: no"
                                                          : "Up/Down  Enter  Esc: back";
    if (app.showHud) m.text(help, 300, 368, false, a * 0.6f, 9); // (our key hints)
}

// ---- Battle input ----

void battleKey(App& app, SDL_Keycode key) {
    App::Battle& b = app.battle;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE || key == SDLK_AC_BACK;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || key == SDLK_E;
    if (battletalk::on(app)) { // a talk: the pages turn as on the field
        if (enter) {
            if (!app.dialogue.empty()) tapDialogue(app);
            else app.tapPending = true;
        }
        return;
    }
    const bool up = key == SDLK_UP || key == SDLK_W, down = key == SDLK_DOWN || key == SDLK_S;
    const bool left = key == SDLK_LEFT || key == SDLK_A, right = key == SDLK_RIGHT || key == SDLK_D;
    // (the Vita: ○ alone goes on - _btl_main 0x44b, __menuu_MbResultPrc 3 / 6 / 8, _btlact_gene's
    // Analyze 0xc1c..; × does nothing there)
    if (b.phase == App::Battle::Results) {
        if (enter) resultsTap(app);
        return;
    }
    if (b.phase == App::Battle::Won) {
        if (enter) b.wonTap = true;
        return;
    }
    if (b.phase == App::Battle::Lost) {
        if (enter || back) b.lostTap = true;
        return;
    }
    if (b.analyze.on) { // Analyze's lines: the next
        if (enter) analyzeTap(app);
        return;
    }
    if (b.phase != App::Battle::Command || !battleMenuReady(app)) return;
    switch (b.menu) {
    case App::Battle::Top:
    case App::Battle::Commands:
        if (up || down) { // (round the four, over what can't be chosen)
            const bool top = b.menu == App::Battle::Top;
            int c = b.cursor;
            for (int k = 0; k < 4; ++k) {
                c = (c + (up ? 3 : 1)) % 4;
                if (!(top ? battleTopBarred(app, c) : battleCommandBarred(app, c))) break;
            }
            if (c != b.cursor) b.cursor = c, menuSound(app, se::kCursor);
        } else if (enter) { if (b.menu == App::Battle::Top) battleChooseTop(app, b.cursor); else battleChooseCommand(app, b.cursor); }
        else if (back && b.menu == App::Battle::Commands) battleBack(app);
        break;
    case App::Battle::Targets:
    case App::Battle::Items:
    case App::Battle::Genes: {
        const int n = int(b.picks.size());
        const bool targets = b.menu == App::Battle::Targets;
        if ((up || down || (targets && (left || right))) && n > 0) {
            b.pick = (b.pick + ((up || left) ? n - 1 : 1)) % n;
            b.pickTop = std::clamp(b.pickTop, std::max(0, b.pick - btlui::kItemRows + 1), b.pick);
            if (targets) {
                const cr::BattleActionDef* act = app.battleActions.get(b.targetAction);
                cameraForTarget(app, b.picks[size_t(b.pick)], act && b.rules.isGroupScope(*act), true);
            }
            menuSound(app, se::kCursor);
        } else if (enter && n > 0) {
            if (targets) battleChooseTarget(app, b.picks[size_t(b.pick)]);
            else if (b.menu == App::Battle::Genes) battleChooseGene(app, b.picks[size_t(b.pick)]);
            else battleChooseItem(app, b.picks[size_t(b.pick)]);
        } else if (back)
            battleBack(app);
        break;
    }
    case App::Battle::EscapeAsk:
        if (up || down || left || right) { b.confirm ^= 1; menuSound(app, se::kCursor); }
        else if (enter) battleEscapeAnswer(app, b.confirm == 0, false);
        else if (back) battleEscapeAnswer(app, false, true);
        break;
    }
}

void battleClick(App& app, float px, float py) {
    using namespace btlui;
    App::Battle& b = app.battle;
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    // (the Vita: the menus 43 right - __menub_mainPrc's boxes at (int)(x + 43.352966) + 0x148 / 0xb0 / 0xc0)
    const float uiX = app.renderer.uiX(), rightX = (app.renderer.phoneLayout() ? uiX : 2 * uiX) + 2 * vitaEdge43(app);
    const float rx = (lx - rightX) / 2, my = ly / 2;
    if (battletalk::on(app)) { // a talk: the skip corner, else the next page
        if (skipCorner(app, lx, ly)) holdSkip(app, kSkipPointer, true);
        else if (!app.dialogue.empty()) tapDialogue(app);
        else app.tapPending = true;
        return;
    }
    if (b.phase == App::Battle::Results) { // __MbResult_IsTouchChack: (0, 0, 480, 320)
        resultsTap(app);
        return;
    }
    if (b.phase == App::Battle::Won) { // (anywhere ends the victory's hold)
        b.wonTap = true;
        return;
    }
    if (b.phase == App::Battle::Lost) { // (anywhere: an event battle's game over goes on a tap)
        b.lostTap = true;
        return;
    }
    if (b.analyze.on) { // _pad_chk_trg_up: anywhere turns Analyze's line
        analyzeTap(app);
        return;
    }
    if (b.phase != App::Battle::Command || !battleMenuReady(app)) return;
    const bool onBack = my >= kBackY && my < kBackY + 24 && rx >= kBackX && rx < kBackX + 232;
    auto buttonAt = [&]() {
        for (int i = 0; i < 4; ++i)
            if (rx >= kButtonX && rx < kButtonX + 136 && my >= kButtonY + kButtonStep * i && my < kButtonY + kButtonStep * i + 40) return i;
        return -1;
    };
    switch (b.menu) {
    case App::Battle::Top:
        if (const int i = buttonAt(); i >= 0) { b.cursor = b.view.cursor = i; battleChooseTop(app, i); } // (the page as shown lights the tapped plate as it goes)
        break;
    case App::Battle::Commands:
        if (const int i = buttonAt(); i >= 0) { b.cursor = b.view.cursor = i; battleChooseCommand(app, i); }
        else if (onBack) battleBack(app);
        break;
    case App::Battle::Targets: {
        const int n = int(b.picks.size());
        for (int i = 0; i < n; ++i) {
            const float y = kPlateBottom - 48.0f * float(n - i);
            if (rx >= kPlateX && rx < kPlateX + 256 && my >= y && my < y + 40) {
                b.pick = i;
                battleChooseTarget(app, b.picks[size_t(i)]);
                return;
            }
        }
        if (onBack) battleBack(app);
        break;
    }
    case App::Battle::Items:
    case App::Battle::Genes:
        if (rx >= 232 && rx < 492 && my >= 112 && my < 112 + kItemPitch * kItemRows) {
            const int index = b.pickTop + int((my - 112) / kItemPitch);
            if (index < int(b.picks.size())) {
                b.pick = index;
                if (b.menu == App::Battle::Genes) battleChooseGene(app, b.picks[size_t(index)]);
                else battleChooseItem(app, b.picks[size_t(index)]);
            }
        } else if (onBack)
            battleBack(app);
        break;
    case App::Battle::EscapeAsk: { // ComUty_YesNoChoiceProc: Yes (336, 104), No (336, 160), 136 x 40 (the Vita's not moved)
        const float yx = kYesNoX - vitaEdge43(app);
        if (rx >= yx && rx < yx + 136 && my >= kYesY && my < kYesY + 40) battleEscapeAnswer(app, true, false);
        else if (rx >= yx && rx < yx + 136 && my >= kNoY && my < kNoY + 40) battleEscapeAnswer(app, false, false);
        break;
    }
    }
}

} // namespace crapp
