#include "app/app.hpp"

namespace crapp {

void holdSkip(App& app, unsigned source, bool held) {
    app.skipHeld = held ? app.skipHeld | source : app.skipHeld & ~source;
    app.skipScene = app.skipHeld != 0;
}
void clearSkip(App& app) {
    app.skipHeld = 0;
    app.skipScene = false;
}
// The skip button's corner, top right, in logical units
bool skipCorner(const App& app, float lx, float ly) {
    if (app.messageSkip) return false; // Uty_Chg_MsgSkip (G+0x2d): the corner ignored and its icon hidden while set
    return lx >= app.renderer.layoutRight() - 76 && ly < app.renderer.layoutTop() + 72;
}

// The viewer's side of a running script (see cr::EventHost).
// ---- Scene effects (Omega: ScnEffectRead / ScnEffectStart / ScnEffectStop / ScnEffectProc) ----
// Call 2020 (Uty_ScnEffectRead) reads set var 0's textures into tex2d slots 0x30..0x58, call 2009
// (Uty_ScnEffect_Init) turns effect n (1..30) on or off. Every 20 Hz frame ScnEffectProc runs each
// effect on: its counters, UpdateRandom's fading alpha channels, and its layers - tex2d_add over
// the screen's rectangle (the iPad's adj = (-16, -32) 512 x 384 in tex2d units), the texture's
// (u, v) window wrapping, moving with its counters and by half the field's scroll the other way.
// The weather (2..10, 23's grass, 29) runs only with the Options' Environment Effects on (GetScreenEffectState);
// field-only effects need the field's bit (G+0x364 & 0x40) - in a battle (bit 0x20) the weather
// uses its own copies. Layers at z -1998 (the magma, the elevator shaft, the space and sky
// backdrops: 11..22, 26..28, 30) go behind the field's painting, the rest over it.
// 0x4a / 0x4d (fireflies, smoke) blend additively (mceAppearance_setBlendMode 1).
namespace scenefx {
struct SetTexture { int set; const char* file; int slot; int w, h; };
// ScnEffectRead's sets (Omega 1.0.0): file, slot, tex2d_read's logical size
constexpr SetTexture kSetTextures[] = {
    {0, "t01_Emergency", 0x46, 4, 4}, {0, "t01_Emergency", 0x47, 4, 4}, {0, "t01_Emergency", 0x48, 4, 4},
    {1, "d01_cr2snow02", 0x3c, 128, 128}, {1, "d01_cr2fog02", 0x3e, 256, 256}, {1, "d01_cr2smoke", 0x32, 256, 256},
    {1, "d01_cr2snow02", 0x3d, 256, 256}, {1, "d01_cr2fog02", 0x3f, 256, 256}, {1, "d01_cr2smoke", 0x33, 256, 256},
    {1, "d01_frame01", 0x45, 512, 512}, // (_ipad on the iPad: chk_hd_name)
    {2, "d02_cr2rain01", 0x36, 256, 256}, {2, "d02_cr2rain02", 0x38, 256, 256}, {2, "d02_cr2fog", 0x3a, 256, 256},
    {2, "d02_cr2rain01", 0x37, 256, 256}, {2, "d02_cr2rain02", 0x39, 256, 256}, {2, "d02_cr2fog", 0x3b, 256, 256},
    {3, "d03_cr2fog", 0x30, 256, 256}, {3, "d03_cr2fog", 0x31, 256, 256}, {3, "d03_cr2Firefly", 0x4a, 256, 256},
    {4, "d04_cr2Ash", 0x4b, 256, 256}, {4, "d04_cr2fog", 0x4c, 256, 256},
    {5, "p0001_fog", 0x44, 512, 256}, {5, "d0530_bg01", 0x58, 1024, 1024},
    {6, "d06_smoke", 0x4d, 256, 256}, {6, "d06_Magma", 0x4e, 256, 256}, {6, "d0602_Magma", 0x4f, 256, 256},
    {7, "d06_elv01", 0x50, 512, 1024}, {7, "d06_elv02", 0x51, 512, 1024}, {7, "d06_elv03", 0x52, 512, 1024},
    {8, "d06_canv", 0x53, 512, 512}, {8, "d0612_grass", 0x54, 256, 256},
    {9, "d06_frame01", 0x55, 512, 512}, {9, "d06_frame02", 0x56, 512, 512},
    {10, "d0501-b00a00", 0x40, 512, 1024},
};
constexpr int kFirstSlot = 0x30, kSlots = 41;
// The iPad's screen rectangle (ScnEffectStart's _adj_xx / yy / ww / hh) in tex2d units
constexpr float kAdjX = -16, kAdjY = -32, kAdjW = 512, kAdjH = 384;
} // namespace scenefx

void loadSceneEffectSet(App& app, int set) { // ScnEffectRead: slots 0x30..0x58 cleared, the set's read
    App::SceneFx& fx = app.fx;
    if (set < 0 || set > 10) return;
    for (cr::Texture& t : fx.tex) app.renderer.destroy(t);
    fx.texW.fill(0), fx.texH.fill(0);
    fx.set = set;
    for (const auto& st : scenefx::kSetTextures) {
        if (st.set != set) continue;
        cr::Image img;
        const size_t k = size_t(st.slot - scenefx::kFirstSlot);
        if (loadImage(app, std::string(st.file) + ".mct", img)) fx.tex[k] = app.renderer.upload(img);
        fx.texW[k] = float(st.w), fx.texH[k] = float(st.h);
    }
}

void endSceneEffects(App& app) { // ScnEffectEnd: every effect stopped, the textures gone
    for (cr::Texture& t : app.fx.tex) app.renderer.destroy(t);
    const uint32_t rng = app.fx.rng;
    const int ct = app.fx.frameCt, ct1 = app.fx.frameCt1;
    app.fx = App::SceneFx();
    app.fx.rng = rng, app.fx.frameCt = ct, app.fx.frameCt1 = ct1;
}

void setSceneEffect(App& app, bool on, int id) { // ScnEffectStart / ScnEffectStop
    if (id < 0 || id >= 31) return;
    App::SceneFx& fx = app.fx;
    const size_t k = size_t(id);
    if (on) {
        if (fx.on[k]) return; // (already on: left as it is)
        fx.on[k] = true;
        fx.timerA[k] = fx.timerB[k] = 0;
    } else if (fx.on[k]) {
        fx.on[k] = false;
        fx.timerA[k] = 0;
    }
}

// InitRandom(channel, type, alpha, max, speed, intervals 0..3, max / min time): set up when the
// channel's type changes
void initSceneChannel(App& app, int c, int type, int alpha, int speed, int iv0, int iv1, int iv2, int iv3, int maxT, int minT) {
    App::SceneFx& fx = app.fx;
    App::SceneFx::Channel& ch = fx.ch[c];
    if (ch.type == type) return;
    ch.type = type, ch.alpha = alpha & 255, ch.speed = speed;
    ch.iv[0] = iv0, ch.iv[1] = iv1, ch.iv[2] = iv2, ch.iv[3] = iv3;
    ch.mode = ch.count = 0;
    fx.maxTime = maxT, fx.minTime = minT;
}

// UpdateRandom: channels 0 and 1 fade in by speed for interval 0 frames, hold a random while
// (min .. max time), fade out for interval 2, hold for interval 3 (bytes: alpha wraps)
void updateSceneChannels(App& app) {
    App::SceneFx& fx = app.fx;
    for (int c = 0; c < 2; ++c) {
        App::SceneFx::Channel& ch = fx.ch[c];
        switch (ch.mode) {
        case 0:
            ch.alpha = (ch.alpha + ch.speed) & 255;
            if (((ch.count + 1) & 255) == ch.iv[0]) {
                ch.count = 0, ch.mode = 1;
                if (fx.maxTime == fx.minTime) ch.iv[1] = fx.maxTime;
                else {
                    fx.rng = fx.rng * 1103515245u + 12345u;
                    ch.iv[1] = (fx.minTime + int((fx.rng >> 16) % uint32_t(std::max(1, fx.maxTime - fx.minTime)))) & 255;
                }
            } else ch.count = (ch.count + 1) & 255;
            break;
        case 1:
            if (((ch.count + 1) & 255) == ch.iv[1]) ch.count = 0, ch.mode = 2;
            else ch.count = (ch.count + 1) & 255;
            break;
        case 2:
            ch.alpha = (ch.alpha - ch.speed) & 255;
            if (((ch.count + 1) & 255) == ch.iv[2]) ch.count = 0, ch.mode = 3;
            else ch.count = (ch.count + 1) & 255;
            break;
        default:
            if (((ch.count + 1) & 255) == ch.iv[3]) ch.count = 0, ch.mode = 0;
            else ch.count = (ch.count + 1) & 255;
            break;
        }
    }
}

// The weather runs only with the Options' Environment Effects on (GetScreenEffectState)
bool weatherEffect(int id) { return (id >= 2 && id <= 10) || id == 29; } // (23's grass too, not its canvas)

// A counter that wraps to 0 past `last`
void sceneTick(int& t, int last) { if (++t > last) t = 0; }

// One 20 Hz frame of every effect on: its channels and counters (ScnEffectProc's bookkeeping;
// drawSceneBack / drawSceneEffects draw from the same state). `battle`: in a battle (field-only
// effects stand still)
void stepSceneEffects(App& app) {
    App::SceneFx& fx = app.fx;
    const bool battle = app.battle.active;
    for (int id = 1; id < 31; ++id) {
        if (!fx.on[size_t(id)]) continue;
        const bool weatherOn = app.environmentEffects;
        int& a = fx.timerA[size_t(id)];
        int& b = fx.timerB[size_t(id)];
        switch (id) {
        case 1: a ^= 1; break; // the concentration lines: every other frame (gac + 0x188)
        case 2: case 3: case 4: // the alarm lights: a 31-frame pulse, on the field
            if (!battle && weatherOn && ++a >= 31) a = 0;
            break;
        case 5: case 29: // snow, the frame over it: slot 5's counters
            if (!weatherOn) break;
            initSceneChannel(app, 0, 5, 0x70, 3, 0x25, 0, 0x25, 0x28, 1, 1);
            initSceneChannel(app, 1, 5, 0x87, 1, 0x78, 0, 0x78, 0x3c, 0xf0, 0x78);
            initSceneChannel(app, 2, 5, 0x87, 1, 0x78, 0, 0x78, 0x3c, 0xf0, 0x78);
            updateSceneChannels(app);
            fx.ch[0].iv[1] = 0x96;
            sceneTick(fx.timerA[5], 0x4f);
            sceneTick(fx.timerB[5], 0x4f);
            break;
        case 6: // smoke and the frame
            if (!weatherOn) break;
            sceneTick(a, 400);
            sceneTick(b, 0x1b8);
            initSceneChannel(app, 0, 6, 0x70, 3, 0x25, 0, 0x25, 0x28, 1, 1);
            updateSceneChannels(app);
            fx.ch[0].iv[1] = 0x96;
            break;
        case 7: // rain
            if (!weatherOn) break;
            initSceneChannel(app, 0, 7, 0x37, 5, 0x28, 0, 0x28, 0x1e, 0x78, 0x3c);
            initSceneChannel(app, 1, 7, 0x87, 3, 0x28, 0, 0x28, 0x1e, 0x78, 0x3c);
            updateSceneChannels(app);
            sceneTick(a, 199);
            break;
        case 8: // fog and fireflies
            if (!weatherOn) break;
            initSceneChannel(app, 0, 8, 0x19, 0xb, 0x14, 0, 0x14, 0x28, 0x28, 0x29);
            initSceneChannel(app, 1, 8, 0, 0xc, 0x14, 0, 0x14, 1, 0, 1);
            updateSceneChannels(app);
            sceneTick(a, 0x200);
            sceneTick(b, 0x2cf);
            break;
        case 9: // ash
            if (!weatherOn) break;
            initSceneChannel(app, 0, 9, 0x87, 3, 0x28, 0, 0x28, 0x3c, 0x78, 0x3c);
            initSceneChannel(app, 1, 9, 0x40, 3, 0x28, 0, 0x28, 0x3c, 0x78, 0x3c);
            updateSceneChannels(app);
            sceneTick(a, 0xf0);
            sceneTick(b, 0xb4);
            break;
        case 10: if (weatherOn) sceneTick(a, 0xf0); break; // the steam
        case 11: case 12: sceneTick(fx.timerA[11], 600); break; // the magma: slot 11's counter
        case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 26: case 27: {
            // the elevator shaft: the walls slide for 100 frames (40 for 14 / 18 / 22 / 26, which start at 60)
            if (battle) break;
            const int start = (id == 14 || id == 18 || id == 22 || id == 26) ? 60 : 0;
            if (a + start < 100) ++a;
            break;
        }
        case 23: // the grass's pulse
            if (weatherOn) sceneTick(a, 0x50), sceneTick(b, 0x3c);
            break;
        case 25: sceneTick(a, 0x78); break; // the frames' blink
        case 28: // the space backdrop scrolls (ever down)
            if (battle) break;
            if (--fx.frameCt > 2000) fx.frameCt = 0;
            if (++fx.frameCt1 > int(scenefx::kAdjW)) fx.frameCt1 = 0;
            break;
        case 30: sceneTick(a, 0x808), sceneTick(b, 0x78); break; // the sky
        default: break;
        }
    }
}

// ScnEffectRead's pictures read clamped (tex2d_read's 1; the layers wrap, 2) and drawn at the screen's adj
// from uv (0, 0): d01's frost frame (0x45), d06's canvas (0x53) and frames (0x55 / 0x56)
bool scenePicture(int slot) { return slot == 0x45 || slot == 0x53 || slot == 0x55 || slot == 0x56; }

// A layer: the texture's (u, v) window, wrapping, over tex2d rectangle (x, y, w, h) (+16 / +32 to
// menu units, doubled on screen; the uv in the slot's logical texels, as tex2d_read sized it); the
// screen's extra width (widescreen) gets more of the texture. `grey` tints it
void drawSceneLayer(App& app, int slot, float x, float y, float w, float h, float u, float v, int alpha, float grey = 1) {
    const int k = slot - scenefx::kFirstSlot;
    if (k < 0 || k >= scenefx::kSlots) return;
    const cr::Texture& t = app.fx.tex[size_t(k)];
    if (!t.id || alpha <= 0) return;
    const float lw = app.fx.texW[size_t(k)] > 0 ? app.fx.texW[size_t(k)] : float(t.width);
    const float lh = app.fx.texH[size_t(k)] > 0 ? app.fx.texH[size_t(k)] : float(t.height);
    const float ku = float(t.width) / lw, kv = float(t.height) / lh; // (the file's texels per logical one)
    cr::Renderer& r = app.renderer;
    const float a = float(alpha & 255) / 255.0f;
    if (scenePicture(slot)) {
        // ScnEffectProc: tex2d_add(slot, adj_xx, adj_yy, z, 0, 0, adj_ww, adj_hh) - ScnEffectStart's adj, the
        // iPad's (-16, -32) 512 x 384 for its _ipad art, else the iPhone's (0, 0) 480 x 320 (the plain files'
        // picture fills only that corner of the 512 x 512 texture). One picture over the visible screen - the
        // original's place at 4:3 / 3:2; wider, stretched to the edges as Android 1.1.6 does its full-screen
        // pictures (tex2d_add_scaWH, x width / 480, y height / 320), not wrapped (issue #8)
        const bool phone = r.phoneLayout();
        const float s = phone ? r.layoutScale() : 1.0f;
        const float hw = r.screenW() * 0.5f / s, hh = 384.0f / s; // (the visible screen's half-size: layout units)
        const float pw = phone ? 480.0f : 512.0f, ph = phone ? 320.0f : 384.0f;
        // (the origin is uiX: the screen's centre is x 512 from it)
        r.drawQuadUV(t, 512 - hw, 384 - hh, 2 * hw, 2 * hh, 0, 0, pw * ku, ph * kv, grey, grey, grey, a);
        return;
    }
    const float extra = r.uiX() / 2; // menu units beyond the 512 at each side
    float mx = x + 16, my = y + 32;
    if (w >= 512) { mx -= extra; u -= extra; w += 2 * extra; }
    const float u0 = u - std::floor(u / lw) * lw, v0 = v - std::floor(v / lh) * lh;
    // ScnEffectRead: d03's fireflies (0x4a) and d06's smoke (0x4d) get mceAppearance_setBlendMode(1)
    const bool additive = slot == 0x4a || slot == 0x4d;
    if (additive) r.setAdditive(true);
    for (float dy = 0; dy < h;) {
        const float vv = std::fmod(v0 + dy, lh), sh = std::min(lh - vv, h - dy);
        for (float dx = 0; dx < w;) {
            const float uu = std::fmod(u0 + dx, lw), sw = std::min(lw - uu, w - dx);
            r.drawQuadUV(t, (mx + dx) * 2, (my + dy) * 2, sw * 2, sh * 2, uu * ku, vv * kv, sw * ku, sh * kv, grey, grey, grey, a);
            dx += sw;
        }
        dy += sh;
    }
    if (additive) r.setAdditive(false);
}

// ScnEffectProc's layers at z -1998, drawn before the field's painting: 11 / 12 the magma, 13..22,
// 26, 27 the elevator shaft's walls, 28 the space backdrop, 30 the sky. sx, sy: half the scroll - GetScroll's, whose
// y counts down the screen (FieldCamera +8 = viewH / 2 - the target's y): our offY's negative, as FrmPanCam takes it
// (#18: the sky above the ship's deck, not its sea)
void drawSceneBack(App& app, bool battle) {
    using namespace scenefx;
    const App::SceneFx& fx = app.fx;
    if (fx.set < 0) return;
    const float sx = battle ? 0.0f : app.scroll.offX * 0.5f, sy = battle ? 0.0f : -app.scroll.offY * 0.5f;
    cr::Renderer& r = app.renderer;
    r.setOrigin(r.uiX());
    for (int id = 1; id < 31; ++id) {
        if (!fx.on[size_t(id)]) continue;
        const float a = float(fx.timerA[size_t(id)]), b = float(fx.timerB[size_t(id)]);
        switch (id) {
        case 11: case 12: {
            if (battle) break;
            const float m = float(fx.timerA[11]);
            if (id == 11) drawSceneLayer(app, 0x4e, kAdjX, kAdjY, kAdjW, kAdjH, 2 * m * 256 / 600 - sx, -2 * m * 256 / 600 + 256 - sy, 255);
            else drawSceneLayer(app, 0x4f, kAdjX, kAdjY, kAdjW, kAdjH, 0, -24 * m * 256 / 600 + 256 - sy, 255);
            break;
        }
        case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 26: case 27: {
            if (battle) break;
            const float t = a + ((id == 14 || id == 18 || id == 22 || id == 26) ? 60 : 0);
            const int slot = id <= 15 ? 0x50 : (id <= 19 || id >= 26) ? 0x51 : 0x52;
            float v = 0;
            switch (id) {
            case 13: case 17: v = 0; break;
            case 16: case 20: v = 704; break;
            case 14: case 18: case 21: case 26: v = 704 - t * 7.04f; break;
            default: v = t * 7.04f; break; // 15, 19, 22, 27
            }
            drawSceneLayer(app, slot, 0, sy + 300 + kAdjY, 480, 320, 16, v, 255);
            break;
        }
        case 28:
            if (!battle) drawSceneLayer(app, 0x40, kAdjX, kAdjY, kAdjW, kAdjH, 0, float(fx.frameCt) * 1.025f, 255);
            break;
        case 30: { // the sky: two layers of clouds, rising and falling a little (sin, 3 degrees a frame)
            const float v = 352 - sy + kAdjY + 5.0f * std::sin(b * 3.0f * 3.14159265f / 180.0f);
            float h2 = sy + 256 + kAdjH / 2;
            if (h2 < kAdjH) drawSceneLayer(app, 0x58, kAdjX, kAdjY, kAdjW, kAdjH, a * 8 - sx, v, 255);
            if (h2 > kAdjH) h2 = kAdjH;
            drawSceneLayer(app, 0x58, kAdjX, kAdjY, kAdjW, h2, a * 0.498f - sx, v, 255);
            break;
        }
        default: break;
        }
    }
    r.setOrigin(r.uiX());
}

// (CR1:U's name for the back pass, kept for its callers)
void drawSpaceBackdrop(App& app, bool battle) { drawSceneBack(app, battle); }

// ScnEffectProc's layers over the field (z -51..-48). In a battle the weather draws its own copies
// (the next slot) and nothing scrolls
void drawSceneEffects(App& app, bool battle) {
    using namespace scenefx;
    const App::SceneFx& fx = app.fx;
    if (fx.set < 0) return;
    const float sx = battle ? 0.0f : app.scroll.offX * 0.5f, sy = battle ? 0.0f : -app.scroll.offY * 0.5f;
    cr::Renderer& r = app.renderer;
    r.setOrigin(r.uiX());
    const int ch0 = fx.ch[0].alpha, ch1 = fx.ch[1].alpha, ch2 = fx.ch[2].alpha;
    for (int id = 1; id < 31; ++id) {
        if (!fx.on[size_t(id)]) continue;
        if (!app.environmentEffects && weatherEffect(id)) continue;
        const float a = float(fx.timerA[size_t(id)]), b = float(fx.timerB[size_t(id)]);
        switch (id) {
        case 1: { // syuutyuusen's concentration lines on alternate frames: four mirrored quarters
            // (_bkanTexUv_ipad: each tex2d (x - 16, y - 32) its own (u, v) and size, z -50)
            if (!(fx.timerA[1] & 1)) break;
            if (!app.speedLinesTex.id) {
                cr::Image img;
                if (loadImage(app, "syuutyuusen.mct", img)) app.speedLinesTex = app.renderer.upload(img);
            }
            if (!app.speedLinesTex.id) break;
            static const int kUv[4][4] = {{0, 0, 256, 192}, {256, 0, -256, 192}, {256, 192, -256, -192}, {0, 192, 256, -192}};
            const float k = app.speedLinesTex.width / 256.0f, extra = r.uiX(); // (widescreen: the outer halves stretched over the sides)
            for (const auto& q : kUv) // (a negative size flips the uv only)
                r.drawQuadUV(app.speedLinesTex, q[0] ? 512.0f : -extra, 2.0f * float(q[1]), 512 + extra, 384, q[0] * k, q[1] * k, q[2] * k,
                             q[3] * k);
            break;
        }
        case 2: case 3: case 4: { // the alarm lights: a grey level 0 -> 128 -> 0, at 0xa0
            if (battle) break;
            const float g = a < 15 ? a / 15 * 128 : (a - 15) / 15 * -128 + 128;
            drawSceneLayer(app, 0x44 + id, kAdjX, kAdjY, kAdjW, kAdjH, 0, 0, 0xa0, std::max(0.0f, g) / 255.0f);
            break;
        }
        case 5: case 29: {
            const float t = float(fx.timerB[5]);
            const float u1 = t * -12.8f - sx, v1 = t * -12.8f - sy, w2 = t * -25.6f;
            if (id == 5) {
                drawSceneLayer(app, battle ? 0x3d : 0x3c, kAdjX, kAdjY, kAdjW, kAdjH, u1, v1, 255);
                drawSceneLayer(app, battle ? 0x3f : 0x3e, kAdjX, kAdjY, kAdjW, kAdjH, u1, v1, ch1 - (battle ? 64 : 32));
                drawSceneLayer(app, battle ? 0x3f : 0x3e, kAdjX, kAdjY, kAdjW, kAdjH, w2 + 128 - sx, w2 + 64 - sy, ch2 - (battle ? 64 : 32));
            }
            drawSceneLayer(app, 0x45, kAdjX, kAdjY, kAdjW, kAdjH, 0, 0, ch0);
            break;
        }
        case 6:
            drawSceneLayer(app, battle ? 0x33 : 0x32, kAdjX, kAdjY, kAdjW, kAdjH, a * -0.64f - sx, a * 0.64f - sy, 255);
            drawSceneLayer(app, 0x45, kAdjX, kAdjY, kAdjW, kAdjH, 0, 0, ch0);
            break;
        case 7:
            drawSceneLayer(app, battle ? 0x37 : 0x36, kAdjX, kAdjY, kAdjW, kAdjH, 0, a * -51.2f - sy, ch0);
            drawSceneLayer(app, battle ? 0x39 : 0x38, kAdjX, kAdjY, kAdjW, kAdjH, a * -13.251f - sx, a * -49.455f - sy, ch1);
            drawSceneLayer(app, battle ? 0x3b : 0x3a, kAdjX, kAdjY, kAdjW, kAdjH, a * -1.28f - sx, a * 1.28f - sy, 255);
            break;
        case 8: {
            drawSceneLayer(app, battle ? 0x31 : 0x30, kAdjX, kAdjY, kAdjW, kAdjH, a - sx, a * 0.5f - sy, 0x80);
            const float f = 64.0f * std::sin(b * 0.5f * 3.14159265f / 180.0f), t = b * 1.42222f;
            drawSceneLayer(app, 0x4a, kAdjX, kAdjY, kAdjW, kAdjH, -f - sx, (t - sy) * 0.5f, ch0);
            drawSceneLayer(app, 0x4a, kAdjX, kAdjY, kAdjW, kAdjH, f - sx, t + 64 - sy, ch1);
            break;
        }
        case 9: {
            const float m = a * -1.066f;
            drawSceneLayer(app, 0x4b, kAdjX, kAdjY, kAdjW, kAdjH, m - sx, m - sy, ch0);
            drawSceneLayer(app, 0x4b, kAdjX, kAdjY, kAdjW, kAdjH, 128, m - sy + 128, ch1);
            drawSceneLayer(app, 0x4c, kAdjX, kAdjY, kAdjW, kAdjH, b * -1.42222f + 128 - sx, b * -1.42222f - sy, 255);
            break;
        }
        case 10: // the puzzle rooms' clouds: (-16, 128) 512 x 256 on the iPad; off the iPad (Android
            // 1.1.6 0x394e8) y oy / scale + 64, the texture's dense bottom on the screen's - Part 3
            // 1231-1233 s: the clouds move from y 560 down, as the port's now
            drawSceneLayer(app, 0x44, -16, r.phoneLayout() ? 64 + androidEdgeY(app) : 128.0f, 512, 256, a * 2.133f - sx, 0, 255);
            break;
        case 11: case 12: { // the magma's smoke (with Environment Effects)
            if (!app.environmentEffects) break;
            const float m = float(fx.timerA[11]);
            drawSceneLayer(app, 0x4d, kAdjX, kAdjY, kAdjW, kAdjH, 0, 2 * m * 256 / 600, 0x80);
            drawSceneLayer(app, 0x4d, kAdjX, kAdjY, kAdjW, kAdjH, 128, m * 256 / 600 + 128, 0x60);
            break;
        }
        case 23: case 24: { // the grass (23, pulsing) and the canvas over it all
            if (id == 23 && app.environmentEffects) {
                const int pulse = int(b) < 30 ? int(b) * 5 : (-5 * int(b) + 0x36) & 255;
                const float gx = a * 4, gy = a * -1.25f + 100;
                const float off[3][2] = {{0, 12}, {12, 24}, {24, 0}};
                for (const auto& o : off) drawSceneLayer(app, 0x54, gx + o[0], gy + o[1], 256, 256, 0, 0, pulse);
            }
            drawSceneLayer(app, 0x53, kAdjX, kAdjY, kAdjW, kAdjH, 0, 0, 255);
            break;
        }
        case 25: drawSceneLayer(app, a < 60 ? 0x55 : 0x56, kAdjX, kAdjY, kAdjW, kAdjH, 0, 0, 255); break;
        default: break;
        }
    }
    r.setOrigin(r.uiX());
}

// ---- The field camera's scroll (FieldCamera::setScrollPlayer / setScroll2D / setScroll3D, ScrollAutoCtl) ----
// fldread_prc doubles each field's view on the iPad (the stage record's viewW / viewH and the
// painting's rect), so a 1024 x 1024 painting covers 2048 x 2048 screen pixels, two to a texel.
// FieldCamera::setCamera3D puts the 3D viewport (the view) at the screen's centre plus the
// offset. With the camera on the player (CameraPlayer, FieldScene::updateCamera) the offset
// centres the player's head (position + 7 + record +70) every frame, within the painting's rect;
// with it free (CameraFree) the scripts move it: FrmAdjCam slides to a shot's (x, y), FrmPanCam
// slides by twice its (dx, dy), FrmSetCam centres the view (SetScroll 0, 0: on the field it is a 3D
// camera op), Mapscroll centres an NPC or a view pixel.
bool paintedField(const App& app) {
    return app.mode == Mode::Maps && app.showBackground && app.background.id && app.haveStageCam;
}
bool scrollingField(const App& app) { return paintedField(app); }
float fieldViewW(const App& app) { return 2.0f * float(app.stageCam.viewWidth > 0 ? app.stageCam.viewWidth : 1024); }
float fieldViewH(const App& app) { return 2.0f * float(app.stageCam.viewHeight > 0 ? app.stageCam.viewHeight : 1024); }
// Painted Rooms: Fill (the port's): the view zoomed until the painting's rect covers the screen - 1
// in the original presentation, or where the rect already covers it
float fieldFillZoom(const App& app) {
    if (app.video.paintings != 1 || !app.haveStageCam) return 1;
    const float SW = app.renderer.screenW(), H = cr::Renderer::kLogicalH;
    const int* rc = app.stageCam.rect;
    const bool haveRect = rc[2] > 0 && rc[3] > 0;
    const float rw = haveRect ? 2.0f * float(rc[2]) : fieldViewW(app), rh = haveRect ? 2.0f * float(rc[3]) : fieldViewH(app);
    return std::max(1.0f, std::max(SW / std::max(rw, 1.0f), H / std::max(rh, 1.0f)));
}

cr::Mat4 stageViewProjection(const App& app) {
    const cr::StageCamera& sc = app.stageCam;
    const float aspect = sc.viewHeight > 0 ? float(sc.viewWidth) / float(sc.viewHeight) : 1.0f;
    return cr::Mat4::perspective(sc.fovDegrees * 3.14159265f / 180.0f, aspect, 10.0f, 4000.0f) *
           cr::Mat4::lookAt({sc.eye[0], sc.eye[1], sc.eye[2]}, {sc.target[0], sc.target[1], sc.target[2]}, {0, 1, 0});
}

// Where world point p falls in the (doubled) view, in pixels from its top-left
bool fieldViewPoint(const App& app, const cr::Vec3& p, float& vx, float& vy) {
    const cr::Mat4 vp = stageViewProjection(app);
    const float* m = vp.m;
    const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 1e-4f) return false;
    vx = ((m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w * 0.5f + 0.5f) * fieldViewW(app);
    vy = (0.5f - (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w * 0.5f) * fieldViewH(app);
    return true;
}

// FieldCamera::setScroll2D: centre view pixel (vx, vy), kept inside the painting's rect (a
// screen's half from its edges; a rect narrower than the screen stays centred); the offset
// moves `rate` of the way there
void scrollFieldTo(App& app, float vx, float vy, float rate) {
    const float V = fieldViewW(app), Vh = fieldViewH(app);
    const float fz = fieldFillZoom(app); // (Fill: less of the view on the screen)
    // The paintings' layer is drawn larger about the screen's centre on the Vita's screen (1 / 0.85 of
    // the iPhone layout's) and the Android's (16 / 15): the screen holds that much less of the view
    // (the Vita's calcScroll clamps to its 960 x 544 screen - FUN_81002e88(0x3c0, 0x220) - so the
    // view still reaches the painting's edges: the ship's side, the player at its bottom right)
    cr::Renderer& r = app.renderer;
    const int keepLayer = r.layer();
    r.setLayer(3);
    const float k = r.phoneLayout() ? r.layoutScale() : 1.0f;
    float cx = r.screenW() / 2, cy = cr::Renderer::kLogicalH / 2.0f;
    r.layoutToScreen(cx, cy); // (the Vita's pictures also sit 12 of its pixels up: the screen's centre is that much lower in the view)
    const float shiftY = (cy - cr::Renderer::kLogicalH / 2.0f) / k;
    r.setLayer(keepLayer);
    const float SW = r.screenW() / (fz * k), H = cr::Renderer::kLogicalH / (fz * k);
    const int* rc = app.stageCam.rect;
    const bool haveRect = rc[2] > 0 && rc[3] > 0;
    const float rx = haveRect ? 2.0f * float(rc[0]) : 0, ry = haveRect ? 2.0f * float(rc[1]) : 0;
    const float rw = haveRect ? 2.0f * float(rc[2]) : V, rh = haveRect ? 2.0f * float(rc[3]) : Vh;
    auto axis = [](float v, float lo, float len, float screen) {
        return len <= screen ? lo + len / 2 : std::clamp(v, lo + screen / 2, lo + len - screen / 2);
    };
    const float tx = V / 2 - axis(vx, rx, rw, SW), ty = axis(vy - shiftY, ry, rh, H) + shiftY - Vh / 2;
    if (SDL_getenv("CR_SCROLL_DEBUG")) std::printf("scroll clamp: screen %.0f x %.0f, view %.0f x %.0f, rect %.0f %.0f %.0f %.0f\n", SW, H, V, Vh, rx, ry, rw, rh);
    app.scroll.offX += (tx - app.scroll.offX) * rate;
    app.scroll.offY += (ty - app.scroll.offY) * rate;
}

// FieldCamera::setScrollPlayer, every frame while the camera follows the player
// FieldScene::updateCamera -> setScrollPlayer(0.3): the view eases 30 % of the way to the player's
// head each 20 Hz frame; onFadingIn's is 1.0 - the first follow after a map comes in snaps
void followPlayerScroll(App& app, float rate) {
    if (!scrollingField(app) || app.cam.free || !app.havePlayer) return;
    if (app.puzzle.stage >= 0 && app.puzzle.scroll.state == 1) return; // (ScrollCamera's: FieldCamera + 0 cleared, the view held where it was taken)
    app.scroll.scripted = false;
    if (app.scroll.snap) rate = 1.0f, app.scroll.snap = false;
    const cr::Vec3 head = shownWalker(app).pos + cr::Vec3{0, 7.0f + float(app.stageCam.lookHeight), 0};
    float vx, vy;
    if (fieldViewPoint(app, head, vx, vy)) scrollFieldTo(app, vx, vy, rate);
    if (SDL_getenv("CR_SCROLL_DEBUG"))
        std::printf("scroll: head view (%.0f, %.0f) of %.0f x %.0f, rect %d %d %d %d -> offset (%.0f, %.0f)\n", vx, vy,
                    fieldViewW(app), fieldViewH(app), app.stageCam.rect[0], app.stageCam.rect[1], app.stageCam.rect[2],
                    app.stageCam.rect[3], app.scroll.offX, app.scroll.offY);
}

// ScrollAutoCtl, once a 20 Hz step with the camera free: the slide
void stepFieldScroll(App& app) {
    App::FieldScroll& fs = app.scroll;
    if (!fs.sliding) return;
    if (!scrollingField(app) || !app.cam.free) { fs.sliding = false; return; }
    ++fs.t;
    const float k = fs.frames > 0 ? std::min(1.0f, float(fs.t) / float(fs.frames)) : 1.0f;
    const float x = fs.fromX + (fs.toX - fs.fromX) * k, y = fs.fromY + (fs.toY - fs.fromY) * k;
    fs.curX = x, fs.curY = y;
    fs.offX = fs.offY = 0;
    scrollFieldTo(app, fieldViewW(app) / 2 - x, fieldViewH(app) / 2 + y, 1.0f);
    if (fs.t >= fs.frames) fs.sliding = false;
}

// The scripts' shots on a painted field: jump (frames 0) or slide to offset (x, y)
// A script's move (npc_autoctl_prc), a frame: true once it has arrived
bool stepMove(App& app, App::Move& m) {
    ++m.t;
    const float t = float(m.t) / float(m.frames);
    const cr::Vec3 p{m.from.x + (m.to.x - m.from.x) * t, m.from.y + (m.to.y - m.from.y) * t, m.from.z + (m.to.z - m.from.z) * t};
    const float yaw = m.yawFrom + (m.yawTo - m.yawFrom) * t;
    setActor(app, m.npc, p, m.turn ? &yaw : nullptr, m.mode == 2);
    return m.t >= m.frames;
}
bool stepRescale(App& app, App::Rescale& r) { // (npc_autoctl_prc mode 3: the scale in a straight line)
    ++r.t;
    const float t = float(r.t) / float(r.frames);
    if (cr::FieldNpc* n = findNpc(app, r.npc)) {
        for (int k = 0; k < 3; ++k) n->axes[k] = r.from[k] + (r.to[k] - r.from[k]) * t;
        for (auto& d : app.npcDraws)
            if (d.id == r.npc) d.world = npcWorld(*n);
    }
    return r.t >= r.frames;
}
void stepScriptCam(App& app) { // (EvAutoCtl / CvAutoCtl: camera moves are straight lines)
    if (app.cam.t >= app.cam.frames) return;
    App::ScriptCam& c = app.cam;
    ++c.t;
    const float t = float(c.t) / float(c.frames);
    c.eye = c.eyeFrom + (c.eyeTo - c.eyeFrom) * t;
    c.target = c.tgtFrom + (c.tgtTo - c.tgtFrom) * t;
}

void scriptFieldScroll(App& app, float x, float y, int frames) {
    App::FieldScroll& fs = app.scroll;
    if (frames <= 0) {
        fs.sliding = false;
        fs.curX = x, fs.curY = y, fs.scripted = true;
        fs.offX = fs.offY = 0;
        scrollFieldTo(app, fieldViewW(app) / 2 - x, fieldViewH(app) / 2 + y, 1.0f);
        return;
    }
    fs.fromX = fs.scripted ? fs.curX : fs.offX;
    fs.fromY = fs.scripted ? fs.curY : fs.offY;
    fs.scripted = true;
    fs.toX = x;
    fs.toY = y;
    fs.t = 0;
    fs.frames = frames;
    fs.sliding = true;
    stepFieldScroll(app); // (ScrollAutoCtl's first step: this frame's, after the script - see stepEvents)
}

// ---- Video settings ----

// Resolutions offered (up to 3840x2160); the window list leaves out what doesn't fit the display
constexpr int kResolutions[][2] = {{1024, 768}, {1152, 768}, {1280, 720}, {1280, 960}, {1366, 768}, {1440, 960}, {1600, 900}, {1600, 1200},
                                   {1920, 1080}, {1920, 1440}, {2560, 1440}, {2880, 2160}, {3200, 1800}, {3840, 2160}};
constexpr int kRenderScales[] = {50, 67, 75, 100, 125, 150, 200};

int keyValue(const char* text, const char* key, int fallback) {
    const char* at = SDL_strstr(text, key);
    return at ? SDL_atoi(at + SDL_strlen(key)) : fallback;
}

void loadVideo(App& app, const char* text) {
    App::Video& v = app.video;
    v.width = std::clamp(keyValue(text, "\nwidth=", v.width), 320, 3840);
    v.height = std::clamp(keyValue(text, "\nheight=", v.height), 240, 2160);
    v.display = v.savedDisplay = std::clamp(keyValue(text, "\ndisplay=", v.display), 0, 2);
    v.fullscreenKind = std::clamp(keyValue(text, "\nfullscreen_kind=", v.display ? v.display : v.fullscreenKind), 1, 2);
    v.widescreen = keyValue(text, "\nwidescreen=", v.widescreen) != 0;
    // painted_fill= 0 / 1 (Original / Fill); the old painted_rooms= (0 Original, 1 Whole, 2 Fill Width) and
    // zoom_paintings=: their Fill Width becomes Fill, Whole the original
    const int legacy = keyValue(text, "\npainted_rooms=", keyValue(text, "\nzoom_paintings=", 0) ? 2 : 0);
    v.paintings = std::clamp(keyValue(text, "\npainted_fill=", legacy == 2 ? 1 : 0), 0, 1);
    v.renderScale = std::clamp(keyValue(text, "\nrender_scale=", v.renderScale), 25, 400);
    v.msaa = std::clamp(keyValue(text, "\nmsaa=", v.msaa), 1, 8);
    v.vsync = keyValue(text, "\nvsync=", v.vsync) != 0;
    v.frameLimit = std::clamp(keyValue(text, "\nframe_limit=", v.frameLimit), 0, 1000);
    v.upscaler = std::clamp(keyValue(text, "\nupscaler=", v.upscaler), 0, kUpscalers - 1);
    v.upscaleMode = std::clamp(keyValue(text, "\nupscale_mode=", v.upscaleMode), 0, 4);
    v.sharpness = std::clamp(keyValue(text, "\nsharpness=", v.sharpness), 0, 100);
    v.hdTextures = keyValue(text, "\nhd_textures=", v.hdTextures) != 0;
    v.avoidCutout = keyValue(text, "\navoid_cutout=", v.avoidCutout) != 0;
    v.cutoutBars = keyValue(text, "\ncutout_bars=", v.cutoutBars) != 0;
    v.smoothModels = keyValue(text, "\nsmooth_models=", v.smoothModels);
    v.hdModels = keyValue(text, "\nhd_models=", v.hdModels) != 0;
    if (v.smoothModels != 2 && v.smoothModels != 3) v.smoothModels = 0;
    {
        const int a = keyValue(text, "\nanisotropy=", v.anisotropy);
        v.anisotropy = a >= 16 ? 16 : a >= 8 ? 8 : a >= 4 ? 4 : a >= 2 ? 2 : 1;
    }
    v.sharpTextures = keyValue(text, "\nsharp_textures=", v.sharpTextures) != 0;
    v.layout = std::clamp(keyValue(text, "\nlayout=", v.layout), 0, 4);
    app.showHud = keyValue(text, "\ncontrol_hints=", app.showHud) != 0;
    app.environmentEffects = keyValue(text, "\nenvironment_effects=", app.environmentEffects) != 0;
    app.lockOrientation = keyValue(text, "\nlock_orientation=", app.lockOrientation) != 0;
    app.lockLandscapeFlipped = keyValue(text, "\nlock_landscape_flipped=", app.lockLandscapeFlipped) != 0;
    if (const char* at = SDL_strstr(text, "\npad_confirm=")) app.padConfirmEast = SDL_strncmp(at + 13, "south", 5) != 0;
    app.fieldSpeed = std::clamp(keyValue(text, "\nfield_speed=", app.fieldSpeed), 1, 5);
    if (const char* at = SDL_strstr(text, "\ncheats=")) { // (Options' Cheats: hp,mp,oz,first strike,no battles,one hit,exp)
        int v[8] = {0, 0, 0, 0, 0, 0, 1, 0};
        std::sscanf(at + 8, "%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        App::Cheats& ch = app.cheats;
        ch.hp = v[0], ch.mp = v[1], ch.oz = v[2], ch.firstStrike = v[3], ch.noEncounters = v[4], ch.oneHit = v[5];
        ch.exp = v[6] == 2 || v[6] == 3 || v[6] == 5 || v[6] == 10 ? v[6] : 1;
        ch.fullBreak = v[7];
    }
    app.battleSpeed = std::clamp(keyValue(text, "\nbattle_speed=", app.battleSpeed), 1, 5);
}

// Keep the preferred landscape direction in the same settings file as the lock.
// SDL's hint constrains subsequent Android window updates; the Activity call
// applies the change immediately, without recreating the window or GL context.
#ifdef SDL_PLATFORM_ANDROID
// The camera cutout's safe insets from Android's DisplayCutout (GameActivity.cutoutInsets), the window's pixels;
// asked twice a second, kept between (false: not known) - from CR1-U fe07107, a player's report: the notch over
// the game with Avoid on (SDL 3.4's safe area zeroed on Android 11 - 14 as it goes fullscreen, none before 11)
static bool androidCutout(float out[4]) {
    static float last[4] = {0, 0, 0, 0};
    static bool known = false;
    static Uint64 asked = 0;
    const Uint64 now = SDL_GetTicks();
    if (asked == 0 || now - asked >= 500) {
        asked = now;
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env && activity) {
            jclass cls = env->GetObjectClass(activity);
            jmethodID method = cls ? env->GetMethodID(cls, "cutoutInsets", "()[I") : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear(), method = nullptr;
            auto arr = method ? static_cast<jintArray>(env->CallObjectMethod(activity, method)) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear(), arr = nullptr;
            const bool wasKnown = known;
            known = false;
            if (arr && env->GetArrayLength(arr) == 4) {
                jint v[4];
                env->GetIntArrayRegion(arr, 0, 4, v);
                if (!wasKnown || v[0] != jint(last[0]) || v[1] != jint(last[1]) || v[2] != jint(last[2]) || v[3] != jint(last[3]))
                    SDL_Log("screen: Android's camera cutout %d,%d,%d,%d px (left, top, right, bottom)", int(v[0]), int(v[1]), int(v[2]), int(v[3]));
                for (int k = 0; k < 4; ++k) last[k] = float(v[k]);
                known = true;
            }
            if (arr) env->DeleteLocalRef(arr);
            if (cls) env->DeleteLocalRef(cls);
            env->DeleteLocalRef(activity);
        }
    }
    for (int k = 0; k < 4; ++k) out[k] = last[k];
    return known;
}
#endif

void applyOrientation(App& app, bool captureDirection) {
    if (captureDirection && app.window) {
        const auto orientation = SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(app.window));
        if (orientation == SDL_ORIENTATION_LANDSCAPE || orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
            app.lockLandscapeFlipped = orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    }
    const char* hint = !app.lockOrientation ? "LandscapeLeft LandscapeRight" :
        app.lockLandscapeFlipped ? "LandscapeRight" : "LandscapeLeft";
    SDL_SetHint(SDL_HINT_ORIENTATIONS, hint);
#ifdef SDL_PLATFORM_ANDROID
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetMethodID(cls, "setOrientationLock", "(ZZ)V") : nullptr;
    if (method) env->CallVoidMethod(activity, method, jboolean(app.lockOrientation), jboolean(app.lockLandscapeFlipped));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not apply the screen orientation lock");
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
}

// Auto's layout on an iPhone or iPad: the screen in use - its short side under 540 points (an iPhone's
// 375..440, the iPhone Duo's cover screen 466) the iPhone layout, else (an iPad's 744 on, the Duo
// unfolded 626) the iPad's - the original's two releases' screens; a foldable changes it as it folds
// (SDL_AppEvent: the window resized)
bool smallScreen(const App& app) {
#ifdef SDL_PLATFORM_IOS
    int w = 0, h = 0;
    return app.window && SDL_GetWindowSize(app.window, &w, &h) && w > 0 && h > 0 && std::min(w, h) < 540;
#else
    (void)app;
    return false;
#endif
}
bool autoPhoneLayout(const App& app) { return app.phoneData || app.vitaOnly || smallScreen(app); }

// Window size / fullscreen mode and the renderer's options from app.video
void applyVideo(App& app) {
    App::Video& v = app.video;
    cr::Renderer& r = app.renderer;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    // (a phone's or tablet's: no resolution, window or aspect choice - the whole screen at its own pixels, the
    // picture widening to its shape; a settings file's other values, an older version's, set aside)
    v.display = 0, v.widescreen = true;
    r.setFillScreen(true);
#else
    r.setFillScreen(SDL_getenv("CR_PHONE_FILL") != nullptr); // (tests: a phone's fill on a desktop)
#endif
    r.setWidescreen(v.widescreen);
    // 2.2.0's / Omega's iPhone screens (3:2): the iPhone's data, or the Vita's alone - its art is the
    // iPhone's Retina sheets (the Vita ran the iPhone code path) - unless the iPad layout is asked for
    const bool phone = v.layout >= 2 || (v.layout == 0 && autoPhoneLayout(app));
    r.setVitaScreen(v.layout == 3 || (v.layout == 0 && app.vitaOnly)); // (its 960 x 544: the Vita alone's own)
    r.setAndroidScreen(v.layout == 4); // (the Android release's: docs/ANDROID.md)
    const bool changed = phone != r.phoneLayout();
    r.setPhoneLayout(phone);
    if (changed && app.rendererReady) { // the UI's sheets again, by chk_hd_name for this layout
        cr::Image wi;
        if (loadImage(app, "window.mct", wi, &app.androidWindow))
            app.renderer.destroy(app.windowTex), app.windowTex = app.renderer.upload(wi), noteWindowAtlas(app, wi);
        for (cr::Texture& t : app.titleArt) app.renderer.destroy(t);
    }
    r.setRenderScale(v.renderScale / 100.0f);
    r.setSamples(v.msaa);
    r.setVSync(v.vsync);
    cr::UpscaleSettings up;
    up.mode = kUpscaler[std::clamp(v.upscaler, 0, kUpscalers - 1)];
    up.ratio = kUpscaleModes[std::clamp(v.upscaleMode, 0, 4)].ratio;
    up.sharpness = v.sharpness / 100.0f;
    r.setUpscaler(up);
    cr::TextureFiltering tf;
    tf.anisotropy = v.anisotropy;
    tf.sharp = v.sharpTextures;
    // (an upscaler's scene is smaller: the mips its output would take - DLSS's and FSR's advice)
    tf.lodBias = up.mode != cr::Upscaler::Off ? -std::log2(up.ratio) : 0.0f;
    r.setFiltering(tf);
    if (!app.window) return;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    constexpr bool kPhone = true; // (a phone's or tablet's: the whole screen, the system's bars hidden - SDL's immersive mode)
#else
    constexpr bool kPhone = false;
#endif
    if (v.display == 0 && !kPhone) {
        SDL_SetWindowFullscreen(app.window, false);
        // The size in pixels: the window's in points, so divide by the display's density
        const float density = std::max(SDL_GetWindowPixelDensity(app.window), 0.25f);
        if (!SDL_SetWindowSize(app.window, int(v.width / density + 0.5f), int(v.height / density + 0.5f)))
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "window size %dx%d: %s", v.width, v.height, SDL_GetError());
    } else {
        SDL_DisplayMode mode{};
        const bool exact = v.display == 1 &&
                           SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(app.window), v.width, v.height, 0.0f,
                                                               true, &mode);
        SDL_SetWindowFullscreenMode(app.window, exact ? &mode : nullptr); // nullptr: borderless at the desktop's size
        SDL_SetWindowFullscreen(app.window, true);
    }
    SDL_SyncWindow(app.window);
    applyOrientation(app);
    // The window manager applies the size when it gets to it: wait for it (up to a second) so the
    // next frame, and captures, have it
    for (int i = 0; i < 50 && v.display == 0; ++i) {
        SDL_PumpEvents();
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
        if (std::abs(pw - v.width) <= 2 && std::abs(ph - v.height) <= 2) break;
        SDL_Delay(20);
    }
    SDL_PumpEvents();
    if (SDL_getenv("CR_VIDEO_DEBUG")) {
        int w = 0, h = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(app.window, &w, &h);
        SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
        std::fprintf(stderr, "video: asked %dx%d, window %dx%d points, %dx%d pixels, density %g, driver %s\n", v.width,
                     v.height, w, h, pw, ph, SDL_GetWindowPixelDensity(app.window), SDL_GetCurrentVideoDriver());
    }
}
// F11 / Alt+Enter (a desktop's): windowed <-> fullscreen - the kind last chosen, borderless at first -,
// the Options' Display setting itself (shown there, saved)
void toggleFullscreen(App& app) {
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    App::Video& v = app.video;
    // (for this session only: settings.ini keeps Options' Display - savedDisplay - and fullscreen_kind, #15)
    if (v.display) v.toggleKind = v.display;
    v.display = v.display ? 0 : std::clamp(v.toggleKind ? v.toggleKind : v.fullscreenKind, 1, 2);
    SDL_Log("display: %s (F11 / Alt+Enter, this session)", v.display == 0 ? "windowed" : v.display == 1 ? "fullscreen" : "borderless");
    applyVideo(app);
#else
    (void)app;
#endif
}

void stepChoice(int& value, const int* choices, int count, int dir) {
    int i = 0;
    while (i < count - 1 && choices[i] < value) ++i;
    if (choices[i] != value && dir < 0) ++i; // between two: step from the next one down
    value = choices[std::clamp(i + dir, 0, count - 1)];
}

// adjustVideo's settings: 0 resolution, 1 display, 2 aspect, 3 painted rooms, 4 render scale,
// 5 MSAA, 6 vsync, 7 the original presentation, 8 anti-aliasing (MSAA / FSR Native AA / DLAA),
// 9 upscaler, 10 upscale mode, 11 sharpness, 12 HD textures, 13 smoothed models, 14 layout,
// 15 the frame rate, 16 anisotropic filtering, 17 texture filtering

// Anti-aliasing choices: Off, MSAA 2 / 4 / 8, FSR (Native AA), DLSS (DLAA)
int aaChoice(const App::Video& v) {
    if (v.upscaler) return 3 + v.upscaler;
    return v.msaa >= 8 ? 3 : v.msaa >= 4 ? 2 : v.msaa >= 2 ? 1 : 0;
}

std::string aaName(const App::Video& v) {
    if (v.upscaler == 1) return v.upscaleMode == 0 ? "FSR Native AA" : "FSR 3.1";
    if (v.upscaler == 2) return v.upscaleMode == 0 ? "DLAA" : "DLSS";
    if (v.upscaler == 3) return v.upscaleMode == 0 ? "MetalFX AA" : "MetalFX";
    return v.msaa <= 1 ? std::string(tr("Off")) : std::to_string(v.msaa) + "x MSAA";
}

void adjustVideo(App& app, int row, int dir) {
    App::Video& v = app.video;
    switch (row) {
    case 0: { // resolution
        int i = 0;
        const int n = int(sizeof kResolutions / sizeof kResolutions[0]);
        for (int k = 0; k < n; ++k)
            if (kResolutions[k][0] == v.width && kResolutions[k][1] == v.height) i = k;
        SDL_Rect usable{0, 0, 3840, 2160};
        if (app.window) SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(app.window), &usable);
        const float density = app.window ? std::max(SDL_GetWindowPixelDensity(app.window), 0.25f) : 1.0f;
        for (int step = 0; step < n; ++step) {
            i = (i + dir + n) % n;
            const bool fits = v.display != 0 || (kResolutions[i][0] / density <= usable.w && kResolutions[i][1] / density <= usable.h);
            if (fits) break;
        }
        v.width = kResolutions[i][0];
        v.height = kResolutions[i][1];
        break;
    }
    case 1:
        v.display = v.savedDisplay = (v.display + dir + 3) % 3; // (Options': saved - F11's aren't)
        if (v.display) v.fullscreenKind = v.display, v.toggleKind = 0; // (F11's from now on)
        break;
    case 2: v.widescreen = !v.widescreen; break;
    case 3: v.paintings = (v.paintings + dir + 2) % 2; break;
    case 4: stepChoice(v.renderScale, kRenderScales, int(sizeof kRenderScales / sizeof kRenderScales[0]), dir); break;
    case 5: {
        static const int kMsaa[] = {1, 2, 4, 8};
        stepChoice(v.msaa, kMsaa, 4, dir);
        v.msaa = std::min(v.msaa, app.renderer.maxSamples());
        break;
    }
    case 6: v.vsync = !v.vsync; break;
    case 7: v = App::Video(); break; // the original presentation
    case 8: { // MSAA up to what the GPU has, then the temporal upscalers at native resolution
        const int maxMsaa = app.renderer.maxSamples();
        int c = aaChoice(v);
        for (int step = 0; step < 6; ++step) {
            c = (c + dir + 6) % 6;
            const int samples = c >= 1 && c <= 3 ? 1 << c : 1;
            if (samples <= maxMsaa) break;
        }
        if (c >= 4) {
            v.upscaler = c - 3;
            v.upscaleMode = 0;
        } else {
            v.upscaler = 0;
            v.msaa = c ? 1 << c : 1;
        }
        break;
    }
    case 9:
        do v.upscaler = (v.upscaler + dir + kUpscalers) % kUpscalers;
        while (!upscalerHere(v.upscaler));
        break;
    case 10: v.upscaleMode = (v.upscaleMode + dir + 5) % 5; break;
    case 11: v.sharpness = std::clamp(v.sharpness + dir * 5, 0, 100); break;
    case 14: v.layout = (v.layout + dir + 5) % 5; break;
    case 15: { // the frame rate: 30, 60, 120, 144, unlimited
        static const int kLimits[] = {30, 60, 120, 144, 0};
        int i = 0;
        while (i < 4 && kLimits[i] != v.frameLimit) ++i;
        v.frameLimit = kLimits[(i + dir + 5) % 5];
        break;
    }
    case 16: { // anisotropic filtering: off, 2x .. 16x (up to the GPU's)
        static const int kAniso[] = {1, 2, 4, 8, 16};
        int i = 0;
        while (i < 4 && kAniso[i] != v.anisotropy) ++i;
        const int most = std::max(app.renderer.maxAnisotropy(), 1);
        for (int step = 0; step < 5; ++step) {
            i = (i + dir + 5) % 5;
            if (kAniso[i] <= std::max(most, 1) || kAniso[i] == 1) break;
        }
        v.anisotropy = kAniso[i];
        break;
    }
    case 17: v.sharpTextures = !v.sharpTextures; break;
    case 18: v.hdModels = !v.hdModels; break; // (what loads from now on)
    case 13: v.smoothModels = (v.smoothModels == 0 ? (dir > 0 ? 2 : 3) : v.smoothModels == 2 ? (dir > 0 ? 3 : 0) : (dir > 0 ? 0 : 2)); break;
    case 12: { // HD textures: what loads from now on (the next room); the UI atlas at once
        v.hdTextures = !v.hdTextures;
        cr::Image wi;
        if (app.rendererReady && loadImage(app, "window.mct", wi, &app.androidWindow)) {
            app.renderer.destroy(app.windowTex);
            app.windowTex = app.renderer.upload(wi);
            noteWindowAtlas(app, wi);
        }
        cr::Image ci; // (the cut-in windows' atlas as well)
        if (app.rendererReady && loadImage(app, "window_cutin.mct", ci)) {
            app.renderer.destroy(app.cutinTex);
            app.cutinTex = app.renderer.upload(ci);
        }
        break;
    }
    default: break;
    }
    applyVideo(app);
    saveMix(app);
}



const char* const kOptionPageName[kOptionPages] = {N_("Game"), N_("Sound"), N_("Display"), N_("Detail"), N_("Cheats"), N_("Graphics")};

std::string sysLabel(const App& app, int id, const char* fallback) {
    const std::string t = sysText(app, id);
    return t.empty() ? fallback : t;
}

void saveImportAnswer(App& app, bool yes) {
    const bool finished = app.saveImport.state == vitaimport::Session::Result && app.saveImport.succeeded;
    app.saveImport.answer(yes, app.saveDirOverride.empty() ? cr::saveDir() : app.saveDirOverride);
    if (finished) {
        SDL_RemovePath(dataPath(app).c_str()); // the import wrote save1 / save2 / resume: fold them anew
        app.optionsOpen = false;
        app.trackerArmed = false;
        bootTitle(app);
    }
}



void refreshFanArt(App& app) {
    if (!app.rendererReady) return;
    cr::Image wi;
    if (loadImage(app, "window.mct", wi, &app.androidWindow)) { // (as HD Textures' switch does)
        app.renderer.destroy(app.windowTex);
        app.windowTex = app.renderer.upload(wi);
        noteWindowAtlas(app, wi);
        app.parchSplitFor = ~0u; // (the phone parchment split from it again)
    }
    cr::Image ci;
    if (loadImage(app, "window_cutin.mct", ci)) {
        app.renderer.destroy(app.cutinTex);
        app.cutinTex = app.renderer.upload(ci);
    }
    app.renderer.destroy(app.charSheet), app.charSheet = cr::Texture(), app.charSheetTried = false; // (the rest as they next load)
    for (cr::Texture& t : app.titleArt) app.renderer.destroy(t), t = cr::Texture();
}

std::vector<OptRow> optionRows(App& app, int page) {
    std::vector<OptRow> rows;
    auto gauge = [&](const std::string& label, int* v, int sound) {
        OptRow r;
        r.label = label;
        r.kind = OptControl::Gauge;
        r.gauge = v;
        r.value = *v;
        r.sound = sound; // hear the new level
        r.act = [&app, v](int dir) {
            *v = std::clamp((*v + 5) / 10 * 10 + dir * 10, 0, 100); // (ComUty_SideGuageProc: steps of 10)
            applyMix(app);
            saveMix(app);
        };
        rows.push_back(r);
    };
    auto onOff = [&](const std::string& label, bool* flag, std::function<void()> after) {
        OptRow r;
        r.label = label;
        r.kind = OptControl::Pair;
        r.words[0] = 33; // On
        r.words[1] = 34; // Off
        r.selected = *flag ? 0 : 1;
        r.act = [&app, flag, after](int i) {
            *flag = i == 0;
            if (after) after();
            saveMix(app);
        };
        rows.push_back(r);
    };
    auto value = [&](const std::string& label, const std::string& text, std::function<void(int)> act) {
        OptRow r;
        r.label = label;
        r.kind = OptControl::Value;
        r.valueText = text;
        r.act = std::move(act);
        rows.push_back(r);
    };
    auto pair = [&](const std::string& label, const char* a, const char* b, int selected, std::function<void(int)> act) {
        OptRow r;
        r.label = label;
        r.kind = OptControl::Pair;
        r.texts[0] = a;
        r.texts[1] = b;
        r.selected = selected;
        r.act = std::move(act);
        rows.push_back(r);
    };
    App::Video& v = app.video;
    switch (page) {
    case 0: { // Game
        if (languageSheet(app)) { // 3.1.0 (__menuu_OptionPrc 5): one Language row, its arrows through the eight
            // languages (left: 0 -> 7), the name window_char's (88 x 24 at u = language x 88)
            // (then a fan translation's - #11 -, by its name: window_char has no cell for it)
            value(sysLabel(app, 58082, "Language"), languageChoiceName(app, languageChoice(app)), [&app](int dir) {
                const int n = cr::kLanguages + 1;
                for (int k = 1; k <= n; ++k) { // (only those the data has)
                    const int c = ((languageChoice(app) + (dir < 0 ? -1 : 1) * k) % n + n) % n;
                    if (choiceUsable(app, c)) {
                        setLanguageChoice(app, c);
                        break;
                    }
                }
            });
            if (!app.fanText) rows.back().valueTex = &app.charSheet;
            const float uv[4] = {88.0f * float(app.language), 0, 88, 24};
            std::memcpy(rows.back().valueUv, uv, sizeof uv);
        } else {
        OptRow lang;
        lang.label = sysLabel(app, 58082, "Language");
        lang.kind = OptControl::Pair;
        lang.words[0] = 37; // Japanese
        lang.words[1] = 38; // English
        lang.selected = app.fanText ? -1 : app.language == 0 ? 0 : app.language == 1 ? 1 : -1;
        lang.act = [&app](int i) { setLanguageChoice(app, i); };
        rows.push_back(lang);
        value(tr("Text Language"), languageChoiceName(app, languageChoice(app)), [&app](int dir) {
            const int n = cr::kLanguages + 1;
            for (int k = 1; k <= n; ++k) {
                const int c = ((languageChoice(app) + (dir ? dir : 1) * k) % n + n) % n;
                if (choiceUsable(app, c)) {
                    setLanguageChoice(app, c);
                    break;
                }
            }
        });
        }
        { // the port's own text (platform/ui_text.hpp): Auto (the text language's), English, then each catalog's
            const std::vector<uitext::Language> langs = uitext::languages();
            const std::string shown = app.uiLang == "auto" ? trf("Auto ({0})", {uitext::languageName(uitext::language())})
                                                           : uitext::languageName(app.uiLang);
            value(tr("Menu Language"), shown.empty() ? app.uiLang : shown, [&app, langs](int dir) {
                int at = -1; // -1 Auto, else an index into langs
                for (size_t i = 0; i < langs.size(); ++i)
                    if (langs[i].code == app.uiLang) at = int(i);
                const int n = int(langs.size()) + 1;
                at = ((at + 1 + (dir ? dir : 1)) % n + n) % n - 1;
                app.uiLang = at < 0 ? "auto" : langs[size_t(at)].code;
                applyUiLanguage(app);
                saveMix(app);
            });
            rows.back().hint = tr("The language of the port's own text - these options and its messages; Auto: the text language's");
        }
        onOff(sysLabel(app, 58108, "Environment Effects"), &app.environmentEffects, nullptr);
        onOff(sysLabel(app, 58109, "Lock Screen Orientation"), &app.lockOrientation,
              [&app] { applyOrientation(app, app.lockOrientation); });
        onOff(tr("Control Hints"), &app.showHud, nullptr);
        rows.back().hint = tr("Show the control hints and status overlay. Off hides the WASD help; F3 is a shortcut.");
        value(tr("Import Vita Saves"), tr("Choose Folder"), [&app](int dir) {
            if (dir == 0 && app.optionsFromTitle) app.saveImport.begin("omega", app.window);
        });
        rows.back().enabled = app.optionsFromTitle;
        rows.back().hint = app.optionsFromTitle ? tr("Choose your decrypted Vita save export. Existing saves will be backed up.")
                                               : tr("Import saves from the title screen's Options.");
        { // the Vita copy to take assets from (a folder dialog; Left / Right: none)
            std::string shown = tr("None");
            if (!app.vitaPath.empty()) {
                std::string p = app.vitaPath;
                while (p.size() > 1 && p.back() == '/') p.pop_back();
                shown = p.substr(p.rfind('/') + 1);
                if (shown.size() > 18) shown = shown.substr(0, 17) + "~";
            }
            if (app.vitaOnly) shown = tr("The game's data");
            else if (app.vitaPath != app.vitaLoaded) shown = trf("{0} (next start)", {shown});
            else if (!app.vitaPath.empty() && !app.vita.isOpen()) shown = trf("{0} (not found)", {shown});
            value(tr("Vita Assets"), shown, [&app](int dir) {
                if (app.vitaOnly) return; // (it is the whole game here)
                if (dir == 0) chooseVitaFolder(app);
                else if (!app.vitaPath.empty()) app.vitaPath.clear(), app.vitaNote.clear(), saveMix(app);
            });
            rows.back().enabled = !app.vitaOnly && !app.androidOnly;
            rows.back().hint = app.vitaOnly ? trf("Running from the Vita copy alone: {0}", {app.vitaRoot})
                               : !app.vitaNote.empty() ? app.vitaNote
                               : tr("Enter: your Vita copy's PCSG00499 folder - its textures, voices, music and scripts; Left / Right: none");
        }
        { // the Android copy to take assets from (a folder holding its main.*.obb and APK; Left / Right: none)
            std::string shown = tr("None");
            if (!app.androidPath.empty()) {
                std::string p = app.androidPath;
                while (p.size() > 1 && p.back() == '/') p.pop_back();
                shown = p.substr(p.rfind('/') + 1);
                if (shown.size() > 18) shown = shown.substr(0, 17) + "~";
            }
            if (app.androidOnly) shown = tr("The game's data");
            else if (app.androidPath != app.androidLoaded) shown = trf("{0} (next start)", {shown});
            else if (!app.androidPath.empty() && !app.android.isOpen()) shown = trf("{0} (not found)", {shown});
            value(tr("Android Assets"), shown, [&app](int dir) {
                if (app.androidOnly) return; // (it is the whole game here)
                if (dir == 0) chooseAndroidFolder(app);
                else if (!app.androidPath.empty()) app.androidPath.clear(), app.androidNote.clear(), saveMix(app);
            });
            rows.back().enabled = !app.vitaOnly && !app.androidOnly;
            rows.back().hint = app.androidOnly ? trf("Running from the Android release alone: {0}", {app.android.path()})
                               : !app.androidNote.empty() ? app.androidNote
                               : tr("Enter: a folder with your Android copy's main.*.obb and its APK - its textures and the art "
                                    "only it has; Left / Right: none");
        }
        const auto speed = [&](const char* label, int* x, const char* what) { // (Off, 2x .. 5x)
            value(label, *x > 1 ? std::to_string(*x) + "x" : std::string(tr("Off")), [&app, x](int dir) {
                *x = (*x - 1 + (dir ? dir : 1) + 5) % 5 + 1;
                saveMix(app);
            });
            rows.back().hint = what;
        };
        speed(tr("Field Speed"), &app.fieldSpeed, tr("Walking the field 2 .. 5 times as fast - the scenes, talk and menus as they are"));
        speed(tr("Battle Speed"), &app.battleSpeed, tr("The battles 2 .. 5 times as fast"));
        OptRow swap; // the controller's ○ (east) / × (south): Off as on the Vita, ○ confirms
        swap.label = tr("Swap \u25cb and \u00d7");
        swap.kind = OptControl::Pair;
        swap.words[0] = 33; // On
        swap.words[1] = 34; // Off
        swap.selected = app.padConfirmEast ? 1 : 0;
        swap.act = [&app](int i) {
            app.padConfirmEast = i == 1;
            saveMix(app);
        };
        swap.hint = (app.pad ? trf("Controller: {0}.", {SDL_GetGamepadName(app.pad) ? SDL_GetGamepadName(app.pad) : ""}) + " " : std::string(tr("With a controller:")) + " ") +
                    (app.padConfirmEast ? tr("\u25cb (the right button) confirms, \u00d7 (the bottom one) goes back - as on the Vita")
                                        : tr("\u00d7 (the bottom button) confirms, \u25cb (the right one) goes back"));
        rows.push_back(swap);
        OptRow manual; // the topics and their pages (MuScrList + MsgUty)
        manual.label = sysLabel(app, 58081, "Instruction Manual");
        manual.act = [&app](int) { openManual(app, 0); };
        rows.push_back(manual);
        OptRow title;
        if (app.optionsFromTitle) { // ModeOptionMenu(1): erasing the save data, asked twice
            title.label = sysLabel(app, 58122, "Reset Data");
            title.act = [&app](int) { app.resetAsk = 1, app.resetConfirm = 1; };
        } else {
            title.label = sysLabel(app, 58123, "Return to Title");
            title.enabled = app.mode == Mode::Maps && !app.booting;
            title.act = [&app](int) { app.resetAsk = 3, app.resetConfirm = 1; }; // OptionPrc 0xc: "return to the title?" (57060)
        }
        rows.push_back(title);
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
        OptRow quit; // ours, the desktop's (#16): asked first, as Return to Title is
        quit.label = tr("Quit Game");
        quit.act = [&app](int) { app.resetAsk = 4, app.resetConfirm = 1; };
        quit.quitWord = true;
        rows.push_back(quit);
#endif
        break;
    }
    case 1: { // Sound
        gauge(tr("Master Volume"), &app.mix.master, int(se::kVolume));
        gauge(sysLabel(app, 58085, "BGM"), &app.mix.music, -1);
        gauge(sysLabel(app, 58086, "Sound Effects"), &app.mix.effects, int(se::kVolume));
        gauge(tr("Voice Volume"), &app.mix.voice, -2); // (-2: __MuOptionSetInfo plays battle clip 0x2b1 at the new level)
        char test[32];
        SDL_snprintf(test, sizeof test, "%03d", app.soundTestEntry);
        value(tr("Sound Test"), test, [&app](int dir) {
            const int n = std::max(1, app.sound.entries());
            if (dir == 0) app.sound.play(uint32_t(app.soundTestEntry));
            else app.soundTestEntry = ((app.soundTestEntry + dir) % n + n) % n;
        });
        rows.back().enterSounds = true;
        break;
    }
    case 2: { // Display
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        // (a phone's or tablet's: the whole screen at its own resolution and shape, always - applyVideo)
        { // Avoid (the interface clear, the picture full), Black Bars (the picture boxed clear too), Ignore
            static const char* const kCutout[] = {N_("Avoid"), N_("Black Bars"), N_("Ignore")};
            const int at = !v.avoidCutout ? 2 : v.cutoutBars ? 1 : 0;
            value(tr("Camera Cutout"), tr(kCutout[at]), [&app, at](int dir) {
                const int to = ((at + (dir ? dir : 1)) % 3 + 3) % 3;
                app.video.avoidCutout = to != 2, app.video.cutoutBars = to == 1;
                saveMix(app);
            });
        }
        rows.back().hint = tr("Avoid: the menus and buttons clear of the camera's notch or punch hole, the picture full. "
                              "Black Bars: the whole picture clear of it. Ignore: everything to the edges");
#else
        static const char* const kDisplay[] = {N_("Window"), N_("Fullscreen"), N_("Borderless")};
        value(tr("Resolution"), v.display == 2 ? tr("Desktop") : std::to_string(v.width) + " x " + std::to_string(v.height),
              [&app](int dir) { adjustVideo(app, 0, dir ? dir : 1); });
        value(tr("Display"), tr(kDisplay[v.display]), [&app](int dir) { adjustVideo(app, 1, dir ? dir : 1); });
        pair(tr("Aspect"), "4:3", tr("Wide"), v.widescreen ? 1 : 0, [&app](int i) {
            if (app.video.widescreen != (i == 1)) adjustVideo(app, 2, 1);
        });
#endif
        static const char* const kLayout[] = {N_("Auto"), "iPad", "iPhone", "Vita", "Android"};
        value(tr("Layout"), tr(kLayout[std::clamp(v.layout, 0, 4)]), [&app](int dir) { adjustVideo(app, 14, dir ? dir : 1); });
        rows.back().hint = tr("The screens' layout: the iPad release's (4:3), the iPhone's (3:2), the Vita's or the Android "
                              "release's (16:9, its screens centred); Auto follows the data");
        static const char* const kPaintings[] = {N_("Original"), N_("Fill")};
        value(tr("Painted Rooms"), tr(kPaintings[std::clamp(v.paintings, 0, 1)]), [&app](int dir) { adjustVideo(app, 3, dir ? dir : 1); });
        onOff(tr("VSync"), &v.vsync, [&app] { applyVideo(app); });
        value(tr("Frame Rate"), v.frameLimit ? std::to_string(v.frameLimit) : std::string(tr("Unlimited")),
              [&app](int dir) { adjustVideo(app, 15, dir ? dir : 1); });
        rows.back().hint = tr("Frames drawn a second at most; the game runs at its own 20 a second, shown moving between them");
        OptRow restore;
        restore.label = tr("Original Presentation");
        restore.hint = tr("Every display and graphics option back to the original's");
        restore.act = [&app](int) { adjustVideo(app, 7, 0); };
        rows.push_back(restore);
        break;
    }
    case 3: { // Detail: the textures and the models
        onOff(tr("HD Textures"), &v.hdTextures, nullptr);
        rows.back().act = [&app](int i) {
            if (app.video.hdTextures != (i == 0)) adjustVideo(app, 12, 1);
        };
        {
            const bool have = !app.hdDir.empty() && SDL_GetPathInfo(app.hdDir.c_str(), nullptr);
            rows.back().hint = have ? tr("waifu2x / Real-ESRGAN upscales from textures/hd (tools/hd_textures.py); from the next room on")
                                    : tr("None in textures/hd yet: --export-textures, then tools/hd_textures.py");
        }
        onOff(tr("HD Models"), &v.hdModels, nullptr);
        rows.back().act = [&app](int i) {
            if (app.video.hdModels != (i == 0)) adjustVideo(app, 18, 1);
        };
        {
            const bool have = !app.modelsDir.empty() && SDL_GetPathInfo(app.modelsDir.c_str(), nullptr);
            rows.back().hint = have ? tr("Replacement models (glTF) from models/hd, in place of the game's; from the next room on")
                                    : tr("None in models/hd yet: --export-models, edit in Blender, save as models/hd/<name>.glb");
        }
        static const char* const kSmooth[] = {N_("Off"), "", N_("Smooth"), N_("Smoother")};
        value(tr("Smooth Models"), tr(kSmooth[v.smoothModels]), [&app](int dir) { adjustVideo(app, 13, dir ? dir : 1); });
        rows.back().hint = v.smoothModels ? tr("Rounder models (4x / 9x triangles), hard edges kept; from the next room on")
                                          : tr("Rounds the models' silhouettes - characters, monsters, props, stages (PN triangles)");
        {
            const int most = std::max(app.renderer.maxAnisotropy(), 1);
            value(tr("Anisotropic Filtering"), v.anisotropy > 1 && most > 1 ? std::to_string(std::min(v.anisotropy, most)) + "x" : std::string(tr("Off")),
                  [&app](int dir) { adjustVideo(app, 16, dir ? dir : 1); });
            rows.back().enabled = most > 1;
            rows.back().hint = most > 1 ? tr("Textures stay sharp on floors and walls seen at an angle") : tr("Not on this GPU");
        }
        pair(tr("Texture Filtering"), tr("Smooth"), tr("Sharp"), v.sharpTextures ? 1 : 0, [&app](int i) {
            if (app.video.sharpTextures != (i == 1)) adjustVideo(app, 17, 1);
        });
        rows.back().hint = tr("Sharp: the world's textures magnified by the nearest texel (square pixels up close)");
        break;
    }
    case 4: { // Cheats (the port's own): the user's - "Infinite HP and MP. Infinite Maxed out Money, xp multipliers. always advantage"
        App::Cheats& ch = app.cheats;
        onOff(tr("Infinite HP"), &ch.hp, nullptr);
        rows.back().hint = tr("The pair's HP never drops: the damage still shows, nobody falls");
        onOff(tr("Infinite MP"), &ch.mp, nullptr);
        rows.back().hint = tr("Genes cost no MP; the pair's MP stays full");
        onOff(tr("Max Oz"), &ch.oz, nullptr);
        rows.back().hint = tr("The Oz held at the most (9,999,999)");
        static const int kExp[] = {1, 2, 3, 5, 10};
        value(tr("EXP Multiplier"), std::to_string(ch.exp) + "x", [&app](int dir) {
            int i = 0;
            for (int k = 0; k < 5; ++k)
                if (kExp[k] == app.cheats.exp) i = k;
            app.cheats.exp = kExp[(i + (dir ? dir : 1) + 5) % 5];
            saveMix(app);
        });
        rows.back().hint = tr("The EXP a won battle gives, multiplied");
        onOff(tr("Always First Strike"), &ch.firstStrike, nullptr);
        rows.back().hint = tr("Every random encounter starts with the party's first strike - never an ambush");
        onOff(tr("No Random Battles"), &ch.noEncounters, nullptr);
        rows.back().hint = tr("No random encounters on the field (the story's battles still come)");
        onOff(tr("One-Hit Kills"), &ch.oneHit, nullptr);
        rows.back().hint = tr("Any blow, gene or ability of the pair's that hurts an enemy fells it");
        onOff(tr("Full Break Gauge"), &ch.fullBreak, nullptr);
        rows.back().hint = tr("The Break gauge the pair's and full from the first turn - it never changes hands");
        break;
    }
    default: { // Graphics: what the Vulkan renderer adds
        const bool scalable = app.renderer.scalable();
        const std::string why = app.renderer.upscalerProblem(kUpscaler[std::clamp(v.upscaler, 0, kUpscalers - 1)]);
        auto unavailable = [&](OptRow& r) {
            if (v.upscaler && !why.empty()) r.hint = trf("{0} is off here: {1}", {kUpscalerName[v.upscaler], tr(why)});
        };
        value(tr("Render Scale"), scalable ? std::to_string(v.renderScale) + "%" : "100%", [&app](int dir) {
            if (app.renderer.scalable()) adjustVideo(app, 4, dir ? dir : 1);
        });
        rows.back().enabled = scalable;
        rows.back().hint = scalable ? tr("The frame at this size, then scaled to the window") : tr("Needs the Vulkan renderer");
        const std::string na = v.upscaler && !why.empty() ? " n/a" : ""; // chosen, but can't run here
        value(tr("Anti-aliasing"), scalable ? aaName(v) + na : tr("Off"), [&app](int dir) {
            if (app.renderer.scalable()) adjustVideo(app, 8, dir ? dir : 1);
        });
        rows.back().enabled = scalable;
        rows.back().hint = !scalable ? tr("Needs the Vulkan renderer")
                           : v.upscaler ? tr("Temporal: jittered frames accumulated (MSAA is off meanwhile)")
                                        : tr("MSAA, or FSR Native AA / DLAA at full resolution");
        unavailable(rows.back());
        value(tr("Upscaler"), scalable ? tr(kUpscalerName[v.upscaler]) + na : tr("Off"), [&app](int dir) {
            if (app.renderer.scalable()) adjustVideo(app, 9, dir ? dir : 1);
        });
        rows.back().enabled = scalable;
        rows.back().hint = tr("The scene renders smaller and is reconstructed; the menus stay sharp");
        unavailable(rows.back());
        value(tr("Upscale Mode"), v.upscaler == 2 && v.upscaleMode == 0 ? "DLAA" : tr(kUpscaleModes[v.upscaleMode].name),
              [&app](int dir) {
                  if (app.video.upscaler) adjustVideo(app, 10, dir ? dir : 1);
              });
        rows.back().enabled = scalable && v.upscaler != 0;
        {
            const float ratio = kUpscaleModes[v.upscaleMode].ratio;
            char h[96];
            SDL_snprintf(h, sizeof h, "The scene at %d%% of the frame per side", int(100.0f / ratio + 0.5f));
            rows.back().hint = h;
        }
        value(tr("Sharpness"), std::to_string(v.sharpness) + "%", [&app](int dir) {
            if (app.video.upscaler == 1 || app.video.upscaler == 4) adjustVideo(app, 11, dir ? dir : 1);
        });
        rows.back().enabled = scalable && (v.upscaler == 1 || v.upscaler == 4);
        rows.back().hint = tr("FSR's sharpening pass (RCAS); DLSS has none");
        pair(tr("Text Outline"), tr("Original"), tr("Fixed"), app.cleanTextOutline ? 1 : 0, [&app](int i) {
            app.cleanTextOutline = i == 1;
            app.menu.setCleanOutline(app.cleanTextOutline);
            saveMix(app);
        });
        rows.back().hint = tr("Original: eight offset draws; Fixed: one antialiased pass.");
        break;
    }
    }
    return rows;
}

// Menu units of row i: the top of its 24-tall bar; its control is centred on the bar
float optRowY(int i) { return kOptRowTop + kOptRowPitch * float(i); }
// A control's x (unscaled design units from kOptCtrlX) on screen
float optCtrlX(float x) { return kOptCtrlX + (x - kOptCtrlX) * kOptCtrlScale; }


// The page's scroll kept in range - and, `follow`, moved to show the cursor's row
void optionsScrollFix(App& app, int n, bool follow) {
    const int most = std::max(0, n - kOptVisible);
    if (follow && app.optionsRow >= 0 && app.optionsRow < n) {
        if (app.optionsRow < app.optionsScroll) app.optionsScroll = app.optionsRow;
        if (app.optionsRow >= app.optionsScroll + kOptVisible) app.optionsScroll = app.optionsRow - kOptVisible + 1;
    }
    app.optionsScroll = std::clamp(app.optionsScroll, 0, most);
}
// The scroll bar's track (menu units) and the thumb on it
void optionsBar(int n, int scroll, float& top, float& height, float& thumbY, float& thumbH) {
    top = kOptRowTop - 2, height = kOptRowPitch * float(kOptVisible - 1) + 28;
    thumbH = std::max(height * float(kOptVisible) / float(std::max(n, 1)), 24.0f);
    const int most = std::max(1, n - kOptVisible);
    thumbY = top + (height - thumbH) * float(scroll) / float(most);
}
// A point on the bar (menu y): the scroll that puts the thumb's middle there; the cursor kept on screen
void optionsBarTo(App& app, float my) {
    const int n = int(optionRows(app, app.optionsPage).size());
    float top, height, ty, th;
    optionsBar(n, app.optionsScroll, top, height, ty, th);
    const float f = std::clamp((my - top - th / 2) / std::max(height - th, 1.0f), 0.0f, 1.0f);
    app.optionsScroll = int(std::lround(f * float(std::max(0, n - kOptVisible))));
    if (app.optionsRow != kOptionsBackRow)
        app.optionsRow = std::clamp(app.optionsRow, app.optionsScroll, std::min(n, app.optionsScroll + kOptVisible) - 1);
}
// The mouse wheel / a drag over the rows: `rows` rows on; the cursor kept on screen
void optionsScrollBy(App& app, int rows) {
    const int n = int(optionRows(app, app.optionsPage).size());
    app.optionsScroll += rows;
    optionsScrollFix(app, n, false);
    if (app.optionsRow != kOptionsBackRow && n > 0)
        app.optionsRow = std::clamp(app.optionsRow, app.optionsScroll, std::min(n, app.optionsScroll + kOptVisible) - 1);
}
// Text in lines no wider than `width` (at most `most`), by words
std::vector<std::string> wrapText(cr::MenuPainter& m, const std::string& s, float width, float size, int most) {
    std::vector<std::string> lines(1);
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find(' ', at);
        if (end == std::string::npos) end = s.size();
        const std::string word = s.substr(at, end - at);
        const std::string tried = lines.back().empty() ? word : lines.back() + " " + word;
        if (!lines.back().empty() && m.textWidth(tried, size) > width) {
            if (int(lines.size()) == most) {
                lines.back() += "...";
                return lines;
            }
            lines.push_back(word);
        } else {
            lines.back() = tried;
        }
        at = end + 1;
    }
    return lines;
}

void openOptions(App& app, int page) {
    const bool first = !app.optionsOpen;
    if (first) app.optionsFromTitle = app.titleOpen, app.resetAsk = 0;
    app.optionsOpen = true;
    app.optionsPage = (page + kOptionPages) % kOptionPages;
    app.optionsRow = 0;
    app.optionsScroll = 0;
    app.optionsAnim = 0;
    // __menuu_OptionSet (3.1.0's as 1.0.0's): the first time - in any mode, the title's too - flag
    // 0x43 set and OptionPrc 0..2: MsgUty_WinOpen / SetMsg(0x28) - block 5's 40, 56040 "About
    // Option" in the manual's message window (no picture: _mwin[5]) - alone: no menu mask, the page
    // held back until the window has shut (then MuCom_MenuSetMask(1) and OptionPrc 3's slide-in).
    // (The title's flags are the save's, as the Progress Tracker's 0x44: allPairsFlags)
    if (first && !app.game.flag(0x43) && !seenFlag(app, 0x43) && !(app.titleOpen && allPairsFlags(app).flag(0x43))) {
        app.game.setFlag(0x43, true);
        markSeen(app, 0x43);
        openManual(app, 56040);
    }
}

// OptionPrc 0..2: "About Option" up (openOptions), the page and its mask not yet
bool optionsAbout(const App& app) { return app.optionsOpen && app.manualOpen && app.manualIntro; }

// The menus' coordinates are the iPhone screen's - (0, 0) its corner, not CR1's iPad 512 x 384
// menu space - so a menu sits (+16, +32) menu units further in. Omega's iPad build is that layout
// doubled and centred (tex2d_add: screen = 2 x + (32, 64), the sheets' pictures doubled with it:
// its title's logo and copyright line fill x 32..992), so for Omega this holds in both layouts.
// (CR1:U: only the iPhone layout.)
bool phoneMenus(const App&) { return true; }

// A tap's menu units under the same offset
void phoneMenuPoint(const App& app, float& mx, float& my) {
    if (phoneMenus(app)) mx -= 16, my -= 32;
}
// Widescreen (the iPad layout): the origins of a screen's left- and right-hand groups - kept to
// the screen's edges, as the later widescreen release does (and the shop and the battle here),
// the rest centred (uiX). The iPhone layout's one 480-wide screen doesn't spread
// (inside the display's safe area: off a camera hole)
float wideLeft(const App& app) {
    const cr::Renderer& r = app.renderer;
    // (no further out than a camera cutout's inset; where it reaches the interface itself the renderer draws that
    // smaller - Renderer::interfaceRect - which clears the cutout already: the layout's own place then, not pushed in twice)
    return r.phoneLayout() || r.interfaceShrink() < 1 ? r.uiX() : r.safeLeft();
}
float wideRight(const App& app) {
    const cr::Renderer& r = app.renderer;
    return r.phoneLayout() || r.interfaceShrink() < 1 ? r.uiX() : 2 * r.uiX() - r.safeRight();
}
// The Android layout (docs/ANDROID.md "The phone's edges"): how far the phone's own edges lie
// outside the centred 480 x 320 screen, in tex2d units - Android 1.1.6's gac +0x1e2688 / +0x1e268c
// (the centring offset in pixels) over +0x1e2690 (the 2D scale): (80, 20) on a 1280 x 720 phone.
// The pieces 1.1.6 puts on the phone's edges and corners add or subtract it (the HUD's badge and
// rows, the field's corner buttons, the message's arrow...); 0 in the iPhone, iPad and Vita layouts
float androidEdgeX(const App& app) {
    const cr::Renderer& r = app.renderer;
    if (!r.androidScreen()) return 0;
    // tex2d x 0 is layout x uiX + 32; the screen's left edge is layout x w/2 - w/2 / scale
    return (r.uiX() + 32 - r.screenW() * 0.5f * (1 - 1 / r.layoutScale())) / 2;
}
float androidEdgeY(const App& app) {
    const cr::Renderer& r = app.renderer;
    return r.androidScreen() ? (64 - r.layoutTop()) / 2 : 0.0f; // (tex2d y 0 is layout y 64)
}
// The Vita layout's edge groups: the Vita draws the iPhone's 480 x 320 screen at 0.85 x 2 on its 960 x
// 544 one - 564.7 tex2d units across - and moves the pieces meant for the screen's edges out by a fixed
// 43.352966 tex2d (24 sites in its decomp: __menub_mainPut's badge and rows left, the command plates
// right; __menuf_mainPut's corner, '!' and puzzle panel; _PutMsg's skip button and next mark; _PrcMsg's
// skip tap) or by a whole 43 in its tables and constants (the choice plates 328 -> 371, Back 192 -> 235,
// HELP 144 -> 187, the clock board, the scroll lists' table 0x8110d5fc) - the left-hand pieces left,
// the right-hand ones right, so each group meets the screen's edge. 0 in the other layouts
float vitaEdgeX(const App& app) { return app.renderer.vitaScreen() ? 43.352966f : 0.0f; }
float vitaEdge43(const App& app) { return app.renderer.vitaScreen() ? 43.0f : 0.0f; }
// The left / right groups' origins with the Vita's whole-number shift (layout units: 2 a tex2d unit)
float vitaLeft(const App& app) { return wideLeft(app) - 2 * vitaEdge43(app); }
float vitaRight(const App& app) { return wideRight(app) + 2 * vitaEdge43(app); }
// A logical point in menu units of the group drawn from `origin`
void menuPointAt(const App& app, float lx, float ly, float origin, float& mx, float& my) {
    mx = (lx - origin) / 2, my = ly / 2;
    phoneMenuPoint(app, mx, my);
}
bool pressIn(const App& app, int screen, float origin, float x, float y, float w, float h) {
    if (!app.press.down || app.press.screen != screen) return false;
    float lx, ly, mx, my;
    app.renderer.toLogical(app.press.px, app.press.py, lx, ly);
    menuPointAt(app, lx, ly, origin, mx, my);
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

void menuPointFit(const App& app, float lx, float ly, float& mx, float& my) {
    mx = (lx - app.renderer.uiX()) / 2, my = ly / 2;
    if (app.renderer.phoneLayout()) mx = (mx - kPhoneFitX) / kPhoneFitK, my = (my - kPhoneFitY) / kPhoneFitK;
}
// A logical point (toLogical's) in a MuCom menu's units
void menuPoint(const App& app, float lx, float ly, float& mx, float& my) {
    mx = (lx - app.renderer.uiX()) / 2, my = ly / 2;
    phoneMenuPoint(app, mx, my);
}

void drawOptions(App& app, float, float) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    if (optionsAbout(app)) return; // (OptionPrc 0..2: the message window alone - drawManual)
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    // (a translation the font can't draw - a menu language other than the text's - shown in English)
    uitext::setCoverage([&app](const std::string& s) { return app.font.covers(s); }, app.font.generation());
    const PhoneFit phoneFit(app, m); // the iPhone layout: its screen's menu coordinates
    // Behind the menu: the scene under the menu mask (drawFrame's drawMenuMask)
    r.setOrigin(r.uiX());
    // The windows slide in: rows from the left, controls from the right (WinDispOn -240 / +240)
    const float t = std::min(1.0f, app.optionsAnim / 8.0f), ease = 1 - (1 - t) * (1 - t);
    const float rowsX = -240 * (1 - ease), ctrlX = 240 * (1 - ease), a = t;
    m.picture(103, 8, 4); // "Options" (_stMuPictInfo_Option: Omega's 103, CR1's 102)
    const std::vector<OptRow> rows = optionRows(app, app.optionsPage);
    const int n = int(rows.size()), shown = std::min(n, kOptVisible);
    optionsScrollFix(app, n, false);
    { // (the port's: a darker panel under the page's name, the rows and the help line - readable
      // over any scene - and the help's lines)
        const float shade[4] = {0, 0, 0, 0.5f * a}, fade[4] = {0, 0, 0, 0.2f * a};
        const float nameW = m.textWidth(tr(kOptionPageName[app.optionsPage]), 16);
        m.quad(228, 10, nameW + 16, 24, shade, shade);
        m.quad(12, kOptRowTop - 8, 250, optRowY(shown) + 42 - kOptRowTop, shade, shade);
        m.quad(262, kOptRowTop - 8, 238, optRowY(shown) + 42 - kOptRowTop, shade, fade);
    }
    m.text(tr(kOptionPageName[app.optionsPage]), 236, 14, false, a, 16);
    for (int i = app.optionsScroll; i < n && i < app.optionsScroll + kOptVisible; ++i) {
        const OptRow& row = rows[size_t(i)];
        const float y = optRowY(i - app.optionsScroll), mid = y + 12;
        const bool cursor = app.optionsRow == i;
        const float dim = row.enabled ? a : a * 0.45f;
        // (the cursor row as the Vita shows it - white, the others 0xc8 grey (the Vita: at most 0xd2), its gauge light cyan - while the keys or a
        // pad lead; under a finger or the mouse every row alike, 3.1.0's white at 0xb3: #17)
        const bool focus = !app.pointerLast, vita = r.vitaScreen();
        m.rowBar(20 + rowsX, y, row.label, dim, false, focus && !cursor ? 0xc8 / 255.0f : 1, (vita ? 0xd2 : 0xb3) / 255.0f);
        // The control in the original's design units (buttons 8 above the bar, gauges 4 below),
        // scaled about (kOptCtrlX, the bar's middle)
        m.setScale(kOptCtrlX + ctrlX, mid, kOptCtrlScale);
        const float cx = kOptCtrlX + ctrlX;
        switch (row.kind) {
        case OptControl::Check:
            if (row.quitWord) { // ("Exit" - the title's Quit plate's word - or our "Quit" where that lettering isn't the menu's language)
                const bool art = !app.vita.isOpen() && !(app.fanText && app.fan.isOpen()) && uitext::language() == "en";
                m.button(cx + 120, y - 8, -1, art ? std::string() : std::string(tr("Quit")), false, dim, cursor && row.enabled);
                if (art) drawExitWord(app, m, cx + 130, y, dim);
            } else m.button(cx + 120, y - 8, 23, "", false, dim, cursor && row.enabled);
            break;
        case OptControl::Gauge: {
            App::GaugeEase& e = app.gaugeEase[row.gauge];
            if (e.to < 0) e.from = e.to = row.value;
            if (row.value != e.to) e.from = e.to, e.to = row.value, e.left = 10; // (a new step: from the last knob)
            const float fill = e.left > 0 ? float(e.to) + float(e.from - e.to) * float(e.left) / 10.0f : -1.0f;
            m.gauge(cx, y + 4, row.value, a, cursor && focus, fill);
            break;
        }
        case OptControl::Pair:
            for (int k = 0; k < 2; ++k)
                m.button(cx + 120 * k, y - 8, row.words[k], row.texts[k], row.selected == k, a, false);
            break;
        case OptControl::Value: // our picker: the value on a button with the Back arrows either side
            m.button(cx + 60, y - 8, -1, row.valueTex ? std::string() : row.valueText, false, dim, cursor && row.enabled);
            if (row.valueTex) // (where the button's word goes)
                m.imagePieceTo(*row.valueTex, cx + 70, y, 88, 24, row.valueUv[0], row.valueUv[1], row.valueUv[2], row.valueUv[3], dim);
            m.piece(cx + 36, y + 4, 336, 880, 16, 16, dim);
            m.piece(cx + 180, y + 4, 352, 880, 16, 16, dim);
            break;
        }
        m.setScale(0, 0, 1);
        if (cursor && row.kind == OptControl::Pair) m.text(">", kOptCtrlX + ctrlX - 12, y + 4, false, a);
    }
    if (n > kOptVisible) { // the scroll bar: a dark track, the thumb in the buttons' teal
        float top, height, ty, th;
        optionsBar(n, app.optionsScroll, top, height, ty, th);
        const float track[4] = {0.05f, 0.07f, 0.09f, 0.75f * a}, trackEdge[4] = {0.22f, 0.25f, 0.28f, 0.75f * a};
        const float lit[4] = {0.55f, 0.92f, 0.92f, a}, deep[4] = {0.16f, 0.5f, 0.55f, a};
        m.quad(kOptBarX - 1, top - 1, kOptBarW + 2, height + 2, trackEdge, trackEdge);
        m.quad(kOptBarX, top, kOptBarW, height, track, track);
        m.quad(kOptBarX + 1, ty + 1, kOptBarW - 2, th - 2, lit, deep);
    }
    // "n / total" from the game's 14 x 14 digits, and Back
    m.digit(app.optionsPage + 1, 20, kOptPageY, a);
    m.digit(-1, 34, kOptPageY, a);
    m.digit(kOptionPages, 48, kOptPageY, a);
    m.backButton(192, kOptBackY, app.optionsRow == kOptionsBackRow, app.optionsBob, a);
    if (app.optionsRow >= 0 && app.optionsRow < n && !rows[size_t(app.optionsRow)].hint.empty()) { // (under the rows shown, clear of Back)
        const std::vector<std::string> lines = wrapText(m, rows[size_t(app.optionsRow)].hint, 470, 11, 2);
        for (size_t l = 0; l < lines.size(); ++l) m.text(lines[l], 20, optRowY(shown) + 2 + 13 * float(l), false, a, 11);
    }
    if (app.showHud) // (our key hints: off in a Release build, F3)
        m.text("Up/Down  Left/Right  Enter  Q/E or PgUp/PgDn: page  Esc: back", 20, 372, false, a * 0.8f, 9);
    if (app.saveImport.active()) {
        r.setOrigin(0);
        r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 0.75f);
        r.setOrigin(r.uiX());
        m.picture(12, 0, 220, 1);
        std::string message = app.saveImport.state == vitaimport::Session::Picking
            ? "Choose the folder containing your decrypted Vita saves." : app.saveImport.message;
        std::replace(message.begin(), message.end(), '\n', ' ');
        const auto lines = wrapText(m, message, 455, 13, 8);
        for (size_t l = 0; l < lines.size(); ++l) m.text(lines[l], 24, 234 + 16 * float(l), false, 1, 13);
        if (app.saveImport.state == vitaimport::Session::Confirm) {
            m.plate(336, 104, 0, 0, app.saveImport.focus == 0, 1, false);
            m.plate(336, 160, 1, 1, app.saveImport.focus == 1, 1, false);
        } else if (app.saveImport.state == vitaimport::Session::Result) {
            m.plate(336, 104, -1, -1, true, 1, false, "OK");
        }
    }
    if (app.resetAsk) { // OptionPrc 9 / 10: the message board (57078, then 57079), Yes / No at (336, 104 / 160)
        std::vector<std::vector<std::string>> pages;
        if (app.resetAsk == 4) pages = {{std::string(tr("Quit the game?"))}}; // (ours: Quit Game)
        else app.systemText.message(app.resetAsk == 1 ? 57078 : app.resetAsk == 2 ? 57079 : 57060, textLanguage(app), nullptr, pages, nullptr);
        drawYesNoBoard(app, pages.empty() ? std::vector<std::string>{} : pages[0], app.resetConfirm); // (the rows' window goes: WinDispOff(5))
    }
    r.setOrigin(r.uiX());
}

void drawYesNoBoard(App& app, const std::vector<std::string>& lines, int cursor) {
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    r.setOrigin(0);
    r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 0.75f);
    r.setOrigin(r.uiX());
    m.picture(12, 0, 220, 1);
    for (size_t l = 0; l < lines.size(); ++l) m.text(lines[l], 24, 234 + 16.0f * float(l), false, 1, 14);
    m.plate(336, 104, 0, 0, cursor == 0, 1, false);
    m.plate(336, 160, 1, 1, cursor == 1, 1, false);
}

// One 20 Hz step: the slide-in and the Back arrows' bob (0..5 and back)
void stepOptions(App& app) {
    app.saveImport.poll();
    takeVitaFolder(app);
    takeAndroidFolder(app);
    if (!app.optionsOpen) return;
    if (optionsAbout(app)) { app.optionsAnim = 0; return; } // (the page slides in once "About Option" has shut)
    for (auto& [setting, e] : app.gaugeEase) // (SideGuageDraw's count: one a frame)
        if (e.left > 0) --e.left;
    ++app.optionsAnim;
    app.optionsBob += app.optionsBobDir;
    if (app.optionsBob >= 5 || app.optionsBob <= 0) app.optionsBobDir = -app.optionsBobDir;
}


void resetAnswer(App& app, bool yes) {
    if (yes && app.resetAsk == 1) {
        app.resetAsk = 2, app.resetConfirm = 1;
        menuSound(app, se::kChoose);
        return;
    }
    if (app.resetAsk == 4) { // Quit Game (ours, #16): Yes - the app closes, as its window's close button does
        app.resetAsk = 0;
        menuSound(app, se::kChoose);
        if (!yes) return;
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        return;
    }
    if (app.resetAsk == 3) { // Return to Title (OptionPrc 0xd / 0x18 / 0x19): Yes - black over 500 ms, the music off,
        // twelve frames, then MuCom_ReturnTitle (stepEvents); No - the page again
        app.resetAsk = 0;
        menuSound(app, se::kChoose);
        if (!yes) return;
        playBgm(app, 1u << 27);
        app.fadeGrey = 0, app.fadeFrom = 0, app.fadeTo = 1, app.fadeFrames = 10, app.fadeT = 0;
        app.titleReturnIn = 12;
        return;
    }
    app.resetAsk = 0;
    if (!yes) { menuSound(app, se::kChoose); return; }
    menuSound(app, se::kMenuBase + 13); // (__menuu_OptionPrc 10: 0x9e - Omega's numbering; 13 was CR1's)
    for (int pair = 1; pair <= 4; ++pair) SDL_RemovePath(slotPath(app, pair).c_str());
    SDL_RemovePath(dataPath(app).c_str());
    SDL_RemovePath(savePath(app, "resume.dat").c_str());
    app.game = cr::GameState();
    app.playSeconds = 0; // (prty_init_all: the play time is in the block it clears)
}

// Keys: Up/Down rows (then Back), Left/Right change, Enter activates, Q/E PgUp/PgDn pages
void optionsKey(App& app, SDL_Keycode key) {
    if (app.saveImport.active()) {
        if (app.saveImport.state == vitaimport::Session::Picking) {
            if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) app.saveImport.cancel();
        } else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
            app.saveImport.focus ^= 1;
        } else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) saveImportAnswer(app, false);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
            saveImportAnswer(app, app.saveImport.focus == 0);
        return;
    }

    if (app.resetAsk) { // Reset Data's question: Yes / No
        switch (key) {
        case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT: case SDLK_W: case SDLK_S: case SDLK_A: case SDLK_D:
            app.resetConfirm ^= 1, menuSound(app, se::kCursor);
            break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_E: resetAnswer(app, app.resetConfirm == 0); break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: resetAnswer(app, false); break;
        default: break;
        }
        return;
    }
    std::vector<OptRow> rows = optionRows(app, app.optionsPage);
    const int n = int(rows.size());
    int& row = app.optionsRow;
    auto at = [&]() -> OptRow* { return row >= 0 && row < n ? &rows[size_t(row)] : nullptr; };
    switch (key) {
    case SDLK_ESCAPE: case SDLK_BACKSPACE: app.optionsOpen = false; menuSound(app, se::kBack); return;
    case SDLK_UP: case SDLK_W: row = row == kOptionsBackRow ? n - 1 : row > 0 ? row - 1 : kOptionsBackRow; break;
    case SDLK_DOWN: case SDLK_S: row = row == kOptionsBackRow ? 0 : row + 1 < n ? row + 1 : kOptionsBackRow; break;
    case SDLK_HOME: row = 0; break;
    case SDLK_END: row = n - 1; break;
    // the original's page arrows sit on its Back bar, and OptionPrc plays the Back sound for them
    case SDLK_Q: case SDLK_PAGEUP: openOptions(app, app.optionsPage - 1); menuSound(app, se::kBack); return;
    case SDLK_E: case SDLK_PAGEDOWN: case SDLK_TAB: openOptions(app, app.optionsPage + 1); menuSound(app, se::kBack); return;
    case SDLK_LEFT: case SDLK_RIGHT: case SDLK_A: case SDLK_D: {
        OptRow* r = at();
        if (!r || !r->enabled || !r->act || r->kind == OptControl::Check) return;
        const int dir = (key == SDLK_RIGHT || key == SDLK_D) ? 1 : -1;
        if (r->kind == OptControl::Pair) r->act(dir > 0 ? 1 : 0);
        else r->act(dir);
        if (r->sound >= 0) menuSound(app, uint32_t(r->sound));
        else if (r->sound == -2) app.sound.playBattleLine(0x2b1, -77); // (the Voice gauge's sample line, as a click's: #20)
        return;
    }
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: {
        if (row == kOptionsBackRow) { app.optionsOpen = false; menuSound(app, se::kBack); return; }
        OptRow* r = at();
        if (!r || !r->enabled || !r->act) return;
        if (r->kind == OptControl::Pair) r->act(r->selected == 0 ? 1 : 0);
        else r->act(0);
        if (r->sound >= 0 && !r->enterSounds) menuSound(app, uint32_t(r->sound));
        return;
    }
    default: return;
    }
    optionsScrollFix(app, n, true);
    menuSound(app, se::kCursor);
}

// A click at frame pixel (px, py): menu units, then the control there
// Which part of Options' < Back > bar a point (menu units) is on: -1 the left arrow, 0 Back, 1 the
// right arrow, 3 the bar between them, 2 off the bar
int optionsBackPart(float mx, float my) {
    if (my < kOptBackY - 4 || my >= kOptBackY + 30 || mx < 0xc0 || mx >= 0x175 + 0x33) return 2;
    if (mx < 0xc0 + 0x33) return -1;
    if (mx >= 0x175) return 1;
    return mx >= 0xf8 && mx < 0xf8 + 0x78 ? 0 : 3;
}

void optionsClick(App& app, float px, float py) {
    float lx, ly;
    app.renderer.toLogical(px, py, lx, ly);
    float mx, my;
    menuPointFit(app, lx, ly, mx, my);
    if (app.saveImport.active()) {
        if (app.saveImport.state == vitaimport::Session::Result) {
            if (mx >= 336 && mx < 472 && my >= 104 && my < 144) saveImportAnswer(app, false);
        } else if (app.saveImport.state == vitaimport::Session::Confirm && mx >= 336 && mx < 472) {
            if (my >= 104 && my < 144) saveImportAnswer(app, true);
            else if (my >= 160 && my < 200) saveImportAnswer(app, false);
        }
        return;
    }

    if (app.resetAsk) { // the Yes / No plates (0x88 x 0x28)
        if (mx >= 336 && mx < 472 && my >= 104 && my < 144) resetAnswer(app, true);
        else if (mx >= 336 && mx < 472 && my >= 160 && my < 200) resetAnswer(app, false);
        return;
    }
    // ComUty_CmdBack 2's three parts (as the Camp's): the left arrow (0xc0, 0x33 wide) the page before,
    // Back (0xf8, 0x78) closes, the right arrow (0x175, 0x33) the page after
    if (const int part = optionsBackPart(mx, my); part != 2) {
        if (part == -1 || part == 1) { openOptions(app, app.optionsPage + part); menuSound(app, se::kBack); }
        else if (part == 0) { app.optionsOpen = false; menuSound(app, se::kBack); }
        return;
    }
    if (my >= kOptPageY - 4 && my < kOptPageY + 18 && mx >= 16 && mx < 66) { // "n / m": the next page
        openOptions(app, app.optionsPage + 1);
        menuSound(app, se::kBack);
        return;
    }
    std::vector<OptRow> rows = optionRows(app, app.optionsPage);
    if (int(rows.size()) > kOptVisible && mx >= kOptBarX - 8 && mx < kOptBarX + kOptBarW + 8) { // the scroll bar: there, and held
        float top, height, ty, th;
        optionsBar(int(rows.size()), app.optionsScroll, top, height, ty, th);
        if (my >= top - 4 && my < top + height + 4) {
            optionsBarTo(app, my);
            app.optionsBarDrag = true;
            return;
        }
    }
    for (int i = app.optionsScroll; i < int(rows.size()) && i < app.optionsScroll + kOptVisible; ++i) {
        OptRow& r = rows[size_t(i)];
        const float y = optRowY(i - app.optionsScroll);
        if (my < y - 4 || my >= y + 28) continue;
        app.optionsRow = i;
        if (!r.enabled || !r.act) return;
        auto inX = [&](float x0, float x1) { return mx >= optCtrlX(x0) && mx < optCtrlX(x1); };
        int dir = 2; // the control hit: -1 / 0 / +1, or 2 for the row's label (just moves the cursor)
        switch (r.kind) {
        case OptControl::Check: if (inX(344, 456)) dir = 0; break;
        case OptControl::Pair:
            if (inX(224, 336)) dir = 0;
            else if (inX(344, 456)) dir = 1;
            break;
        case OptControl::Gauge: { // set the value where the track was hit (knob at x + 11 + v x 2.03)
            if (!inX(224, 444) || !r.gauge) return;
            const float design = kOptCtrlX + (mx - kOptCtrlX) / kOptCtrlScale; // back to design units
            *r.gauge = std::clamp(int(std::lround((design - 235) / 2.03f / 10)) * 10, 0, 100); // (steps of 10, as the keys')
            applyMix(app);
            saveMix(app);
            if (r.sound >= 0) menuSound(app, uint32_t(r.sound));
            else if (r.sound == -2) app.sound.playBattleLine(0x2b1, -77); // (FUN_0000ab64(0x2002b1, 0); its own tag: a battle's lines go on)
            return;
        }
        case OptControl::Value:
            if (inX(260, 276)) dir = -1;
            else if (inX(404, 420)) dir = 1;
            else if (inX(284, 396)) dir = 0;
            break;
        }
        if (dir == 2) { menuSound(app, se::kCursor); return; }
        r.act(dir);
        if (r.sound >= 0 && !(dir == 0 && r.enterSounds)) menuSound(app, uint32_t(r.sound));
        else if (r.sound == -2 && dir != 0) app.sound.playBattleLine(0x2b1, -77); // (the Voice gauge's sample line)
        return;
    }
}

// Uty_TexDisp_Proc, a frame: 0 fades in over g7 frames (255 / g7 a frame), 1 holds until the
// counter passes g8, 2 fades out (255 / g9 a frame) until it reaches g7, 3 clears flag 0 and ends
void stepTexDisp(App& app) {
    App::TexDisp& t = app.texDisp;
    if (!t.on) return;
    const int in = t.g[7], hold = t.g[8], out = t.g[9];
    switch (t.state) {
    case 0:
        t.alpha = in > 0 ? 255 / in * t.counter : 0;
        if (t.counter >= in) t.alpha = 255, t.counter = 0, t.state = 1;
        break;
    case 1:
        if (hold < t.counter) t.counter = 0, t.state = 2;
        break;
    case 2:
        t.alpha = out > 0 ? std::max(0, 255 / out * (out - t.counter)) : 0;
        if (t.counter >= in) t.alpha = 0, t.counter = 0, t.state = 3;
        break;
    case 3:
        app.game.setFlag(0, false);
        t = App::TexDisp();
        return;
    default: break;
    }
    ++t.counter;
}

// The Vita's logo opcodes (FUN_81031854 / FUN_81031bee): fade in over `in` frames, hold `hold`
// (cut short by a tap or confirm), fade out over `out`, then 10 frames of black before the script
// goes on. Its pictures are full screens at their sheets' top-left, drawn to fit: sqex / mve / mclogo
// 15/16 x 10/16 of their sheet's width (the Vita's 480 x 320 of 512 x 512, centred on (240, 161);
// the iPad's 960 x 640 of 1024 x 1024, centred on (480, 322)), the warning (picture 254) 960 x 544,
// the whole of its sheet
void stepVitaLogo(App& app) {
    App::VitaLogo& l = app.vitaLogo;
    if (!l.phase) return;
    ++l.frame;
    if (l.phase == 1 && l.frame >= std::max(1, l.in)) l.phase = 2, l.frame = 0;
    else if (l.phase == 2 && (l.frame >= l.hold || l.skip)) l.phase = 3, l.frame = 0;
    else if (l.phase == 3 && l.frame >= std::max(1, l.out)) l.phase = 4, l.frame = 0;
    else if (l.phase == 4 && l.frame >= 10) {
        l.phase = 0;
        for (cr::Texture& t : l.tex) app.renderer.destroy(t);
    }
}
void drawVitaLogo(App& app) {
    const App::VitaLogo& l = app.vitaLogo;
    cr::Renderer& r = app.renderer;
    r.setOrigin(0);
    r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, 1);
    const cr::Texture& t = l.tex[std::clamp(l.slot, 0, 1)];
    const float a = l.phase == 1 ? float(l.frame) / float(std::max(1, l.in)) : l.phase == 2 ? 1.0f
                    : l.phase == 3 ? 1.0f - float(l.frame) / float(std::max(1, l.out)) : 0.0f;
    if (t.id && a > 0) {
        // the picture in its sheet, from the top-left: the warning all of it; a logo 15/16 x 10/16 of
        // the sheet's width (the Vita's 480 x 320 of 512, the iPad's 960 x 640 of 1024)
        const int s = std::clamp(l.slot, 0, 1);
        const float sw = float(t.width);
        const float pw = l.warning[s] ? sw : sw * 15 / 16, ph = l.warning[s] ? float(t.height) : sw * 10 / 16;
        const float k = std::min(r.screenW() / pw, float(cr::Renderer::kLogicalH) / ph);
        float x = (r.screenW() - pw * k) / 2, y = (cr::Renderer::kLogicalH - ph * k) / 2, w = pw * k, h = ph * k;
        // (the screen's rect: the iPhone / Vita layout's own scaling not laid over it - else the warning and the
        // logos came out 1.2 times the screen, cut at its edges, with the Vita copy as the game; CR1-U's 7875355)
        if (r.phoneLayout()) r.screenToLayout(x, y), w /= r.layoutScale(), h /= r.layoutScale();
        r.drawQuadUV(t, x, y, w, h, 0, 0, pw, ph, 1, 1, 1, a);
    }
    r.setOrigin(r.uiX());
}

// Uty_TexDisp_Put: tex2d_add(slot, x, y, u, v, w, h) - the piece (u, v) w x h of the slot's picture at tex2d (x, y),
// whatever its size (1.0.0 0x276e0: no other case). An _hd sheet holds it at twice those units (daimei01_hd's
// "CHAOS RINGS" box at (358, 297), the SD sheet's at (179, 148)), at the screen's (2x + 32, 2y + 64). The opening's
// title (j001_d05 / c101_t01: slot 1, (0, 0) 480 x 320 over the eclipse effect) was drawn - on every layout - as a
// 1024 x 768 card instead: its box at (358, 297) of the 4:3 area, up and left of the eclipse's black disc, which
// the effect centres on (512, 384) - the box showing over the ring (CRO-U #7). Now at (390, 361) 240 x 53, its
// centre on the disc's, as the reference video has it (Part 1 600 s: the title inside the black disc)
void drawTexDisp(App& app) {
    const App::TexDisp& t = app.texDisp;
    if (!t.on || t.alpha <= 0 || t.g[0] < 0 || t.g[0] >= 16) return;
    const cr::Texture& tex = app.portraits[t.g[0]].tex;
    if (!tex.id || t.g[5] <= 0 || t.g[6] <= 0) return;
    cr::Renderer& r = app.renderer;
    const float a = float(t.alpha) / 255.0f, tk = float(tex.width) / 1024.0f; // (the HD sheet's pixels: 1024 wide)
    r.drawQuadUV(tex, 32 + 2.0f * float(t.g[1]), 64 + 2.0f * float(t.g[2]), 2.0f * float(t.g[5]), 2.0f * float(t.g[6]),
                 2.0f * float(t.g[3]) * tk, 2.0f * float(t.g[4]) * tk, 2.0f * float(t.g[5]) * tk, 2.0f * float(t.g[6]) * tk, 1, 1, 1, a);
}


void stepTutoTex(App& app) {
    App::TutoTex& t = app.tutoTex;
    if (!t.on) return;
    auto finish = [&] { app.renderer.destroy(t.tex); t = App::TutoTex(); };
    if (t.state == 1) t.state = 2;
    else if (t.state == 2) {
        if (app.game.flag(4075)) {
            app.game.setFlag(4075, false);
            t.timer = 20, t.frame = 0;
            if (++t.page >= t.pages) { finish(); return; }
            app.tutoPage = std::max(app.tutoPage, t.page);
            if (const char* path = SDL_getenv("CR_TUTO_SHOT"); path && t.page == 0) { // a look (tests)
                static bool once = false;
                if (!once) { once = true; t.timer = 20; }
            }
        }
        if (!app.game.flag(3)) { app.game.setFlag(4075, false); finish(); return; }
    }
    stepTutoFrames(t);
}

void stepTutoFrames(App::TutoTex& t) { // (the frame timer alone: the manual's picture too)
    if (--t.timer < 0 && t.page >= 0 && t.page < 4) {
        const int n = t.frames[t.page];
        t.timer = ++t.frame == n - 1 ? 40 : 20;
        if (t.frame >= n) t.frame = 0;
    }
}

// MsgUty_TexDispDraw: while flag 3 is set, the page's frame (frame x 384, row x 256: 384 x 256 of
// the 2048-wide sheet) at tex2d (2x, 2y) of the HD frame - the screen's (2x + 32, 2y + 64)
void drawTutoTex(App& app) {
    if (app.game.flag(3)) drawTutoPicture(app, app.tutoTex);
}

void drawTutoPicture(App& app, const App::TutoTex& t) {
    if (!t.on || !t.tex.id || t.page < 0 || t.page >= 4 || t.frames[t.page] < 0) return;
    cr::Renderer& r = app.renderer;
    const float ox = r.uiX(), k = float(t.tex.width) / 2048.0f;
    r.setOrigin(0);
    r.drawQuadUV(t.tex, ox + 2 * t.x + 32, 2 * t.y + 64, 384, 256, 384.0f * float(t.frame) * k, 256.0f * float(t.row[t.page]) * k,
                 384 * k, 256 * k);
    r.setOrigin(ox);
}

// Uty_DrawController_Put: the cross key (1864, 1584, 144 x 144 of window_hd's pixels) at (64, 384)
// and its centre (1864, 1448, 96 x 96) at (88, 408) of the HD frame - the screen's (x + 32, y + 64)
// - until the script clears flag 0
void drawPadArt(App& app) {
    if (!app.padArt) return;
    if (!app.game.flag(0)) { app.padArt = false; return; }
    if (!app.windowTex.id) return;
    cr::Renderer& r = app.renderer;
    cr::MenuPainter& m = app.menu;
    m.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    r.setOrigin(r.uiX());
    // Uty_DrawController_Put (2.2.0's too): tex2d_add(0x2a, 32, 192, (928, 796) 96 x 96) - the grey
    // ring - and (48, 208, (944, 724) 64 x 64) - the black button; the screen's (96, 448) and (128, 480)
    m.pieceTo(48, 224, 96, 96, 928, 796, 96, 96, 1); // menu units: logical / 2
    m.pieceTo(64, 240, 64, 64, 944, 724, 64, 64, 1);
}

// Uty_DrawExclamationMark_Put: the screen dimmed (black 0xc0) and the "!" (468, 440, 44 x 48) at
// tex2d (220, 180), until the script clears flag 0
void drawExclamation(App& app) {
    if (!app.exclamation) return;
    if (!app.game.flag(0)) { app.exclamation = false; return; }
    cr::Renderer& r = app.renderer;
    r.setOrigin(0);
    r.fillRect(0, 0, r.screenW(), cr::Renderer::kLogicalH, 0, 0, 0, float(0xc0) / 255.0f);
    r.setOrigin(r.uiX());
    if (!app.windowTex.id) return;
    app.menu.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
    app.menu.piece(220 + 16, 180 + 32, 468, 440, 44, 48, 1);
}

float screenFadeAlpha(const App& app) {
    if (app.fadeFrames <= 0) return 0;
    const float t = std::min(1.0f, float(app.fadeT) / float(app.fadeFrames));
    return app.fadeFrom + (app.fadeTo - app.fadeFrom) * t;
}
float screenFadeBackAlpha(const App& app) { // op 58's quad (PutMask's second)
    if (app.fadeBackFrames <= 0) return 0;
    const float t = std::min(1.0f, float(app.fadeBackT) / float(app.fadeBackFrames));
    return app.fadeBackFrom + (app.fadeBackTo - app.fadeBackFrom) * t;
}

// The 2D is one queue sorted by draw order: ScnEffect's layer-0 overlays -120, the back fade and the
// menu mask -100 (50 on the title / save), Tex2d's portraits 50, the window texture 101, the front
// fade (Screen, op 16) 189, the message text 200, the window texture while NowLoading 500. The front
// fade: once a frame, over the portraits and the message panel, under the text (drawFrame decides
// where - the message's lines and name call it first)
void drawFrontFade(App& app) {
    if (app.mode != Mode::Maps || app.frontFadeDrawnAt == app.frameNo) return;
    app.frontFadeDrawnAt = app.frameNo;
    const float a = screenFadeAlpha(app);
    if (a <= 0.001f) return;
    cr::Renderer& r = app.renderer;
    const float keep = r.origin();
    const int layer = r.layer();
    r.setOrigin(0), r.setLayer(0);
    r.fillRect(0, 0, r.screenW(), float(cr::Renderer::kLogicalH), app.fadeGrey, app.fadeGrey, app.fadeGrey, a);
    r.setOrigin(keep), r.setLayer(layer);
    app.order2d.frontFade = ++app.order2d.seq;
}

// A model mdl_set_alpha has run on, as it stays for good (back at 255 too): every Appearance
// alpha-blended (blend mode 0), property bit 6 cleared (no colour array: glColor), its alpha test at
// most 1; mdl_put: texture unit 0 modulated, colour 1's alpha replaced by the model's
cr::Material fadedMaterial(cr::Material mt, float alpha) {
    mt.blendSrc = 4, mt.blendDst = 5;
    mt.properties &= ~0x40u;
    mt.alphaThreshold = std::min<uint8_t>(mt.alphaThreshold, 1);
    mt.texBlend = 4;
    mt.diffuse[3] = std::clamp(alpha, 0.0f, 1.0f);
    return mt;
}

// A mesh's place in the 3D draw queue: __mceQueue_addQueueDrawMesh (3.1.0 decomp.c 94504, 1.0.0
// 0x8fa80) keys each mesh (its Appearance's draw order << 1) | (its blending on), and
// __mceQueue_queueFlush (94140) sorts the whole queue - every figure's meshes together - by that key
// before drawing any (__mceQueue_qsortDq, 94252). A faded model (mdl_set_alpha) is blended throughout
int queueKey(const cr::Material& base, float alpha, bool faded) {
    const bool blended = faded || alpha < 0.999f || base.blended();
    return base.drawOrder * 2 + (blended ? 1 : 0);
}

// Draws a model's visible meshes: opaque materials first, then blended ones by draw order.
// `override`, if given, replaces every material (texture unbound); `alpha` < 1 fades it - `faded`:
// as mdl_set_alpha / mdl_put leave it (fadedMaterial), else the old way (the alpha multiplied in).
// `pass` 1 draws those of draw order under 0 alone, 2 those of 0 and over (0 all); `key` other than
// kAllKeys: only the meshes of that queue key (queueKey)
void drawLoaded(App& app, const LoadedModel& m, const cr::Mat4& mvp, const cr::Material* override, float alpha, float matTime, int pass,
                bool faded, int key) {
    const cr::Model& cpu = m.cpu;
    static const cr::Material kDefaultMaterial;
    auto baseOf = [&](size_t i) -> const cr::Material& {
        const int a = i < cpu.meshes.size() ? cpu.meshes[i].appearance : -1;
        return a >= 0 && a < int(cpu.materials.size()) ? cpu.materials[a] : kDefaultMaterial;
    };
    // `matTime` >= 0: the Appearances' own actions colour them at that time (keyType-12 RGBA in
    // segment 1, as effects); a blended one gone fully clear isn't drawn
    std::vector<cr::Material> animated;
    if (matTime >= 0 && !m.packageActions.empty()) {
        animated.resize(m.meshes.size());
        for (size_t i = 0; i < m.meshes.size(); ++i) {
            animated[i] = baseOf(i);
            const cr::Action* ma = effectMaterialAction(m, &baseOf(i));
            if (!ma || ma->segments.size() < 2) continue;
            for (const cr::Track& t : ma->segments[1]) {
                float v[4] = {1, 1, 1, 1};
                if (t.type == 12 && t.comps == 4 && cr::evaluateTrack(t, std::min(matTime, ma->duration), v))
                    for (int k = 0; k < 4; ++k) animated[i].diffuse[k] = std::clamp(v[k], 0.0f, 1.0f);
            }
        }
    }
    auto materialOf = [&](size_t i) -> const cr::Material& { return i < animated.size() ? animated[i] : baseOf(i); };
    std::vector<size_t> order(m.meshes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const cr::Material &ma = materialOf(a), &mb = materialOf(b);
        if (ma.blended() != mb.blended()) return !ma.blended();
        return ma.drawOrder < mb.drawOrder;
    });
    for (size_t i : order) {
        if (i < cpu.meshes.size() && !cpu.meshes[i].visible) continue;
        if (pass && (materialOf(i).drawOrder >= 0) != (pass == 2)) continue;
        if (key != kAllKeys && queueKey(baseOf(i), alpha, faded) != key) continue;
        if (i < animated.size() && animated[i].blended() && animated[i].diffuse[3] <= 0.004f) continue;
        const cr::GpuMesh& mesh = m.meshes[i];
        const cr::Texture* tex =
            mesh.texture >= 0 && mesh.texture < int(m.textures.size()) ? &m.textures[mesh.texture] : nullptr;
        if (override) {
            app.renderer.drawMesh(mesh, nullptr, mvp, *override);
        } else if (faded) {
            app.renderer.drawMesh(mesh, tex, mvp, fadedMaterial(materialOf(i), alpha), app.smoothFilter);
        } else if (alpha < 0.999f) {
            // mdl_set_alpha: every Appearance alpha-blended (blend mode 0), its alpha test at most 1;
            // mdl_put: texture unit 0 modulated, colour 1's alpha the model's
            cr::Material faded = materialOf(i);
            faded.diffuse[3] *= alpha;
            faded.blendSrc = 4, faded.blendDst = 5;
            faded.alphaThreshold = std::min<uint8_t>(faded.alphaThreshold, 1);
            faded.texBlend = 4;
            app.renderer.drawMesh(mesh, tex, mvp, faded, app.smoothFilter);
        } else {
            app.renderer.drawMesh(mesh, tex, mvp, materialOf(i), app.smoothFilter);
        }
    }
}

// ---- Shadows (FUN_00016008: the field's npc_shodow; the battle's fan is FUN_0001628c) ----

// A model's node by its record user id (findObject3D type 0x13: 1102 the head, 1101 the body), -1 none
int nodeOfUser(const cr::Model& m, int user) {
    for (const auto& t : m.records)
        if (t.user == user)
            for (const auto& [obj, n] : m.objectNodes)
                if (obj == t.id) return n;
    return -1;
}

// A shadow's fan (FUN_00016008 / FUN_0001628c, the field's and the battle's alike): 32 tex2d_add_tri
// (centre, point i, point i + 1) of a black ellipse - the points i / 31 turns round, the last back to
// 0 - with radii rx (the model's x) and rz (its z), turned by its yaw, at `at`; the centre at alpha
// `a`, the rim's points at 0 (the colours (a << 24, 0, 0): 3.1.0 0x161f4 / 0x1621c / 0x16224 and
// 0x16474 / 0x1649c / 0x164a4 - r5 is 0): the shadow fades out to its edge (Part 3 1112.2 s, the
// flyer's shadow on the library floor: darkest at its middle, no rim)
void addShadowFan(std::vector<float>& tris, std::vector<uint8_t>& alpha, cr::Vec3 at, float yaw, float rx, float rz, float a) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    auto point = [&](int i) {
        const float t = float(i == 32 ? 0 : i) / 31.0f * 2.0f * 3.14159265f;
        const float x = rx * std::sin(t), z = rz * std::cos(t);
        return cr::Vec3{at.x + x * cy + z * sy, at.y, at.z + z * cy - sy * x};
    };
    tris.reserve(tris.size() + 32 * 9);
    for (int i = 0; i < 32; ++i) {
        const cr::Vec3 p0 = point(i), p1 = point(i + 1);
        tris.insert(tris.end(), {at.x, at.y, at.z, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z});
    }
    const uint8_t centre = uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    for (int i = 0; i < 32; ++i) alpha.insert(alpha.end(), {centre, 0, 0});
}

// The fans into tex2d slot 0x5e (draw order 990: after the models): one dynamic mesh of black
// vertices - untextured, double-sided, depth tested, not written. `mesh` grows to hold them all
void drawShadowFans(App& app, cr::GpuMesh& mesh, const std::vector<float>& tris, const std::vector<uint8_t>& alpha, const cr::Mat4& vp) {
    if (alpha.empty()) return;
    const size_t verts = alpha.size(), room = std::max<size_t>(verts, 5 * 32 * 3);
    if (!mesh.vao || size_t(mesh.indexCount) < verts) {
        app.renderer.destroy(mesh);
        cr::Mesh m;
        m.positions.assign(room * 3, 0.0f);
        m.uvs.assign(room * 2, 0.0f);
        m.colors.assign(room * 4, 0);
        for (uint32_t i = 0; i < room; ++i) m.indices.push_back(i);
        mesh = app.renderer.upload(m);
    }
    const size_t cap = size_t(mesh.indexCount);
    std::vector<float> pos = tris, uv(cap * 2, 0.0f);
    pos.resize(cap * 3, 0.0f);
    std::vector<uint8_t> col(cap * 4, 0);
    for (size_t v = 0; v < verts && v < cap; ++v) col[v * 4 + 3] = alpha[v];
    app.renderer.updatePositions(mesh, pos, uv, col);
    cr::Material m; // (the battle's 9626: properties & ~0x10)
    m.culling = 2; // (none)
    m.properties = 8;
    m.blendSrc = 4, m.blendDst = 5;
    m.drawOrder = 990;
    app.renderer.drawMesh(mesh, nullptr, vp, m);
}

// A shadow for a model whose colour's top byte is `a255` (0 none): percent < 0 at the model's
// position (y - 0.3 + 0.1) at |percent|, else under its node 1102 (btlcamera_getpos2: that node's x and
// z - the model's own place when it has none) at the model's height + 0.1
void modelShadow(App& app, const LoadedModel& lm, const std::vector<cr::Mat4>& pose, const cr::Mat4& world,
                 float rx, float rz, int percent, float modelAlpha) {
    const int a255 = int(std::lround(std::clamp(modelAlpha, 0.0f, 1.0f) * 255.0f));
    if (a255 == 0) return; // (+0xfb4 = 0: none)
    cr::Vec3 at{world.m[12], world.m[13], world.m[14]};
    if (percent < 0) at.y += -0.3f + 0.1f;
    else {
        const int node = nodeOfUser(lm.cpu, 1102);
        if (node >= 0 && node < int(pose.size())) {
            const cr::Mat4 nw = world * pose[size_t(node)];
            at.x = nw.m[12], at.z = nw.m[14];
        }
        at.y += 0.1f;
    }
    const float yaw = std::atan2(world.m[8], world.m[10]);
    const int alpha = std::abs(percent) * a255 / 100; // ((|percent| x alpha) / 100) << 24
    if (alpha <= 0 || rx <= 0 || rz <= 0) return;
    addShadowFan(app.fieldShadowTris, app.fieldShadowAlpha, at, yaw, rx, rz, float(std::min(alpha, 255)) / 255.0f);
    ++app.shadowsDrawn;
}

// npc_shodow (draw3D, after mdl_put): the leader's (radii 3 / 3, 80%) whenever its model's colour is
// not 0, then each NPC whose table row's +18 has bit 15 (+30..+36: radii and percent)
void drawFieldShadows(App& app, const cr::Mat4& vp) {
    app.fieldShadowTris.clear(), app.fieldShadowAlpha.clear();
    if (app.havePlayer && playerShown(app) && !app.player.meshes.empty())
        modelShadow(app, app.player, app.player.world, playerWorld(shownWalker(app)), 3, 3, 80, shownPlayerAlpha(app));
    for (const App::NpcDraw& d : app.npcDraws) {
        if (d.index < 0 || d.index >= int(app.npcs.size()) || !app.npcs[size_t(d.index)].hasShadow()) continue;
        const cr::FieldNpc& n = app.npcs[size_t(d.index)];
        auto it = app.npcModels.find(d.model);
        if (it == app.npcModels.end() || it->second.meshes.empty()) continue;
        LoadedModel& lm = it->second;
        cr::Mat4 world;
        float alpha;
        shownNpc(app, d, world, alpha);
        if (n.shadow[3] >= 0 && !lm.cpu.skeleton.nodes.empty()) { // (the head where this frame's pose has it)
            const cr::Action* act = d.action >= 0 && d.action < int(lm.actions.size()) ? &lm.actions[size_t(d.action)] : nullptr;
            poseWorld(lm.cpu.skeleton, lm.actions, act, d.time, d.blend, lm.world);
        }
        modelShadow(app, lm, lm.world, world, n.shadow[1], n.shadow[2], n.shadow[3], alpha);
    }
    drawShadowFans(app, app.fieldShadows, app.fieldShadowTris, app.fieldShadowAlpha, vp);
}

// Pieces of window_hd.mct (2048 x 2048), measured from its alpha
extern const float kWinMessagePanel[4] = {0, 1408, 960, 192}; // grey message window
extern const float kWinSmallPanel[4] = {0, 1600, 512, 96}; // dark small window
// The message windows (the reference playthrough: speech with portraits up sits on the torn
// parchment, its name on the blue streak painted into it; other speech on the grey band, the
// name on a small dark tab). The skip button and the page's "next" diamond beside them.
constexpr float kWinParchment[4] = {1024, 248, 1024, 280};
// PutMsg's pieces (tex2d_add u, v, w, h on the 1024 sheet, doubled on window_hd): the skip button
// (336, 848) 32 x 32; the "next" mark - a down arrow over a bar - (512, 36) 20 x 20, olive on the
// parchment (style 2), (532, 36) white on the other windows
constexpr float kWinSkip[4] = {672, 1696, 64, 64};
constexpr float kWinNext[2][4] = {{1024, 72, 40, 40}, {1064, 72, 40, 40}};
constexpr float kWinBadge[4] = {936, 880, 88, 96};         // the "!" bubble over an NPC in reach (FollowWindow type 0: tex2d (468, 440) 44 x 48)
constexpr float kWinFlag[4] = {960, 32, 64, 64};           // FlagWindow's plain "!" over a person with news (type 1: tex2d (480, 16) 32 x 32)

// A message page in the game's message window, across the whole screen's width: the parchment
// while portraits are up, else the grey band; `next`: the page waits for a tap (the diamond)
// The page's lines at (x, y): as one block, or - when a line is centred (0x17 flips a line; the
// windowless style 0x10 centres them all unless flipped) - one by one, the centred ones about the
// layout's middle (CmdFont: tex2d x 240)
// PutMsg / CmdFont: a line every 24 tex2d units, the glyphs 16 - doubled on the iPad (and the iPhone's
// Retina screen) as all of Omega's tex2d is: 48 px a line (CR1's iPad-native layout had half that)
constexpr float kMsgScale = 4.0f;
// The name plate's line (one line: its glyphs' size from namePt)
constexpr float kNameScale = 3.1f;
// The lines' font size (_CmdFont / _CmdFont_waku's _setfont_utf16_text): 16 pt in 3.1.0; 1.0.0's
// 17, 16 in style 0x20
float messagePt(const App& app) { return multilingualData(app) || (app.msgStyle & 0x20) ? 16.0f : 17.0f; }
// The name plate's (_PrcMsg 0x15's _font_setfont_utf16_line1): 14 pt; in style 0x20 16 for
// Japanese, Chinese and Korean, 17 for the rest (both releases)
float namePt(const App& app) { return app.msgStyle & 0x20 ? (cjkLanguage(app.language) ? 16.0f : 17.0f) : 14.0f; }
void drawMessageLines(App& app, const std::vector<std::string>& page, float x, float y, float scale, bool centreAll) {
    drawFrontFade(app); // (the text at 200: over the front fade)
    app.order2d.text = ++app.order2d.seq;
    cr::Renderer& r = app.renderer;
    bool any = centreAll;
    for (const std::string& l : page) any |= !l.empty() && l[0] == '\x01';
    const std::vector<std::string> shown = typedPage(page, app.textLimit); // typing: centred on the whole line
    // (x, y) is CmdFont's: the glyphs' box (the reference video's Part 1 298 s: the lines' left
    // edge at tex2d 36) - the Hud's rasterised text has a margin round it
    const float pad = cr::Hud::pad(scale);
    if (!any) {
        app.dialogueHud.drawAt(r, shown, x - pad, y - pad, scale, 0, 0, 0.0f);
        return;
    }
    const float step = app.dialogueHud.lineStep(scale);
    for (size_t k = 0; k < page.size() && k < app.lineHud.size(); ++k) {
        const bool flip = !page[k].empty() && page[k][0] == '\x01';
        const std::string text = flip ? page[k].substr(1) : page[k];
        cr::Hud& h = app.lineHud[k];
        float tint[3];
        app.dialogueHud.tint(tint);
        h.setTint(tint[0], tint[1], tint[2]);
        const float w = h.measure(r, {text}, scale);
        const float lx = flip != centreAll ? cr::Renderer::kLogicalW / 2 - w / 2 : x - pad;
        if (k >= shown.size()) break; // (not typed yet)
        const std::string& part = shown[k];
        const std::string drawn = flip && !part.empty() ? part.substr(1) : part;
        if (!drawn.empty()) h.drawAt(r, {drawn}, lx, y - pad + step * float(k), scale, 0, 0, 0.0f);
    }
}

// The Android build's message strip for the screen's width (tex2d_setup_tex: window_cutin_480 ..
// _1366, the first at least as wide). Width is in layout units, so the separate parchment
// also works with Android art in the iPad / iPhone layouts. Wider screens stretch the largest.
int androidCutinWidth(float wv) {
    static const int kWidths[] = {480, 800, 854, 960, 1024, 1280, 1366};
    const int* w = std::find_if(std::begin(kWidths), std::end(kWidths), [&](int k) { return int(wv) <= k; });
    return w == std::end(kWidths) ? 0 : *w;
}

const cr::Texture* androidCutin(App& app, float width) {
    const int w = androidCutinWidth(std::min(width, 1366.0f));
    if (w == 0 || !app.android.isOpen()) return nullptr;
    const std::string name = "window_cutin_" + std::to_string(w) + ".mct";
    if (name != app.cutinWideName) {
        app.renderer.destroy(app.cutinWideTex);
        cr::Image img;
        if (loadImage(app, name, img)) app.cutinWideTex = app.renderer.upload(img);
        app.cutinWideName = name;
    }
    return app.cutinWideTex.id ? &app.cutinWideTex : nullptr;
}

// The phone parchment (window_hd's (1024, 240, 960, 256): the iPhone's and the Vita's, the name streak
// painted into its top-left corner) split as Android keeps it - the streak as a piece of its own (392 x 80,
// as Android's (1024, 240)) and the paper without it - so a wider screen can have the paper scaled to it and
// the streak where the iPhone puts it. The streak is told from the paper by colour (blue / green over the
// paper's beige); under it the torn edge runs on between its ends with the tear's detail from the paper
// beside it, mirrored, and the paper's own tone column by column
bool splitParchment(App& app) {
    if (app.parchSplitFor == app.windowTex.id) return app.parchPaperTex.id && app.parchStreakTex.id;
    app.parchSplitFor = app.windowTex.id;
    app.renderer.destroy(app.parchPaperTex);
    app.renderer.destroy(app.parchStreakTex);
    cr::Image wi;
    if (!loadImage(app, "window.mct", wi) || wi.rgba.empty()) return false;
    const int lw = wi.logicalW > 0 ? wi.logicalW : wi.width;
    const float k = float(wi.width) / float(std::max(lw, 1));
    auto P = [&](float u) { return int(std::lround(u * k)); };
    const int W = P(960), H = P(256), ox = P(1024), oy = P(240), BX = P(392), BY = P(80);
    if (W < 16 || ox + W > wi.width || oy + H > wi.height) return false;
    std::vector<float> px(size_t(W) * size_t(H) * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W * 4; ++x) px[size_t(y) * size_t(W) * 4 + size_t(x)] = wi.rgba[(size_t(oy + y) * size_t(wi.width) + size_t(ox)) * 4 + size_t(x)];
    auto at = [&](std::vector<float>& v, int x, int y) { return &v[(size_t(y) * size_t(W) + size_t(x)) * 4]; };
    std::vector<float> m(size_t(W) * size_t(H), 0.0f);
    int s0 = W, s1 = -1;
    for (int y = 0; y < BY; ++y)
        for (int x = 0; x < BX; ++x) {
            const float* c = at(px, x, y);
            const float v = c[3] > 8 ? std::clamp((std::max(c[1], c[2]) - c[0] - 6) / 40.0f, 0.0f, 1.0f) : 0.0f;
            m[size_t(y) * size_t(W) + size_t(x)] = v;
            if (v > 0.3f) s0 = std::min(s0, x), s1 = std::max(s1, x);
        }
    if (s1 < 0) return false;
    std::vector<float> edgeY(static_cast<size_t>(W), static_cast<float>(H));
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            if (at(px, x, y)[3] > 128) { edgeY[size_t(x)] = float(y); break; }
    const int L = 0, R = std::min(s1 + 3, W - 1), span = std::min(P(150), W - R - 2); // (from the piece's left edge)
    if (span < 10) return false;
    float refMean = 0;
    for (int i = 1; i <= span; ++i) refMean += edgeY[size_t(R + i)];
    refMean /= float(span);
    auto mirror = [](int i, int n) { const int t = i % (2 * n); return t < n ? t : 2 * n - 1 - t; };
    // the paper's broad tone per column (well inside it), smoothed across columns
    std::vector<float> band(size_t(W) * 3, 0.0f), tone(size_t(W) * 3, 0.0f);
    const int b0 = std::min(P(150), H - 2), b1 = std::min(P(250), H);
    for (int x = 0; x < W; ++x)
        for (int y = b0; y < b1; ++y)
            for (int c = 0; c < 3; ++c) band[size_t(x) * 3 + size_t(c)] += at(px, x, y)[c] / float(b1 - b0);
    const int kw = std::max(1, P(20));
    for (int x = 0; x < W; ++x)
        for (int c = 0; c < 3; ++c) {
            float sum = 0;
            for (int d = -kw; d <= kw; ++d) sum += band[size_t(std::clamp(x + d, 0, W - 1)) * 3 + size_t(c)];
            tone[size_t(x) * 3 + size_t(c)] = sum / float(2 * kw + 1);
        }
    std::vector<float> paper = px;
    const float taper = float(std::max(1, P(24))), deep = float(P(60)), blendLen = float(std::max(1, P(30)));
    for (int x = L; x < R; ++x) {
        const float w = std::min({1.0f, float(x - L + 1) / taper, float(R - x) / taper});
        const int c = R + 1 + mirror(x - L, span);
        // (Android's strip runs its torn top level under the streak, below the paper beside it and rising
        // where the tail ends - Part 1 66 s: about 45 px lower under the streak than past it; the piece's own
        // edge drops away to the left, under the portrait, so it is not followed there)
        const float drop = float(P(34)) * std::min(1.0f, float(R - x) / taper); // (level, rising where the tail ends)
        const float ex = edgeY[size_t(R)] + drop + w * (edgeY[size_t(c)] - refMean);
        for (int y = 0; y < H; ++y) {
            const float d = float(y) - ex;
            float* o = at(paper, x, y);
            if (d < -2) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            const int yc = int(std::lround(edgeY[size_t(c)] + d));
            if (yc >= H) break;
            if (yc < 0) continue;
            const float* rc = at(px, c, yc);
            const float t = d < deep ? 0.0f : std::min(1.0f, (d - deep) / blendLen); // (back into the piece's own paper deeper down)
            for (int ch = 0; ch < 3; ++ch) {
                const float v = std::clamp(rc[ch] + tone[size_t(x) * 3 + size_t(ch)] - tone[size_t(c) * 3 + size_t(ch)], 0.0f, 255.0f);
                o[ch] = v * (1 - t) + at(px, x, y)[ch] * t;
            }
            o[3] = d < deep ? rc[3] : 255.0f;
        }
    }
    app.parchTop = refMean / k;
    cr::Image pi, si;
    pi.width = W, pi.height = H, pi.rgba.resize(size_t(W) * size_t(H) * 4);
    for (size_t i = 0; i < pi.rgba.size(); ++i) pi.rgba[i] = uint8_t(std::lround(std::clamp(paper[i], 0.0f, 255.0f)));
    si.width = BX, si.height = BY, si.rgba.resize(size_t(BX) * size_t(BY) * 4);
    for (int y = 0; y < BY; ++y)
        for (int x = 0; x < BX; ++x) {
            const float* c = at(px, x, y);
            uint8_t* o = &si.rgba[(size_t(y) * size_t(BX) + size_t(x)) * 4];
            o[0] = uint8_t(c[0]), o[1] = uint8_t(c[1]), o[2] = uint8_t(c[2]);
            o[3] = uint8_t(std::lround(c[3] * m[size_t(y) * size_t(W) + size_t(x)]));
        }
    if (k != 1.0f) pi.logicalW = 960, pi.logicalH = 256, si.logicalW = 392, si.logicalH = 80;
    app.parchPaperTex = app.renderer.upload(pi);
    app.parchStreakTex = app.renderer.upload(si);
    return app.parchPaperTex.id && app.parchStreakTex.id;
}

bool drawAndroidMessagePanel(App& app, float x, float y, float w, float h, float nameX, float nameY) {
    const cr::Texture* strip = androidCutin(app, w);
    if (!strip) return false;
    app.renderer.drawQuadUV(*strip, x, y, w, h, 0, 0, std::min(w, 1366.0f), h);
    static const float kWinNamePhone[4] = {1024, 240, 392, 80};
    drawWinPiece(app, kWinNamePhone, nameX, nameY, 392, 80);
    return true;
}

// PutMsg's "next" mark, steady (no blink), by the window's style: the parchment's (2) olive, the
// others' white; at tex2d x 452 (the iPhone's safe area: 28 in from its right; the iPad's 44) and
// y 296 (the iPad 328: 48 logical above the bottom), style 4's at y 100, a tutorial band's (8) 84
// below its top; none on a window without those bits (the grey panel 0x40 alone). Android 1.1.6
// adds ox to x, and oy to y but for styles 4 and 8 (the phone's bottom right). `right` / `bottom`:
// the safe area's edges in logical units; `rectY` the style's rect y (tex2d)
void drawMessageMark(App& app, int style, int rectY, float right, float bottom) {
    const cr::Renderer& r = app.renderer;
    const float ex = 2 * androidEdgeX(app) + 2 * vitaEdgeX(app), ey = 2 * androidEdgeY(app); // (the Vita: x 495.35)
    const float x = right - (r.phoneLayout() ? 56.0f : 88.0f) + ex;
    float y;
    if (style & 2) y = bottom - 48 + ey;
    else if (style & 4) y = 2.0f * 100 + 64;
    else if (style & 8) y = 2.0f * float(rectY + 84) + 64;
    else if (style & 0x30) y = bottom - 48 + ey;
    else return;
    drawWinPiece(app, kWinNext[style & 2 ? 0 : 1], x, y, 40, 40);
}

void drawMessageBox(App& app, const std::vector<std::string>& page, const std::string& speaker, bool next,
                    const std::string& pageNo) {
    cr::Renderer& r = app.renderer;
    auto ink = [&] { // a message's 0x16 colour over the window's own
        if (app.msgColour == 1) app.dialogueHud.setTint(0, 0, 0);
        else if (app.msgColour == 0) app.dialogueHud.setTint(1, 1, 1);
    };
    const float W = cr::Renderer::kLogicalW;
    // The screen's edges in the 1024 layout's units - in the iPhone layout (2.2.0) the safe area
    // tex2d (0, 0)..(480, 320), which its corners keep to (the skip button at (444, 0), the "next"
    // mark at y 296)
    const bool phone = r.phoneLayout();
    const float H = phone ? 704.0f : float(cr::Renderer::kLogicalH), top = phone ? 64.0f : 0.0f;
    const float left = phone ? 32.0f : -r.uiX(), width = phone ? 960.0f : r.screenW();
    // Android 1.1.6's PutMsg: the "next" mark (+ ex, + ey: the phone's bottom right; beside a
    // tutorial's band + ex only) and the skip button (+ ex, - ey: its top right), 1024 units
    const float ex = 2 * androidEdgeX(app), ey = 2 * androidEdgeY(app);
    // The Vita's _PutMsg: the skip button at (487.35, 0) and the "next" mark at x 495.35 - 3.1.0's + 43.352966
    // (vita decomp 51608, 51590); its bands (draw_abeblack, draw_vista) from x - 43.35 across its whole screen;
    // the lines at x -7 when a name shows (36 without) and the name at (-12, 205) - 3.1.0's (24, 206) - or,
    // over the 0x20 band, at (-27, 236) beside the streak at x - 43.35 (vita decomp 51502-51535, 90345)
    const float vx2 = 2 * vitaEdgeX(app);
    // (the bands across the screen - draw_vista's, draw_abeblack's -: the visible screen's whole width; in the iPhone
    // layout on a screen wider than its 3:2 they stopped short at (32, 960) - CR1-U 9b36e2c. The Android and Vita
    // screens' widen by their edges, ex / vx2)
    const bool wideBand = phone && !r.androidScreen() && !r.vitaScreen();
    const float bandL = wideBand ? (r.screenW() - r.layoutRight()) - r.uiX() - 1 : left - 2 * androidEdgeX(app) - vx2,
                bandW = wideBand ? 2 * (r.layoutRight() - r.screenW() / 2) + 2 : width + 2 * (2 * androidEdgeX(app) + vx2);
    const bool vitaNamed = r.vitaScreen() && !speaker.empty();
    bool portraits = false;
    for (const App::Portrait& p : app.portraits) portraits = portraits || (p.tex.id && p.alpha > 0.5f);
    // PrcWin / PutWin: the window fades in and out (its alpha); PutMsg draws the text, the name and
    // the mark only while it is up (state 3) - a window never opened by a script draws as is
    const int ws = app.msgWin.state;
    const bool fading = ws == 1 || ws == 2 || ws == 4 || ws == 5, textOn = !fading;
    const float wa = fading ? app.msgWin.alpha : 1.0f;
    // The glyphs' sizes (the lines keep their 24 tex2d step): kMsgScale makes 2 logical px a point
    const float em = messagePt(app) / 2;
    app.dialogueHud.setEm(em);
    for (cr::Hud& h : app.lineHud) h.setEm(em);
    app.nameHud.setEm(namePt(app) * 2 / kNameScale);
    // the name's x is drawfont_text's: its glyphs' left (Part 1 298 s: "Vieg" from tex2d 24) - the
    // Hud's rasterised text has a margin round it (its y keeps the font's measured offset)
    const float namePad = cr::Hud::pad(kNameScale);
    if (!app.windowTex.id) { // no window art: a plain box
        app.dialogueHud.setTint(1, 1, 1);
        app.dialogueHud.drawAt(r, typedPage(page, app.textLimit), 60, H - 210, 2.0f, W - 120, 170);
        return;
    }
    // __EvtOpen's style picks the window (PutWin) and the text's place (PutMsg): its rect from
    // 0xd3d30 (tex2d x, y and the text's y below it; logical = 2 tex2d + (32, 64))
    const int style = app.msgStyle;
    const int base = style & 2 ? 0 : style & 4 ? 2 : style & 8 ? 4 : style & 0x10 ? 6 : style & 0x20 ? 8 : style & 0x40 ? 10 : 0;
    static const short kRect[12][2] = {{176, 60}, {176, 60}, {8, 8},  {224, 8}, {16, 16}, {108, 16},
                                       {0, 44},   {0, 0},    {0, 264}, {0, 0},  {8, 16},  {216, 16}};
    const short* rect = kRect[base | (app.msgLines == 1)];
    const float right = left + width;
    if (base != 0 || style & 0x80 || style & 0x200) {
        const float wy = 2.0f * rect[0] + 64, ty = 2.0f * (rect[0] + rect[1]) + 64;
        float tx = 2.0f * (vitaNamed ? -7 : 36) + 32;
        bool outline = false;
        if (style & 0x80 || style & 0x210) { // no window: the text alone (0x10: outlined in black -
            // _CmdFont_waku outlines white text only: after 0x16 1 it is plain black, j001_d06 6818)
            outline = (style & 0x10) != 0 && app.msgColour != 1;
        } else if (style & 0x20) { // a black band across the bottom (tex2d y 252..352), the name on its edge
            // (draw_vista; Android: x - ox .. 480 + ox, the phone's whole width)
            r.fillRect(bandL, 2.0f * 252 + 64, bandW, 768 - (2.0f * 252 + 64), 0, 0, 0, wa);
            if (!speaker.empty() && textOn) { // PutMsg: drawfont_l at tex2d (16, 236), just above the band
                // draw_vista2: the grey streak under the name - window (612, 14) 140 x 22 at tex2d
                // (0, 238), the iPad's (-16, 238); Android's at (0, 238) too (Part 1 815 s: x 160 ..)
                static const float kWinNameStreak[4] = {1224, 28, 280, 44};
                drawWinPiece(app, kWinNameStreak, 2.0f * (phone ? 0 : -16) + 32 - vx2, 2.0f * 238 + 64, 280, 44, wa);
                drawFrontFade(app); // (the name: text, over the front fade)
                const float nx = 2.0f * (r.vitaScreen() ? -27 : 16) + 32, ny = 2.0f * 236 + 64 - 11; // (the larger box's offset)
                app.nameHud.setTint(0, 0, 0); // (an outline, so it reads over a bright scene too)
                for (int k = 0; k < 4; ++k) app.nameHud.drawAt(r, {speaker}, nx - namePad + (k & 1 ? 2 : -2), ny + (k & 2 ? 2 : -2), kNameScale, 0, 0, 0.0f);
                app.nameHud.setTint(1, 1, 1);
                app.nameHud.drawAt(r, {speaker}, nx - namePad, ny, kNameScale, 0, 0, 0.0f);
            }
        } else if (style & 8) { // a tutorial's band: dark, its edges soft (8 / 88 / 8 tex2d), the text further left
            // (draw_abeblack: 0xc0 at the window's alpha; Android: x - ox .. 480 + ox, the phone's whole
            // width - Part 3 352.0 s, Part 9 653.0 s)
            const float a = 0.75f * wa, y1 = wy + 16, y2 = wy + 192, bl = bandL, bw = bandW;
            for (int k = 0; k < 8; ++k) { // the 2D path has no per-vertex colour: the edges in strips
                const float t = (float(k) + 0.5f) / 8.0f;
                r.fillRect(bl, wy + 2.0f * float(k), bw, 2.0f, 0, 0, 0, a * t);
                r.fillRect(bl, y2 + 2.0f * float(k), bw, 2.0f, 0, 0, 0, a * (1 - t));
            }
            r.fillRect(bl, y1, bw, y2 - y1, 0, 0, 0, a);
            // _PutMsg: x 8 for the languages _MuComFontSet doesn't take as CJK (3.1.0: English, French,
            // German, Spanish; 1.0.0: English), else 36
            if (multilingualData(app) ? !cjkLanguage(app.language) : app.language == 1) tx = 2.0f * 8 + 32;
        } else if (style & 0x40) { // the grey panel (960 x 192 at its rect)
            drawWinPiece(app, kWinMessagePanel, 32, wy, 960, 192, wa);
            if (!speaker.empty() && textOn) {
                drawWinPiece(app, kWinSmallPanel, 32, wy - 36, 300, 40);
                drawFrontFade(app); // (the name: text, over the front fade)
                app.nameHud.setTint(1, 1, 1);
                app.nameHud.drawAt(r, {speaker}, 62, wy - 34, 1.9f, 0, 0, 0.0f);
            }
        }
        const bool centreAll = (style & 0x10) != 0;
        if (outline && textOn) {
            app.dialogueHud.setTint(0, 0, 0);
            for (int k = 0; k < 8; ++k) {
                static const float d[8][2] = {{-2, -2}, {0, -2}, {2, -2}, {-2, 0}, {2, 0}, {-2, 2}, {0, 2}, {2, 2}};
                drawMessageLines(app, page, tx + d[k][0], ty + d[k][1], kMsgScale, centreAll);
            }
        }
        // _SetMsg: the text black (0xff000000), white for the styles with bits 0x78
        if (style & 0x78) app.dialogueHud.setTint(1, 1, 1);
        else app.dialogueHud.setTint(0, 0, 0);
        ink();
        if (textOn) drawMessageLines(app, page, tx, ty, kMsgScale, centreAll);
        (void)pageNo; // (PutMsg draws no page count)
        if (next && textOn) drawMessageMark(app, style, rect[0], right, H);
        if (app.runner.running() && !app.messageSkip) drawWinPiece(app, kWinSkip, right - 72 + ex + vx2, top - ey, 64, 64);
        return;
    }
    (void)portraits;
    float textY;
    float textX = left + 68;
    if (phone) { // 2.2.0 (FUN_0001c940, the Retina path): the parchment's 480 x 128 - window_hd
        // (1024, 240, 960, 256) - at tex2d (0, y + 16); the name (drawfont_text) at (24, 206), the
        // lines at (36, y + text y) - no +18 - (the port's font offsets as for the iPad)
        static const float kWinParchmentPhone[4] = {1024, 240, 960, 256};
        const short* pr = kRect[app.msgLines == 1];
        const float stripW = r.screenW() / r.layoutScale();
        const float stripY = 2.0f * pr[0] + 20 + 64;
        const float stripBottom = r.layoutTop() + cr::Renderer::kLogicalH / r.layoutScale();
        // Android's atlas needs its separate full-width strip and overlaid name streak even
        // when the selected layout is iPhone (including Auto), rather than Android.
        float parchK = 1, parchX = 32;
        const float parchY = 2.0f * (pr[0] + 16) + 64;
        if (!((app.androidWindow || r.androidScreen()) && !fading &&
              drawAndroidMessagePanel(app, (W - stripW) / 2, stripY, stripW, stripBottom - stripY,
                                      32, 2.0f * (pr[0] + 16) + 64))) {
            // (the Vita's PutWin, FUN_8106daf4's Retina path, stretches the piece across its wider screen,
            // from x - 43.35 at 566.7 wide. The port scales it alike on both axes, to the screen's width
            // from its left edge - no distortion, the bottom reaching the screen's; the name follows
            // its streak)
            const float x0 = std::min(32.0f, std::floor((W - stripW) / 2)), x1 = std::max(992.0f, std::ceil((W + stripW) / 2));
            const float K = (x1 - x0) / 960.0f;
            if (K > 1.001f && splitParchment(app)) { // (the paper scaled, the streak where the iPhone has it - Android's way)
                const float top = app.parchTop * (K - 1); // (scaled about its top edge: the edge stays where the streak expects it)
                app.renderer.drawQuadUV(app.parchPaperTex, x0, parchY - top, 960 * K, 256 * K, 0, 0, 960, 256, 1, 1, 1, wa);
                // (the Vita's streak - part of its stretched piece - starts at the screen's edge, x - 43.35)
                parchX = 32 - vx2;
                app.renderer.drawQuadUV(app.parchStreakTex, parchX, parchY, 392, 80, 0, 0, 392, 80, 1, 1, 1, wa);
            } else {
                parchK = K, parchX = x0;
                drawWinPiece(app, kWinParchmentPhone, x0, parchY, 960 * parchK, 256 * parchK, wa);
            }
        }
        if (!speaker.empty() && textOn) {
            drawFrontFade(app); // (the name: text, over the front fade)
            app.nameHud.setTint(1, 1, 1);
            // (the Vita's at (-12, 205): from the streak's left edge, x - 43.35)
            const float nameX = r.vitaScreen() ? -12.0f + vx2 / 2 : 24.0f, nameY = r.vitaScreen() ? 205.0f : 206.0f;
            const float nx = parchX + 2.0f * nameX * parchK, ny = parchY + (2.0f * nameY + 64 - parchY) * parchK;
            // (5 units lower than PutMsg's (24, 206): the port's font sits higher in the streak than
            // Android's - Part 1 66 s, the name's top 20 px under the streak's, the port's 13)
            app.nameHud.drawAt(r, {speaker}, nx - namePad, ny - 25 + 5, kNameScale, 0, 0, 0.0f);
        }
        app.dialogueHud.setTint(0.12f, 0.10f, 0.08f);
        textX = 2.0f * (vitaNamed ? -7 : 36) + 32;
        textY = 2.0f * (pr[0] + pr[1]) + 64;

    } else if (app.cutinTex.id) { // Omega's iPad (PutWin): window_cutin's (0, 112, 512, 144) at tex2d (x - 16,
        // y + 32) - the screen's bottom 288; PutMsg: the name at tex2d (24, 222), the lines at x 36
        // from y + text y + 18, the "next" mark at y 328
        const short* pr = kRect[app.msgLines == 1];
        app.renderer.drawQuadUV(app.cutinTex, left, 2.0f * (pr[0] + 32 - 32) + 128, width, 288, 0, 112, 512, 144, 1, 1, 1, wa);
        if (!speaker.empty() && textOn) {
            drawFrontFade(app); // (the name: text, over the front fade)
            app.nameHud.setTint(1, 1, 1);
            app.nameHud.drawAt(r, {speaker}, 2.0f * 24 + 32 - namePad, 2.0f * 222 + 64 - 25, kNameScale, 0, 0, 0.0f);
        }
        app.dialogueHud.setTint(0.12f, 0.10f, 0.08f); // ink
        textX = 2.0f * 36 + 32;
        textY = 2.0f * (pr[0] + pr[1] + 18) + 64;
    } else if (true) { // the parchment (style 2, and the default)
        drawWinPiece(app, kWinParchment, left, H - 280, width, 280, wa);
        if (!speaker.empty() && textOn) {
            drawFrontFade(app); // (the name: text, over the front fade)
            app.nameHud.setTint(1, 1, 1);
            app.nameHud.drawAt(r, {speaker}, left + 44 - namePad, H - 285, kNameScale, 0, 0, 0.0f); // on the streak (piece y 14..52)
        }
        app.dialogueHud.setTint(0.12f, 0.10f, 0.08f); // ink
        textY = H - 196;
    } else {
        drawWinPiece(app, kWinMessagePanel, left, H - 170, width, 170);
        if (!speaker.empty()) {
            drawWinPiece(app, kWinSmallPanel, left, H - 206, 300 - left, 40);
            app.nameHud.setTint(1, 1, 1);
            app.nameHud.drawAt(r, {speaker}, left + 30, H - 204, 1.9f, 0, 0, 0.0f);
        }
        app.dialogueHud.setTint(1, 1, 1);
        textY = H - 140;
    }
    ink();
    if (textOn) drawMessageLines(app, page, textX, textY, kMsgScale, false); // text and name from the screen's left
    if (next && textOn) drawMessageMark(app, style, 0, right, H); // (steady: PutMsg doesn't blink it)
    // a scene is playing: PutMsg's skip button at tex2d (444, 0) on the iPhone, (460, -32) on the
    // iPad - 36 in from the safe area's right, at its top (Android: + ox, - oy)
    if (app.runner.running() && !app.messageSkip) drawWinPiece(app, kWinSkip, right - 72 + ex + vx2, top - ey, 64, 64);
}

void drawWinPiece(App& app, const float* piece, float x, float y, float w, float h, float alpha) {
    app.renderer.drawQuadUV(app.windowTex, x, y, w, h, piece[0], piece[1], piece[2], piece[3], 1, 1, 1, alpha);
}

// ---- Battles (btl_main) ----

// The pair's average level (the encounter tables' minimum levels)
int partyLevel(const App& app) { // fldencount_encount: the scenario pair's two records (even one held back)
    const auto& pr = app.game.pairs[size_t(std::clamp(app.game.scenario, 0, cr::kScenarios - 1))];
    auto lv = [&](int c) { return c >= 0 && c < cr::kCharacters ? app.game.party[size_t(c)].level : 1; };
    return (lv(pr[0]) + lv(pr[1])) / 2;
}

// A system text's first line (sys.evt ids >= 51000), empty if none; `name` fills string 0
std::string sysText(const App& app, int id, const std::string& name) {
    std::vector<std::vector<std::string>> pages;
    if (app.systemText.message(id, textLanguage(app), nullptr, pages, nullptr,
                               [&](int n) { return n == 0 || n == 9 ? name : std::string(); }) &&
        !pages.empty() && !pages[0].empty())
        return pages[0][0];
    return std::string();
}

// FlagWindow's type-1 window over a person: its row +22 flag set (0 = none), no bubble in its slot (in
// reach or lingering), not talked to since it was last in reach - shown, standing in the room
bool flagMarkUp(const App& app, const cr::FieldNpc& n) {
    return (n.kind & 0x10) && n.link[0] != 0 && app.game.flag(n.link[0]) && !app.examineLinger.count(n.id) &&
           !app.flagTalked.count(n.id) && n.model >= 0 && npcActive(app, n) && n.pos[1] < 900.0f;
}

// The follow windows over the field's NPCs (__menuf_mainPut, 1.0.0 32691-32732; 3.1.0 35983-36008).
// Type 0, the "!" bubble over the examinable NPC in reach: tex2d (468, 440) 44 x 48 at (x - 22, y - 24)
// ("[E]" without the window sheet). Type 1, FlagWindow's marker (init 1.0.0 80838-80884 / 3.1.0 86770-86808,
// update 80996-81250): a plain red "!" (480, 16) 32 x 32 at (x - 16, y - 16) over each person (row +14 &
// 0x10) whose row +22 flag is set - a hall pair with something new to say, the flag set by the hall's
// script and cleared when talked to; the bubble takes its slot while that one is up (in reach or
// lingering), and a person talked to has none until back in reach (state 2). `vp` maps the world to
// the 3D rectangle (rx, ry, rw, rh) of the logical screen.
void drawPrompt(App& app, const cr::Mat4& vp, float rx, float ry, float rw, float rh) {
    app.promptX = app.promptY = -1;
    if (app.runner.running() || !app.dialogue.empty() || app.itemWindowOpen) return;
    // FollowWindow::update / FlagWindow::update: the window's point is the box's top, pos + offset + (0, sizeY, 0)
    auto project = [&](const cr::FieldNpc& n, float& sx, float& sy) {
        const float k = n.model == 0 || n.model == 1 ? 1.0f : 0.0f;
        const float p[3] = {n.pos[0] + k * n.offset[0] + n.offset[0], n.pos[1] + (k + 1) * n.offset[1] + n.box[1],
                            n.pos[2] + k * n.offset[2] + n.offset[2]};
        const float* m = vp.m;
        const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (cw <= 0) return false;
        float x = rx + ((m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * 0.5f + 0.5f) * rw;
        float y = ry + (0.5f - (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * 0.5f) * rh;
        // (the view's rect is in the paintings' layer 3 - on the Vita's screen larger than the 2D's)
        cr::Renderer& r = app.renderer;
        const int keep = r.layer();
        r.setLayer(3), r.layoutToScreen(x, y), r.setLayer(0), r.screenToLayout(x, y), r.setLayer(keep);
        sx = x, sy = y;
        return true;
    };
    float sx, sy;
    if (app.windowTex.id && app.havePlayer)
        for (const cr::FieldNpc& n : app.npcs)
            if (flagMarkUp(app, n) && project(n, sx, sy))
                drawWinPiece(app, kWinFlag, sx - 32, sy - 32, 64, 64); // (480, 16) 32 x 32 at (x - 16, y - 16)
    const cr::FieldNpc* n = interactTarget(app);
    if (!n || !project(*n, sx, sy)) return;
    app.promptX = sx, app.promptY = sy; // (a tap there examines it: touchPadDown)
    if (app.windowTex.id) drawWinPiece(app, kWinBadge, sx - 44, sy - 48, 88, 96); // the "!" bubble: piece (468, 440) 44 x 48, centred
    else app.promptHud.drawAt(app.renderer, {"[E]"}, sx - 24, sy - 16, 2.0f, 0, 0, 0.8f);
}

void selectAction(App& app, int delta) {
    LoadedModel& lm = app.model;
    const int n = int(lm.actions.size()) + 1; // + rest pose
    lm.action = wrap(lm.action + 1 + delta, size_t(n)) - 1;
    lm.time = 0;
    updateTitle(app);
}

void drawCheckerboard(cr::Renderer& r) {
    const int W = cr::Renderer::kLogicalW, H = cr::Renderer::kLogicalH;
    for (int y = 0; y < H; y += 32)
        for (int x = 0; x < W; x += 32) {
            const float c = ((x + y) / 32) % 2 ? 0.25f : 0.18f;
            r.fillRect(float(x), float(y), 32, 32, c, c, c, 1);
        }
}

// FieldCamera::setCamera3D for a script's camera in a room without a painting: the stage view's
// viewport (the view doubled as fldread_prc does on the iPad - viewW x viewH screen pixels -
// centred on the 1024 x 768 screen, moved by the quake) with aspect viewW / viewH in whole
// numbers and the stage's FOV: a 1024 view is a 2048 x 2048 frame of which the screen shows the
// middle. That frustum, mapped onto the whole screen (wider screens see past its sides)
// FieldCamera::setCamera3D's near and far planes, from the live eye and target and the map (3.1.0):
// near = 1 + an offset - eye y / 10 above 200, else the eye's distance / 10 when it is over 100 and
// eye and target differ in height by over 25 (not on maps 46, 130, 86, 87), else 10; none on maps
// 19, 28, 65, 97, 102, 138 and 13, 29, 47, 66, 71, 91, 152, 174, 185, 191, 197, 208, 214; eye y / 2 on
// 15, 22, 34; 100 on 83 - and none while flag 0x1003 is set; far 2000 (map 83 4000, flag 0x1003 7000)
void fieldDepthRange(const App& app, const cr::Vec3& eye, const cr::Vec3& target, float& zNear, float& zFar) {
    const int map = app.mapNumber;
    auto in = [map](std::initializer_list<int> l) { return std::find(l.begin(), l.end(), map) != l.end(); };
    float off = 10;
    if (eye.y > 200) off = eye.y / 10;
    else {
        const cr::Vec3 d = eye - target;
        const float dist = std::sqrt(cr::dot(d, d));
        if (dist > 100 && std::fabs(eye.y - target.y) > 25 && !in({46, 130, 86, 87})) off = dist / 10;
    }
    if (in({19, 28, 65, 97, 102, 138})) off = 0;
    if (in({13, 29, 47, 66, 71, 91, 152, 174, 185, 191, 197, 208, 214})) off = 0;
    if (in({15, 22, 34})) off = eye.y * 0.5f;
    if (map == 83) off = 100;
    const bool wide = app.game.flag(0x1003);
    zNear = 1 + (wide ? 0 : off);
    zFar = wide ? 7000.0f : map == 83 ? 4000.0f : 2000.0f;
    if (zNear >= zFar * 0.5f) zNear = zFar * 0.5f; // (a guard: the original has none)
}

cr::Mat4 scriptCameraView(const App& app, const cr::Vec3& eye, const cr::Vec3& target) {
    const cr::StageCamera& sc = app.stageCam;
    const float SW = app.renderer.screenW(), H = cr::Renderer::kLogicalH;
    const float fz = fieldFillZoom(app);
    const float w = fieldViewW(app) * fz, h = fieldViewH(app) * fz;
    // (setCamera3D casts the corner's sum to int: a corner left of the screen moves a pixel on a
    // positive fraction - the shake's whole pixels taken against the corner, the layout's own
    // centring left as it is)
    const float cx0 = (cr::Renderer::kLogicalW - w) / 2, cy0 = (H - h) / 2;
    const float rx = app.renderer.uiX() + cx0 + (std::trunc(cx0 + app.fieldQuake.off[0]) - std::trunc(cx0));
    const float ry = cy0 + (std::trunc(cy0 + app.fieldQuake.off[1]) - std::trunc(cy0));
    cr::Mat4 toScreen;
    toScreen.m[0] = w / SW, toScreen.m[5] = h / H;
    toScreen.m[12] = (2 * rx + w) / SW - 1, toScreen.m[13] = 1 - (2 * ry + h) / H;
    const float whole = float(std::max(1, sc.viewWidth / std::max(1, sc.viewHeight)));
    float zNear, zFar;
    fieldDepthRange(app, eye, target, zNear, zFar);
    return toScreen * cr::Mat4::perspective(sc.fovDegrees * 3.14159265f / 180.0f, whole, zNear, zFar) *
           cr::Mat4::lookAt(eye, target, {0, 1, 0});
}

// btl_lose (flag 0xffd set by the defeat, btl_main 0x4b1): an ordinary battle lost - back to the
// scenario's Ark (Omega: area 9, map 175 - 189 once flag 0x4f4 is set -; scenario 1 area 10, map
// 198) at (-140, -1, 25) facing 180, a fifth of the Oz gone
void loseToArk(App& app) {
    endBattle(app, false);
    // btl_main 0x4b1 (3.1.0 decomp 6896): the defeat sets flag 0xffd - btl_lose acts on it, and the
    // Ark's area script (j001_t01 0x4782 -> 0x5831, c101_t01 0x4945 -> 0x4edc) plays the scene after a
    // defeat while it is set, then clears it
    app.game.setFlag(0xffd, true);
    const bool second = app.game.scenario == 1;
    cr::WarpRequest w;
    w.valid = true;
    w.area = second ? 10 : 9;
    w.map = second ? 198 : app.game.flag(0x4f4) ? 189 : 175;
    w.pos[0] = -140, w.pos[1] = -1, w.pos[2] = 25;
    w.angleDegrees = 180;
    cr::addOz(app.game, -(app.game.oz * 20 / 100));
    performWarp(app, w, -1);
    // btl_lose stores the load flags as 0x27 (over prc's | 0xd): bit 1 runs the area's script 0
    // (fldread_evt_init2), bit 3 clear - no room entry script (EvrStart 5)
    app.pendingEntry = App::Entry::Area;
    app.lastEvent = "lost: back to the Ark";
}

// The menu asking for the mask, and its shape (as their Set / Prc / Clr calls have it): the camp,
// options, the shop, the World Select, the last battle's pairs and a battle's results the slant
// (1), the save screen and the tutorials' pictures the whole screen (0; the save screen at once,
// MuCom_MenuOneFrSetMask). -1: none
int menuMaskWanted(const App& app, bool& atOnce) {
    atOnce = false;
    if (app.mode != Mode::Maps) return -1;
    if (app.saveScreen.open) { atOnce = true; return 0; }
    if (app.tutoTex.on) return 0;
    if (app.campOpen) { // __menuu_CampMainPrc: the Status page's member's details on the right's (type 2); none
        // while a once-only message is up (set after 56010, cleared for 56016) or the Emergency Exit has
        // taken the windows off (CampMain 0xd: MuCom_MenuClrMask)
        if (app.camp.notice || (app.camp.phase == App::Camp::Exit && app.camp.exitT >= kCampExitHide)) return -1;
        return app.camp.page == App::Camp::Status && app.camp.phase == App::Camp::Slot ? 2 : 1;
    }
    if ((app.optionsOpen && !optionsAbout(app)) || app.shop.open || app.worldChoiceOpen || app.lastBattle.open) return 1;
    if (app.ach.listOpen) return 3; // (the Store menu's Set: MuCom_MenuSetMask(3))
    if (app.battle.active && app.battle.phase == App::Battle::Results && app.battle.result.stage != 3) return 1; // (MbResultPrc 0 .. 10)
    return -1;
}

int menuMaskAlpha(const App& app) {
    const App::MenuMask& m = app.menuMask;
    return m.from + (m.to - m.from) * std::min(m.t, m.ms) / std::max(1, m.ms);
}

// A frame: a menu opening fades the mask in (0 -> 0xa0 over 400 ms, or at once), one closing
// fades it out from where it is; a change of shape between menus keeps it
void stepMenuMask(App& app) {
    App::MenuMask& m = app.menuMask;
    bool atOnce = false;
    const int want = menuMaskWanted(app, atOnce);
    if (want >= 0 && (m.want < 0 || (atOnce && want != m.type)))
        m.tuto = app.tutoTex.on, m.from = 0, m.to = 0xa0, m.t = 0, m.ms = atOnce ? 1 : 400; // (_SetFade_back 400 ms, tutorials too)
    else if (want < 0 && m.want >= 0) m.from = menuMaskAlpha(app), m.to = 0, m.t = 0, m.ms = 400;
    else m.t = std::min(m.t + 50, m.ms);
    if (want >= 0) m.type = want;
    m.want = want;
}

void settleMenuMask(App& app) { // (captures: the menu's mask as it stands once in)
    stepMenuMask(app);
    app.menuMask.t = app.menuMask.ms;
}

// PutMask's quad (tex2d-space +-512 x +-384 at z -1: over the 3D, under the 2D); widescreen
// carries the shapes out to the screen's sides

void drawMenuMask(App& app) {
    if (app.ach.listOpen) return; // (the achievement list draws its own over the title's picture)
    drawMenuMaskShape(app, menuMaskAlpha(app));
}
void drawMenuMaskShape(App& app, int alpha) {
    if (alpha <= 0) return;
    cr::Renderer& r = app.renderer;
    r.setOrigin(0);
    struct Picture { // (the mask is the picture's: to the screen's edges, unshrunk by a camera cutout - interfaceRect)
        cr::Renderer& r;
        explicit Picture(cr::Renderer& rr) : r(rr) { r.setPictureLayer(true); }
        ~Picture() { r.setPictureLayer(false); }
    } picture(r);
    const float a = float(alpha) / 255.0f, SW = r.screenW(), H = cr::Renderer::kLogicalH, x0 = r.uiX();
    switch (app.menuMask.type) {
    case 1: { const float q[8] = {0, 0, x0 + 427, 0, x0 + 747, H, 0, H}; r.fillQuad(q, 0, 0, 0, a); break; }
    case 2: { const float q[8] = {x0 + 597, 0, SW, 0, SW, H, x0 + 277, H}; r.fillQuad(q, 0, 0, 0, a); break; }
    case 3: { // PutMask: the left edge's corners at the alpha, the right edge's at 0x10 at most - fading across
        // the screen (Android: the phone's; Part 15 3857.5 s against 3855.5 s: x 0 at 0.37 of the title's
        // brightness, x 640 0.68, x 1216 0.91)
        const float L = r.phoneLayout() ? SW * 0.5f - SW * 0.5f / r.layoutScale() : 0.0f, R = r.phoneLayout() ? r.layoutRight() : SW;
        const float right = std::min(a, 16.0f / 255.0f);
        for (int k = 0; k < 32; ++k) {
            const float t = (float(k) + 0.5f) / 32;
            r.fillRect(L + (R - L) * float(k) / 32, 0, (R - L) / 32 + 0.5f, H, 0, 0, 0, a + (right - a) * t);
        }
        break;
    }
    default: r.fillRect(0, 0, SW, H, 0, 0, 0, a); break;
    }
}

void drawFrame(App& app, bool present) {
    if (app.battle.active && app.battle.exit) { // the battle's way out, between frames
        const int e = app.battle.exit;
        app.battle.exit = 0;
        if (e == 1) endBattle(app, false);
        else if (e == 4) endBattle(app, true);
        else if (e == 3) loseToArk(app);
        else bootTitle(app);
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(app.window, &w, &h);
    auto& r = app.renderer;
    { // the display's safe area, in the window's pixels (CR_SAFE_INSETS "l,t,r,b": tests)
        float l = 0, t = 0, rr = 0, b = 0;
        int ww = 0, wh = 0;
        SDL_Rect safe;
        if (const char* e = SDL_getenv("CR_SAFE_INSETS"); e && std::sscanf(e, "%f,%f,%f,%f", &l, &t, &rr, &b) == 4) {
        } else if (SDL_GetWindowSafeArea(app.window, &safe) && SDL_GetWindowSize(app.window, &ww, &wh) && ww > 0 && wh > 0) {
            const float sx = float(w) / float(ww), sy = float(h) / float(wh);
            l = float(safe.x) * sx, t = float(safe.y) * sy, rr = float(ww - safe.x - safe.w) * sx, b = float(wh - safe.y - safe.h) * sy;
        }
#ifdef SDL_PLATFORM_ANDROID
        { // (and Android's own DisplayCutout - SDL's safe area can stay zero on some phones: GameActivity.cutoutInsets)
            float c[4];
            if (androidCutout(c)) l = std::max(l, c[0]), t = std::max(t, c[1]), rr = std::max(rr, c[2]), b = std::max(b, c[3]);
        }
#endif
        bool avoid = app.video.avoidCutout, bars = app.video.cutoutBars;
        if (const char* e = SDL_getenv("CR_CUTOUT"); e && *e) // (tests: avoid | bars | ignore, over Options)
            avoid = SDL_strcmp(e, "ignore") != 0, bars = SDL_strcmp(e, "bars") == 0;
        if (!avoid) l = t = rr = b = 0; // (Options: the picture to the screen's edges, under a camera's cutout too)
        r.setSafeInsets(l, t, rr, b);
        r.setCutoutBars(avoid && bars); // (Black Bars: the whole picture boxed clear of it)
    }
    { // what the system gives the game, and how the game fits itself in it - logged once, and when it changes
      // (a phone's fold, a rotation, a window's resize: chaosrings.log - iOS's in the Files app)
        int pw = 0, ph = 0, ww = 0, wh = 0;
        SDL_GetWindowSize(app.window, &ww, &wh);
        SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
        SDL_Rect safe{}, bounds{}, usable{};
        SDL_GetWindowSafeArea(app.window, &safe);
        const SDL_DisplayID d = SDL_GetDisplayForWindow(app.window);
        SDL_GetDisplayBounds(d, &bounds);
        SDL_GetDisplayUsableBounds(d, &usable);
        char line[512];
        SDL_snprintf(line, sizeof line,
                     "window %dx%d pt, %dx%d px (density %.2f, scale %.2f), display %dx%d at %d,%d (usable %dx%d at %d,%d), safe area %dx%d at %d,%d; "
                     "layout %s, %s, cutout %s",
                     ww, wh, pw, ph, SDL_GetWindowPixelDensity(app.window), SDL_GetWindowDisplayScale(app.window), bounds.w, bounds.h, bounds.x, bounds.y,
                     usable.w, usable.h, usable.x, usable.y, safe.w, safe.h, safe.x, safe.y, r.phoneLayout() ? "iPhone" : "iPad",
                     app.video.widescreen ? "wide" : "fixed", !app.video.avoidCutout ? "ignored" : app.video.cutoutBars ? "black bars" : "avoided");
        if (app.screenLogged != line) app.screenLogged = line, SDL_Log("screen: %s", line);
    }
    r.beginFrame(w, h);
    // the examine prompt ("!" over what can be checked): placed through the scene's camera, drawn
    // with the UI once the scene is done - not upscaled with it (FSR / DLSS / MetalFX)
    struct {
        bool on = false;
        cr::Mat4 vp;
        float x = 0, y = 0, w = 0, h = 0;
    } prompt;
    bool fieldHiddenDrawn = false; // (the field pass left the field out itself: no black over it)
    ++app.frameNo;
    auto promptLater = [&](const cr::Mat4& vp, float x, float y, float pw, float ph) { prompt.on = true, prompt.vp = vp, prompt.x = x, prompt.y = y, prompt.w = pw, prompt.h = ph; };
    // W: the game's 2D layout (1024); SW: the whole logical screen (wider in widescreen)
    const float W = cr::Renderer::kLogicalW, H = cr::Renderer::kLogicalH, SW = r.screenW();
    const Uint64 now = app.clock.ticksNS();
    const float realDt = std::min(app.lastTicks && now >= app.lastTicks ? float(double(now - app.lastTicks) * 1e-9) : 0.0f, 0.1f);
    app.lastTicks = now;
    if (app.mode == Mode::Maps && !app.booting && !app.titleOpen && (app.havePlayer || app.battle.active))
        app.playSeconds += realDt;
    // Field Speed / Battle Speed: the battles, and the field while the player walks it, faster
    const float dt = realDt * float(app.mode == Mode::Maps && app.battle.active && !app.optionsOpen && !app.manualOpen ? app.battleSpeed
                                         : padField(app)                                                           ? app.fieldSpeed
                                                                                                                   : 1);
    app.drawFrames = dt * kFramesPerSecond;

    if (app.mode == Mode::Textures) {
        drawCheckerboard(r);
        if (app.currentTex.id) {
            const float tw = float(app.currentTex.width), th = float(app.currentTex.height);
            const float s = std::min({SW / tw, H / th, 1.0f});
            r.drawQuad(app.currentTex, (SW - tw * s) / 2, (H - th * s) / 2, tw * s, th * s);
        }
    } else if (app.mode == Mode::Maps && app.battle.active) {
        drawBattle(app, dt);
    } else if (app.mode == Mode::Maps && app.booting) {
        // The title: no field; the boot script runs at 20 Hz by itself (it shows its pictures
        // through Tex2d, drawn with the 2D below)
        r.fillRect(0, 0, SW, H, 0, 0, 0, 1);
        for (app.bootClock += dt; app.bootClock >= 1.0f / kFramesPerSecond; app.bootClock -= 1.0f / kFramesPerSecond) {
            stepEvents(app);
        }
    } else if (app.mode == Mode::Maps && app.showBackground) {
        // The field as the game composes it: background, 3D through the stage camera, foreground.
        // The background is the scene rendered from that camera, so both share one rectangle: the
        // stage view. Originally (FieldCamera::setCamera3D) that is the doubled view centred on the
        // screen and moved by the scroll; the options can show the whole painting instead, at the
        // screen's height (wider screens get bars at the sides) or its width (top and bottom cut).
        r.fillRect(0, 0, SW, H, 0, 0, 0, 1);
        // A room without a painting is fully 3D: there a script's free camera films the whole screen
        // (CameraPlayer leaves the live eye / target as the last shot set them, until the map is
        // loaded again - FieldCamera keeps them; only fldread_prc copies the stage record back)
        const bool scriptCam = app.cam.set && !app.background.id;
        cr::Vec3 eye, tgt;
        cr::Mat4 mvp;
        const bool have3D = app.haveStageCam || scriptCam;
        if (have3D) {
            const cr::StageCamera& sc = app.stageCam;
            cr::Vec3 camEye, camTarget; // (the script's camera as this frame shows it)
            shownCamera(app, camEye, camTarget);
            eye = scriptCam ? camEye : cr::Vec3{sc.eye[0], sc.eye[1], sc.eye[2]};
            tgt = scriptCam ? camTarget : cr::Vec3{sc.target[0], sc.target[1], sc.target[2]};
            const float aspect = scriptCam ? SW / H : sc.viewHeight > 0 ? float(sc.viewWidth) / float(sc.viewHeight) : 1.0f;
            const float fov = app.haveStageCam ? sc.fovDegrees : 60.0f;
            float zNear, zFar; // (FieldCamera::setCamera3D's, the script's camera and the stage's alike)
            fieldDepthRange(app, eye, tgt, zNear, zFar);
            app.nearPlane = zNear, app.farPlane = zFar;
            mvp = cr::Mat4::perspective(fov * 3.14159265f / 180.0f, aspect, zNear, zFar) * cr::Mat4::lookAt(eye, tgt, {0, 1, 0});
            if (scriptCam && app.haveStageCam && sc.viewWidth > 0 && sc.viewHeight > 0) mvp = scriptCameraView(app, eye, tgt);
            r.setCamera(zNear, zFar, fov * 3.14159265f / 180.0f);
            animateModel(app, dt);
            animateNpcs(app, dt);
            updateNpcJoints(app);
            updatePlayer(app, dt, std::atan2(eye.x - tgt.x, eye.z - tgt.z));
            updateNpcJoints(app);
        }
        // The stage view on screen (vx, vy, vw, vh)
        float vx, vy, vw, vh;
        if (scrollingField(app)) {
            followPlayerScroll(app, 0.3f);
            const float fz = fieldFillZoom(app);
            vw = fieldViewW(app) * fz;
            vh = fieldViewH(app) * fz;
            float sx, sy;
            shownScroll(app, sx, sy);
            vx = (SW - vw) / 2 + sx * fz;
            vy = (H - vh) / 2 - sy * fz;
        } else {
            vw = vh = std::min(SW, H);
            vx = (SW - vw) / 2;
            vy = (H - vh) / 2;
        }
        // FieldCamera::setCameraBack / setCamera3D: the viewport moves by the quake's offset
        // (whole pixels: the corner's sum is cast to int - toward zero, so a corner left of the
        // screen moves a pixel on any positive fraction, as the original's 1-pixel tremors do)
        vx += std::trunc(vx + app.fieldQuake.off[0]) - std::trunc(vx), vy += std::trunc(vy + app.fieldQuake.off[1]) - std::trunc(vy);
        // Layers are drawn at native pixel size, centred in the stage view (viewW x viewH:
        // 1024 for most fields, 512 for d0108/d0117/d0208/d0219). 512x1024 backgrounds such
        // as d0501/d0507/d0511 occupy the middle strip x 256..768
        const float viewSize = app.haveStageCam && app.stageCam.viewWidth > 0 ? float(app.stageCam.viewWidth) : 1024.0f;
        auto drawLayer = [&](const cr::Texture& t) {
            const float k = vw / viewSize, w = t.width * k, h = t.height * k;
            r.drawQuad(t, vx + (vw - w) / 2, vy + (vh - h) / 2, w, h);
        };
        // (the field's picture - its paintings, its 3D view, its effects, the prompts over them, the fades - fills the
        // screen whatever a camera cutout does to the interface: a painting narrower than the screen shrunk while its
        // 3D view kept its place would part them - CR1-U, the Ark hall's door beside its archway on the iPhone Duo)
        r.setPictureLayer(true);
        drawSceneBack(app, false); // ScnEffectProc's z -1998 layers (the sky, the shaft, ...): behind the painting
        r.setOrigin(0);
        r.setLayer(3); // (the Vita's screen: fldread_prc's paintings and the view with them at full size)
        // Screen's toggle (G+0x8f13e): drawBack leaves out the painting and mdl_put every model (the
        // effects' too); the shadows (npc_shodow) and ScnEffect's layers are drawn all the same
        const bool hidden = app.fieldHidden;
        fieldHiddenDrawn = hidden;
        if (app.background.id && !hidden) drawLayer(app.background);
        r.setOrigin(0);
        if (have3D) {
            if (scriptCam) {
                const bool whole = app.haveStageCam && app.stageCam.viewWidth > 0; // (scriptCameraView places the view, quake and all)
                r.begin3DRect(whole ? 0 : std::trunc(app.fieldQuake.off[0]), whole ? 0 : std::trunc(app.fieldQuake.off[1]), SW, H);
            }
            else r.begin3DRect(vx, vy, vw, vh);
            // Field props are occlusion geometry: all their materials are untextured / solid-alpha
            // and black-diffuse, placed over pillars and door frames of the painting. They only
            // write depth, so the player and NPCs disappear behind them
            if (!hidden) {
                r.setColorWrite(false);
                if (app.showProps) drawLoaded(app, app.model, mvp);
                r.setColorWrite(true);
                // mdl_put: slot 0 (the props) drawn at once, then every other slot into the draw queue in
                // slot order - the player's (1, gac + 0x1338) before the NPCs' -, the whole queue sorted by
                // draw order: the ship's waves (c152, orders 0..40, blended with depth writes) after the
                // player and after every NPC's opaque meshes (Vieg on the paddle wheel, map 70's opening)
                drawFigureQueue(app, mvp);
            }
            drawFieldShadows(app, mvp); // (draw order 990: after the models)
            if (app.puzzle.stage >= 0) puzzle::drawFx(app, mvp);
            if (!hidden) drawEffects(app, app.fieldEffects, mvp, &eye, &tgt); // (the camera: for FrmPlayEff's camera-facing effects)
            if (app.showCollision && app.collision.vao) {
                cr::Material colMat; // translucent so the painted floor shows through
                colMat.culling = 2; // (none)
                colMat.properties = 8;
                colMat.blendSrc = 4;
                colMat.blendDst = 5;
                colMat.diffuse[3] = 0.45f;
                colMat.ambient[0] = colMat.ambient[1] = colMat.ambient[2] = 0;
                r.drawMesh(app.collision, nullptr, mvp, colMat);
                if (app.triggerMesh.vao) r.drawMesh(app.triggerMesh, nullptr, mvp, colMat);
            }
            r.end3D();
            if (scriptCam) promptLater(mvp, 0, 0, SW, H);
            else promptLater(mvp, vx, vy, vw, vh);
        }
        r.setOrigin(0); // (as the background's: the painting at the 3D view's place - in widescreen a 2D origin left at uiX
        // put it beside its models; CR1-U's 7875355, the Ark hall's door)
        if (app.foreground.id && !hidden) drawLayer(app.foreground);
        r.setLayer(0);
        drawSceneEffects(app);
        r.setOrigin(0);
    } else { // Models and Maps share the 3D path
        animateModel(app, dt);
        if (app.mode == Mode::Maps) {
            animateNpcs(app, dt);
            updateNpcJoints(app);
            updatePlayer(app, dt, app.yaw);
            updateNpcJoints(app);
        }

        r.fillRect(0, 0, SW, H, 0.22f, 0.24f, 0.28f, 1);
        const LoadedModel& m = app.model;
        const float dist = m.radius * 2.6f * app.zoom;
        // Orbit in Y-up world space; Z-up models are rotated into it
        const cr::Mat4 world = app.zUp ? cr::Mat4::zUpToYUp() : cr::Mat4{};
        const cr::Vec3 c = app.zUp ? cr::Vec3{m.center.x, m.center.z, -m.center.y} : m.center;
        const cr::Vec3 eye{c.x + dist * std::cos(app.pitch) * std::sin(app.yaw), c.y + dist * std::sin(app.pitch),
                           c.z + dist * std::cos(app.pitch) * std::cos(app.yaw)};
        const cr::Mat4 view = cr::Mat4::lookAt(eye, c, {0, 1, 0});
        const cr::Mat4 proj = cr::Mat4::perspective(0.8f, r.aspect(), m.radius * 0.01f, m.radius * 20.0f);
        r.setCamera(m.radius * 0.01f, m.radius * 20.0f, 0.8f);
        const cr::Mat4 mvp = proj * view * world;
        r.begin3D();
        if (app.mode == Mode::Maps) {
            cr::Material occluder; // field props: shown as translucent grey in the 3D view
            occluder.culling = 2; // (none)
            occluder.properties = 8;
            occluder.blendSrc = 4;
            occluder.blendDst = 5;
            occluder.ambient[0] = occluder.ambient[1] = occluder.ambient[2] = 0.35f;
            occluder.diffuse[0] = occluder.diffuse[1] = occluder.diffuse[2] = 0;
            occluder.diffuse[3] = 0.5f;
            drawLoaded(app, m, mvp, &occluder);
            drawNpcs(app, mvp);
        } else {
            drawLoaded(app, m, mvp);
        }
        if (app.mode == Mode::Maps && app.havePlayer && playerShown(app))
            drawLoaded(app, app.player, mvp * playerWorld(shownWalker(app)), nullptr, shownPlayerAlpha(app), -1.0f, 0, app.playerFaded), modelDrawn(app);
        if (app.mode == Mode::Maps) drawFieldShadows(app, mvp);
        if (app.mode == Mode::Maps && app.puzzle.stage >= 0) puzzle::drawFx(app, mvp);
        if (app.mode == Mode::Maps) drawEffects(app, app.fieldEffects, mvp, &eye, &c);
        if (app.mode == Mode::Maps && app.collision.vao && app.showCollision) {
            cr::Material colMat;          // opaque, untextured, double-sided, depth test + write
            colMat.culling = 2; // (none)
            colMat.properties = 8 | 16;
            colMat.ambient[0] = colMat.ambient[1] = colMat.ambient[2] = 0;
            r.drawMesh(app.collision, nullptr, mvp, colMat);
            if (app.triggerMesh.vao) r.drawMesh(app.triggerMesh, nullptr, mvp, colMat);
        }
        r.end3D();
        if (app.mode == Mode::Maps) promptLater(mvp, 0, 0, SW, H);
    }
    // On-screen status: window title bars are unreliable (Wayland) or absent (Android)
    std::vector<std::string> lines{app.status};
    if (app.mode == Mode::Maps)
        lines.insert(lines.end(), {"Left/Right: map   PgUp/PgDn: jump 10   B: background   C: collision   F: smooth filter",
                                   "M: other map using this collision   drag/wheel: camera   Tab: textures",
                                   "WASD: walk (Shift: run)   E/Enter: examine   P: player   O: party member   N: NPCs   V: volume"});
    else if (app.mode == Mode::Models)
        lines.insert(lines.end(), {"Left/Right: model   PgUp/PgDn: jump 10   N/B: animation   Space: pause",
                                   "-/=: speed   U: up-axis   F: smooth filter   drag/wheel: camera   Tab: maps"});
    else
        lines.push_back("Left/Right texture  Tab models");
    r.endScene(); // the scene is complete: upscaled to the frame (when on); the UI over it at full size
    if (prompt.on) {
        r.setOrigin(0);
        drawPrompt(app, prompt.vp, prompt.x, prompt.y, prompt.w, prompt.h);
    }
    if (app.mode == Mode::Maps) {
        if (app.fieldHidden && !app.booting && !fieldHiddenDrawn) r.fillRect(0, 0, SW, H, 0, 0, 0, 1); // Screen's toggle: the field not drawn (drawBack / mdl_put skipped)
        const float ab = screenFadeBackAlpha(app); // Screen's back channel (-100); the front one (189): drawFrontFade
        if (ab > 0.001f) r.fillRect(0, 0, SW, H, app.fadeBackGrey, app.fadeBackGrey, app.fadeBackGrey, ab);
        for (app.menuMask.clock += dt; app.menuMask.clock >= 1.0f / kFramesPerSecond; app.menuMask.clock -= 1.0f / kFramesPerSecond)
            stepMenuMask(app);
        drawMenuMask(app);
    }
    r.setPictureLayer(false); // (the interface from here on: kept off a camera cutout - Renderer::interfaceRect)
    r.setOrigin(r.uiX()); // the game's 2D from here on: its 1024 x 768 layout, centred
    app.order2d = App::Order2D();
    // A message window this frame (the calls of drawMessageBox below): the front fade waits for its
    // text - else it goes over the portraits at once
    const bool heldNow = app.heldPage && !(!app.runner.running() || !app.dialogue.empty() || app.lastPage.empty());
    const bool messageNow = app.mode == Mode::Maps && (!app.dialogue.empty() || (app.choiceFlag >= 0 && !app.lastPage.empty()) ||
                                                       (app.choiceFlag < 0 && (heldNow || (app.msgWin.state >= 1 && app.msgWin.state <= 5))));
    if (app.mode == Mode::Maps) {
        if (!app.dialogue.empty()) { // 0x1c: 1 = the left speaks, 2 = the right; the other side dims
            const size_t pi = std::min(app.dialoguePage, app.dialogue.size() - 1);
            if (pi < app.dialogueInfo.size() && app.dialogueInfo[pi].side)
                for (App::Portrait& p : app.portraits)
                    if (p.side) p.shade = (p.side < 0) == (app.dialogueInfo[pi].side == 1) ? 1.0f : 128.0f / 255.0f;
        }
        r.setLayer(3); // (the Vita's screen: a script's Tex2d images at full size)
        for (const App::Portrait& p : app.portraits) // Tex2d: portraits and title cards (over a battle in its talks)
            if (p.tex.id && p.alpha > 0.001f && (!app.battle.active || battletalk::on(app))) {
                if (p.flags & 0x8000) { // a full-screen card (tex2d_img_prc): the iPad's sheets hold a
                    // 1024 x 768 picture for the screen (tex2d (-32, -64)), 2.2.0's and the Vita's a 960 x
                    // 640 one for tex2d (0, 0) 480 x 320. Scaled to cover the screen - the whole width in
                    // widescreen -, centred and cut at the sides or top and bottom (the iPhone layout's
                    // screen: (32, 64) 960 x 640); the slide moves it
                    // A plain (SD) sheet - 512 wide, tyuui's - holds a 480 x 320 card: tex2d_img_prc draws it
                    // 480 x 320 at tex2d (0, 0), the iPad's (32, 64) 960 x 640, not over the whole screen
                    const bool phone = r.phoneLayout(), small = app.phoneData || app.vitaOnly || phone; // (the iPhone layout: _hd art)
                    if (!small && p.tex.width < 1024) {
                        const float ox = phone ? 0.0f : 0.0f;
                        r.drawQuadUV(p.tex, 32 + ox + p.x, 64 + p.y, 960, 640, 0, 0, 480, 320, p.shade, p.shade, p.shade, p.alpha);
                        app.order2d.portrait = ++app.order2d.seq;
                        continue;
                    }
                    const float pw = small ? 960.0f : 1024.0f, ph = small ? 640.0f : 768.0f;
                    if (p.ipadCard) {
                        // Android 1.1.6 (tex2d_img_prc, mode 2: tex2d_add_sca of the 1024 x 768 at the
                        // screen's width / 1024): the iPad sheet's picture over the whole phone, centred -
                        // Part 1 11 s, the title: 1.25 at (0, -120) on the 1280 x 720 phone (correlation
                        // 0.95; the _hd picture in the iPhone's screen left bars and lost its edges)
                        // (logical: the iPhone screen's (32, 64) is the phone's (2 ox, 2 oy))
                        const float ox = 2 * androidEdgeX(app), oy = 2 * androidEdgeY(app);
                        const float pW = 960 + 2 * ox, pH = 640 + 2 * oy, s = pW / 1024;
                        r.drawQuadUV(p.tex, 32 - ox + p.x, 64 - oy + pH / 2 - 384 * s + p.y, 1024 * s, 768 * s, 0, 0, 1024, 768,
                                     p.shade, p.shade, p.shade, p.alpha);
                        app.order2d.portrait = ++app.order2d.seq;
                        continue;
                    }
                    // (the iPhone layout's screen in its own units: on one wider than 3:2 the card covers it - its
                    // height alone left bars at the sides; CR1-U 9b36e2c)
                    const float vw = phone ? r.screenW() / r.layoutScale() : SW, vh = phone ? 640.0f : float(H), top = phone ? 64.0f : 0.0f;
                    const float k = std::max(vw / pw, vh / ph);
                    r.drawQuadUV(p.tex, 512 - pw * k / 2 + p.x, top + (vh - ph * k) / 2 + p.y, pw * k, ph * k, 0, 0, pw, ph,
                                 p.shade, p.shade, p.shade, p.alpha);
                    app.order2d.portrait = ++app.order2d.seq;
                    continue;
                }
                const float w = float(p.tex.width) * p.scale, h = float(p.tex.height) * p.scale; // zooms about its centre
                // Widescreen: side portraits keep to the screen's edges (as the later widescreen
                // release does), centre images to the middle
                // (Android's __EvtTex2d: the side ones 2 ex further out, to the phone's edges)
                const float edge = r.vitaScreen() ? 0.0f
                                   : p.side < 0  ? wideLeft(app) - r.uiX() - 2 * androidEdgeX(app)
                                   : p.side > 0  ? wideRight(app) - r.uiX() + 2 * androidEdgeX(app)
                                                 : 0.0f; // (inside the safe area)
                const float x = edge + p.x - (w - float(p.tex.width)) / 2, y = p.y - (h - float(p.tex.height)) / 2;
                r.drawQuadUV(p.tex, x, y, w, h, p.side == 1 ? float(p.tex.width) : 0, 0,
                             p.side == 1 ? -float(p.tex.width) : float(p.tex.width), float(p.tex.height), p.shade,
                             p.shade, p.shade, p.alpha); // right-hand portraits are mirrored (slot +0xe)
                app.order2d.portrait = ++app.order2d.seq;
            }
        r.setLayer(0);
        if (app.itemWindowOpen) drawItemWindow(app);
        if (!messageNow && !(app.mode == Mode::Maps && app.battle.active)) drawFrontFade(app);
    }
    trackFieldMenus(app);
    if (fieldMenuWanted(app)) { // the corner button and its bar
        for (app.fieldMenu.clock += dt; app.fieldMenu.clock >= 1.0f / kFramesPerSecond; app.fieldMenu.clock -= 1.0f / kFramesPerSecond)
            stepFieldMenu(app), stepPuzzleHud(app), stepTouchPad(app);
        app.fieldMenu.fade = fieldHudShown(app) ? 320 : 0; // (gone the frame a script starts)
        drawFieldMenu(app);
        r.setOrigin(r.uiX());
    } else
        app.fieldMenu.fade = 0, app.fieldMenu.clock = 0, touchPadUp(app);
    if (app.mode == Mode::Maps && app.puzzleBanner.on) { // Uty_PuzzleStart_Put, over the scene that runs it
        drawPuzzleBanner(app);
        r.setOrigin(r.uiX());
    }
    if (app.mode == Mode::Maps && app.texDisp.on) drawTexDisp(app);
    if (app.mode == Mode::Maps && app.vitaLogo.phase) drawVitaLogo(app);
    if (app.mode == Mode::Maps && app.tutoTex.on) drawTutoTex(app);
    if (app.mode == Mode::Maps && app.exclamation) drawExclamation(app);
    if (app.mode == Mode::Maps && app.padArt) drawPadArt(app);
    if (app.mode == Mode::Maps && app.battle.active) {
        drawBattleHud(app, W, H);
        // (the mask is over the battle's 2D too: the opening white washes the badge, the rows and the
        // banner band with the scene - Part 3 1203.14 / 1203.44 s)
        if (!messageNow) drawFrontFade(app);
    }
    if (app.mode == Mode::Maps && app.titleOpen && !app.optionsOpen && !app.saveScreen.open && // (the offer's windows over it)
        !app.charSelectOpen && !app.scc.open && !app.collection.open && !app.manualOpen && !app.ach.listOpen)
        drawTitleMenu(app);
    if (app.ach.listOpen) { // the achievement list (the title's picture 127)
        for (app.ach.listClock += dt; app.ach.listClock >= 1.0f / kFramesPerSecond; app.ach.listClock -= 1.0f / kFramesPerSecond)
            stepAchievementList(app);
        if (!app.titleOpen) closeAchievementList(app);
        else drawAchievementList(app);
    }
    if (app.optionsOpen) {
        for (app.optionsClock += dt; app.optionsClock >= 1.0f / kFramesPerSecond; app.optionsClock -= 1.0f / kFramesPerSecond)
            stepOptions(app);
        if (!app.campOpen && !app.saveScreen.open) drawOptions(app, W, H); // (else above those, below)
    }
    if (!app.hideHud && app.showHud) app.hud.draw(r, lines);
    if (app.mode == Mode::Maps && app.worldChoiceOpen) { // Util 1: the World Select
        for (app.worldSelect.clock += dt; app.worldSelect.clock >= 1.0f / kFramesPerSecond && app.worldChoiceOpen;
             app.worldSelect.clock -= 1.0f / kFramesPerSecond)
            stepWorldSelect(app);
        drawWorldSelect(app);
        r.setOrigin(r.uiX());
    }
    if (app.mode == Mode::Maps && app.choiceFlag >= 0) { // Util 3: Yes / No beside the question
        if (app.dialogue.empty() && !app.lastPage.empty()) drawMessageBox(app, app.lastPage, "", false, "");
        // __menuu_EventPut: ComUty_YesNoChoiceDraw - the plates at (336, 104) and (336, 160) of
        // window 0, which slides in from 360 to the right
        const float t = std::min(1.0f, float(app.choiceAnim) / 4.0f), dx = 360 * (1 - t) * (1 - t);
        if (app.windowTex.id) {
            app.menu.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
            r.setOrigin(r.uiX());
            const PhoneMenu phoneMenu(app, app.menu);
            // (__menuu_EventPrc 0x321 clears the lit plate (+0x34) every frame and ComUty_YesNoChoiceProc
            // sets it only while a finger is down on one: touch play shows both dark - Part 6 58.5 s,
            // "Retry puzzle?" -; keys and pads keep the cursor's lit)
            const bool lit = !app.pointerLast;
            const bool hy = pressIn(app, kPressChoice, r.uiX(), 336, 104, 136, 40), hn = pressIn(app, kPressChoice, r.uiX(), 336, 160, 136, 40);
            app.menu.plate(336 + dx, 104, 0, 0, (lit && app.choiceCursor == 0) || hy, 1, false);
            app.menu.plate(336 + dx, 160, 1, 1, (lit && app.choiceCursor == 1) || hn, 1, false);
        } else {
            const float cx = W - 260, cy = H - 360;
            r.fillRect(cx, cy, 200, 110, 0.05f, 0.08f, 0.2f, 0.85f);
            app.choiceHud.drawAt(r, {std::string(app.choiceCursor == 0 ? "> " : "  ") + "Yes",
                                     std::string(app.choiceCursor == 1 ? "> " : "  ") + "No"},
                                 cx + 36, cy + 24, 2.0f, 0, 0, 0.0f);
        }
    }
    if (app.heldPage && (!app.runner.running() || !app.dialogue.empty() || app.lastPage.empty())) app.heldPage = false;
    if (app.mode == Mode::Maps && app.dialogue.empty() && app.heldPage && app.choiceFlag < 0) { // (a page that ended itself)
        std::string speaker;
        std::vector<std::vector<std::string>> name;
        if (app.lastPageInfo.nameId >= 0 && app.systemText.message(app.lastPageInfo.nameId, textLanguage(app), nullptr, name) &&
            !name.empty() && !name[0].empty())
            speaker = name[0][0];
        drawMessageBox(app, app.lastPage, speaker, false, "");
    }
    if (app.mode == Mode::Maps && !app.dialogue.empty()) {
        const size_t pi = std::min(app.dialoguePage, app.dialogue.size() - 1);
        std::string speaker; // PrcMsg 0x15: the name plate
        if (pi < app.dialogueInfo.size() && app.dialogueInfo[pi].nameId >= 0) {
            std::vector<std::vector<std::string>> name;
            if (app.systemText.message(app.dialogueInfo[pi].nameId, textLanguage(app), nullptr, name) && !name.empty() &&
                !name[0].empty())
                speaker = name[0][0];
        }
        const cr::MessageInfo* info = pageInfo(app);
        if (app.typeOn) app.textLimit = app.dialogueTyped;
        app.msgColour = info ? info->colour : -1;
        drawMessageBox(app, app.dialogue[pi], speaker, pageTyped(app) && !(info && info->closeAfter >= 0), std::string());
        app.textLimit = 1 << 30;
    } else if (app.mode == Mode::Maps && !app.heldPage && app.choiceFlag < 0 && app.msgWin.state >= 1 && app.msgWin.state <= 5) {
        drawMessageBox(app, {}, "", false, ""); // the window alone (_PutWin: in every state but 6) - fading in before its text, up between messages, fading out after
    }
    drawFrontFade(app); // (189: over the message window, when no text came)
    r.setOrigin(r.uiX());
    if (app.nowLoading.on && app.windowTex.id) { // NowLoadProc (draw order 500): 8 frames of (820 + 24k, 96, 24 x 24) at tex2d (464, 320)
        app.menu.begin(r, app.windowTex, &app.font, app.cleanTextOutline);
        r.setOrigin(r.uiX());
        const bool phone = app.renderer.phoneLayout(); // 2.2.0: (448, 288), the iPhone screen's corner (Android: + ex, + ey)
        app.menu.piece((phone ? 448 + androidEdgeX(app) : 464) + 16, (phone ? 288 + androidEdgeY(app) : 320) + 32, 820 + 24.0f * float(app.nowLoading.frame % 8), 96, 24, 24, 1);
    }
    // The menus over everything else (Options draws above them)
    if (app.charSelectOpen) {
        ++app.charSelectAnim;
        for (app.startPage.clock += dt; app.startPage.clock >= 1.0f / kFramesPerSecond; app.startPage.clock -= 1.0f / kFramesPerSecond)
            stepCharSelect(app);
        if (app.charSelectOpen) drawCharSelect(app);
    }
    if (app.scc.open) {
        for (app.scc.clock += dt; app.scc.clock >= 1.0f / kFramesPerSecond; app.scc.clock -= 1.0f / kFramesPerSecond)
            stepScenarios(app);
        if (app.scc.open) drawScenarios(app);
    }
    if (app.collection.open) {
        for (app.collection.clock += dt; app.collection.clock >= 1.0f / kFramesPerSecond; app.collection.clock -= 1.0f / kFramesPerSecond)
            stepCollection(app);
        if (app.collection.open) drawCollection(app);
    }
    if (app.campOpen && !app.optionsOpen && !app.scc.open && !app.collection.open) {
        for (app.campClock += dt; app.campClock >= 1.0f / kFramesPerSecond && app.campOpen && !app.scc.open; app.campClock -= 1.0f / kFramesPerSecond)
            stepCamp(app);
        if (app.campOpen && !app.scc.open) drawCamp(app);
    }
    if (app.mapScreen.open) {
        for (app.mapScreen.clock += dt; app.mapScreen.clock >= 1.0f / kFramesPerSecond; app.mapScreen.clock -= 1.0f / kFramesPerSecond)
            stepMapScreen(app);
        drawMapScreen(app);
    }
    if (app.lastBattle.open) {
        for (app.lastBattle.clock += dt; app.lastBattle.clock >= 1.0f / kFramesPerSecond; app.lastBattle.clock -= 1.0f / kFramesPerSecond)
            stepLastBattle(app);
        drawLastBattle(app);
    }
    if (app.staffRoll.open) {
        const bool* keys = SDL_GetKeyboardState(nullptr); // the corner button held: Space / Enter, or the mouse there
        float mx = 0, my = 0;
        const bool held = SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK;
        bool corner = false;
        if (held) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(app.window, &ww, &wh);
            float lx, ly, tx, ty;
            r.toLogical(mx * (ww > 0 ? float(r.frameWidth()) / float(ww) : 1.0f), my * (wh > 0 ? float(r.frameHeight()) / float(wh) : 1.0f), lx, ly);
            logicalToTex2d(app, lx, ly, tx, ty);
            const float ex = app.staffRoll.edgeX, ey = app.staffRoll.edgeY; // (Android's hold box: + ex, - ey)
            corner = app.staffRoll.phone ? tx >= 444 + ex && tx < 476 + ex && ty >= -ey && ty < 32 - ey : tx >= 460 && tx < 492 && ty >= -32 && ty < 0;
        }
        app.staffRoll.fast = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN] || corner ||
                             (app.pad && SDL_GetGamepadButton(app.pad, SDL_GAMEPAD_BUTTON_WEST)); // (the Vita's □ held: __menuu_StaffRollPrc)
        for (app.staffRoll.clock += dt; app.staffRoll.clock >= 1.0f / kFramesPerSecond; app.staffRoll.clock -= 1.0f / kFramesPerSecond)
            stepStaffRoll(app);
        drawStaffRoll(app);
    }
    if (app.shop.open) {
        for (app.shopClock += dt; app.shopClock >= 1.0f / kFramesPerSecond; app.shopClock -= 1.0f / kFramesPerSecond)
            stepShop(app);
        if (app.shop.open) drawShop(app);
    }
    if (app.saveScreen.open) {
        for (app.saveClock += dt; app.saveClock >= 1.0f / kFramesPerSecond; app.saveClock -= 1.0f / kFramesPerSecond)
            stepSaveScreen(app);
        if (app.saveScreen.open) drawSaveScreen(app);
    }
    if (app.resumePrompt && !app.optionsOpen) drawResumePrompt(app);
    if (app.optionsOpen && (app.campOpen || app.saveScreen.open)) drawOptions(app, W, H);
    if (app.manualOpen) {
        for (app.manualMsg.clock += dt; app.manualMsg.clock >= 1.0f / kFramesPerSecond && app.manualOpen;
             app.manualMsg.clock -= 1.0f / kFramesPerSecond)
            stepManual(app);
        ++app.manualAnim;
        if (app.manualOpen) drawManual(app);
    }
    stepAchievements(app, realDt); // achievement_prc: last in gameapp_prc, over everything
    drawAchievementBanner(app);
    drawStickPointer(app); // (the port's: a controller's touch in a puzzle room)
    r.setOrigin(0);
    r.endFrame(present);
}

void setMode(App& app, Mode mode) {
    app.mode = mode;
    if (mode == Mode::Models) showModel(app, app.modelIndex);
    if (mode == Mode::Maps) showMap(app, app.mapIndex);
    updateTitle(app);
}

void step(App& app, int delta) {
    if (app.mode == Mode::Textures) showTexture(app, app.texIndex + delta);
    else if (app.mode == Mode::Models) showModel(app, app.modelIndex + delta);
    else showMap(app, app.mapIndex + delta);
}

} // namespace crapp
