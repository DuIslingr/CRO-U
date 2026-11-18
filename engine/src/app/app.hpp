#pragma once
#include "platform/vita_import_ui.hpp"
#include <SDL3/SDL.h>
#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#endif

#include <algorithm>
#include <atomic>
#include <bitset>
#include <climits>
#include <functional>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <tuple>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "assets/hd_texture.hpp"
#include "audio/at9.hpp"
#include "scene/smooth.hpp"
#include "assets/gltf_model.hpp"
#include "game/save.hpp"
#include "game/shop.hpp"
#include "assets/animation.hpp"
#include "assets/collision.hpp"
#include "assets/battle_data.hpp"
#include "assets/event.hpp"
#include "assets/gxt.hpp"
#include "assets/psarc.hpp"
#include "assets/obb.hpp"
#include "game/encounter.hpp"
#include "game/event_vm.hpp"
#include "game/field_walker.hpp"
#include "game/stage_tables.hpp"
#include "game/battle_rules.hpp"
#include "game/genes.hpp"
#include "game/puzzle.hpp"
#include "game/treasure.hpp"
#include "game/achievements.hpp"
#include "assets/model.hpp"
#include "assets/texture.hpp"
#include "audio/music.hpp"
#include "audio/sound.hpp"
#include "core/file.hpp"
#include "core/timing.hpp"
#include "core/math.hpp"
#include "gfx/font.hpp"
#include "gfx/hud.hpp"
#include "gfx/menu_painter.hpp"
#include "gfx/renderer.hpp"
#include "platform/paths.hpp"
#include "platform/setup.hpp"
#include "platform/input_cancel.hpp"
#include "platform/atomic_file.hpp"
#include "platform/lifecycle_clock.hpp"
#include "platform/ui_text.hpp"
#include "scene/pose.hpp"

#ifndef SDL_MAIN_USE_CALLBACKS
#ifdef SDL_PLATFORM_ANDROID
extern "C" SDL_DECLSPEC SDL_AppResult SDLCALL SDL_AppEvent(void* appstate, SDL_Event* event);
#else
extern "C" SDL_AppResult SDLCALL SDL_AppEvent(void* appstate, SDL_Event* event);
#endif
#endif

namespace crapp {

using uitext::tr; // the port's own text in the player's language (platform/ui_text.hpp)
using uitext::trf;

enum class Mode { Textures, Models, Maps };

// Original animations are keyed in frames, played a frame a game frame (core/timing.hpp: 20 a second)
using cr::kFramesPerSecond;

struct LoadedModel {
    cr::Model cpu;                       // skeleton, skin data, rest pose
    std::vector<cr::GpuMesh> meshes;
    std::vector<cr::Texture> textures;
    std::vector<cr::Action> actions;     // only those that fit the skeleton
    int builtin = 0;                     // the package's own (slots 0..builtin-1); then <stem>e*.mca
    std::map<std::string, int> loaded;   // LoadAct files already appended: .mca -> action index
    int action = -1;                     // -1 = rest pose
    float time = 0;
    int loopFrom = -1;                   // after its end, frames loopFrom.. again (mdl_set_act_motion_loop); -1 whole
    cr::MotionBlend blend;               // the 2-frame cross-fade into a new action (the player's)
    std::vector<char> slotOn;            // a battle model's package slots _btl_anm_load loads (empty: all)
    cr::Vec3 center;
    float radius = 1;
    int triangles = 0;
    std::vector<cr::Mat4> world;
    std::vector<float> skinned;
    std::vector<cr::Action> packageActions; // effects: every action, as materials' refGid name them (by gid)
    int materialTable = 0;                  // the action table (package action file) bound to them: _mdl_set_act
    std::vector<float> uvScratch;          // effects: a mesh's scrolled texture coordinates
};

// One playing eff3d effect (eff3d_set_with_pos[_and_ang]): an eff.bin model at a point, scaled
// (percent / 100) and turned about Y; deleted at its action's end unless it loops (until eff3d_delete)
struct Effect {
    int id = 0;
    cr::Vec3 pos;
    float time = 0;
    bool loop = false;
    float scale = 1, yaw = 0;
    float pitch = 0, roll = 0; // FrmPlayEff2: the record's X and Z angles (radians)
    // Turned to the camera each frame (mdl + 0xfc7): Omega 3.1.0's eff3d_set_with_pos[_and_ang] set
    // the byte only with G+0x364 bit 0x20 - a battle (btl_init) -, so a script's effect started in
    // the field keeps its own angles (CR1 2.2.0 sets it always)
    bool faceCamera = false;
    bool keepAngles = false;     // a battle's effect with +0xfc7 cleared after it is set (m225's 0x18): its own angles
    bool keepDepthWrite = false; // eff3d_set_with_pos_and_ang (FrmPlayEff2 at a record's point): its materials' depth writes kept
    int handle = 0;  // for stopping it
    int key = 0;     // FrmPlayEff's (effect + 1) x (actor + 1)
};

struct App {
    crinput::CancelWatch inputCancel;
    crtime::LifecycleClock clock;
    std::atomic<bool> graphicsReset{false};
    SDL_ThreadID gameThread = 0;
    bool pointerCancelled = false;
    SDL_Window* window = nullptr;
    bool rendererReady = false;
    cr::Renderer renderer;
    cr::Hud hud;
    std::string status;  // what the title bar shows, also drawn on screen
    std::string dataDir;
    // The Vita release (PCSG00499) as a source of assets (--vita, CR_VITA or settings.ini's vita=):
    // cro.psarc's textures, model packages and scripts (every .evt: eight languages, the voice
    // cues), its sound/ voices (voiceevnt / voicebtl.psarc) and music (bgm.psarc)
    std::string vitaPath;                  // the setting (applies at the next start)
    std::string vitaLoaded;                // what this run opened
    std::string vitaNote;                  // Options: the last folder chosen's outcome
    std::string vitaRoot;                  // its CR1 folder (resources/, sound/)
    std::string vitaSound;                 // its sound/ folder (with the trailing '/')
    bool vitaOnly = false;                 // no iOS data: the Vita copy is the whole game
    cr::PsarcArchive vita;
    cr::PsarcArchive vitaBgm, vitaVoice[2];
    int vitaTextures = 0;
    // The Android release (Google Play) as a source of assets (--android, CR_ANDROID or
    // settings.ini's android=): its expansion file's textures (8-bit palettes where the iPad's
    // are PVRTC) and the art only it has (docs/ANDROID.md), its table read from the APK beside it
    std::string androidPath, androidLoaded, androidNote;
    cr::ObbArchive android;
    bool androidOnly = false;              // no iOS data: the Android release is the whole game
    int androidTextures = 0;
    int ipadImages = 0, hdImages = 0;    // base names loaded as <base>_ipad.mct / <base>_hd.mct (chk_hd_name)                // how many images / model packages came from it
    Mode mode = Mode::Textures;

    std::vector<std::string> textures;
    int texIndex = 0;
    cr::Texture currentTex;
    cr::MctInfo texInfo;

    std::vector<std::string> models;
    std::vector<std::string> animFiles;
    std::vector<std::string> maps;      // field collision meshes (*_10.cmp etc.)
    int mapIndex = 0;
    cr::GpuMesh collision;
    int collisionPolys = 0;
    cr::GpuMesh fieldShadows;            // the field's shadow fans (drawShadowFans)
    std::vector<float> fieldShadowTris; // ... this frame's triangles (xyz x 3 each)
    std::vector<uint8_t> fieldShadowAlpha; // and their vertices' alpha
    int shadowsDrawn = 0;               // (tests: field shadows drawn so far)
    int effectDepthWrites = 0;          // (tests: effect materials drawn writing depth)
    // (tests: the last frame's 2D order - the sequence numbers of its portraits, its front fade and
    // its message text, 0 none)
    struct Order2D { int seq = 0, portrait = 0, frontFade = 0, text = 0; } order2d;
    int frontFadeDrawnAt = -1;          // drawFrontFade: the frame it last drew (once a frame)
    int frameNo = 0;
    bool pointerLast = false;           // the last press a touch / click (no key or pad since): touch play's look
    cr::Texture charSheet;              // tex2d slot 0x30: window_char (3.1.0: the languages' names, the boards' names)
    bool charSheetTried = false;
    float nearPlane = 10, farPlane = 4000; // setCamera3D's, the last field frame's (tests)
    std::string mapProps;               // prop model shown with the current map, if any
    bool showCollision = false;            // C: the collision and trigger meshes over the field (a debugging aid)
    bool showBackground = false;        // B: show the field's pre-rendered background
    bool hideHud = false;               // --capture renders without the text overlay
    bool padKeyNow = false;             // a key sent for a controller button (padKey): its Esc never quits
    bool viewer = false;                // launched as the viewer (--viewer / --map / --model ...): its map / model stepping keys
#ifdef CR_RELEASE
    bool showHud = false;               // the status / key help at the top: off in a Release build (F3)
#else
    bool showHud = true;
#endif
    cr::Texture background, foreground; // dXXXX-b00a00.mct / dXXXX-f00a00.mct
    cr::StageCamera stageCam;           // the field's camera from its event file
    bool haveStageCam = false;
    std::vector<cr::StageCamera> stageCams; // every map number using this collision file
    std::vector<float> stageFit;            // fraction of collision vertices each camera frames
    int stageCamIndex = 0;
    bool stageCamBorrowed = false;          // no camera of its own: best fit from the same family
    cr::Collision collisionData;           // the current field's collision, for walking on
    int mapNumber = -1, area = -1;         // the field being shown (map 0..129, area = event file)
    cr::EventFile events;                  // the current area's event file
    std::vector<cr::FieldNpc> npcs;        // this map's NPCs and trigger boxes
    std::vector<bool> npcWarps;            // per NPC: its touch script warps (from a trial run)
    cr::GpuMesh triggerMesh;               // trigger boxes, shown with the collision overlay
    cr::GpuMesh puzzleBeams;               // the candle switches' beams (CandleSwitch::draw3D)
    cr::GpuMesh battleShadows;             // the fighters' floor shadows (_btl_shadow's fans)
    cr::GameState game;                    // flags / variables the scripts use (new game)
    int lastKick = 0;                      // G+0x8a980: the touch box last kicked - kept across warps and loads,
                                           // 0 once the leader stands in none (RectWrapper::checkEventKick)
    std::string lastWarp;                  // shown in the status line
    std::string lastEvent;                 // last examined object / items, for the status line
    cr::Hud dialogueHud, promptHud, itemHud;
    std::array<cr::Hud, 16> lineHud;       // the message window's lines one by one (some centred; _PutMsg's 16 slots)
    cr::EventRunner runner;                // the script being played, a frame at a time
    cr::SoundSystem sound;                 // sndeff.bin through SDL audio
    cr::MusicPlayer music;                 // bgmNN.caf (AAC) through FAAD2 + SDL audio
    int musicArea = -1;                    // area whose default track is playing
    // Mixer, 0..100 like the original's Options sliders (music and effects default to 80 there;
    // master and voice are additions - the original has no voice audio)
    struct Mix { int master = 100, music = 80, effects = 80, voice = 80; } mix; // (the options' defaults 80 / 80 / 80)
    bool optionsOpen = false;              // V: the Options screen (__menuu_Option*, plus our pages)
    int optionsPage = 0;                   // Game, Sound, Display, Graphics
    int optionsRow = 0;                    // the cursor: a row, or kOptionsBackRow
    int optionsScroll = 0;                 // the first row shown (a page of more than kOptVisible scrolls)
    bool optionsBarDrag = false;           // the scroll bar's thumb held
    int optionsAnim = 0;                   // frames since the page appeared (windows slide in)
    int optionsBob = 0, optionsBobDir = 1; // ComUty_CmdBack's arrows
    struct GaugeEase { int from = -1, to = -1, left = 0; }; // SideGuageProc / Draw: the fill 10 frames from the old
    std::map<const int*, GaugeEase> gaugeEase;           // knob to the new (#17), by the setting it shows
    float optionsClock = 0;
    bool environmentEffects = true;        // the original's Environment Effects (weather; stored)
    bool lockLandscapeFlipped = false; // the landscape direction retained by the orientation lock
    bool lockOrientation = false;          // Lock Screen Orientation (an iPad setting; stored)
    cr::MenuPainter menu;
    bool persist = true;                   // settings.ini is written (not by tests and captures)
    std::string screenLogged;              // the last "screen:" log line's values (logged again when they change)
    // The title: the boot script (j001_t99's EvrStart 0: the logos, the title picture and music,
    // then Util 0's menu - Start / Scenarios / Options)
    bool booting = false;                  // no field behind it; the script ticks on its own
    float bootClock = 0;
    bool titleOpen = false;                // Util 0
    int titleCursor = 0;
    int titleAnim = 0;
    int titleStage = 0;                    // __menuu_TitlePrc: 0 the title alone, a tap away (state 1); 1 the plates (7 / 8)
    int resumeIn = 0;                      // the Progress Tracker's answer: frames left before the load / Start (TitlePrc 0x1d / 0x1e)
    int titleReturnIn = 0;                 // Options' Return to Title: frames of black left before the title (OptionPrc 0x18)
    // __menuu_TitlePrc 3..6: the version-up boards after the tap (titleStage 2) - "New fun stuff
    // available after clearing the game!", each newly opened "Breaking the Limit" trial, "Check out
    // the Info Board next to Piu-Piu", "Voices added!" - one a tap, once a launch (_gbVerUpMsg)
    struct TitleBoard {
        int state = 0;                     // 4: the trials' boards (or the one board), 5: the Info Board's / voices
        int first = -1, count = 0;         // DAT_e5b08 / e5b0c: the next trial named, how many are left
        bool voices = false, cleared = false; // DAT_e5b10: "Voices added!" waits; flag 0x1f
        int lines[3] = {0, 0, 0};          // the board's system messages (__Title_ChangeMsg)
    } titleBoard;
    bool verUpShown = false;
    bool optionsFromTitle = false;         // ModeOptionMenu(1): Reset Data instead of Return to Title
    int quitAsk = 0, quitConfirm = 1;      // the title's Quit question (ours, the desktop's: #16), No under the cursor
    int resetAsk = 0, resetConfirm = 1;    // Reset Data's two questions (OptionPrc 9 / 10: 1, 2), No under the cursor
    bool captureTitle = false;             // --capture ... --title
    // Video. The defaults are the original presentation: a 1024x768 window, 4:3, the paintings at
    // the screen's height, native resolution, no MSAA, vsync
    struct Video {
        int width = 1024, height = 768;    // window size (windowed) or display mode (fullscreen)
        int display = 0;                   // 0 windowed, 1 fullscreen (that mode), 2 borderless (the desktop's)
        int fullscreenKind = 2;            // F11 / Alt+Enter's fullscreen: the last one chosen (1 or 2)
        int savedDisplay = 0;              // settings.ini's display=: Options' choice alone - F11 / Alt+Enter and the
                                           // command line change display for the session (#15)
        int toggleKind = 0;                // F11's fullscreen this session (0: fullscreenKind's)
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        bool widescreen = true;            // the screen widens to the window (up to 21:9); else 4:3 (a phone's: wide, filling it)
#else
        bool widescreen = false;           // the screen widens to the window (up to 21:9); else 4:3
#endif
        int paintings = 0;                 // painted rooms: 0 the original's (2x, a screen-sized window following
                                           // the player), 1 Fill: the same, zoomed until the room's painting covers
                                           // the screen (no bars on a wide screen) - the port's own
        int renderScale = 100;             // % of the window's pixels (Vulkan)
        int msaa = 1;                      // 1, 2, 4, 8 (Vulkan)
        bool vsync = true;
#ifdef SDL_PLATFORM_IOS
        int frameLimit = 0;                // (an iPhone's ProMotion: 120 - Info.plist's CADisableMinimumFrameDurationOnPhone)
#else
        int frameLimit = 60;               // frames drawn a second at most (0: as many as the display takes); the
#endif
                                           // game itself runs at 20 (core/timing.hpp), the frames between show
                                           // it moving
        int upscaler = 0;                  // 0 off, 1 FSR 3.1, 2 DLSS (Vulkan; MSAA is off meanwhile)
        int upscaleMode = 0;               // kUpscaleModes: 0 native AA (DLAA) .. 4 ultra performance
        int sharpness = 25;                // % (FSR's RCAS pass; 0 = none)
        bool hdTextures = false;           // upscaled replacements from hdDir (what loads next)
        bool avoidCutout = true;           // phones: the layout kept clear of a camera's notch / punch hole (Options, Display)
        bool cutoutBars = false;           // ... and the picture too: boxed inside the safe area, black bars over the cutout
        int smoothModels = 0;              // 0 off, 2 / 3: PN-triangle level for characters and monsters
        bool hdModels = false;             // replacement models from modelsDir (glTF; what loads next)
        int anisotropy = 16;               // anisotropic filtering's samples on the mipmapped textures (1 off)
        bool sharpTextures = false;        // those magnified by the nearest texel (square pixels up close)
        int layout = 0;                    // 0 the data's own (2.2.0's iPhone layout for its data), 1 iPad, 2 iPhone
    } video;
    bool phoneData = false;                // the data is a voiced iPhone release's (voiceevnt.bin, no _ipad art)
    std::string hdDir;                     // HD textures: textures/hd/ in the game folder, or CR_HD_TEXTURES
    int hdLoaded = 0, hdMissing = 0;       // replacements found / not found since startup
    std::set<std::string> hdRefused;       // replacements left out (not their pictures' shape): said once
    std::string modelsDir;                 // replacement models: models/hd/ in the game folder, or CR_HD_MODELS
    int hdModelsLoaded = 0;                // since startup
    int language = 1;                      // text language (cr::kLanguageName): English by default
    std::string uiLang = "auto";           // the port's own text (platform/ui_text.hpp): "auto" (the text language's) or a code
    // A fan translation as a language of its own (cr::kFanLanguage; #11): `language` stays English's (its
    // layout, its font), the messages from the translation's own files and its art over the game's
    struct FanSource {
        cr::PsarcArchive archive;                            // its cro.psarc (a rePatch's)
        std::unordered_map<std::string, std::string> files;  // or a folder of the files themselves: base name (lower case) -> path
        std::string where, code, name;                       // where it is; its language ("ru"); that in itself ("Русский")
        bool isOpen() const { return archive.isOpen() || !files.empty(); }
        bool read(const std::string& name, std::vector<uint8_t>& out) const;
    };
    FanSource fan;
    bool fanText = false;                  // the fan translation is the text language (settings.ini's fan_text=)
    cr::Font font;                         // system font for the game's text (all languages)
    struct VoiceCue { int at = 0, clip = -1; }; // PrcMsg 0x1e at `at` characters of the page (clip -1: 0xffff, keep)
    std::vector<std::vector<VoiceCue>> dialogueVoices; // per page: the voice clips it starts, in typing order
    int dialogueSerial = 0, voicedSerial = -1, voicedPage = -1; // which page's voices were started
    size_t voiceCue = 0;                   // the page's next cue (the typing waits at it while a clip still plays)
    bool voiceKeep = false;                // PrcMsg 0x1e 0xffff: the page's clip goes on past the next tap
    int voicesPlayed = 0;                  // for tests
    int soundTestEntry = 0;
    std::vector<int> voiceEntries;         // bank entries marked as voice (sound test, T)
    cr::Hud optionsHud;
    cr::Texture windowTex;                 // window.mct (chk_hd_name: window_ipad on the iPad): the UI atlas
    bool androidWindow = false;            // the loaded Android atlas uses a separate dialogue parchment
    cr::Texture cutinTex;                  // window_cutin.mct (tex2d slot 0x2b on the iPad): the talk parchment
    cr::Texture parchPaperTex, parchStreakTex; // splitParchment's: the phone parchment's paper and its name streak apart
    unsigned parchSplitFor = ~0u;          // (the window atlas they were made from)
    float parchTop = 0;                    // the split paper's top edge beside the streak (piece units): it stays put as the paper scales
    cr::Texture cutinWideTex;              // window_cutin_<w>.mct (Android, slot 0x2f): androidCutin's
    std::string cutinWideName;
    cr::Texture speedLinesTex;             // syuutyuusen(_ipad).mct (slot 0x2e): scene effect 1
    cr::Texture titleArt[2];               // __menuu_TitleSet: title1.mct (MuCom slot 0x11), chaos.mct (0x12)
    bool titleArtIpad = false;             // title1 came from its _ipad sheet (pictures 9 / 10; else 7 / 8)
    float footPrev = 0;                    // player's animation time last frame (footsteps)
    std::vector<uint32_t> soundLog;        // every sound started (self-test)
    std::vector<uint32_t> menuSoundLog;    // the menus' sounds, latest last (self-test)
    std::string runnerWhat;                // what started it, for the status line
    // Scripts the game starts itself when a field loads (fldread_prc): script 0 when the area's
    // file loads (init_evnt), EvrStart entry 5 on other room changes. Queued until the running
    // script (e.g. the exit that warped here) finishes.
    enum class Entry { None, Area, Room } pendingEntry = Entry::None;
    int entryArea = -1;                    // area whose script 0 last ran
    std::map<int, bool> npcShown;          // npc ctl +0x8c bit 0, cleared: set by npc_setup at each load, then Born / Kill (npc 0 = the player)
    std::map<int, int> examineLinger;      // FollowWindow / FlagWindow: an NPC's window, frames left after leaving reach (10)
    std::set<int> flagTalked;              // FlagWindow::Work state 2: a person talked to - no "!" until back in reach
    bool hudMenusWere = false;             // the Camp, Options or the save screen open last frame (their Clr: _MfMenu_RestFieldState)
    bool scriptAreaWarped = false;         // the running script's Warp changed the area (abandoned with its file)
    std::map<int, std::pair<int, int>> npcJoints; // Joint: npc -> (parent npc, its node), this room
    struct Move {                          // FrmAdjc / FrmAdjc2 (npc_autoctl_prc), npc 0 = the player
        int npc = 0;
        cr::Vec3 from, to;
        float yawFrom = 0, yawTo = 0;      // radians, when turning
        bool turn = false;
        int mode = 0;                      // 2: y moves too, else it follows the floor
        int frames = 1, t = 0;
    };
    std::vector<Move> moves;
    struct Rescale {                       // AdjScale (npc_autoctl_set_scale: autoctl mode 3) from -> to over `frames`
        int npc = 0;
        float from[3] = {1, 1, 1}, to[3] = {1, 1, 1};
        int frames = 1, t = 0;
    };
    std::vector<Rescale> rescales;
    std::set<int> placedNpcs;              // actors a script placed or showed: parked slots (y 1000, origin) appear
    std::map<int, std::map<int, int>> actorSlots; // LoadAct: npc -> slot -> action index in its model
    struct ScriptCam {                     // FrmSetCam / FrmAdjCam / FrmPanCam after CameraFree
        bool free = false, set = false;
        cr::Vec3 eye, target, eyeFrom, eyeTo, tgtFrom, tgtTo;
        int frames = 0, t = 0;
    } cam;
    // The painted field's scroll (FieldCamera +4 / +8): the view's centre moved from the screen's,
    // x right and y up; ScrollAutoCtl's slide (FrmAdjCam / FrmPanCam) from -> to over `frames`
    struct FieldScroll {
        float offX = 0, offY = 0;
        bool snap = true;                  // the next follow lands at once (onFadingIn's rate 1.0 as the map comes in)
        // The scroll the scripts set (GetScroll / SetScroll's): not held to the painting - only what is
        // shown is (offX / offY) - so FrmPanCam there and back again over its edge comes back where it was
        float curX = 0, curY = 0;
        bool scripted = false;
        bool sliding = false;
        float fromX = 0, fromY = 0, toX = 0, toY = 0;
        int t = 0, frames = 0;
        int mapScrolls = 0;                // Mapscroll ops seen (self-test)
    } scroll;
    // Scene effects (Omega's ScnEffectRead / ScnEffectStart / ScnEffectProc): a field's weather and
    // backdrops. The set's textures (tex2d slots 0x30..0x58, with tex2d_read's logical sizes), which
    // effects are on (1..30), each effect's two counters (+0x178 / +0x180 of its own slot) and
    // UpdateRandom's fading alpha channels
    struct SceneFx {
        int set = -1;
        std::array<cr::Texture, 41> tex;   // slot 0x30 + i
        std::array<float, 41> texW{}, texH{};
        std::array<bool, 31> on{};
        std::array<int, 31> timerA{}, timerB{};
        struct Channel { // InitRandom / UpdateRandom: _s_type, _s_alpha, _s_speed, _s_interval0..3, _s_mode, _s_count
            int type = -1, alpha = 0, speed = 0, iv[4] = {}, mode = 0, count = 0;
        } ch[3];
        int maxTime = 0, minTime = 0;      // _s_maxTime / _s_minTime (shared)
        uint32_t rng = 12345;
        int frameCt = 0, frameCt1 = 0;     // _g_nFrameCt / _g_nFrameCt1: effect 28's scroll (not reset by a set)
    } fx;
    struct Portrait {                      // Tex2d slots (+0xbd10 + slot * 0x70 in the original)
        cr::Texture tex;
        int image = -1;
        float x = 0, y = 0, alpha = 0;     // 1024x768 screen pixels
        float home = 0;                    // resting x (left / right / centre)
        int side = 0;                      // -1 left, 1 right: the side it slides from
        float shade = 1;                   // 0x1c in a message dims the listener's side to 128/255
        float xFrom = 0, xTo = 0, yFrom = 0, yTo = 0; // the slide (+0x14..+0x1e): t of frames
        int frames = 0, t = 0;
        float aFrom = 0, aTo = 0;          // the fade (+0x20..+0x26): aT of aFrames
        int aT = 0, aFrames = 0;
        float gFrom = 1, gTo = 1;          // the grey level (+0x28..+0x2e, low bit 0x20) into shade
        int gT = 0, gFrames = 0;
        bool dying = false;                // low bit 2 (+0xbd0c = -1): goes once its fade and slide end
        uint16_t flags = 0;                // the slot's flag word (+0x4): Tex2d flags OR'd in
        bool ipadCard = false;             // a full-screen card from the _ipad sheet (the Android layout)
        float scale = 1;                   // 0x40: centre images zoom from 200% to 100% over 4 frames
        int zoomT = 0, zoomFrames = 0;
    };
    Portrait portraits[16];
    bool itemWindowOpen = false;           // Uty_GetItem
    // The skip corner: _PrcMsg turns a page (and ends typing) on every frame _pad_chk_circle(476, -16, 20)
    // finds a touch held there - pages turn while it's held and stop with the finger. skipHeld: 1 the X
    // key, 2 the pad's square, 4 a pointer held on the corner; skipScene = any of them
    unsigned skipHeld = 0;
    bool skipScene = false;
    int msgStyle = 2, msgLines = 2;        // the script's last Open: the message window's style and lines
    // The window itself (PrcWin: _SetWin / _ClrWin): 0 none yet, 1 / 2 fading in over 4 frames
    // (the text waits), 3 up, 4 / 5 fading out over 5 frames, 6 gone
    struct MsgWin { int state = 0, t = 0; float alpha = 0; } msgWin;
    bool fieldHidden = false;              // Screen's toggle (gac + 0x8ecde): the field not drawn
    bool npcNativesPending = false;        // npc_setup: the rows' natives (+8) to run for the room's NPCs as they are created
    int msgColour = -1;                    // the page's message's 0x16 colour (MessageInfo::colour), -1 none
    double playSeconds = 0;                // TOTAL PLAYTIME: time on the field and in battles
    // The save / load screen (__menuu_SaveLoad*): from the Camp menu (save) or the title's Start
    // (load / new game). phase 0 choose, 1 confirm (Yes / No), 2 done (a message, then it closes)
    struct SaveScreen {
        bool open = false, load = false, haveSave = false;
        int cursor = 0, phase = 0, confirm = 0, message = 0, timer = 0, anim = 0;
        int pending = 0;                   // what Yes does: 1 save, 2 load, 3 new game
        bool toTitle = false;              // Util 6's (save menu mode 3): the title follows, saved or not
        bool askLeave = false;             // ... its "return to the title without saving?" (0x3c) after a No
        int kind = 0;                      // the title's choice, var 1 on the way out (SaveLoadClr):
                                           // 1 the last battle, 2 new, 3 new with the clear data, 4 load
        int lines[3] = {};                 // the message board's further lines (system message ids)
        unsigned emblems = 0;              // the pair's three emblems (flags 31 + 2p, 32 + 2p, 39)
        bool allClear = false;             // flags 32, 34, 36, 38: every pair's story cleared
        bool cleared = false;              // flag 40 + p: this pair has clear data to carry over
        cr::SaveData data;                 // the save slot's contents (shown on the left)
    } saveScreen;
    bool campOpen = false;                 // the Camp menu (K on the field)
    // The shop (Util 2, __menuu_Shop*): Talk / Buy / Sell, then a category, the list, how many,
    // Yes / No. Talk hides it and plays the area's EvrStart 4 (the shopkeeper) while the script
    // that opened the shop is parked.
    struct Shop {
        // Member: __Buy_CharChoiceProc's pair (Buy's Weapons / Armor / Jewel, Sell's all but Items)
        enum Phase { Main, Category, Member, List, Count, EquipAsk, Talk } phase = Main;
        bool open = false, selling = false;
        // listCursor: the focus row, on MuScrList's line at y 150; scroll: the rows' offset now, on
        // its way to 48 x the focus 8 a frame (listTop: kept equal to the focus)
        int cursor = 0, category = 0, listCursor = 0, listTop = 0, count = 1, confirm = 0;
        float scroll = 0;
        int mark = 0;                      // the message board's mark (ComUty_PictAnimInit(1, 4, 20): out and back)
        int bob = 0, bobDir = 1;           // ComUty_CmdBack's arrows (0..5 and back) and the gauge's (PictMove 0..4)
        int member = 0;                    // the board's character: the pair's first (0) or second
        int bgmBack = 0;                   // ShopSet: var 0, the music to put back as it closes (+0x18)
        bool noBgmRecovery = false;        // SetShopBgmNoRecovery (Uty_EndMenu 2) (+0x1c)
        int anim = 0, timer = 0, message = 0; // message: the board's system message (0 none)
        int level = 2;                     // MuCom_GetShopBuyLevel
        bool talkRunning = false;          // Talk's script is the runner (the shop's own is parked)
        std::vector<int> list;             // the category's items
        cr::Texture keeper;                // c101_L01_hd.mct: the shopkeeper
    } shop;
    cr::EventRunner parkedRunner;          // the script that opened the shop, while Talk runs
    float shopClock = 0;
    cr::ItemTable itemTable;               // sys.evt: item prices, icons, kinds
    // Character Select (Util 4, __menuu_Start*): the boot script's pair choice before the save /
    // load screen; the chosen pair (1..4) goes to var 1 through that screen
    // Scenarios (__menuu_Sccnarios*): the bingo cards - from the title (mode 0, to look at) or the
    // Camp (mode 1, where a cell the bag has a present for opens)
    struct Scenarios {
        bool open = false;
        int mode = 0;
        int anim = 0;                      // frames since the page came up
        int card = -1;                     // the card shown, -1 the overview
        int cursor = 0;                    // (keys) the overview's card, a card's cell
        std::array<int, 16> cell{};        // __SetBingoPushAndOpen: 0 shut, 1 opening, 2 open
        std::array<bool, 16> canPush{};    // a present in the bag for it not yet used on any card
        int pushing = -1, pushClock = 0;   // the cell opening (states 0xf..0x11)
        float zoom = 1, panX = 0, panY = 0; // a whole card's picture: tap to zoom, drag about (tex2d)
        int zoomDir = 0;                   // 1 in, 2 out (DAT_e60f8)
        int bob = 0;                       // ComUty_PictMove: the Touch! / PUSH! pictures' 0..4
        bool bobBack = false;
        bool keys = false;                 // (the port's) a key moved the cursor: show it
        float clock = 0;
        float dragMoved = 0;
        cr::Texture sheet, cardTex, photo; // tex_scenario, tex_card_<colour>, present0N_hd
        int cardLoaded = -1;
        cr::GameState flags;               // the title's: every save's flags together
        // SccnariosPrc 0x12..0x14, a card's sixteenth cell opened: tex_card_complete's seven pieces fly
        // in and bob (eleven keyframe steps), the music muted, then white over 21 frames and back
        int complete = 0;                  // 0 none, 1 the pieces, 2..5 the white (DAT_ed5dc's states)
        int cStep = 0, cCount = 0;         // DAT_ed544 / ed548
        std::array<std::array<int, 2>, 7> piece{}; // DAT_ed550: where each piece is
        cr::Texture completeTex;
        // A card's windows 6 (its name), 7 (the frame and cells) and 8 (the photo): ComUty_WinDispOn /
        // Off, PrcWin's states (1 / 2 opening, 3 up, 4 / 5 closing, 6 gone) sliding in x over 4 frames
        struct Win { int state = 6, t = 0; float from = 0, to = 0; };
        std::array<Win, 3> win{};
        // < > (SccnariosPrc 8 -> 0xc..0xe, 5): the windows slide out (6 left; 7 and 8 left for >, right
        // for <), the next card is read and they slide in from the other side (6, 7) - 1 that, 2 the
        // card's windows coming on (states 6 / 7: no input, the photo unseen), 0 the card up (8)
        int phase = 0, phaseCount = 0;
        int switchDir = 0, nextCard = -1; // DAT_ed314: 1 >, 2 <
    } scc;
    struct Collection { // the Camp's Collection (__menuu_Collection*)
        bool open = false;
        int anim = 0;
        float clock = 0, spin = 0;
        cr::Texture tex;               // collection_rate
    } collection;
    struct Achievements { // game/achievements.hpp: the earned set (settings.ini), achievement_prc's banner, the list
        cr::achievements::Flags flags; // the system config flags (1..32 earned; 0x2b.. what the battles and the shop saw)
        cr::achievements::Banner banner;
        std::vector<int> queue;        // earned, banner not shown yet (a battle's wait for the field)
        float clock = 0;
        bool tap = false, eatUp = false; // a press for the banner (pad_chk_trg_down); its release kept from what's under it
        bool live = true;              // earned at all (off in the self-test's other checks and in captures)
        // The list (Android's Store menu in mode 4, from the title's picture 127; ours in every layout)
        bool listOpen = false;
        int listAnim = 0, listCursor = 0;
        float listScroll = 0, listClock = 0;
        bool listDrag = false;
        float listDragY = 0, listDragScroll = 0;
        std::vector<int> listRows;     // the achievements listed, in order (secret ones only once earned)
        cr::Texture icons;             // achievement.mct (Android's art; none in iOS data)
        bool iconsTried = false;
        std::vector<int> earnedLog;    // (self-test) every achievement earned this run, in order
    } ach;
    bool manualOpen = false;               // Options > Instruction Manual
    int manualCursor = 0, manualTop = 0, manualAnim = 0;
    int manualIntro = 0;                   // the page shown before a topic is chosen (56040), 0 none
    bool charSelectOpen = false;
    int charSelectCursor = 0, charSelectAnim = 0, chosenPair = 1, charSelectPage = 0;
    struct StartPage { // StartPrc / StartPut: the second scenario's page, pulled down by its cord
        int phase = 0;             // 0 still, 1 falling, 2 / 3 its bounce, 4 going back up, 5 / 6 the card in / out
        float card = -160;         // serect 28's x (DAT_e59a8): in on the second page once flag 0x643 is set
        int t = 0;                 // frames into the fall (DAT_e596c)
        float y = -440;            // its offset (DAT_e5994): from e59b0 down to 0
        bool shown = false;        // window 1 on
        float sway = 0;            // the cord's turn (DAT_e5988: _gafMoveTbl summed, out and back)
        int swayStep = 0;
        bool swayBack = false;
        bool holding = false;      // a finger on the cord (DAT_e599d)
        float holdY = 0, pull = 0; // ... where it went down, how far it's pulled (0..24)
        bool padPull = false, padPullFresh = false; // the Vita's Down held pulling it (a fresh press each turn)
        int bob = 0;               // the arrow's PictMove (0..4, 2 a step)
        bool bobBack = false;
        float clock = 0;
        cr::Texture serect;        // slot 0x11: the cord, the arrow
    } startPage;
    cr::Texture charSelectArt, charSelectArt2; // Select Scenario's sheets: chara1 / chara2 (slot 0x10), chara3 (0x12)
    struct Camp {                          // the Camp menu's page and cursors (__menuu_CampMain*)
        enum Page { Main, Items, Equip, Status, Genes } page = Main;
        enum Phase { List, Member, Slot, Ask, Entries, Result, Exit } phase = List; // Status: Slot = the member's
                                           // details; Entries: a plate's known-entry list (CampMainPrc 0x1a-0x1d,
                                           // Genes 4-5); Result: the board after an item is used (0's 8-10); Exit:
                                           // the Emergency Exit's frames (0's 7, 0xf-0x12, then CampMain 9, 0xd-0x10)
        int cursor = 0;                    // Main: the plate
        int btnRow = -1;                   // Main, the Vita's pad: on the buttons below the boards (DAT_81121d64 / d58) -
                                           // 0 Scenarios, 1 Collection; -1 on the plates
        int category = 0;                  // Items: campui::kCategoryKind index
        std::vector<int> list;
        int listCursor = 0, listTop = 0;
        int plate = 0;                     // Entries: the plate chosen (DAT_000ec71c / DAT_000ec770)
        int plateCursor = 0, plateTop = 0; // ... and where the plate list was, for Back
        int member = 0;                    // the pair's first or second
        int slot = 0;                      // Equip: weapon, armor, jewel, Human Genes, gene plates 1-3
        int slotTop = 0;                   // the slot list's first row shown (a MuScrList of seven, four rows)
        int count = 1, confirm = 0;        // Items: how many seeds, Yes / No
        int resultItem = 0, resultCount = 0; // Result: the item used and how many (DAT_000ec6ac / ec6c4)
        int exitT = 0;                     // Exit: frames since Yes
        int statusView = 0;                // Status details: 0 the board, 1 the equipment, 2 the skills (CampMainPrc 3's 5 / 8 / 0xb)
        int viewTop[3] = {0, 0, 0};        // ... each list's first row
        int notice = 0;                    // a once-only message up over the Camp (MsgUty: 56010 / 56016), 0 none
        std::string message;
        int messageFrames = 0;
        int anim = 0;
        // The scrolling list's offset (MuScrList eb6c0, here positive, in menu units): 48 x the focus row once
        // settled, stepping 8 a frame toward it; scrollKey names the list it belongs to (-1 none yet)
        float scroll = 0;
        int scrollKey = -1;
        int mark = 0;                      // the message board's mark (DAT_000ec5b4's count, one a frame)
        // Scenarios chosen (CampMainPrc 6 -> 7, FUN_0006c548): the main page's windows go - the boards (2)
        // to x -240, the plates (3) to +360, the Scenarios / Collection pictures (0x14 / 0x15) to -120,
        // the rest fading - and Scenarios opens 4 frames on (frames into it, 0 none)
        int leaving = 0;
        bool closing = false;              // ... the same frames for Back on the main page, then the Camp closes
        // Back from a page to the main page (CampMainPrc's last states, then CampMain 9): frames since the page's
        // windows began going, 0 none - the main page's come back once they have gone (campPageOutFrames)
        int pageOut = 0;
        // The Items page's states (CampMainPrc 0): the one left takes its windows off, the next puts its own on a
        // frame or two later (campPhaseEase); phaseT counts frames since the change (100: settled)
        Page shownPage = Main;
        Phase shownPhase = List, prevPhase = List;
        int phaseT = 100;
        // Equip's states (CampMainPrc 1, equipWins): the list a plate's entries replaced (or the entries the
        // plate list replaced), going with its windows; how a list was left - 0 a choice, 1 Back, 2 Remove; the
        // L / R member change (7 -> 10): frames since, its way (+1 the windows go right, -1 left), who was shown
        std::vector<int> goneList;
        int goneCursor = 0;
        int equipHow = 0;
        int swapT = 100, swapDir = 0, swapFrom = 0;
        bool barIn = true;                 // the main page's Back bar (window 4) coming on with it: at the Camp's
                                           // opening and back from Items (the other pages leave it up)
        std::array<cr::Texture, 31> plateArt; // gene_m*c.mct, loaded when a board first shows them
        std::array<cr::Texture, 11> statusArt; // _gkpachFaceImage: the records' Status portraits (j039_L01..)
    } camp;
    float campClock = 0;
    struct CampPointer {
        bool down = false, dragging = false, bar = false;
        float startX = 0, startY = 0, lastY = 0, remainder = 0;
        float curPx = -1e9f, curPy = -1e9f;   // where it is now (the frame's pixels): the control held lights
        Camp::Page page = Camp::Main;
        Camp::Phase phase = Camp::List;
    } campPointer;
    float saveClock = 0;
    // The Progress Tracker's offer (TitlePrc 0x16..0x1f): "About Progress Tracker" (56041) once a
    // launch while flag 0x44 is clear, then the interrupted party's boards, its date and Yes / No
    int resumePrompt = 0;                  // 1: "You have save data from your last session" (Yes / No, 0x1b); 2: its windows going (0x1c..)
    int resumeCursor = 0;                  // 0 Yes (the upper plate), 1 No
    int resumeNotice = 0;                  // 0x17..0x19: 1 the message being read, 2 its window closing
    bool resumeYes = false;                // DAT_ecb6c: Yes chosen (the music off and the black, then the load)
    float resumeWin = 0;                   // windows 4..7 (ComUty_WinDispOn / Off): 0 out .. 1 in
    int resumeMark = 0;                    // the board's tap mark (ComUty_PictAnim of picture 13: 1 tick a frame, out and back)
    // Prompts seen outside a save (the title's: About Option 0x43, the Progress Tracker 0x44, the
    // version-up boards' marks 0x6f..0x7b, 0x75): 3.1.0 sets them in the flags in memory, which only a
    // later save keeps - so with no save written they came back each launch. Kept in settings.ini
    // (seen_flags=) and read as the save's (allPairsFlags, titleAnnounce)
    std::vector<int> seenFlags;
    bool interruptShown = false;           // _gbTutoFlg (MuTitle_GetInInterruptFlg): the message shown this launch (TitleSet keeps it)
    int titleOut = 0;                      // frames since the title's plates began going for the offer (window 3 off)
    cr::SaveData resumeData;
    vitaimport::Session saveImport;
    std::string saveDirOverride;           // tests: where save.dat / resume.dat go
    bool trackerArmed = false;             // G+0x34e1d: the Progress Tracker saves from the second scene's end on
    // A controller, as the Vita's buttons (its manual, 56000 / 56025): the D-pad / left stick move
    // and pick, ○ confirms, × cancels (the Japanese release's; padConfirmEast false swaps them), △
    // opens / closes the menu, □ skips text, L / R turn pages; in puzzle rooms START restarts,
    // SELECT shows the help, and □ (no text to skip) uses the ability, which the Vita leaves to touch
    SDL_Gamepad* pad = nullptr;
    // The other controllers connected, open too: SDL sends a controller's buttons and sticks only once it's open,
    // and one pad can come as two (CRO-U #4: a Flydigi Vader 4 Pro listed twice as "Xbox 360 Controller", its
    // input on the second while the first was taken). The one pressed or moved last becomes `pad`
    std::vector<SDL_Gamepad*> morePads;
    // The self-test's own controllers (its virtual pads, by joystick id): while it runs, no other is opened or taken -
    // another program's virtual "Xbox 360 Controller" (a session driving an emulator, say) can't take a case over.
    // Empty: every pad
    std::vector<SDL_JoystickID> padAllow;
    bool padConfirmEast = true;
    // CRO-U's own (Options, Game: Field Speed, Battle Speed): the field - while the player walks it,
    // nothing over it - and the battles run this many times as fast (1, or 2 .. 5)
    int fieldSpeed = 1, battleSpeed = 1;
    // Options' Cheats (the port's own, at the user's word; settings.ini's cheats=): the pair's HP and MP never run
    // down (in battle the numbers still show), the Oz at the most, the battles' EXP multiplied, every random
    // encounter the party's first strike - or none at all -, the pair's blows felling what they hit
    struct Cheats {
        bool hp = false, mp = false, oz = false, firstStrike = false, noEncounters = false, oneHit = false, fullBreak = false;
        int exp = 1; // 1, 2, 3, 5, 10
    } cheats;
    int padHeld = -1;                      // the direction held for the menus' repeat (0 up 1 down 2 left 3 right)
    float padHeldFor = 0;
    // A touch held on a menu (_pad_chk_updw_box): the screen it went down on and where it is now (the
    // frame's pixels). The control under it is lit while held; its action runs as it lifts there (trg)
    struct Press { bool down = false; int screen = 0; float px = 0, py = 0; } press;
    int countHeldSide = 0, countHeldFrames = 0; // a count's < or > held by a touch (_ComUty_NumCountProc's +0x14)
    float countClock = 0;
    int padCountDir = 0;                   // a count's L / R (or left / right) held: -1 / +1 (FUN_8105bea8)
    float padCountFor = 0;
    crinput::HeldOver padHeldOver;         // (dead after a cancel or a session's end, until let go)
    int itemWindowItem = 0;
    int itemWindowRow = 0, itemWindowFade = 0; // Uty_GetItem's band: its row (y = 32 x row - 32) and fade steps
    bool itemWindowClosing = false;        // flag 0 raised: it slides on down and fades (Uty_GetItem_Proc)
    std::string itemWindowText;            // font block 6: "You got ...!" (10), Oz (0x55), empty (0x71)
    int itemWindowIcon = -1;
    bool tapPending = false;               // action key while a script waits for a tap
    float fadeGrey = 0, fadeFrom = 0, fadeTo = 0; // Screen
    float fadeBackGrey = 0, fadeBackFrom = 0, fadeBackTo = 0; // Screen's back channel (op 58, _SetFade_back)
    int fadeBackFrames = 0, fadeBackT = 0;
    // The menus' mask (MuCom_MenuSetMask / ClrMask: SetFade_back(black, 0 <-> 0xa0, 400 ms) with
    // G+0x39c60 its shape - 0 the screen, 1 its left past a slant, 2 its right, 3 fading downward)
    struct MenuMask {
        int type = 0, want = -1;           // the shape; the menu asking for it (-1 none)
        int from = 0, to = 0, t = 0, ms = 1; // the fade: from + (to - from) x t / ms, t + 50 a frame
        bool tuto = false;                 // a tutorial picture's (MuCom_MenuSetMask / ClrMask: the same 400 ms)
        float clock = 0;
    } menuMask;
    int fadeFrames = 0, fadeT = 0;
    cr::SystemText systemText;             // sys.evt: messages >= 51000
    std::vector<std::vector<std::string>> dialogue; // pages of the messages the last event showed
    size_t dialoguePage = 0;
    std::vector<cr::MessageInfo> dialogueInfo; // per page: the speaker's name plate and side
    // PrcMsg's typing: the page's characters out so far (one a frame), a 0x1a pause's or the page
    // turn's frames left, the next pause; dialogueInstant: the typing flag (G+0x8887b) down - a tap
    // or the skip corner brought the rest out at once (still held by pauses, line breaks and voices)
    int dialogueTyped = 0, dialogueHold = 0, dialoguePause = 0;
    bool dialogueInstant = false;
    bool typeOn = true;                    // false: pages appear whole (tests, captures)
    int textLimit = 1 << 30;               // drawMessageLines: how many characters it draws
    std::vector<std::string> lastPage;     // the page just read: stays up while a choice asks about it
    bool heldPage = false;                 // ... or because it ended itself (0x14): up until Close (PrcMsg)
    cr::MessageInfo lastPageInfo;
    int choiceFlag = -1;                   // Util 3: the flag a yes/no choice answers (-1 = none open)
    std::vector<std::string> unsupported;  // battles / menus scripts asked for (--explore reports them)
    cr::BattleData battleData;             // sys.evt: monsters, enemy groups, battle animations and cameras
    cr::BattleActionTable battleActions;   // sys.evt: the actions (attacks, genes, items)
    cr::EncounterCounter encounters;       // this map's random encounters (fldencount_*)
    bool noEncounters = false;             // --no-encounters (the original's unencountflg)
    // _get_random's stream (CRView::setup seeds it with the clock: CFAbsoluteTime's seconds)
    cr::GameRandom rng{uint32_t(std::time(nullptr) - 978307200)};
    struct Battle {                        // btl_main: the stage, the pair and up to three enemies
        bool active = false;
        cr::BattleStart start;
        int flags = 0;                     // Battle's u8 (G+0x354c6)
        int formation = 0;
        // the boss's end (btl_main 0x3de .. 0x3e6): G+0xa67 set as the last boss fell, its fighter (+0x3a4),
        // its down motion's sounding frame (+0x3a8: _btlanmtbl [16]), G+0xa5c's countdown
        bool bossEnd = false;
        int bossFighter = 2, bossKey = 0, bossModel = -1, bossWait = 0;
        // the story battles' set pieces between rounds (btl_main 0x352 -> 0x366 / 0x37a): which, and
        // 0x37a's new stage (G+0xa14)
        int setPiece = 0, setPieceStage = -1;
        LoadedModel stage;
        struct Actor {
            std::string model;             // battle model package (cache key)
            int modelId = 0;               // kBattleModelFile index, for its action slots
            bool enemy = false;
            int monster = 0, level = 1;
            uint32_t hp = 0, hpMax = 0;
            std::string name;
            cr::Vec3 pos;
            float yaw = 0;                 // +0xfac: the yaw shown (radians)
            // _mdl_put's GetNewMdlDir: the shown yaw turns toward the wanted one (+0xf98) by turnStep
            // degrees a frame (+0xfb0: 15, set again once it's there); btlmove_setpos_angy's move turns
            // it home over the move (turnHome: at the move's start)
            float wantYaw = 0, turnStep = 15;
            bool turnHome = false;
            float approachYaw = 0;         // the attack's heading out (DAT_000dfb58), turned to on the way back
            bool approached = false;
            int slot = 0, action = -1;     // action slot (BattleAnim) and the package's action index
            float time = 0;
            float scale = 1, scaleY = 1;   // +0xfa0..: the monster row's +0x46 / +0x48 (read_btlcpl); +0xf98 is the yaw in turns
            int fighter = -1;              // BattleRules::f index: 0, 1 the pair, 2..4 the enemies
            int then = -1;                 // the slot after this one ends (-1 loop, -2 hold the last frame)
            cr::MotionBlend blend;         // mdl_set_act_motion_nextanm's 2-frame cross-fade into a new slot
            cr::Vec3 home, from, to;       // its place; a move (btlmove_setpos) from -> to
            float homeYaw = 0;
            int moveFrom = 0, moveTo = 0;  // the move's frames of the current slot
            int loopFrom = -1, loopTo = -1; // mdl_set_act_motion_loop's frames (-1: the slot's own)
            bool castRest = false;         // a cast's loop let go (btlact_gene / item 2000): to its end, then
                                           // the idle - the turn's 40 frames don't wait for it
            bool fading = false;           // a fallen monster
            bool hidden = false;           // the model's +0xfb4 cleared (a boss's end, a defeat): not drawn
            float alpha = 1;
        };
        std::vector<Actor> actors;
        // Between the battle's frames (as App::Between for the field): each step's actors and
        // camera at its start and end, and how far into the next step the clock is
        struct Between {
            bool valid = false;
            float alpha = 1;
            std::vector<Actor> actors[2];
            cr::Vec3 eye[2], target[2];
            float roll[2] = {};
            int cam[2] = {-1, -1}, camSets[2] = {};
            bool camCut = false;           // the later step cut the camera (a new one, not eased)
        } between;
        std::map<std::string, LoadedModel> models;
        int frames = 0;
        float clock = 0;
        struct Shadow { int fighter = -1; float alpha = 0; cr::Vec3 at; float rx = 0, rz = 0; }; // _btl_shadow's fans drawn (tests)
        std::vector<Shadow> shadows;
        std::vector<float> shadowTris;     // their triangles this frame (xyz x 3 each)
        std::vector<uint8_t> shadowAlpha;  // and their vertices' alpha
        int fieldTrack = -1;               // music to resume afterwards
        bool requested = false;            // a script's Battle, started once its step ends
        bool eventBattle = false;          // G+0xa4e: a running script started it - it waits, and goes
                                           // on in the battle's talks and after it
        struct BreakFx {                   // btlbreak_effect_*: "BREAK!! The battle situation changed"
            bool on = false;
            int state = 0, t = 0, limit = 90; // +0x98 (0 in, 10 hold, 0x3e6 out), +4, +8
            int pos = -480, alpha = 0, slide = 0; // +0x34 the labels along the stripes, +0x38, +0x3c
        } breakFx;
        cr::BattleStart request;
        int requestFlags = 0;
        // The rules (game/battle_rules.hpp) and the scene around them (btl_main's states)
        cr::BattleRules rules;
        enum Phase { Intro, Command, Acting, WinTalk, BossDown, SetPiece, Won, Results, Lost, Fled, StoryEnd, RoundEnd } phase = Intro;
        int phaseFrames = 0;
        int introStep = 300;     // the opening's btl_main state past 200: 300 (the counter), 400 (after it), 499 (the menu's flag set)
        bool wonTap = false; // a tap in the victory's hold (state 1099: 140 frames or pad_chk_trg_up)
        bool lostTap = false;    // a tap on the defeat (0x513: at once, or after 40 frames of an event battle's game over picture)
        bool ending = false;     // btl_main 9000 -> 0x238b: the black mask over 10 frames, the music fading, 12 frames, then the way out
        int endFrames = 0, pendingExit = 0;
        int idleSince = -1;      // the action's frame its actors came to rest (btlact 1999: ten frames more, then the next)
        int tutorialDue = 0;     // the action's tutorial (0xff3..0xff5) for btl_main 700's frame
        bool nullHitDue = false; // group 0x2b5's scene for 799's
        int handover = 0;        // frames before the next action's turn (btl_main 700's 9999 frame and 799's first, see Acting)
        bool breakDue = false;   // a Break lost by the last action: its effect when 799 passes
        cr::Texture gameOver;    // btl_main 0x4b0: gameover.mct faded in over 20 frames for a lost event battle
        enum Menu { Top, Commands, Targets, Items, EscapeAsk, Genes } menu = Top;
        int cursor = 0;
        // The Vita's pad cursors (__menub_mainPrc): the first page's kept for the battle (DAT_8112f1d8,
        // cleared by __menub_mainSet alone), the commands' one a member, cleared as the first page
        // comes in (DAT_8112f1dc..)
        int topCursor = 0, cmdCursor[2] = {0, 0};
        int member = 0;                    // solo: the member choosing
        cr::BattleCommand cmds[2];
        int pendingItem = 0;               // an item chosen, its target next
        int pendingGene = 0;               // a gene ability chosen, its target next
        int targetAction = 0;
        std::vector<int> picks;            // Targets: fighters; Items: item numbers
        int pick = 0, pickTop = 0;
        int confirm = 0;                   // EscapeAsk: 0 Yes
        // The pages as drawn (menub_mainPrc's slides, +0x38): each page comes in from the right
        // (320 -> 0, 80 a frame; 480, 120 in the Android layout: battleSlideDistance) and goes out
        // again before the next comes in; it takes taps only
        // once in (states 2, 101, 901, 801). The page the logic is on is `page`; the view follows it
        struct MenuView {
            int page = -1;                 // the page shown (-1 none)
            Menu menu = Top;
            int cursor = 0, member = 0, pick = 0, pickTop = 0, confirm = 0, targetAction = 0;
            bool pair = false;
            std::vector<int> picks;
        } view;
        int page = 0;                      // bumped at each page change
        int viewState = 0;                 // 0 out, 1 coming in, 2 in, 3 going out, 4 held out
        int slide = 320;                   // the view's offset to the right
        int slideHold = 0;                 // frames held out before the next page (Defend's 20: state 199)
        int menuGone = 100;                // frames since the view went out with no page to show (0x270e, then 500 / 600)
        bool roundWait = false;            // the round's first action waits for the menu to go
        int hudSlide = 0;                  // the left HUD's offset to the left while a talk runs (+0x3c, 120 a frame to 480)
        struct Analyze {                   // btlact_gene 0xc1c..0xc9e: Analyze's lines, each held until a tap
            std::vector<std::string> pages;
            size_t at = 0;
            bool on = false;
            int endAt = -1;                // the turn's frame it ends at (state 2000: 40 frames after the last tap)
        } analyze;
        bool turnOn = false;               // an action is playing
        cr::BattleTurn turn;
        int turnFrame = 0, hitFrame = 0;
        int bounceAt = -1;                 // a reflected gene's turn: when it comes back (-1 not yet met)
        bool hitsShown = false;
        // the pair flags the action's calculation left (a member's fall ends pair mode, a fall the enemies'
        // pair round), held back until its hit: the original calculates there (_btlcalc_prc at the hit
        // frame, after _btlact_*_init chose the cameras and spots from +0xaf0 / +0xaf4)
        bool pairHeld = false, pairAfterHit = false, enemyPairAfterHit = false;
        struct Popup {                     // __dmg_set's slot (+0x990, 0x28 each)
            bool on = false;               // +0x9b7
            cr::Vec3 at;                   // +0x994: the target's node 1102 as it was hit
            int amount = 0, type = 0, timer = 0, target = 0; // +0x9b0, +0x9b6, +0x9ac, +0x9ae
            bool mp = false;
            float x = 0, y = 0;            // +0x9a0 / +0x9a4: where it was last put (480 x 320 units)
        };
        Popup popups[5];                   // __dmg_set's five numbers (a sixth is dropped)
        std::string banner;                // the top bar: the action, the target prompt, messages
        int bannerFrames = 0;
        // ... as drawn: a font slot's text shows from the frame after it is set (font_set_utf16; drawfont_btl a
        // frame on) - Part 9 2973.82 s, Part 11 947.82 s: the camera's cut a frame before "Stress" / "Victory!"
        std::string bannerShown;
        std::vector<cr::BattleNotice> notices; // G+0xa14: the calcs' messages, one per free banner
        int shownHp[5] = {}, shownMp[5] = {}; // what the HUD shows (changes as the hits land)
        int rollHp[2] = {}, rollMp[2] = {};   // the party board's numbers and bars (+0x60 / +0x68): a fifth of the way a frame
        int gaugeShown = 0;                // the badge's gauge, a step every other frame toward the rules'
        float breakPop = 0;
        uint32_t steps = 0;                // battle steps taken (prc's +0x918c4, raised a 20 Hz frame: its parity paces the gauge)
        std::vector<int> levelled;
        bool settled = false;              // the save has the battle's results
        int exit = 0;                      // 1 back to the field (fled), 2 to the title (lost), 4 won
        struct Result {                    // __menuu_MbResult*: the results' stages (_gstMbResult)
            int stage = 0;                 // 0 counting (3), 1 a level-up's line (5 / 6), 2 an item's (7 / 8), 3 going (10..)
            int count = 0, frames = 0;     // DAT_000e50e0: counting frames left ((most levels + 1) x 60); frames in
            int message = 57007;           // the board's one line: Victory!, then 57008 / 57034, 57010 / 57035
            std::string name;
            int value = 0;
            int character[2] = {-1, -1}, level[2] = {}, levelTo[2] = {}, levels[2] = {}, hp0[2] = {};
            float exp[2] = {}, expTo[2] = {}, expStep[2] = {};
            float shownExp = 0, shownOz = 0, shownPurse = 0;
            int expTotal = 0, ozTotal = 0, purseTo = 0;
            std::vector<std::pair<int, int>> ups, items; // (member, levels), (item, how many)
            size_t upNext = 0, itemNext = 0;
        } result;
        cr::Mat4 vp;                       // the last frame's camera (the damage numbers)
        cr::Mat4 proj;                     // and its projection (tests)
        int cam = -1, camActor = 0, camFrame = 0; // btlcamera_set: the camera (+0xa96), its actor (+0xa94), its count (+0xa90)
        int camDur = 0, camHold = 25;      // +0xa92 its frames; +0xae8 as its 10000..10002 read it
        bool camLive = false;              // +0xa88 set: btlcamera_prc still runs it
        bool camHeld = false;              // +0xa98: set by a target cursor (its 1/2 easing left alone)
        bool camSnapped = true;            // flag 0x800 this frame: the shown camera cut to the wanted
        float camEase = 1;                 // +0xfc / +0x100: the shown camera's 1/k a frame
        float camRoll = 0;                 // +0xf8: degrees about the view (gluLookAt under flag 0x100)
        int camSets = 0;                   // btlcamera_set calls (the frames between steps: a cut)
        int attackVariant = -1;            // que +0xc: _btlact_attack_init's random(3) for the camera
        int attackCam2At = -1;             // que +0x10: the approach's last frame (a variant-2 member blow's second camera then)
        int attackBackAt = -1;             // _btlact_attack 1000's que +8: the blow's max frame (its slot's frames - 1), then 0x406
        int attackEndAt = -1;              // 0x406's way back (the return slot's max frame) and 1999's ten done: 9999
        int attackFxAt[2] = {-1, -1};      // que +0xb2 / +0xb4: _btlcalc_prc_0404_effect's frames (the hit's look, no calc)
        bool partnerStands = false;        // que +0x14: a pair's second stands in place (party model 7 / monsters 0xa0..0xa2)
        struct Shot {                      // _btlact_attack 0x6a4 / 0x708: slot 10's shot (eff3d 2 / 3) user -> target
            int effect = 0, at = -1, steps = 0, from = 0, handle = 0, target = -1, k = 0;
        } shot;
        int nodeFx = 0;                    // 0x76c: m103's effect 0x3f (its handle), kept on node 1204
        cr::Vec3 camWantEye{488, 12, 555}, camWantTarget{500, 4, 498}; // +0xa8 / +0xb4
        cr::Vec3 camEye{488, 12, 555}, camTarget{500, 4, 498};         // +0xcc / +0xd8: shown
        std::vector<Effect> effects;       // eff3d: playing effects (32 at most)
        // The voiced releases' battle lines (2.2.0's _btlvoice_*): each fighter's next variant of
        // three (+0xd3), who of a pair speaks this round (+0xd2), the command lines said (+0xafc)
        int voiceVariant[5] = {};
        bool voiceSpeaks[5] = {};
        bool commandSaid[2] = {};
        int voiceRound = -1;
        int voiceLeader = 0;               // btl +0xaf2: the party member who last headed the order (the victory's speaker)
        uint32_t voiceRng = 1;
        int winWait = 0;                   // frames the win has held for a line still playing (btl_main 799, FUN_000926d8)
        struct Quake {                     // Quake (StartQuake / EndQuake): the 3D jolts a few pixels
            int mode = 0;                  // 0 still, 1 to its end, 2 held until EndQuake
            float amp = 0, now = 0, from = 0, to = 0; // pixels; percent of them now, from, to
            int frame = 0, frames = 1;
            float off[2] = {};             // this frame's offset (1024 x 768 pixels)
            uint32_t rng = 1;              // its own (the rules' random numbers stay untouched)
        } quake;
        struct Flash {                     // SetFade_back: a colour over the 3D, under the HUD
            bool on = false;
            float from = 0, to = 0, alpha = 0; // 0..255
            int at = 0, total = 1;         // 50 a frame
        } flash;
        int stageAction = 0;               // a boss's staged action playing (its btlact_430..509), 0 none
        int stageCams = 0;                 // its cameras so far (+0x2c)
        int stageWait = 0;                 // its frames after the hit
        int stageEnd = 0;                  // the later ones: the motion and the wait (0: hit + stageWait)
        int stageMotionEnd = 0;            // ... the motion's length (then the formation's camera)
        int stageChain = 0;                // ... the first of the cameras chained (is_btlcamera), 0 none
        int stageSkip = 0;                 // 241's counter starts at 100 (btlact_435_init)
        float fov = 28.5f;                 // gac + 0x104: some of them widen it
    } battle;
    Battle::Quake fieldQuake;              // the field's (FieldCamera::setOffset: the view moves by it)
    std::vector<uint8_t> effBin;           // eff.bin: 100 effect packages (read when an effect is first needed)
    std::map<int, LoadedModel> effectModels; // eff.bin entries loaded (freed as a battle ends)
    std::vector<Effect> fieldEffects;      // eff3d_set_fld & co. on the field (32 at most)
    int effectHandles = 0;
    std::vector<std::array<int, 3>> frameEffects; // FrmPlayEff's: key (effect + 1) x (actor + 1), effect, handle
    std::array<int, 256> fieldEffectCount{};   // started on the field, by id (tests; Omega's field ids run past 0x90)
    int frameEffectCalls = 0, scriptEffectCalls = 0, scriptQuakes = 0;
    int texDispCalls = 0, tutoCalls = 0, tutoPage = -1, exclamationCalls = 0; // (tests)
    bool paramUp[2] = {};                  // Uty_Btl_ParamUp02 / 01 waiting for the next battle
    bool logoSeen = false;                 // G+0x1f85e6: the logos played (Uty_ChkLogo_flg)
    bool padArt = false;                   // Uty_DrawController: the pad's picture (flag 0)
    std::map<std::string, int> matAnimDraws;   // NPC models drawn with animated materials (tests)
    int battlesFought = 0;                 // this session (tests, status line)
    std::vector<int16_t> battleVoiceTable; // voiced data's sys.evt section +0x40: [speaker x 1000 + situation - 1] = clip
    cr::Hud battleHud;
    int choiceCursor = 0;                  // 0 = Yes, 1 = No
    int choiceAnim = 0;                    // its window's slide in (frames, 4 to settle)
    bool worldChoiceOpen = false;          // Util 1: pick a gate (_menuu_WldChoice*)
    int worldCursor = 0;
    cr::Hud choiceHud;
    cr::Hud nameHud;
    cr::Hud pageHud;                     // the dialogue's [E] n/m
    float captureAt[2] = {NAN, NAN};       // --capture ... --at x z
    bool showProps = true;                 // --no-props: skip the field's occluder geometry
    int captureMessage = 0;                // --capture ... --message <id>: show that message
    bool captureOptions = false;           // --capture ... --volume-menu
    bool newGame = false;                  // --new-game: start where a new game does
    int captureFrames = 0;                 // --capture ... --frames n: play n frames of scripts first
    int captureSettle = 0;                 // --settle n: draw the same moment n times first (temporal AA)
    bool captureSettleMoving = false;      // --settle-moving n: n frames a game frame apart instead
    int captureSettleFps = 0;              // --settle-fps n: those frames 1/n s apart (60: as drawn between steps)
    int captureEvent = -1;                 // --event n: start the area's script n in the room first
    int captureArea = -1;                  // --area n: the event file for the map (routes share maps)
    std::vector<int> captureFlags;         // --set-flags a,b,...: story flags set before the map loads
    std::vector<std::pair<int, int>> captureItems; // --give-items a-b,c,...: one of each in the bag
    bool captureWinBattles = false;        // --win-battles: a battle the scripts start is won at once
    bool testNoTutorials = false;          // self-test: battles don't stop for their tutorials (EvrStart 2)
    int captureTalk = -1;                  // --talk-portrait n: (new game) the n-th hall conversation showing a portrait
    int lastMessageId = -1;                // the last message a script showed (CR_CAPTURE_UNTIL_MSG)
    int captureMenu = 0;                   // --camp (1), --save-screen (2: save, 3: the title's load / new, 7: cleared), --char-select (4), --scenarios (5), --manual (6), --world-select (8), --last-battle (9)
    std::string captureCampView;           // --camp-view items|use|result|equip|slots|wear|status|status-equip|status-skills|genes|plate|entries
    std::string captureFieldMenu;          // --field-menu closed|open|known|off|map[step]
    std::string captureTouchPad;           // --touch-pad dx,dy[,chest]: the pad held that far from the screen's middle
    int captureSceneFx[3] = {-1, 0, 0};    // --scene-fx set effect [frames]
    std::string captureBattleView;         // --battle-view top|commands|targets|items|escape|hit|results
    std::string captureShop;               // --shop main|category|buy|sell|count|confirm|talk
    int captureTitlePair = 0;              // --title-new p: the title's Start, Character Select pair p, New
    int captureScenario = 0;               // --scenario s: --new-game's scenario (1: the boot's Warp 10, 214)
    int captureBattle[2] = {-1, -1};       // --capture ... --battle group stage
    bool showNpcs = true;                  // N (map mode): NPCs, treasure boxes, field objects
    struct NpcDraw {
        std::string model;
        cr::Mat4 world;
        int id = 0;
        int index = -1;      // its place in npcs (puzzle rooms repeat ids: every wall is NPC 129)
        int action = 0;      // fitting-action index; 0 = idle (loops), others play once and hold
        float time = 0;
        bool once = false;
        int loopFrom = 0, loopTo = 0; // Anim: loop frames a..b after the first play (0, 0 = whole)
        float alpha = 1, alphaFrom = 1, alphaTo = 1; // NpcFade / mdl_fadein / mdl_fadeout
        int fadeFrames = 0, fadeLeft = 0;
        bool faded = false;  // mdl_set_alpha has run on it: alpha-blended for good (fadedMaterial)
        cr::MotionBlend blend; // the 2-frame cross-fade into a new action
        // mdl_set_alpha over frames on a hidden model (colour +0xfb4 0 -> 1, _mdl_set_alpha 1.0.0 10171):
        // mdl_put draws it and steps its fade though the NPC is killed, until a Kill or a fade ending at 0
        bool lit = false;
    };
    std::vector<NpcDraw> npcDraws;         // this map's visible NPCs
    // The models of the map's hidden NPCs (Kill, npc_setup's story actors, parked actors): __EvtBorn / Kill
    // (1.0.0 0x25370, npc_born / npc_kill) flip the hidden bit (ctl +0x8c bit 0) and the model's colour
    // (+0xfb4) alone, and _mdl_put leaves a colour-0 model out - not drawn, its action, time and fade not
    // stepped - but Anim / Anim2 / NpcFade / UnloadAct (npc ctl +0, the bit not looked at) act on it all the
    // same, and a Born shows it as it was left
    std::vector<NpcDraw> npcHeld;
    // A puzzle room (PuzzleManager, game/puzzle.hpp): its objects - the room's NPCs that have
    // NPC table +20 bits - on a grid of 3 layers (0 the floor's switches, 1 blocks and gems, 2
    // the warp crystals), the touch ability's state and the ESP count
    struct Puzzle {
        int stage = -1;                    // -1: not in a puzzle room
        int esp = -1;                      // uses left (-1 no limit)
        int pyroCool = 0;                  // Pyrokinesis::update: frames left before another tap counts (20 after one)
        struct Obj {
            int npc = -1;                  // index in npcs
            int type = 0, layer = 1, row = 0, col = 0;
            bool shown = true;
            int state = 1;                 // 1 idle, 2 gone (moving), 3 back; 0 out of play
            int pending = 0;               // 8: a teleport asked for
            int toRow = 0, toCol = 0, outFrames = 4, inFrames = 4, waitOut = 6, waitIn = 6;
            bool onSwitch = false;         // standing on a weigh switch (+0x30)
            bool on = false;               // a weigh switch pressed
            // NormalBlockPK: 0 idle, 1 rising, 2 held (bobbing), 3 sliding, 4 falling, 5 falling onto a switch
            int pk = 0, pkMsg = 0, pkTimer = 0, bob = 0;
            bool ghost = false;            // RedBlock / BlueBlock put away: drawn at 0x60 alpha, not solid
            bool hit = false;              // message 9 (Toughness) / 12 (a candle block's switch) waiting
            // CandleSwitch: 0 out, 1 catching (a neighbour's), 2 lighting, 3 lit, 4 going out
            // (a neighbour's), 5 cooling; csMsg 10 a tap, 11 a neighbour's; the beam from csFrom
            int cs = 0, csMsg = 0, csTimer = 0, csFrom = -1, beamT = 0;
            int flame = 0;                 // its flame's effect (0xc, looped)
            float vx = 0, vy = 0, vz = 0, baseY = 0;
        };
        std::vector<Obj> objs;
        std::vector<int> grid;             // [layer][row][col] -> objs index, -1 empty
        int tpState = 0, tpTimer = 0, tpTarget = -1; // Teleport: 1 vanishing, 2 appearing
        int pkState = 0, pkTarget = -1;    // Psychokinesis: 1 a block held, 2 waiting for it to land
        bool pkPointerCancelled = false; // keep a held block, but require a fresh drag after interruption
        bool pkMoved = false;
        int pkEffect = 0;                  // effect 5 on the leader while a block is held
        float pkFromX = 0, pkFromY = 0;    // where the mouse took hold (screen pixels); -1e9 = by key
        bool busy = false;                 // +0x26: an ability under way
        int scene = 4, sceneTimer = 0;     // PuzzleScene: 4 playing, 2 fading out to reset, 3 fading in
        bool asked[2] = {};                // the two buttons' scripts (EvrStart 6 / 7) running
        int punished = 0;                  // SetPunishmentDispOn: the mark's frames (11), its blink phase (0..2)
        int punishPhase = 0;
        float punishX = 0, punishY = 0;    // its top-left (tex2d), 32 up-left of the touch
        int pop = 0;                       // menuf's +0x44: the count swells for 4 frames when it changes
        int shownEsp = -1000;              // +0x48: the count last shown
        bool hadEsp = false;               // +0x40: it has been above 0 (then the retry button blinks at 0)
        int blink = 0;                     // +0x3c: that blink, 0..10 (shown below 5)
        bool help = true;                  // +0x38 (MfMain_SetPzlHelpState): the pad's two buttons
        float tapX = -1, tapY = -1;        // the touch that asked (tex2d), < 0 from the keyboard
        int clock = 0;                     // frames the room has run (the flames' flicker)
        // _ArrowOn / _ArrowOff (menuf_mainPut draws them): 0 left, 1 up, 2 right, 3 down around a
        // lifted block - on (1) / off (-1), the fade's frames left and kind (2 the slide's, 1 a
        // let-go's), the colour's alpha byte, the screen point (tex2d)
        struct Arrow { int on = -1, timer = 0, mode = 0, alpha = 0xff; float x = 0, y = 0; } arrows[4];
        // CheckTap (teleport / toughness / pyrokinesis): 1 a touch held, 2 a tap (released within
        // 5 frames), 3 held too long; the touch's start (tex2d)
        struct Tap { int state = 0, frames = 0; float x = 0, y = 0; bool released = false; } tap;
        bool pkHeld = false;               // PsychokinesisDetector: the touch that took hold still down
        float pkAtX = 0, pkAtY = 0;        // ... where it is now (tex2d)
        cr::Vec3 leaderWas;                // FieldPCCtrl + 0x28: the leader moved since the last frame
        bool leaderMoved = false;
        int padHold = 0;                   // gac + 0x1e2004: 3 frames after the touch pad was held (taps refused)
        // ScrollCamera: a second finger within 6 frames of the first scrolls the view to it (state
        // 1, the ability paused) until the next single touch lets it go; phase / count its checkTouch
        struct Scroll { int state = 0, phase = 0, count = 0; float x = 0, y = 0; } scroll;
    } puzzle;
    int padLock = 0;                       // gac + 0x1e2006: the puzzle pad's buttons and Camp / Save refused while > 0
                                           // (Util 2046, Uty_puzzle_teleport_cnt, sets it from var 9; not the corner button)
    struct Fingers { int held = 0, fresh = 0; float x = 0, y = 0; } fingers; // SDL touch: down now, new since the last frame (the last one's frame pixels)
    float puzzleStartHeading = 0;          // ... and its heading (FieldPCCtrl::reset)
    float playerAlpha = 1, playerAlphaFrom = 1, playerAlphaTo = 1; // mdl_set_alpha on the leader
    int playerFadeFrames = 0, playerFadeLeft = 0;
    bool playerFaded = false;              // mdl_set_alpha has run on the leader's model (until it is read again)
    cr::Vec3 puzzleStart;                  // where the leader came into the puzzle room (FieldPCCtrl::reset)
    struct CrystalCam { int npc = -1; int phase = 0; } crystalCam; // Uty_WarpCrystalCamera: 0 to the crystal, 1 back
    bool puzzleStartPending = false;
    int bgmNo = 255;                       // G+0x35530: the music last asked for (Uty_GetBgmNo); 255 none (-1)
    int lastFieldBgm = 0;                  // G+0x8a97c: the field's last play request (the music after a battle)
    struct NowLoading { bool on = false; int frame = 0; } nowLoading; // NowLoadProc's spinner
    struct VitaLogo {                      // the Vita's boot opcodes 0x47..0x4a: a full-screen logo
        cr::Texture tex[2];                // by slot (0x47's slot + 16 in the original's Tex2d)
        int slot = -1, phase = 0, frame = 0, in = 0, hold = 0, out = 0;
        bool skip = false;                 // a tap / confirm in the hold: on to the fade-out
        bool warning[2] = {false, false};  // (the slot's picture the warning: the whole of its sheet)
    } vitaLogo;
    struct TexDisp {                       // Uty_TexDisp (2016): a Tex2d slot's picture faded in, held, out
        bool on = false;
        int g[11] = {};                    // slot, x, y, u, v, w, h, fade-in, hold, fade-out, ?
        int state = 0, counter = 0, alpha = 0;
    } texDisp;
    struct TutoTex {                       // Uty_TutoTexDisp (2033) / MsgUty_TexDisp*: a tutorial's frames
        bool on = false;
        int state = 0, timer = 20, frame = 0, page = -1, pages = 0;
        int row[4] = {}, frames[4] = {};
        float x = 0, y = 0;                // tex2d, the picture's top-left (x2: the HD frame)
        cr::Texture tex;
    } tutoTex;
    struct ManualMsg {                     // the manual's page (OptionPrc 0x12..0x16: MsgUty_WinOpen / SetMsg)
        int state = 0;                     // 0 the list, 1 its windows going (0x12), 2 the message up (0x13 / 0x14),
                                           // 3 its window closing (0x15 / 0x16)
        int count = 0, id = 0;             // 0x12's 4 frames; the message (56000 + item - 419, 56040)
        std::vector<std::vector<std::string>> pages;
        size_t page = 0;
        int typed = 0;                     // the page's characters out (PrcMsg)
        bool picture = false;              // one of MsgUty_SetMsg's five: its picture, the window at _mwin[4]
        MsgWin win;                        // PrcWin: the window's fade
        TutoTex pic;                       // MsgUty_TexDisp*: the picture's pages and frames
        float clock = 0;
    } manualMsg;
    bool exclamation = false;              // Uty_DrawExclamationMark: the dimmed screen and "!" (flag 0)
    struct PuzzleBanner {                  // Uty_PuzzleStart (call 2003): two pieces sliding apart into place
        bool on = false;
        float x1 = 100, y1 = 68, x2 = 156, y2 = 140; // tex2d
        int alpha = 0;
    } puzzleBanner;
    std::map<std::string, struct LoadedModel> npcModels; // NPC model cache by file
    std::map<int, std::vector<cr::TreasureBox>> treasureBoxes; // per area: selected boxes and items
    bool treasureReroll = false;           // gac +0x364 bit 0x40000000: set by the World Select's choice
    bool showPlayer = true;                // P: the party leader walking on the field
    int playerChar = 0;                    // the leader's record (game.leaders[scenario]): its field model
    bool messageSkip = false;              // Uty_Chg_MsgSkip
    LoadedModel player;
    // A script's Anim on npc 0 (__EvtAnim: mdl_set_act_motion_loop on the leader, gac +0x1338): its
    // motion and loop start while a script holds the field - FieldPCCtrl::operate sets the leader's
    // stand / walk / run from the stick only when nothing holds it (G+0x364 & 9); -1 none
    int leaderMotion = -1, leaderLoopFrom = 0;
    std::string playerName;
    // the leader's model read by Set_Prtyno (read_mdl: colour 1 white) rather than with the map
    // (fldread_prc's read_mdl_set: as the file has it - f011's 0.8)
    bool playerByReadMdl = false, playerLoadedByReadMdl = false;
    cr::FieldWalker walker;
    bool havePlayer = false;
    // _prty + 0x14..0x20: where the field was last loaded - a warp's arrival (or a load); a save in a
    // puzzle room (area 1) records it, not where the player stands (SaveData_Save / ContData_Save_set)
    float fieldLoad[4] = {0, 0, 0, 0}; // x, y, z, heading (radians)
    bool fieldLoadSet = false;
    float walkClock = 0;                   // movement runs in fixed 20 Hz steps like the original
    // Between the game's frames: each field step's values at its start and its end - the leader,
    // the NPCs drawn, the script's camera, the scroll - and how far into the next step the clock is
    // (alpha). The frames drawn between steps show those values where they are by then (shown*());
    // only a value still as its step left it is shown so: whatever moved it since (a warp, a test
    // stepping the game itself, a new room) is drawn as it is.
    struct Between {
        bool valid = false;
        float alpha = 1;
        cr::Vec3 walker[2];
        float heading[2] = {0, 0}, playerAlpha[2] = {1, 1};
        struct Npc {
            int index = -1, id = 0;
            cr::Mat4 world[2];
            float alpha[2] = {1, 1};
        };
        std::vector<Npc> npcs;
        cr::Vec3 eye[2], target[2];
        float scroll[2][2] = {{0, 0}, {0, 0}};
    } between;
    float walkExpDistance = 0;             // G+0x35520: walked with skill 174 worn, 10 units = 1 EXP banked
    struct FieldMenu {                     // menuf_mainPut / Prc: the corner button and the bar it opens
        bool open = false;                 // G+0x3553c
        int anim = 0;                      // the bar's 4 frames of sliding (in or out)
        int fade = 0;                      // +32 a frame (to 320) on the field, -32 while a scene runs
        float clock = 0;
        int cursor = 0;                    // the Vita's: the bar's button under the buttons' cursor (DAT_8112189c)
    } fieldMenu;
    struct StickPointer {                  // a puzzle room's touch from a controller (the PS TV's way: a pointer the
        bool shown = false, down = false;  // right stick moves, R3 the finger): its place in window pixels
        float x = 0, y = 0;
        int stage = -1;                    // the room it was put in (a new room: back to the screen's middle)
    } stickPointer;
    struct TouchPad {                      // CrossKey: the field's touch pad, where a finger went down
        bool down = false;                 // a finger (or the mouse) holds it
        float ox = 0, oy = 0;              // where it went down (tex2d)
        float dx = 0, dy = 0;              // the finger from there (tex2d = the game's touch units)
        int hold = 0;                      // frames before it shows unmoved (5)
        bool shown = false;                // _SetFldMenu_pad_visible
        float knob[2] = {0, 0};            // the knob's offset, easing toward the finger
    } touchPad;
    bool bigPad = true;                    // the UI atlas has the larger pad (all of Omega's do)
    float promptX = -1, promptY = -1;      // the "!" bubble's centre as drawn (logical), -1 none
    struct MapScreen {                     // __menuu_Map*: the world's map, where the pair is, where to go
        bool open = false, closing = false;
        int fade = 0;                      // 4 frames in, 4 out
        int field = -1;                    // ReadMapMenu's _gnFieldNo (0..3 map_D01..D04, 6 map_D07), the one in `tex`
        cr::Texture tex;
        float clock = 0;
        int spot[2] = {-1, -1};            // the pair's marker's top-left (tex2d), -1 none
        int marks[2][2] = {{-1, -1}, {-1, -1}}; // the story's two markers
        int blink = 0;                     // ComUty_PictAnimDraw's counter: 2 frames of 6
        int page = 0;                      // D07's page (DAT_000e5e0c): warps, doors and boxes, the rest
        int phase = 2, count = 0, alpha = 0; // the pair's marker's fading (DAT_000e5d80 / 88 / 84)
    } mapScreen;
    struct StaffRoll {                     // __menuu_StaffRoll* (Util 7): the credits over the ending's picture
        bool open = false;
        int state = 0, mode = 0, timer = 0; // _gstMuStaffRoll, _geStaffRollMode, DAT_000e5c20
        int next = 17;                     // DAT_000e5c28: the next of block 9's lines
        int countdown = 0;                 // DAT_000e5c2c: frames after the last line
        int hold = 0, endAlpha = 0;        // DAT_000e5c38 (to 120), 5c30: the end picture's alpha
        bool fast = false;                 // DAT_000e5c34: the corner button held (10 x)
        float picY = 0;                    // DAT_000e5d5c: the picture's y (tex2d), up to -(1024 - the screen)
        bool phone = false;                // the iPhone layout (_is_iPadDisplay 0): its numbers
        float edgeX = 0, edgeY = 0;        // the Android layout's androidEdgeX / Y (the fast button)
        struct Line { bool on = false; float y = 0; int entry = 0; } lines[17];
        float win[5] = {}, winTo[5] = {};  // ComUty_WinDispOn / Off: each window's alpha
        float fade = 1, fadeFrom = 1, fadeTo = 1; // SetFade: white (the first scenario) or black over it all
        int fadeAt = 0, fadeFrames = 1;
        bool fadeWhite = true;
        cr::Texture tex, endTex;           // slot 0x10 EndBack0n, 0x11 EndBack01_end (mode 0)
        float clock = 0;
    } staffRoll;
    struct WorldSelect {                   // __menuu_WldChoice* (Util 1): a gate, then its level band
        int state = 0, timer = 0;
        int pair = 0;                      // _MuCom_GetPartyNo
        std::vector<int> worlds;           // the gates offered (0..5) in the pair's order (+0x50 the mask)
        int levels = 2;                    // +0x58: 2 + the pair's eight level flags set
        int cursor = 0, top = 0, levelCursor = 0, levelTop = 0, confirm = 0;
        int world = 0, level = 0;          // +4 the gate focused / chosen, +8 the level band
        bool asking = false;               // +0x25: the board asks "Skip the first half?"
        bool skip = false;                 // +0x26: ... and Yes (flag 1)
        bool cancelled = false;            // +0x24: Back (flag 0)
        float win[11] = {}, winTo[11] = {}, fromX[11] = {}, fromY[11] = {};
        float scroll = 0;                  // MuScrList's rows' offset (48 x the focus row once settled, 8 a frame toward it)
        int scrollList = -1;               // ... of which list (0 the gates, 1 the bands)
        int mark = 0;                      // the board's mark (PictAnim of picture 13, a tick a frame)
        cr::Texture maps[7];               // the gates' pictures (slots 0x11..: e3bd0 / e3be8's names)
        float clock = 0;
    } worldSelect;
    struct LastBattle {                    // __menuu_LastBt* (Util 8): which pair meets the next guardian
        bool open = false;
        int state = 0, timer = 0;
        int count = 0, mask = 0;           // rounds done, pairs used (the menu's statics, not saved)
        std::vector<int> pairs;            // those left to choose (0..3)
        int cursor = 0, chosen = -1, confirm = 0;
        float win[7] = {}, winTo[7] = {};
        cr::Texture tex;                   // hi / mizu / kaze / mu.mct: this round's guardian
        float clock = 0;
    } lastBattle;
    int modelIndex = 0;
    LoadedModel model;
    float yaw = 0.6f, pitch = 0.25f, zoom = 1.0f;
    bool zUp = false, dragging = false;
    bool playing = true;
    bool smoothFilter = false;  // original uses nearest filtering
    float speed = 1.0f;
    Uint64 lastTicks = 0;
    float drawFrames = 0;       // this frame's length in 20 Hz frames (the game's dt)
    float texSpin = 0;          // _mdl_put's _vEuler z: texture 999's turn (0..1)
};

// The skip corner's sources (App::skipHeld): the scene skips while any is held
constexpr unsigned kSkipKey = 1, kSkipPad = 2, kSkipPointer = 4;

// The menus' sounds: __effgo ids (sndeff.bin entries) as the original's *Prc functions use them
namespace se {
// Omega's sndeff.bin has 145 sounds ahead of the menus' (sndeff entry = CR1's + 0x91: StartPrc's
// 0x93 / 0x94, menuf_mainPrc's 0x95 / 0x99 / 0x9a, SaveLoadPrc's 0x9c / 0x9d, ShopPrc's 0xa7)
constexpr uint32_t kMenuBase = 0x91;
constexpr uint32_t kChoose = kMenuBase + 2;  // a plate, button, toggle, Yes or No (ComUty_YesNoChoiceProc's own is 2 too)
constexpr uint32_t kBack = kMenuBase + 3;    // the Back bar (ComUty_BackButtonProc); Options' < > page arrows on it as well
constexpr uint32_t kBuzzer = kMenuBase + 4;  // refused: nothing to sell (BuySell_CommandProc), a count past its end
                                             // (ComUty_NumCountProc), a pair-only command alone (menub_mainPrc)
constexpr uint32_t kPage = kMenuBase + 5;    // the Back bar's < > arrows (MuCamp_NextProc), a list flicked a page
constexpr uint32_t kCampOpen = kMenuBase + 8, kCampClose = kMenuBase + 9; // menuf_mainPrc's Camp button, CampMainPrc's Back bar
constexpr uint32_t kSaved = kMenuBase + 11;  // SaveLoadPrc, after SaveData_Save
constexpr uint32_t kLoaded = kMenuBase + 12; // SaveLoadPrc: Yes to Load / New
constexpr uint32_t kPaid = kMenuBase + 22;   // ShopPrc
constexpr uint32_t kVolume = 0x131; // MuOptionSetInfo: __effgo(0x131) as the SE gauge is let go (the BGM gauge is silent)
constexpr uint32_t kSlide = 0x12d;           // StartPrc: Select Scenario's page dragged over
// The original is touch-only, so it has no cursor sound; keyboard cursor moves are the port's own
constexpr uint32_t kCursor = kMenuBase + 1; // 0x92: MuScrList_Draw's tick as a list's focus moves
} // namespace se

// Temporal upscaling: output / render size per axis (FSR's and DLSS's modes)
struct UpscaleMode {
    const char* name;
    float ratio;
};
constexpr UpscaleMode kUpscaleModes[] = {
    {N_("Native AA"), 1.0f}, {N_("Quality"), 1.5f}, {N_("Balanced"), 1.7f}, {N_("Performance"), 2.0f}, {N_("Ultra Perf."), 3.0f}};
constexpr const char* kUpscalerName[] = {N_("Off"), "FSR 3.1", "DLSS", "MetalFX", "FSR 4"};
constexpr cr::Upscaler kUpscaler[] = {cr::Upscaler::Off, cr::Upscaler::Fsr3, cr::Upscaler::Dlss, cr::Upscaler::MetalFx, cr::Upscaler::Fsr4};
constexpr int kUpscalers = 5;
// (MetalFX is Apple's own, FSR 4 - AMD's DLLs in fsr4/ - Linux's and Windows': the options step over the other's slot)
constexpr bool upscalerHere(int u) {
#ifdef __APPLE__
    return u != 4;
#else
    return u != 3;
#endif
}


// ---- The Options screen ----
// Built from the original's pieces (__menuu_OptionPut: the "Options" plate, label bars, Check /
// On-Off / Japanese-English buttons, gauges, "n / m", Back), but organised by subject on three
// denser pages: rows 36 apart instead of 48, the controls at 85%, Back and the page number lower.
// The original's settings are all here (Instruction Manual, BGM, Sound Effects, Language,
// Environment Effects, Lock Screen Orientation, Return to Title / Reset Data), with their own
// labels in the game's language; ours are added beside them.

enum class OptControl { Check, Gauge, Pair, Value };

struct OptRow {
    std::string label;
    OptControl kind = OptControl::Check;
    int words[2] = {-1, -1};     // Pair: word pictures (Japanese / English, On / Off ...), else texts
    std::string texts[2];
    int selected = -1;           // Pair: the lit button
    int* gauge = nullptr;        // Gauge: the 0..100 setting
    int value = 0;
    std::string valueText;       // Value
    const cr::Texture* valueTex = nullptr; // Value: a picture in place of the text (3.1.0's language name)
    float valueUv[4] = {0, 0, 0, 0};
    bool enabled = true;
    std::string hint;             // shown under the rows while the cursor is on this one
    std::function<void(int)> act; // Gauge / Value: -1 / +1 (Value: 0 = Enter); Pair: 0 / 1; Check: 0
    int sound = int(se::kChoose); // played when act runs (OptionPrc: 2 for every button; gauges below)
    bool enterSounds = false;     // act(0) plays a sound of its own (Sound Test), so none from the menu
    bool quitWord = false;        // Check: "Exit" on its button in place of "Check" (Quit Game, #16)
};

constexpr int kOptionPages = 6; // Game, Sound, Display, Detail, Cheats, Graphics (the last: its default:)
constexpr int kOptionsBackRow = 99;
extern const char* const kOptionPageName[kOptionPages];
// Layout, in menu units
constexpr float kOptRowTop = 52, kOptRowPitch = 36, kOptCtrlX = 224, kOptCtrlScale = 0.85f;
constexpr float kOptBackY = 344, kOptPageY = 350;
// The port's: rows shown at once (the rest scroll, clear of the help line and the Back bar), and
// the scroll bar at the rows' right
constexpr int kOptVisible = 7;
constexpr float kOptBarX = 486, kOptBarW = 8;

bool phoneMenus(const App&);
struct PhoneMenu {
    cr::MenuPainter& m;
    PhoneMenu(const App& app, cr::MenuPainter& p) : m(p) { m.setOffset(phoneMenus(app) ? 16.0f : 0.0f, phoneMenus(app) ? 32.0f : 0.0f); }
    ~PhoneMenu() { m.setOffset(0, 0); }
};
// The port's own screens (the denser Options, the manual), made for the iPad's 512 x 384: in the
// iPhone layout the whole of it scaled to the screen's height (x 0.833), centred
constexpr float kPhoneFitK = 320.0f / 384.0f, kPhoneFitX = 16 + (480 - 512 * kPhoneFitK) / 2, kPhoneFitY = 32;
struct PhoneFit {
    cr::MenuPainter& m;
    PhoneFit(const App& app, cr::MenuPainter& p) : m(p) {
        if (app.renderer.phoneLayout()) m.setFit(kPhoneFitX, kPhoneFitY, kPhoneFitK);
    }
    ~PhoneFit() { m.setFit(0, 0, 1); }
};

// ---- The shop (Util 2: __menuu_ShopPrc / __menuu_ShopPut) ----
// Laid out as ShopPut draws it (its windows, stMenuf_Shop_Rect, all sit at 0, 0), in the menus'
// 512 x 384 units:
//   "Shop" (picture 97) at (8, 4); the Oz bar (152) at (316, 0), nine digits from x 362.
//   The shopkeeper (window 3: c101_L01_hd, tex 10's (1, 3), 256 x 256) at the iPhone screen's
//   (0, 64) - (16, 96) here - on Talk / Buy / Sell, the categories, the pair and the questions.
//   Talk / Buy / Sell plates at (328, 144 + 48k), the categories at (328, 96 + 48k), the pair
//   (Buy's Weapons / Armor / Jewel, Sell's but Items: __Buy_CharChoiceProc) at (328, 176 + 56k),
//   the Back bar (ComUty_CmdBack type 1) at (192, 288).
//   The section labels (the menu font, 13): "Shopping" (window 2, Talk / Buy / Sell), then "Buy"
//   / "Sell" at (12, 40) over line 15 at (0, 56), ">Weapons" at (40, 58) over line 16 at (24, 72).
//   The list (MuScrList type 0) - rows (ComUty_ShopItemPlate: picture 8, 9 for the cursor's,
//   256 x 48) at x 192 from y 150 + its scroll, 48 apart, cut to y 76..280: icon at +12 / +8,
//   name at +56 / +12, price at +158 / +30 (six digits 10 apart), "/OZ" (101) at +226 / +34; the
//   ⬇ / ⬆ jump plates, the gauge at (456, 72). Above them the HELP board with the item's help
//   text; for equipment ◀ Back ▶ (ComUty_CmdBack 2) switches the character.
//   The character's board (ComUty_StateBoardDraw at 12, 92) for equipment: face (144 on) at
//   (4, 60), name box (24, 92), LV box (24, 124), HP / MP / NEXT rows at y 148 / 168 / 188 with
//   their bars, EXP and the stats from y 208, 20 apart, values from x 92; the highlighted item's
//   change beside each (▲ / ▼, the amount in small digits).
//   How many: PRICE (98) at (312, 40) and the total from x 362, the count between < and >
//   buttons at y 60, Yes / No at (336, 104 / 160), the question on the message board (picture
//   12, 480 x 96) at (0, 220), its text from (24, 234).
// Widescreen keeps the left-hand group (heading, labels, shopkeeper, board) to the screen's left
// and the right-hand one (Oz, plates, list, Yes / No) to its right, as the later widescreen
// release does; Back and the message board stay centred.
namespace shopui {
constexpr int kMainWords[3] = {26, 27, 28};          // Talk, Buy, Sell
constexpr int kMainIcons[3] = {20, 22, 23};          // kanIconNoTbl - 1
constexpr int kCategoryWords[5] = {8, 29, 30, 31, 35}; // Items, Weapons, Armor, Jewel; scenario 1's fifth Buy plate (_kgnCommandNameTbl)
constexpr int kCategoryIcons[5] = {8, 24, 25, 26, 8};
// __CategoryComDraw: five plates from y 0x30 when buying in scenario 1 (its OMU items), else four from 0x60
inline int categories(const App& app) { return !app.shop.selling && app.game.scenario != 0 ? 5 : 4; }
// MuScrList type 0 (the table at 0xd2a70): rows 256 x 48 at x 192 from y 150 + the scroll, cut
// to y 76..280 by its masks; the ⬇ / ⬆ jump plates (pictures 0x90 / 0x91) at x 280; the gauge
// (ComUty_GuageBarDraw) at (456, 72), 208 long. Part 12 366 s / Part 8 3476 s: the first row at
// 150, the third cut at 280, ⬇ above at 112
constexpr float kRowX = 192, kRowY = 150, kRowH = 48, kClipTop = 76, kClipBottom = 280;
constexpr float kGaugeX = 456, kGaugeY = 72, kGaugeLen = 208;
constexpr float kBackX = 192, kBackY = 288;
constexpr uint32_t kSoundEquip = 0x91 + 21; // ShopPrc, Yes to "Equip it?": SE 0xa6
// The board's stat rows (the delta table at 0xc8af0, from the board's y): HP, MP, Attack, Defense, Speed, Luck
constexpr float kStatRowY[6] = {56, 76, 136, 156, 176, 196};
} // namespace shopui

// ---- The Camp menu (__menuu_CampMain*, __MuCamp_NextProc and its pages) ----
// Laid out as CampMainPut draws it (stMenuf_CampMain_Rect: every window at 0, 0), in the menus'
// 512 x 384 units:
//   "Camp" (picture 81) at (8, 4); the clock and the purse (ComUty_BattryAndRealTimeAndOzDraw);
//   the pair's boards (ComUty_FaceBoardDraw) at (0, 24 + 88k); the plates Items, Equip, Genes,
//   Status, Options (0xc8600) at (328, 48 + 48k); Back; the message board (picture 12, (0, 220)).
// On a page the boards slide out left, the plates right; the page's title (font 0 at (12, 40) over
// picture 15 at (0, 56)) and section (font 2 at (40, 58), picture 16 at (24, 72)) come in, the
// pair's name plates at (328, 176 + 56k) and the state board (ComUty_StateBoardDraw) at (12, 92).
//   Items: the bag by category (make_item_list: Standard 1-60, Event 61-80, Weapons, Armor,
//     Jewel, Gene Plates; ◀ ▶ skips empty ones) with counts (ComUty_CampItemPlate); the seeds
//     (3-6) and the MP items (24-27) are used on a member (use_item: a seed point each, up to
//     5000 a stat; MP +100 / +250 / +500 / full), the seeds by the count asked (57077).
//   Equip: a member, then weapon / armor / jewel and what the bag holds for it, the board
//     showing what it changes; wearing one puts the old back in the bag.
//   Status: a member's board (at (308, 68)).
// Genes wait for the gene plates. Save is the port's own (the original saves from the field's
// SAVE button and save points).
namespace campui {
constexpr int kWords[5] = {8, 9, 10, 11, 3};                 // Items, Equip, Genes, Status, Options
constexpr bool kReady[5] = {true, true, true, true, true};
constexpr int kEquipSlots = 7;                                 // weapon, armor, jewel, Human Genes, 3 plates
constexpr int kEntries = 5;                                    // (CR1's iPad had Save as a sixth)
constexpr float kPlateX = 328, kPlateY = 48, kPlateStep = 48;
constexpr float kMemberY = 176, kMemberStep = 56;
constexpr int kCategories = 8;
constexpr int kCategoryKind[kCategories] = {16, 3, 6, 7, 8, 10, 11, 12}; // make_item_list (_kanItemTypeTbl)
constexpr int kCategoryLabel[kCategories] = {58071, 58072, 58073, 58074, 58075, 58079, 58124, 58125}; // (_kanStrItemTbl)
constexpr float kRowX = 192, kRowY = 92, kRowH = 48;
constexpr int kRows = 4;
// MuScrList mode 0 (d2a70..): rows at x 192, the focus row's line at y 150, the rows cut to 76..280 (the mask
// quads over 0..76 and 280..356), the gauge at (456, 72), 208 high
constexpr float kFocusY = 150, kListTop = 76, kListBottom = 280, kGaugeX = 456, kGaugeY = 72, kGaugeH = 208;
constexpr float kBackX = 192, kBackY = 344;
constexpr float kPhoneBackY = 286; // 2.2.0's (__MuCamp_NextProc's box (192, 286, 256, 34)); it has no Save plate
constexpr uint32_t kSoundEquip = 0x91 + 21, kSoundUse = 0x91 + 20; // __ChangeEquipItem's 0xa6, an item used (CampMainPrc 7)'s 0xa5
constexpr int kRemove = 200;                                   // the lists' "Remove" entry (item 200, __ChangeEquipItem)
constexpr float kEntriesEquipY = 44;                           // the entry list's Equip plate (the list's -5 row), over the rows
} // namespace campui

// The Emergency Exit's frames after Yes: CampMainPrc 7 takes the Yes / No and its board off (the list went
// when the Exit was chosen), 0xf - 0x12 wait; CampMain 9 puts the clock and purse (window 1) on and 0xd, the
// next frame, takes every window off and clears the mask (its 400 ms fade). A window taken off while it is
// coming on is whole the next frame (ClrWin: state 4 -> 5, whose fade starts at 0xff0): the clock flashes in
// whole at 11, then fades with the "Camp" heading - 3/4, 1/2, 1/4, gone at 15 (Part 13 2946.94-2947.18 s: the
// mask's first step a frame before the clock, TIME / OZ whole a frame, then fading with the heading). 0xe four
// frames on fades to white over 500 ms with sound 0x128 and bgmgo(0x8000000), and 12 frames later the Camp closes
// (0xf, 0x10) - CampMainClr: pcparam_hpmp_recovery, then the scenario's Ark (area 9, map 0xa9 -
// 0xbc once flag 0x4f4 is set -; the second scenario area 10, map 0xc0) at (0, 7.5, 15) facing 0,
// load flags 0x26: the area's script 0 runs (InitFieldScene, bit 0x20) and no room script (bit 8)
constexpr int kCampExitHide = 10, kCampExitGone = 15, kCampExitFade = 16, kCampExitWarp = 30;

struct KeyIcon { float x, y, u, v; bool lit; }; // (tex2d top-left, its piece, the key's flag set)

// ---- The map screen (__menuu_MapSet / MapPrc / MapPut, ReadMapMenu) ----
// ReadMapMenu by area: 2..5 map_D01..D04 (_gnFieldNo 0..3), 8 map_D07 (6), in slot 0x2f, 512 x
// 512 - MuTex group 1 pictures 15 the map 480 x 320, 16 the pair's marker (2 frames of 48), 17 a
// story marker (2 of 16), 18..21 16 x 16 icons. MapSet: the marker on the map's spot
// (_gkashMapCharPosTbl, the map clamped to 0..149; map 151 none) less (24, 20), blinking; on D01..D04
// the two markers of the story's step (the first of the scenario's 25 flags set, _kgnMuMapRootFlg)
// less (8, 16). MapPut: D03's two covers and D04's five (picture 21, 8 higher) until their flags; on
// D07 its 58 rooms (the spots of maps 92..149, x - 8, y + 10) on three pages a tap turns - the
// warps (18), the doors and boxes (19 / 20, _gkastMapIcon) and the rest (21), the last two with
// their legend. A tap closes it (D07: after the third page). Coordinates tex2d, under PhoneMenu.
namespace mapui {
// _gkashMapCharPosTbl (0xcc7d4): each map's spot (s16 x, y; -1 none) - D07's rooms from map 92
constexpr short kMapSpot[150][2] = {
    {72, 216}, {122, 196}, {82, 172}, {176, 152}, {132, 152}, {188, 108}, {327, 218}, {288, 196},
    {426, 180}, {338, 176}, {386, 155}, {350, 132}, {360, 88}, {-1, -1}, {100, 204}, {144, 204},
    {110, 160}, {66, 160}, {154, 160}, {166, 116}, {284, 228}, {334, 206}, {294, 184}, {308, 140},
    {262, 140}, {220, 140}, {354, 120}, {402, 120}, {412, 74}, {-1, -1}, {-1, -1}, {80, 176},
    {124, 176}, {168, 176}, {38, 176}, {92, 132}, {330, 200}, {292, 180}, {254, 156}, {264, 112},
    {304, 136}, {380, 180}, {430, 156}, {418, 200}, {392, 136}, {352, 112}, {364, 68}, {-1, -1},
    {-1, -1}, {-1, -1}, {116, 186}, {82, 232}, {160, 188}, {127, 144}, {172, 144}, {322, 212},
    {278, 212}, {234, 212}, {334, 168}, {288, 168}, {244, 168}, {278, 124}, {430, 132}, {442, 86},
    {344, 124}, {356, 78}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
    {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
    {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
    {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {118, 264}, {138, 264}, {158, 264}, {178, 264},
    {198, 264}, {185, 244}, {192, 224}, {172, 224}, {180, 204}, {212, 224}, {220, 204}, {227, 184},
    {207, 184}, {214, 164}, {221, 144}, {229, 124}, {209, 124}, {169, 124}, {161, 144}, {154, 164},
    {174, 164}, {160, 204}, {140, 204}, {147, 184}, {132, 224}, {187, 184}, {241, 144}, {261, 144},
    {281, 144}, {301, 144}, {309, 124}, {321, 144}, {274, 164}, {254, 164}, {247, 184}, {240, 204},
    {260, 204}, {280, 204}, {287, 184}, {272, 224}, {267, 184}, {294, 164}, {334, 164}, {356, 104},
    {336, 104}, {316, 104}, {344, 84}, {236, 104}, {244, 84}, {250, 64}, {258, 44}, {238, 44},
    {218, 44}, {265, 24}, {285, 24}, {305, 24}, {325, 24}, {332, 4},
};
// _kgstMuMapRootPos (0xccad4): per field D01..D04, per story step, two markers (x, y, kind)
constexpr short kMapMark[4][25][2][3] = {
    {
        {{68, 248, 1}, {-1, -1, 0}},
        {{75, 224, 0}, {80, 204, 1}},
        {{87, 224, 0}, {107, 224, 0}},
        {{92, 204, 0}, {112, 204, 0}},
        {{51, 192, 0}, {71, 192, 0}},
        {{133, 216, 0}, {153, 216, 0}},
        {{124, 204, 0}, {129, 184, 1}},
        {{168, 204, 1}, {173, 184, 0}},
        {{145, 172, 0}, {165, 172, 0}},
        {{180, 160, 0}, {185, 140, 1}},
        {{192, 116, 0}, {323, 252, 1}},
        {{297, 228, 1}, {317, 228, 0}},
        {{335, 252, 1}, {422, 212, 1}},
        {{257, 216, 0}, {277, 216, 0}},
        {{303, 204, 0}, {323, 204, 1}},
        {{290, 204, 0}, {338, 150, 0}},
        {{347, 208, 1}, {367, 208, 0}},
        {{353, 184, 0}, {373, 184, 1}},
        {{341, 184, 0}, {347, 164, 1}},
        {{417, 188, 0}, {397, 188, 1}},
        {{428, 188, 0}, {365, 140, 0}},
        {{353, 140, 0}, {358, 120, 1}},
        {{265, 96, 0}, {-1, -1, 0}},
        {{358, 163, 0}, {378, 163, 1}},
        {{-1, -1, -1}, {-1, -1, -1}},
    },
    {
        {{95, 236, 1}, {-1, -1, 0}},
        {{107, 236, 1}, {127, 236, 1}},
        {{113, 212, 0}, {133, 212, 0}},
        {{100, 212, 0}, {106, 192, 1}},
        {{139, 236, 1}, {134, 256, 0}},
        {{76, 191, 1}, {96, 191, 1}},
        {{82, 167, 0}, {102, 167, 0}},
        {{122, 180, 0}, {142, 180, 0}},
        {{69, 168, 0}, {74, 148, 1}},
        {{157, 168, 0}, {162, 148, 1}},
        {{169, 124, 0}, {280, 260, 1}},
        {{299, 236, 0}, {319, 236, 1}},
        {{287, 236, 0}, {292, 216, 1}},
        {{311, 192, 0}, {331, 192, 1}},
        {{299, 192, 0}, {304, 172, 1}},
        {{295, 160, 0}, {275, 160, 0}},
        {{248, 172, 1}, {228, 172, 1}},
        {{234, 148, 0}, {254, 148, 0}},
        {{215, 171, 1}, {210, 191, 0}},
        {{322, 148, 0}, {342, 148, 1}},
        {{369, 140, 0}, {389, 140, 0}},
        {{404, 128, 0}, {409, 108, 1}},
        {{416, 84, 0}, {-1, -1, 0}},
        {{-1, -1, -1}, {-1, -1, -1}},
        {{-1, -1, -1}, {-1, -1, -1}},
    },
    {
        {{77, 208, 1}, {-1, -1, 0}},
        {{88, 208, 1}, {108, 208, 1}},
        {{94, 184, 0}, {115, 184, 0}},
        {{84, 184, 0}, {89, 164, 1}},
        {{51, 184, 0}, {70, 184, 0}},
        {{44, 208, 1}, {64, 208, 1}},
        {{133, 208, 1}, {153, 208, 1}},
        {{140, 184, 0}, {159, 184, 0}},
        {{172, 184, 0}, {177, 164, 1}},
        {{40, 184, 0}, {45, 164, 1}},
        {{95, 140, 0}, {327, 232, 1}},
        {{301, 208, 1}, {321, 208, 0}},
        {{345, 208, 0}, {365, 208, 1}},
        {{262, 188, 1}, {282, 188, 0}},
        {{257, 164, 0}, {262, 144, 1}},
        {{269, 164, 0}, {289, 164, 1}},
        {{269, 120, 0}, {274, 100, 1}},
        {{395, 188, 0}, {414, 188, 1}},
        {{421, 208, 0}, {427, 188, 1}},
        {{401, 164, 1}, {421, 164, 0}},
        {{409, 252, 0}, {415, 232, 1}},
        {{363, 144, 1}, {382, 144, 0}},
        {{318, 144, 0}, {338, 144, 1}},
        {{357, 120, 0}, {362, 100, 1}},
        {{368, 76, 0}, {-1, -1, 0}},
    },
    {
        {{120, 215, 1}, {-1, -1, 0}},
        {{95, 235, 0}, {100, 215, 1}},
        {{128, 203, 0}, {148, 203, 0}},
        {{110, 190, 0}, {114, 169, 1}},
        {{50, 247, 0}, {70, 247, 0}},
        {{163, 191, 0}, {168, 171, 1}},
        {{175, 147, 0}, {180, 127, 1}},
        {{139, 159, 0}, {159, 159, 0}},
        {{131, 147, 0}, {318, 239, 1}},
        {{289, 227, 0}, {309, 227, 0}},
        {{324, 215, 0}, {330, 195, 1}},
        {{245, 227, 0}, {265, 227, 0}},
        {{236, 215, 0}, {242, 195, 1}},
        {{257, 183, 0}, {277, 183, 0}},
        {{280, 171, 0}, {286, 151, 1}},
        {{301, 183, 0}, {321, 183, 0}},
        {{270, 127, 0}, {275, 107, 1}},
        {{348, 173, 0}, {368, 173, 1}},
        {{336, 171, 0}, {342, 151, 1}},
        {{397, 155, 0}, {417, 155, 1}},
        {{434, 135, 0}, {439, 115, 1}},
        {{348, 127, 0}, {353, 107, 1}},
        {{360, 83, 0}, {-1, -1, 0}},
        {{-1, -1, -1}, {-1, -1, -1}},
        {{-1, -1, -1}, {-1, -1, -1}},
    },
};
// _gkastMapIcon (0xd9070): D07's rooms - a box's flag, the door's, the door found, the rest found
constexpr short kMazeIcon[58][4] = {
    {1662, -1, -1, -1}, {-1, -1, -1, -1}, {1680, -1, -1, 1715}, {1673, -1, -1, -1}, {-1, -1, -1, -1}, {-1, 1, 1692, -1},
    {-1, -1, -1, 1719}, {-1, -1, -1, -1}, {1674, 101, 1701, 1722}, {1668, -1, -1, 1724}, {-1, -1, -1, -1}, {-1, -1, -1, 1726},
    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, 1729}, {1677, -1, -1, 1731}, {-1, -1, -1, -1}, {1678, -1, -1, 1735},
    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1664, -1, -1, 1745}, {1670, 1, 1693, -1},
    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1679, -1, -1, -1}, {-1, -1, -1, -1},
    {-1, 103, 1700, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1},
    {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1666, -1, -1, -1}, {1681, 102, 1703, -1}, {1667, -1, -1, 1777}, {-1, -1, -1, -1},
    {1675, -1, -1, -1}, {-1, -1, -1, -1}, {1663, -1, -1, -1}, {1671, -1, -1, 1788}, {1665, 1, 1695, -1}, {-1, 1692, 1694, -1},
    {1672, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {1669, 105, 1702, 1794}, {-1, -1, -1, -1},
    {1676, -1, -1, -1}, {-1, -1, -1, -1}, {-1, 104, 1704, -1}, {-1, 1694, 1696, -1},
};
constexpr short kMazeWarp[6] = {0, 12, 21, 40, 43, 49};                                  // _gkashWarpTbl.13080
constexpr short kD03Cover[2][2] = {{280, 74}, {402, 276}};                               // flags 0x4b4, 0x4b5
constexpr short kD04Cover[5][2] = {{32, 252}, {182, 99}, {228, 224}, {268, 78}, {439, 98}}; // _C_36_13066, 0x4c8..
constexpr short kVerFlag[5][2] = {{101, 1651}, {102, 1652}, {103, 1653}, {104, 1654}, {105, 1655}}; // _gshVerFlagTbl01 / 02
constexpr int kFlagBase = 2601, kFlagStride = 50, kSteps = 25; // _kgnMuMapRootFlg: 2601 + 50 x scenario
constexpr int kFirstRoom = 92, kRooms = 58, kMazeField = 6;
constexpr int kText[2] = {57111, 57112}; // MuComFontFromIdRegist(n, 6, 0x6f + n)
int field(int area);
int doorIcon(const App& app, const short* ic);
} // namespace mapui

// ---- Scenarios (__menuu_SccnariosSet / Put / Prc: the bingo cards) ----
// The overview (windows 0..5): the eight faces (tex_scenario picture 0) with an X over those whose
// day has passed (flags 0x518.., the first two never), the two Achievement rates and, once flag
// 0x642 is set, the cards (four; five with flags 0x1f, 0x65 and 0x665 too). A card (windows 6..11):
// its picture (present0N_hd) behind sixteen cells; a cell opens with one of its five presents
// (_ganItemTbl) not yet used on another card (_ganPushFlgTop), only from the Camp (mode 1). All
// sixteen open: the card's done (flag 0x64b + card, var 0x16 + 1) and its picture zooms.
// Positions are the iPhone screen's (480 x 320 menu units, at the iPad's (16, 32)); the pictures'
// rectangles (_gkastScc*Mct, groups 3 / 4) are the SD sheets', the _hd sheets twice that
namespace bingo {
constexpr short kSheetPict[16][4] = { // group 3, tex_scenario
    {0, 0, 455, 106}, {462, 0, 50, 100}, {368, 221, 144, 35}, {240, 221, 128, 35}, {352, 192, 160, 29},
    {0, 230, 24, 26}, {160, 181, 20, 18}, {0, 168, 90, 62}, {180, 168, 72, 40}, {0, 106, 90, 62},
    {90, 106, 90, 62}, {180, 106, 90, 62}, {270, 106, 90, 62}, {360, 106, 90, 62}, {90, 168, 59, 60},
    {471, 117, 20, 34}};
constexpr short kCardPict[54][4] = { // group 4, tex_card_<colour>
    {64, 473, 224, 39}, {0, 0, 480, 7}, {0, 228, 480, 8}, {0, 7, 129, 221}, {350, 7, 130, 221}, {0, 480, 55, 32},
    {129, 7, 55, 55}, {184, 7, 55, 55}, {239, 7, 55, 55}, {294, 7, 56, 55}, {129, 62, 55, 55}, {184, 62, 55, 55},
    {239, 62, 55, 55}, {294, 62, 56, 55}, {129, 117, 55, 55}, {184, 117, 55, 55}, {239, 117, 55, 55},
    {294, 117, 56, 55}, {129, 172, 55, 56}, {184, 172, 55, 56}, {239, 172, 55, 56}, {294, 172, 56, 56},
    {0, 240, 55, 55}, {55, 240, 55, 55}, {110, 240, 55, 55}, {165, 240, 56, 55}, {0, 295, 55, 55},
    {55, 295, 55, 55}, {110, 295, 55, 55}, {165, 295, 56, 55}, {0, 350, 55, 55}, {55, 350, 55, 55},
    {110, 350, 55, 55}, {165, 350, 56, 55}, {0, 405, 55, 56}, {55, 405, 55, 56}, {110, 405, 55, 56},
    {165, 405, 56, 56}, {224, 240, 55, 55}, {279, 240, 55, 55}, {334, 240, 55, 55}, {389, 240, 56, 55},
    {224, 295, 55, 55}, {279, 295, 55, 55}, {334, 295, 55, 55}, {389, 295, 56, 55}, {224, 350, 55, 55},
    {279, 350, 55, 55}, {334, 350, 55, 55}, {389, 350, 56, 55}, {224, 405, 55, 56}, {279, 405, 55, 56},
    {334, 405, 55, 56}, {389, 405, 56, 56}};
constexpr int kPushFlag[5] = {2001, 2017, 2033, 2049, 2065}; // + cell: that cell's n-th present used
constexpr int kOpenFlag[5] = {2101, 2117, 2133, 2149, 2165}; // + cell: open on card k
constexpr short kItems[16][5] = {
    {552, 555, 588, 599, 605}, {553, 557, 562, 573, 574}, {559, 568, 583, 597, 607}, {558, 561, 589, 614, 618},
    {563, 572, 604, 620, 621}, {564, 570, 587, 623, 630}, {551, 565, 575, 580, 592}, {566, 567, 569, 615, 619},
    {571, 586, 609, 622, 625}, {576, 579, 590, 613, 626}, {578, 581, 591, 598, 617}, {554, 582, 584, 602, 608},
    {560, 593, 606, 611, 612}, {594, 595, 600, 616, 627}, {556, 596, 610, 624, 628}, {577, 585, 601, 603, 629}};
constexpr short kBirthday[15][2] = { // {flag, points}: the first rate
    {1162, 5}, {1163, 5}, {1202, 5}, {1203, 5}, {1116, 5}, {1242, 10}, {1182, 5}, {1183, 5},
    {1222, 5}, {1223, 5}, {1262, 10}, {1265, 5}, {1244, 10}, {1267, 10}, {1269, 10}};
constexpr short kFestival[14][2] = { // the second, with a point per open cell
    {1693, 3}, {1695, 3}, {1692, 6}, {1694, 6}, {1696, 6}, {1691, 0}, {1697, 6},
    {1698, 6}, {1700, 1}, {1701, 1}, {1702, 1}, {1703, 1}, {1704, 1}, {1699, 4}};
// tex_card_complete (group 5): the seven pieces; their keyframes (0xd5f10: four a piece), the steps'
// from-keyframes (0xd5eb0) and lengths (0xd5ee0): in from off-screen over 10, a bob, then held 30
constexpr short kCompletePict[7][4] = {{0, 141, 194, 192}, {194, 141, 188, 192}, {0, 333, 512, 293}, {0, 626, 512, 293},
                                       {382, 162, 130, 171}, {382, 0, 130, 162}, {0, 0, 382, 141}};
constexpr int kCompletePoint[7][4][2] = {
    {{-132, 246}, {-4, 107}, {-4, 110}, {-4, 104}},  {{437, 251}, {296, 107}, {296, 110}, {296, 104}},
    {{-31, 295}, {-31, 30}, {-31, 33}, {-31, 27}},   {{-31, 296}, {-31, 31}, {-31, 34}, {-31, 28}},
    {{-136, 265}, {-4, 149}, {-4, 152}, {-4, 146}},  {{482, 277}, {352, 154}, {352, 157}, {352, 151}},
    {{56, -115}, {56, 84}, {56, 87}, {56, 81}}};
constexpr int kCompleteFrom[12] = {0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 1, 1};
constexpr int kCompleteLen[11] = {10, 2, 2, 2, 2, 2, 2, 2, 2, 2, 30};
constexpr const char* kCardFile[5] = {"tex_card_red.mct", "tex_card_blue.mct", "tex_card_yellow.mct",
                                      "tex_card_green.mct", "tex_card_violet.mct"};
// The cells (_kgnBingoWindowPosXY) and their PUSH! marks (_kgnBingoPushPosXY)
inline float cellX(int i) { return 129 + 55.0f * float(i % 4); }
inline float cellY(int i) { return 55 + 55.0f * float(i / 4); }
constexpr int kCardFlag = 0x64b; // + card: done
constexpr int kDoneVar = 0x16;
// The photo's zoom (iPad: 1 .. 2, 0.2 a frame) and how far it pans when zoomed (tex2d)
constexpr float kZoomMin = 1, kZoomMax = 2, kZoomStep = 0.2f, kPanX = 480, kPanUp = -152, kPanDown = 320;

cr::GameState& flags(App& app);
bool shown(App& app);
int cards(App& app);
bool done(App& app, int card);
int birthdayRate(App& app);
int festivalRate(App& app);
int presentsUsed(App& app);
void refresh(App& app);
int openAll(App& app);
void loadCard(App& app, int card);
void winOn(App::Scenarios::Win& w, float from);
void winOff(App::Scenarios::Win& w, float to);
void winStep(App::Scenarios::Win& w);
float winX(const App::Scenarios::Win& w);
float winAlpha(const App::Scenarios::Win& w);
void cardWindowsOn(App& app, float from);
float photoAlpha(App& app);
} // namespace bingo

namespace collection {
int have(const App& app, int row);
int percent(const App& app);
} // namespace collection

constexpr int kManualFirstItem = 419, kManualLastItem = 473, kManualFirstPage = 56000, kOptionIntroPage = 56040;

namespace puzzle {
const cr::PuzzleStage* stage(const App& app);
bool cleared(const App& app);
int* cellSlot(App& app, int layer, int r, int c);
int objectAt(App& app, int layer, int r, int c);
cr::Vec3 centre(const App& app, int r, int c);
bool cellOf(const App& app, cr::Vec3 p, int& r, int& c);
bool allSwitchesOn(App& app);
void enter(App& app);
bool allCandlesLit(App& app);
void teleportTo(App& app, int t);
void reset(App& app);
void pkGrab(App& app, int t, float sx = -1e9f, float sy = -1e9f);
void pkSend(App& app, int dir);
void toughTap(App& app, int t);
void pyroTap(App& app, int t);
bool target(const App& app, const App::Puzzle::Obj& o);
bool inWindow(int type, float dx, float dy);
int pick(App& app, float tx, float ty);
void step(App& app);
bool thirdButton(const App& app);
bool ask(App& app, int k);
void stepBanner(App& app);
void stepCrystalCam(App& app);
bool click(App& app, float px, float py);
void drag(App& app, float px, float py);
void release(App& app, float px, float py);
void abilityKey(App& app);
void drawFx(App& app, const cr::Mat4& mvp);
} // namespace puzzle

// The battle HUD (menub_mainPut and its cmd_* pieces), in the menus' 512 x 384 units: the
// tex2d calls' 480 x 320 space moved by (16, 32). Everything comes from window_hd (slot 44) or
// plain colour (slot 45).
namespace btlui {
constexpr float kButtonX = 344, kButtonY = 128, kButtonStep = 48; // cmd_bottom at tex2d (328, 96 + 48k)
constexpr float kPlateX = 224, kPlateBottom = 312;                 // cmd_bottom_teki: y = 312 - 48 (n - i)
constexpr float kBackX = 208, kBackY = 320;                        // cmd_back at tex2d (192, 288)
constexpr float kYesNoX = 352, kYesY = 136, kNoY = 192;              // ComUty_YesNoChoiceDraw: window 6 + (336, 104) / (336, 160)
// The first menu: Solo, Pair, Escape, Options; then Attack, Genes, Items, Defend (icon, word)
constexpr int kTopIcon[4] = {15, 14, 16, 3}, kTopWord[4] = {21, 20, 22, 3};
constexpr int kCmdIcon[4] = {18, 10, 8, 19}, kCmdWord[4] = {24, 10, 8, 25};
// The Break badge's eight ring pieces (0xd1fd0): x, y, u, v, 32 x 32 each
constexpr short kBreakTick[8][4] = {{4, 4, 96, 120},    {4, 4, 64, 120},    {4, 28, 64, 152},  {4, 28, 96, 152},
                                    {28, 28, 128, 152}, {28, 28, 160, 152}, {28, 4, 160, 120}, {28, 4, 128, 120}};
// __dmg_prc: the digits' hop (0xcb398) and the label's (0xcb390), by (383 - timer) / 16
constexpr int kDigitHop[8] = {16, 20, 15, 10, 5, 0, 0, 0}, kLabelHop[8] = {3, 6, 12, 6, 3, 0, 0, 0};
constexpr int kPopupTime = 0x17f; // __dmg_set; 16 off a frame
constexpr int kItemRows = 4;      // the battle lists' rows shown, 48 apart (the plates 44 tall, as the camp's)
constexpr float kItemPitch = 48;
constexpr int kBannerFrames = 40;  // font_set_utf16's 0x28 (the action's name)
constexpr int kMessageFrames = 60; // 0x3c: the calcs' messages and the queued ones
// Action slots (BattleAnim): 0 standing, 1 victory, 3 back to place, 4 defending, 5 hit,
// 6 fallen, 7 evading; attacks use their class (8-10), genes 11, items 12
constexpr int kSlotIdle = 0, kSlotWin = 1, kSlotReturn = 3, kSlotGuard = 4, kSlotHit = 5, kSlotDown = 6, kSlotRise = 7; // (7: getting up - btlcalc_0424's revive)
} // namespace btlui

constexpr int kPropFighter = 5; // an actor that is no fighter: groups 0x291 / 0x22c's extra model
constexpr int kIntroWhite = 4;  // the steps under the encounter's white before btl_main 200 (10, 100, 199 - then 200)

// ---- Battle effects (eff3d_*) ----
// eff.bin is an index of 100 (u32 offset, u32 size) entries, each a model package. An effect plays
// its first action once at a point (an actor's origin, or the node of its bone whose record user
// id the table 0xd37c0 names: 1101 the body, 1102 the head, 1003 the root, 1201.. the limbs) and
// is deleted at its end (eff3d_prc). Its materials (diffuse alpha 0 at rest) take their colour
// from the package action their Appearance's refGid numbers (segment 1, keyType 12 RGBA).
namespace effects {
constexpr int kEffects = 165;
// _eff3d_cpno: the node each effect starts on (0: the actor's place)
constexpr short kNode[kEffects] = {
       0,    0, 1204, 1203, 1101, 1101, 1102, 1003, 1102, 1102, 1102,    0, 1101,    0,    0,
    1101, 1206,    0, 1101, 1101, 1101, 1101, 1101, 1101,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 1102,    0,    0, 1101, 1102,
    1101, 1101, 1102, 1101, 1101, 1102, 1101, 1101, 1102,    0, 1101, 1101, 1101,    0,    0,
    1101, 1101, 1101, 1101, 1101, 1102, 1102, 1102,    0, 1102, 1102, 1102, 1101, 1102, 1102,
    1101, 1101, 1101, 1101, 1101, 1101,    0,    0, 1101, 1101, 1101, 1101, 1101, 1101, 1101,
    1101, 1101,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0};
// What the calcs show (btlcalc_*'s eff3d_set_btl ids)
// (Omega's eff.bin: CR1's 0x15..0x19 less 0x14, 0x1a..0x22 less 0x10, from 0x23 + 5)
constexpr int kHit = 4, kCritical = 5, kFall = 6, kFixed = 0xc, kSacrifice = 0x12, kGene = 0x28,
              kStatus = 0x29, kDrain = 0x2a, kHeal = 0x34, kRevive = 0x36, kCast = 1, kBuff = 0x3a, kAnalyze = 0x3b;
} // namespace effects

namespace battletalk {
constexpr int kFlag = 4095;
bool on(const App& app);
void open(App& app);
void rules(App& app);
bool tutorial(App& app, int key);
void first(App& app);
void between(App& app);
void betweenStart(App& app);
bool end(App& app);
void step(App& app);
} // namespace battletalk

namespace btlvoice {
constexpr int kWinWaitMax = 16 * 20;
bool on(const App& app);
int speaker(const App& app, int k);
int clipFor(const App& app, int sp, int situation);
void play(App& app, int k, int a, int b2, int c);
int chooseSpeaker(App& app, int k);
void playSide(App& app, int k, int a, int b2, int c);
void pickSpeakers(App& app);
void hpChange(App& app, int k, int damage);
void healed(App& app, int k);
void attack(App& app, const cr::BattleTurn& t);
void command(App& app, int member);
void turn(App& app, int k, int action);
bool strongInBattle(const App& app);
bool winWaits(const App& app);
void victory(App& app);
} // namespace btlvoice

extern const float kWinMessagePanel[4];
extern const float kWinSmallPanel[4];

struct ViewerHost : cr::EventHost {
    App& app;
    explicit ViewerHost(App& a) : app(a) {}
    std::vector<App::NpcDraw*> npc(int id);
    void showMessage(int id) override;
    bool messageOpen() override;
    bool tapped() override;
    void showItemWindow(int item) override;
    void playAnim(int id, int slot, int loopFrom, int loopTo) override;
    void playAnim2(int id, int slot, int loopFrom, int loopTo, int start, int blend) override;
    void loadAction(int id, int action, int slot) override;
    void unloadAction(int id, int slot) override;
    bool animBusy(int id) override;
    void scaleNpc(int id, const float* s, int frames) override;
    void autoMove(int id, float x, float z, float speed) override;
    void fadeNpc(int id, float from, float to, int frames) override;
    void jointNpc(int npc, int parent, int node) override;
    void showNpc(int id, bool shown) override;
    void screenFade(float grey, float from, float to, int frames, bool back) override;
    bool screenFading(bool back) override;
    void warp(const cr::WarpRequest& w) override;
    void playSound(uint32_t id) override;
    void playMusic(uint32_t v) override;
    int random(int n) override;
    void recoverPair() override;
    void addLevels(int c, int n) override;
    void treasureBoxesShown(bool on) override;
    void saveEvent() override;
    bool inBattle() override;
    void changeBattleStage(int stage) override;
    void messageSkip(bool on) override;
    void setLeader(int c) override;
    int omuItemValue() override;
    void npcShadow(int npc, bool on) override;
    void puzzleTeleportCount(int n) override;
    void fireWall(int npc) override;
    int bgmNo() override;
    void endMenu(int kind) override;
    int pairNo() override;
    void nowLoading(bool on) override;
    static constexpr float kDeg = 3.14159265f / 180.0f;
    static float travelYaw(float dx, float dz);
    void placeNpc(int id, const float* v, float yawDeg, Facing facing) override;
    void moveNpc(int id, const float* v, float yawDeg, Facing facing, int frames, int mode) override;
    void tex2d(int slot, uint16_t f, int image, const int16_t (*extra)[3]) override;
    int npcMode(int id, int word, int bit, int op) override;
    void loadParty(int pair, bool clearData) override;
    void loadNpc(int id) override;
    void freeCamera(bool on) override;
    void setCamera(const float* eye, const float* target, int frames) override;
    void panCamera(const float* d, int frames) override;
    void sceneEffectSet(int set) override;
    void quake(int amp, int from, int to, int frames) override;
    void vitaLogo(int slot, int picture) override;
    void vitaLogoShow(int slot, int in, int hold, int out) override;
    bool vitaLogoBusy() override;
    void texDisp(const int32_t* g) override;
    void tutoTexDisp(const int32_t* g) override;
    void exclamationMark() override;
    void openWindow(int style, int lines) override;
    void closeWindow() override;
    bool windowClosed() override;
    void toggleFieldHidden() override;
    bool logoSeen() override;
    void setLogoSeen() override;
    void padArt() override;
    void battleParamUp(int fighter) override;
    void puzzleStart() override;
    void warpCrystalCamera(int npc) override;
    void sceneEffect(bool on, int id) override;
    void frameEffect(int actor, int effect, int scale, const float* v, bool angled) override;
    void stopFrameEffect(int effect, int actor) override;
    void npcEffect(int effect, int npc, int dy) override;
    void mapScroll(int npc, float x, float y, float rate) override;
    void lookAtNpc(int npc) override;
    void battle(int group, int stage, int flags) override;
    bool menu(int n) override;
    void askYesNo(int flag) override;
    bool choiceOpen() override;
    void faceNpc(int id, int target) override;
};

void holdSkip(App& app, unsigned source, bool held);
void clearSkip(App& app);
bool skipCorner(const App& app, float lx, float ly);
bool spinsTexture(const cr::Model& m, size_t mesh);
void spinUvs(std::vector<float>& uv, float turns);
void modelDrawn(App& app);
void menuSound(App& app, uint32_t id);
SDL_EnumerationResult SDLCALL collectAssets(void* user, const char*, const char* fname);
int wrap(int i, size_t n);
void updateTitle(App& app);
void showTexture(App& app, int index);
void applyHd(App& app, cr::Image& img);
bool loadImage(App& app, const std::string& mct, cr::Image& img, bool* fromAndroid = nullptr);
bool loadOriginalImage(App& app, const std::string& mct, cr::Image& img, bool* fromAndroid = nullptr);
bool openVita(App& app, const std::string& path);
bool startSound(App& app);
void vitaAudio(App& app);
void chooseVitaFolder(App& app);
void takeVitaFolder(App& app);
bool findAndroid(const std::string& path, cr::ObbArchive& out, std::string* error);
bool openAndroid(App& app, const std::string& path);
void chooseAndroidFolder(App& app);
void takeAndroidFolder(App& app);
std::string storedVitaPath();
std::string storedSetting(const char* key);
void unloadLoaded(App& app, LoadedModel& lm);
void unloadModel(App& app);
std::vector<bool> packageActionsKept(const cr::Model& m);
void loadModelFile(App& app, const std::string& name, LoadedModel& out, bool character);
void mdlMemRules(LoadedModel& lm);
void readMdlRules(LoadedModel& lm, const std::string& name);
LoadedModel& npcModel(App& app, const std::string& file);
void showModel(App& app, int index);
void showMap(App& app, int index, int forcedMap, int forcedArea);
void showMap(App& app, int index);
bool playerShown(const App& app);
bool npcCreateFlags(const App& app, const cr::FieldNpc& n);
bool npcStoryActor(const App& app, int id);
bool npcActive(const App& app, const cr::FieldNpc& n);
void resetFieldNpcs(App& app);
bool npcVisible(const App& app, size_t index);
void playBgm(App& app, uint32_t v);
void endFieldTasks(App& app);
void enterMap(App& app);
int triggerAt(const App& app, const cr::Vec3& p);
bool performWarp(App& app, const cr::WarpRequest& wIn, int fromEvent);
void stepExamineWindows(App& app);
const cr::FieldNpc* interactTarget(const App& app);
bool interact(App& app, bool instant = false);
bool languageUsable(const App& app, int language);
// The port's own text (platform/ui_text.hpp): its catalogs' folders and, before the settings are read (the
// first start's setup), the language settings.ini names - the system's on a first start; then uiLang's
void startUiText(App& app);
void applyUiLanguage(App& app);
// The text slot the messages are read in: cr::kFanLanguage while the fan translation is chosen, else `language`
int textLanguage(const App& app);
// The Options' language choices: 0..7 the game's languages, 8 the fan translation (English's slot, its text)
int languageChoice(const App& app);
bool choiceUsable(const App& app, int choice);
std::string languageChoiceName(const App& app, int choice);
void setLanguageChoice(App& app, int choice); // (applyLanguage, its art, saved)
// The fan translation (#11): in translation/ of a game folder or the profile folder, or at `path` - a
// folder of its files (sys.evt, the areas' .evt, its .gxt art; translation/<code>/), or its cro.psarc.
// Opened, its text set (cr::setFanText) before the scripts' files load
bool openFanTranslation(App& app, const std::string& path);
// The art it redrew - the message window's, window_char's names, the titles' - from the right source again
void refreshFanArt(App& app);
bool multilingualData(const App& app);
bool cjkLanguage(int language);
void applyLanguage(App& app);
std::string itemName(const App& app, int item);
void resetTyping(App& app);
void stepMsgWin(App& app);
void stepMsgWin(App::MsgWin& w);
void advanceDialogue(App& app, bool tapped = true);
int pageChars(const std::vector<std::string>& page);
std::vector<std::string> typedPage(const std::vector<std::string>& page, int limit);
const cr::MessageInfo* pageInfo(const App& app);
bool pageTyped(const App& app);
int pauseHold(int frames);
void stepTyping(App& app);
void tapDialogue(App& app);
void answerChoice(App& app, bool yes, bool cancelled = false);
void answerWorldChoice(App& app, int pos, bool skip = false);
void openItemWindow(App& app, int item);
void showMessages(App& app, const std::vector<int>& ids);
bool checkTriggers(App& app, bool instant = false);
void poseWorld(const cr::Skeleton& skel, const std::vector<cr::Action>& actions, const cr::Action* act, float time, const cr::MotionBlend& blend, std::vector<cr::Mat4>& world);
void animate(App& app, LoadedModel& lm, float dt);
void animateModel(App& app, float dt);
void placePlayer(App& app);
void loadSceneEffectSet(App& app, int set);
void endSceneEffects(App& app);
void setSceneEffect(App& app, bool on, int id);
void stepSceneEffects(App& app);
void drawSpaceBackdrop(App& app, bool battle);
void drawSceneEffects(App& app, bool battle = false);
bool paintedField(const App& app);
bool scrollingField(const App& app);
float fieldViewW(const App& app);
float fieldViewH(const App& app);
float fieldFillZoom(const App& app);
bool fieldViewPoint(const App& app, const cr::Vec3& p, float& vx, float& vy);
void scrollFieldTo(App& app, float vx, float vy, float rate);
void followPlayerScroll(App& app, float rate);
void stepFieldScroll(App& app);
bool stepMove(App& app, App::Move& m);
bool stepRescale(App& app, App::Rescale& r);
void stepScriptCam(App& app);
void scriptFieldScroll(App& app, float x, float y, int frames);
void applyMix(App& app);
std::string userDir();
std::string settingsPath();
void loadMix(App& app);
bool seenFlag(const App& app, int f);
void markSeen(App& app, int f);
void saveMix(const App& app);
std::string settingsText(const App& app);
void setSystemConfFlag(App& app, int f);
void fieldCheckAchievements(App& app);
void battleConfFlag(App& app, int f);
bool achievementBannerUp(const App& app);
void stepAchievements(App& app, float dt);
constexpr bool upscalerHere(int u);
void loadVideo(App& app, const char* text);
void loadSavedVideo(App& app);
void applyOrientation(App& app, bool captureDirection = false);
void applyVideo(App& app);
std::string sysLabel(const App& app, int id, const char* fallback);
std::vector<OptRow> optionRows(App& app, int page);
void optionsScrollFix(App& app, int n, bool follow);
void optionsBarTo(App& app, float my);
void optionsScrollBy(App& app, int rows);
void openOptions(App& app, int page);
void phoneMenuPoint(const App& app, float& mx, float& my);
float wideLeft(const App& app);
float wideRight(const App& app);
float androidEdgeX(const App& app);
float androidEdgeY(const App& app);
float vitaEdgeX(const App& app);
float vitaEdge43(const App& app);
float vitaLeft(const App& app);
float vitaRight(const App& app);
void menuPointAt(const App& app, float lx, float ly, float origin, float& mx, float& my);
void menuPointFit(const App& app, float lx, float ly, float& mx, float& my);
// The Yes / No question (OptionPrc 9 / 10's board): the screen darkened, the message window with its lines, Yes / No at (336, 104 / 160)
void drawYesNoBoard(App& app, const std::vector<std::string>& lines, int cursor);
// "Exit" in the word sheet's lettering (#16) - while the sheet and the menu text are English - centred in an 88-wide word at (x, y); false: none
bool drawExitWord(const App& app, cr::MenuPainter& m, float x, float y, float alpha);
void menuPoint(const App& app, float lx, float ly, float& mx, float& my);
void stepOptions(App& app);
void resetAnswer(App& app, bool yes);
void optionsKey(App& app, SDL_Keycode key);
int optionsBackPart(float mx, float my);
void optionsClick(App& app, float px, float py);
std::string savePath(const App& app, const char* file);
cr::SaveData captureSave(const App& app);
bool applySave(App& app, const cr::SaveData& s);
bool languageSheet(App& app);
std::string slotPath(const App& app, int pair);
std::string dataPath(const App& app);
bool readData(const App& app, cr::SaveData& d);
bool saveGame(App& app);
int currentPair(const App& app);
std::vector<int> savePlates(const App& app);
void openSaveScreen(App& app, bool load);
void closeSaveScreen(App& app);
void saveScreenYes(App& app);
void saveScreenChoose(App& app, int i);
void drawSaveScreen(App& app);
void stepSaveScreen(App& app);
void saveScreenKey(App& app, SDL_Keycode key);
void saveScreenClick(App& app, float px, float py);
int shopCharacter(const App& app);
int ownedCount(const App& app, int item);
int shopUnitPrice(const App& app, int item);
int shopMaxCount(const App& app, int item);
void shopFollowCursor(App::Shop& s);
void openShop(App& app);
void closeShop(App& app);
void stepShop(App& app);
void shopChooseMain(App& app, int i);
void shopChooseCategory(App& app, int c);
void shopChooseMember(App& app, int k);
void shopChooseItem(App& app, int index);
void shopPay(App& app);
std::string shopMessage(const App& app);
void drawShopNumber(cr::MenuPainter& m, int value, float x, float y, int digits, float step, float alpha, int style = 0, bool left = false);
void drawHelpWindow(App& app, cr::MenuPainter& m, int item, float a, float dx = 0, float dy = 0);
void drawShop(App& app);
void shopKey(App& app, SDL_Keycode key);
void shopClick(App& app, float px, float py);
bool keyHints(const App& app);
int plateIcon(int word);
int campCharacter(const App& app, int member);
int campMembers(const App& app);
int memberWord(const App& app, int c);
void drawClockAndOz(App& app, cr::MenuPainter& m, float a);
void drawFaceBoard(App& app, cr::MenuPainter& m, float x, float y, int c, int hp, int hpMax, int mp, int mpMax, float a, const cr::GameState* gs = nullptr);
void memberHpMp(const App& app, int c, int& hp, int& hpMax, int& mp, int& mpMax, const cr::GameState* gs = nullptr);
std::vector<int> campItems(const App& app, int category);
bool campCommonSkill(const App& app);
std::vector<int> campEquipList(const App& app, int c, int slot);
bool campUsable(const App& app, int item);
float campEase(const App& app);
void campTrackPhase(App::Camp& c);
float campListX(const App& app);
int campScrollCount(const App& app);
int campScrollTop(const App& app);
void campScrollBy(App& app, int rows);
int campEntries(const App&);
void openCamp(App& app);
void campNotice(App& app, int id);
void campRefreshList(App& app);
void campOpenPage(App& app, int i, bool sound = true);
int campSeedLimit(const App& app, int character, int item);
void campChoose(App& app);
void stepCampLeaving(App& app);
void campExitWarp(App& app);
void stepCamp(App& app);
void campKey(App& app, SDL_Keycode key);
void campClick(App& app, float px, float py);
void campPointerDown(App& app, float px, float py);
void campPointerMove(App& app, float px, float py);
void campPointerUp(App& app, float px, float py);
void drawCampRow(App& app, cr::MenuPainter& m, float x, float y, int item, int count, bool lit, float a, int style = 0, const std::string& label = std::string());
void stepPuzzleHud(App& app);
void drawPuzzleBanner(App& app);
int staffLines(const App&);
std::string staffLine(const App& app, int entry);
void openStaffRoll(App& app, int mode);
void stepStaffRoll(App& app);
void staffRollTap(App&);
void drawStaffRoll(App& app);
std::string lastBattlePlate(const App& app, int p);
void openLastBattle(App& app);
void lastBattleChoose(App& app, int i);
void lastBattleAnswer(App& app, bool yes);
void stepLastBattle(App& app);
void drawLastBattle(App& app);
void lastBattleKey(App& app, SDL_Keycode key);
void lastBattleClick(App& app, float px, float py);
bool worldAsksSkip(const App& app, int world, int pair);
void openWorldSelect(App& app);
void closeWorldSelect(App& app);
void worldSelectChoose(App& app, int pos);
void worldSelectLevel(App& app, int n);
void worldSelectAnswer(App& app, bool yes, bool cancelled = false);
void stepWorldSelect(App& app);
void drawWorldSelect(App& app);
void worldSelectKey(App& app, SDL_Keycode key);
void worldSelectClick(App& app, float px, float py);
void stepTexDisp(App& app);
void stepVitaLogo(App& app);
void stepTutoTex(App& app);
void stepTutoFrames(App::TutoTex& t);
void drawTutoPicture(App& app, const App::TutoTex& t);
int fieldRoute(const App& app);
bool fieldMenuWanted(const App& app);
bool fieldSceneRunning(const App& app);
bool fieldEncountersOff(const App& app);
bool fieldHudShown(const App& app);
void stepFieldMenu(App& app);
void trackFieldMenus(App& app);
void fieldButtonPos(const App& app, int k, float& x, float& y);
bool fieldBattleButton(const App& app);
bool fieldButtonShown(const App& app, int k);
void drawItemWindow(App& app);
void drawFieldMenu(App& app);
bool fieldBarPad(const App& app);
std::vector<int> fieldBarEntries(const App& app);
void toggleFieldMenu(App& app);
bool fieldBarButton(App& app, int b, bool confirm, bool cancel);
void pressFieldButton(App& app, int k);
bool fieldMenuClick(App& app, float px, float py);
void noteWindowAtlas(App& app, const cr::Image& wi);
void easyButtonCentre(const App& app, float& x, float& y);
bool easyButtonShown(const App& app);
bool touchPadDown(App& app, float px, float py);
void touchPadMove(App& app, float px, float py);
void touchPadUp(App& app);
bool touchPadStick(const App& app, float& sx, float& sy, bool& run);
void stepTouchPad(App& app);
void drawTouchPad(App& app, float alpha);
void drawEasyButton(App& app);
std::vector<KeyIcon> fieldKeyIcons(const App& app);
bool lowBatteryStep(bool& low, int rest);
int fieldLowBattery();
void openMapScreen(App& app);
void closeMapScreen(App& app);
void mapScreenTap(App& app);
void stepMapScreen(App& app);
void drawMapScreen(App& app);
void drawCamp(App& app);
void writeResume(App& app);
void trackerSceneEnd(App& app);
void offerResume(App& app);
void answerResume(App& app, bool yes, bool cancelled = false);
void resumeClick(App& app, float px, float py);
void stepResume(App& app);
int pictAnimFrame(int draws, int ticks, int frames);
void drawResumePrompt(App& app);
void newPairParty(App& app, int pair, bool clearData);
cr::GameState allPairsFlags(const App& app);
void openScenarios(App& app, int mode);
void openScenarios(App& app);
void closeScenarios(App& app);
void stepScenarios(App& app);
void drawScenarios(App& app);
void scenariosChoose(App& app);
void scenariosKey(App& app, SDL_Keycode key);
void scenariosClick(App& app, float mx, float my);
void scenariosDrag(App& app, float dx, float dy);
void openCollection(App& app);
void stepCollection(App& app);
void drawCollection(App& app);
void collectionKey(App& app, SDL_Keycode key);
void collectionClick(App& app, float mx, float my);
std::string manualTitle(const App& app, int id);
std::vector<int> manualTopics(const App& app);
std::vector<std::string> manualPage(const App& app, int id);
void manualTap(App& app);
void openManual(App& app, int intro);
void manualChoose(App& app);
void stepManual(App& app);
void drawManual(App& app);
void manualKey(App& app, SDL_Keycode key);
void manualClick(App& app, float px, float py);
void openCharSelect(App& app);
float pageTop(const App& app);
void charSelectTurn(App& app);
void stepCharSelect(App& app);
void charSelectDrag(App& app, float my);
void charSelectRelease(App& app);
void closeCharSelect(App& app, int scenario);
void drawCharArt(App& app, float alpha, bool banner, float grey = 1);
void drawCharSelect(App& app);
void charSelectKey(App& app, SDL_Keycode key);
void charSelectClick(App& app, float px, float py);
void outPicture(cr::MenuPainter& m, const cr::Texture& tex, int pict, float x, float y, float a, int frame = 0);
void drawTitleMenu(App& app);
void titleStart(App& app);
void titleBoardTap(App& app);
void titleTap(App& app);
void titleChoose(App& app, int i);
float titlePlateY(int i);
// A menu sheet's pictures in its units: twice them on a _hd / _ipad file's (_tex2d_add, chk_hd_name's flags - by the
// name, not the size: the Status page's 512 j039_L01_hd whole, #14), and on any sheet 1024 wide or more
inline float sheetScale(const cr::Texture& t) { return t.hdSheet || t.width >= 1024 ? 2.0f : 1.0f; } // (the title's plate i: Start, Scenarios, Options, and the desktop's Quit)
bool titleKey(App& app, SDL_Keycode key);
void titleClick(App& app, float px, float py);
std::string achievementName(const App& app, int n);
void drawAchievementBanner(App& app);
void openAchievementList(App& app);
void closeAchievementList(App& app);
float achListMaxScroll(const App& app);
int achListFocus(const App& app);
void stepAchievementList(App& app);
void drawAchievementList(App& app);
void achievementListKey(App& app, SDL_Keycode key);
void achievementListDown(App& app, float mx, float my);
void achievementListDrag(App& app, float my);
void achievementListScrollBy(App& app, int rows);
float screenFadeAlpha(const App& app);
void updateVoices(App& app);
bool stepVoiceCue(App& app);
bool fieldToLogical(const App& app, cr::Vec3 p, float& lx, float& ly);
void logicalToTex2d(const App& app, float lx, float ly, float& tx, float& ty);
void stepEvents(App& app);
void fieldWalked(App& app, float dist);
void playFootsteps(App& app);
bool same(const cr::Vec3& a, const cr::Vec3& b);
bool same(const cr::Mat4& a, const cr::Mat4& b);
cr::Vec3 lerp(const cr::Vec3& a, const cr::Vec3& b, float k);
float lerpAngle(float a, float b, float k);
cr::FieldWalker shownWalker(const App& app);
float shownPlayerAlpha(const App& app);
void shownNpc(const App& app, const App::NpcDraw& d, cr::Mat4& world, float& alpha);
void shownCamera(const App& app, cr::Vec3& eye, cr::Vec3& target);
void shownScroll(const App& app, float& x, float& y);
void updatePlayer(App& app, float dt, float camYaw);
cr::Mat4 playerWorld(const cr::FieldWalker& w);
cr::Mat4 npcWorld(const cr::FieldNpc& n);
cr::FieldNpc* findNpc(App& app, int id);
bool actorPos(App& app, int id, cr::Vec3& out);
void setActor(App& app, int id, cr::Vec3 p, const float* yaw, bool keepY);
bool scriptFloor(const App& app, cr::Vec3 p, float r, float& y);
bool flagMarkUp(const App& app, const cr::FieldNpc& n);
void buildNpcDraws(App& app);
std::vector<cr::WalkBody> walkBodies(const App& app);
void animateNpcs(App& app, float dt);
void updateNpcJoints(App& app);
void drawNpcs(App& app, const cr::Mat4& vp);
void drawFigureQueue(App& app, const cr::Mat4& vp); // mdl_put's queue: the player and the NPCs, sorted by queueKey
constexpr int kAllKeys = INT_MIN;
int queueKey(const cr::Material& base, float alpha, bool faded);
void drawLoaded(App& app, const LoadedModel& m, const cr::Mat4& mvp, const cr::Material* override = nullptr, float alpha = 1.0f, float matTime = -1.0f, int pass = 0, bool faded = false, int key = kAllKeys);
void addShadowFan(std::vector<float>& tris, std::vector<uint8_t>& alpha, cr::Vec3 at, float yaw, float rx, float rz, float a);
void drawShadowFans(App& app, cr::GpuMesh& mesh, const std::vector<float>& tris, const std::vector<uint8_t>& alpha, const cr::Mat4& vp);
int androidCutinWidth(float wv);
void drawMessageBox(App& app, const std::vector<std::string>& page, const std::string& speaker, bool next, const std::string& pageNo);
void drawWinPiece(App& app, const float* piece, float x, float y, float w, float h, float alpha = 1);
int partyLevel(const App& app);
std::string sysText(const App& app, int id, const std::string& name = std::string());
int battleAction(const LoadedModel& lm, int slot);
App::Battle::Actor* battleActor(App& app, int fighter);
int realSlot(const LoadedModel& lm, int slot);
void playSlot(App& app, App::Battle::Actor& a, int slot, int then = -1);
float actorDuration(App& app, const App::Battle::Actor& a);
cr::Vec3 actorHead(App& app, const App::Battle::Actor& a);
void setBattleCamera(App& app, int id, int actor = 0, bool held = false);
void stepBattleCamera(App& app);
void battleCameraView(App& app, cr::Vec3& eye, cr::Vec3& target, float* roll = nullptr);
int monsterSize(const App& app, int fighter);
void cameraForTarget(App& app, int target, bool group, bool cursor = false);
void castCamera(App& app, int kind, int user, bool userPair);
void hitCamera(App& app, const cr::BattleTurn& t, int target, bool bounce);
LoadedModel* effectModel(App& app, int id);
cr::Vec3 actorPoint(App& app, const App::Battle::Actor& a, int user, bool* found = nullptr);
void spawnEffect(App& app, int id, int fighter);
int startEffect(App& app, std::vector<Effect>& list, int id, cr::Vec3 at, bool loop, float scale, float yaw);
void stopEffect(std::vector<Effect>& list, int handle);
void stepEffects(App& app, std::vector<Effect>& list);
void stepEffects(App& app);
const cr::Action* effectMaterialAction(const LoadedModel& lm, const cr::Material* mt);
bool effectUvMotion(const LoadedModel& lm, const cr::Material* mt, float time, float shift[2], float scale[2], int unit = 0);
const cr::Texture* secondUnit(const LoadedModel& lm, size_t mesh, const cr::Material* mt, float time, float uv2[4]);
void drawEffects(App& app, const std::vector<Effect>& list, const cr::Mat4& vp, const cr::Vec3* eye, const cr::Vec3* target);
void drawEffects(App& app, const cr::Mat4& vp, const cr::Vec3& eye, const cr::Vec3& target);
bool viegIsOlgar(const App& app);
bool olgarIsDante(const App& app);
int charNameId(const App& app, int c);
int battleInSound(int g);
int monsterNameRow(const App& app, const cr::MonsterDef& m);
void startBattle(App& app, const cr::BattleStart& start, int flags);
void endBattle(App& app, bool won);
void battleChooseTop(App& app, int i);
void battleChooseGene(App& app, int ability);
void battleChooseCommand(App& app, int i);
void battleChooseItem(App& app, int item);
void battleChooseTarget(App& app, int fighter);
void battleEscapeAnswer(App& app, bool yes, bool cancelled);
void faceFighter(App& app, App::Battle::Actor& a, int target);
void startQuake(App::Battle::Quake& q, float amp, float from, float to, int frames, bool hold);
void startQuake(App& app, float amp, float from, float to, int frames, bool hold);
void endQuake(App::Battle::Quake& q, float amp, float from, float to, int frames);
void endQuake(App& app, float amp, float from, float to, int frames);
void stepQuake(App::Battle::Quake& q);
void stepQuake(App& app);
bool cameraDone(const App& app);
void startTurn(App& app);
bool attackSpot(const App& app, int user, int k, int target, cr::Vec3& out);
void showHits(App& app);
void startBreakFx(App& app);
bool stepBreakFx(App& app);
void endRound(App& app);
int battleSlideDistance(const App& app);
void stepMenuView(App& app);
bool battleCursorLit(const App& app);
bool battleMenuReady(const App& app);
void turnActors(App& app);
void turnOver(App::Battle::Actor& a, float want, float frames);
void stepBattle(App& app);
void drawBattle(App& app, float dt);
void placePopup(App::Battle& b, int slot, float& x, float& y, int shown);
void resultsTap(App& app);
void stepResults(App& app);
void drawBattleHud(App& app, float W, float H);
void battleKey(App& app, SDL_Keycode key);
void battleClick(App& app, float px, float py);
void selectAction(App& app, int delta);
void fieldDepthRange(const App& app, const cr::Vec3& eye, const cr::Vec3& target, float& zNear, float& zFar);
cr::Mat4 scriptCameraView(const App& app, const cr::Vec3& eye, const cr::Vec3& target);
void loseToArk(App& app);
int menuMaskWanted(const App& app, bool& atOnce);
int menuMaskAlpha(const App& app);
void settleMenuMask(App& app);
void drawMenuMaskShape(App& app, int alpha);
void drawFrame(App& app, bool present = true);
void setMode(App& app, Mode mode);
void step(App& app, int delta);
void saveFrame(App& app, const char* path);
int runSelfTest(App& app);
void startNewGame(App& app, int scenario = 0);
void bootTitle(App& app);
int runExplore(App& app, int maxActions, int scenario);
int runExportModels(App& app, const char* dirArg);
int runExportTextures(App& app, const char* dirArg);
int runCapture(App& app, const char* mapName, const char* outPath);
bool padMenus(const App& app);
bool titleTapFront(const App& app);
// A menu's held touch (App::press): its screen, and whether it is now inside a control's box - in
// menu units of the group drawn from `origin` (uiX, wideLeft, wideRight)
enum PressScreen { kPressNone = 0, kPressSave, kPressCharSelect, kPressCamp, kPressLastBattle, kPressWorld, kPressShop,
                   kPressBattle, kPressResume, kPressTitle, kPressChoice, kPressCollection, kPressScenarios };
int pressScreen(const App& app);
bool pressIn(const App& app, int screen, float origin, float x, float y, float w, float h);
void pressCountStep(App& app, float dt);
bool padField(const App& app);
void padButton(App& app, int b);
bool padLive(App& app);
bool stickPointerWanted(const App& app);
void stickPointerRelease(App& app);
void stickPointerStep(App& app, float dt);
void drawStickPointer(App& app);
void padStep(App& app, float dt);
void toggleFullscreen(App& app);
bool autoPhoneLayout(const App& app);
void adjustVideo(App& app, int row, int dir);
float screenFadeBackAlpha(const App& app);
int boundActionTable(const std::vector<cr::Action>& actions);
void readMdlHdRules(LoadedModel& lm, int stage);
void npcSetup(App& app);
bool shopPressIn(const App& app, bool right, float x, float y, float w, float h, float shift = 0);
float campScrollPx(const App& app);
bool campJewelFree(const App& app, int member, int item);
void charSelectPress(App& app, float mx, float my);
void battleMenu(App& app, App::Battle::Menu menu);
bool battlePressIn(const App& app, float x, float y, float w, float h);
bool battleTopBarred(const App& app, int i);
void placePair(App& app, bool pair);

} // namespace crapp
