#include "gfx/font.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/file.hpp"
#include "core/text.hpp"
#include "platform/paths.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

#ifdef SDL_PLATFORM_APPLE
#include <CoreText/CoreText.h>
#endif

namespace cr {

struct Font::Face {
    stbtt_fontinfo info;
};

namespace {
// The face's glyph for `c`, with stand-ins for what iOS's font fallback would bring: the Korean
// text's U+223C TILDE OPERATOR (563 times in 3.1.0) is in no Noto Sans CJK face - the fullwidth
// tilde (or the wave dash) in its place
int glyphIndex(const stbtt_fontinfo& info, uint32_t c) {
    int g = stbtt_FindGlyphIndex(&info, int(c));
    if (!g && c == 0x223C) g = stbtt_FindGlyphIndex(&info, 0xFF5E);
    if (!g && c == 0x223C) g = stbtt_FindGlyphIndex(&info, 0x301C);
    return g;
}
} // namespace

// Cyrillic from a proportional face where the font's own is full-width or missing (the Japanese faces: the
// Vita's TB Gothic Pro, Android's TypeBank Gothic - their Cyrillic a CJK cell wide, Russian letter-spaced; CR2-U
// found it first) - the system's sans (bold for a bold face). The games' own text has none: only translations
namespace {
bool isCyrillic(uint32_t c) { return c >= 0x400 && c <= 0x52F; }

std::vector<std::string> cyrillicCandidates(bool bold) {
    std::vector<std::string> out;
#if defined(SDL_PLATFORM_WINDOWS)
    const char* windir = SDL_getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    for (const char* f : bold ? std::vector<const char*>{"segoeuib.ttf", "arialbd.ttf"} : std::vector<const char*>{"segoeui.ttf", "arial.ttf"})
        out.push_back(fonts + f);
#elif defined(SDL_PLATFORM_ANDROID)
    for (const char* f : bold ? std::vector<const char*>{"Roboto-Bold.ttf", "NotoSans-Bold.ttf", "Roboto-Regular.ttf"}
                              : std::vector<const char*>{"Roboto-Regular.ttf", "NotoSans-Regular.ttf"})
        out.push_back(std::string("/system/fonts/") + f);
#elif defined(SDL_PLATFORM_MACOS)
    for (const char* f : bold ? std::vector<const char*>{"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf"}
                              : std::vector<const char*>{"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"})
        out.push_back(f);
#elif !defined(SDL_PLATFORM_IOS)
    const char* const kRegular[] = {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                                    "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                                    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
    const char* const kBold[] = {"/usr/share/fonts/noto/NotoSans-Bold.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                                 "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                                 "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
                                 "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"};
    if (bold) out.assign(std::begin(kBold), std::end(kBold));
    out.insert(out.end(), std::begin(kRegular), std::end(kRegular));
#endif
    return out;
}

// whether a face is bold: its subfamily name (id 2) says so
bool boldFace(const stbtt_fontinfo& info) {
    int len = 0;
    std::string s;
    if (const char* u = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 2))
        for (int k = 1; k < len; k += 2) s += u[k];
    return s.find("Bold") != std::string::npos || s.find("Black") != std::string::npos || s.find("Heavy") != std::string::npos;
}
} // namespace

// (one regular and one bold for the process, loaded when first wanted; null: none found)
static const stbtt_fontinfo* cyrillicFallback(bool bold) {
    static Font faces[2];
    static bool tried[2] = {false, false};
    Font& f = faces[bold ? 1 : 0];
    if (!tried[bold ? 1 : 0]) {
        tried[bold ? 1 : 0] = true;
#ifdef SDL_PLATFORM_APPLE
        loadAppleFont(f, bold ? "Arial-BoldMT" : "ArialMT");
#endif
        for (const std::string& p : cyrillicCandidates(bold))
            if (!f.loaded() && f.load(p)) SDL_Log("font: Cyrillic from %s (the text's face draws it a CJK cell wide)", p.c_str());
    }
    return f.loaded() ? f.cyrillicInfo() : nullptr;
}

Font::Font() = default;
Font::~Font() = default;

const stbtt_fontinfo* Font::cyrillicInfo() const { return face_ ? &face_->info : nullptr; }

bool Font::cyrillicFromFallback() const {
    if (!face_) return false;
    if (cyrillicOwn_ < 0) { // (А's advance: a proportional face's about 0.6 - 0.7 em, a CJK face's full-width one 1)
        const stbtt_fontinfo& info = face_->info;
        const int g = stbtt_FindGlyphIndex(&info, 0x410);
        int adv = 0, lsb = 0;
        if (g) stbtt_GetGlyphHMetrics(&info, g, &adv, &lsb);
        cyrillicOwn_ = g && float(adv) * stbtt_ScaleForMappingEmToPixels(&info, 1) < 0.8f;
    }
    return cyrillicOwn_ == 0 && cyrillicFallback(boldFace(face_->info)) != nullptr;
}

bool Font::openFace(int index) {
    const int offset = stbtt_GetFontOffsetForIndex(data_.data(), index);
    if (offset < 0) return false;
    auto f = std::make_unique<Face>();
    if (!stbtt_InitFont(&f->info, data_.data(), offset)) return false;
    face_ = std::move(f);
    faceIndex_ = index;
    cyrillicOwn_ = -1;
    ++generation_;
    return true;
}

bool Font::load(const std::string& path) {
    face_.reset();
    faceIndex_ = -1;
    if (!readFile(path, data_) || data_.size() < 12 || !openFace(0)) {
        data_.clear();
        return false;
    }
    path_ = path;
    return true;
}

bool Font::loaded() const { return face_ != nullptr; }

bool Font::selectRegion(const char* region) {
    if (!loaded()) return false;
    const int n = stbtt_GetNumberOfFonts(data_.data());
    const size_t rl = std::strlen(region);
    for (int i = 0; i < n; ++i) {
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), i))) continue;
        int len = 0; // full name (id 4), Windows platform, US English: UTF-16BE
        const char* name = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                   STBTT_MS_LANG_ENGLISH, 4);
        if (!name || len < int(2 * rl)) continue;
        std::string ascii;
        for (int k = 1; k < len; k += 2) ascii += name[k];
        // "Noto Sans CJK JP" but not "Noto Sans Mono CJK JP"
        if (ascii.size() >= rl && ascii.compare(ascii.size() - rl, rl, region) == 0 &&
            ascii.find("Mono") == std::string::npos) {
            if (i == faceIndex_) return false;
            return openFace(i);
        }
    }
    return false;
}

bool Font::selectPostScript(const char* name) {
    if (!loaded() || !name) return false;
    const unsigned char* d = data_.data();
    const int n = std::max(1, stbtt_GetNumberOfFonts(d));
    for (int i = 0; i < n; ++i) {
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, d, stbtt_GetFontOffsetForIndex(d, i))) continue;
        std::string ps;
        int len = 0; // PostScript name (id 6): the Windows table's UTF-16BE, else the Mac table's Roman
        if (const char* u = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 6))
            for (int k = 1; k < len; k += 2) ps += u[k];
        else if (const char* m = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, STBTT_MAC_LANG_ENGLISH, 6))
            ps.assign(m, size_t(len));
        if (ps == name) return i == faceIndex_ || openFace(i);
    }
    return false;
}

bool Font::covers(const std::string& utf8) const {
    if (!loaded()) return false;
    for (size_t i = 0; i < utf8.size();) {
        const uint32_t c = nextUtf8(utf8, i);
        if (isCyrillic(c) && cyrillicFromFallback()) continue;
        if (c > 0x20 && c != 0x3000 && !glyphIndex(face_->info, c)) return false;
    }
    return true;
}

bool Font::rasterise(const std::vector<std::string>& lines, float em, float lineStep, int pad, Image& out) const {
    if (!loaded() || lines.empty()) return false;
    const stbtt_fontinfo& info = face_->info;
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, em);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    if (boxAscent_ != 0 || boxDescent_ != 0) { // (setLineBox: in the face's units)
        const float upm = stbtt_ScaleForMappingEmToPixels(&info, 1) > 0 ? 1 / stbtt_ScaleForMappingEmToPixels(&info, 1) : 1000;
        ascent = int(std::lround(boxAscent_ * upm)), descent = int(std::lround(boxDescent_ * upm));
    }

    const float base0 = float(pad) + float(ascent) * scale;

    // Glyph indices per line ('?' for characters the face lacks), each with its face (Cyrillic perhaps the
    // proportional fallback's), then the widest line
    struct Glyph {
        const stbtt_fontinfo* face;
        int g;
    };
    std::vector<std::vector<Glyph>> glyphs(lines.size());
    const int fallback = stbtt_FindGlyphIndex(&info, '?');
    const stbtt_fontinfo* cyr = nullptr;
    bool cyrLooked = false;
    auto scaleOf = [&](const stbtt_fontinfo* f) { return f == &info ? scale : stbtt_ScaleForMappingEmToPixels(f, em); };
    float widest = 1;
    for (size_t l = 0; l < lines.size(); ++l) {
        float x = 0;
        Glyph prev{nullptr, 0};
        // (a line with kana, kanji or hangul keeps the face's own Cyrillic: the Japanese and Korean text's few
        // Cyrillic letters are symbols in it, drawn as the original's face draws them)
        bool cjkLine = false;
        for (size_t i = 0; i < lines[l].size() && !cjkLine;) {
            const uint32_t c = nextUtf8(lines[l], i);
            cjkLine = (c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF);
        }
        for (size_t i = 0; i < lines[l].size();) {
            const uint32_t c = nextUtf8(lines[l], i);
            if (c < 0x20) continue; // (a message's marks, such as the centred line's \x01: not drawn)
            Glyph gl{&info, 0};
            if (isCyrillic(c) && !cjkLine) {
                if (!cyrLooked) cyrLooked = true, cyr = cyrillicFromFallback() ? cyrillicFallback(boldFace(info)) : nullptr;
                if (cyr && stbtt_FindGlyphIndex(cyr, int(c))) gl = {cyr, stbtt_FindGlyphIndex(cyr, int(c))};
            }
            if (!gl.g) gl = {&info, glyphIndex(info, c)};
            if (!gl.g && c > 0x20 && c != 0x3000) gl.g = fallback;
            const float s = scaleOf(gl.face);
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(gl.face, gl.g, &adv, &lsb);
            if (prev.g && prev.face == gl.face) x += float(stbtt_GetGlyphKernAdvance(gl.face, prev.g, gl.g)) * s;
            x += float(adv) * s;
            glyphs[l].push_back(gl);
            prev = gl;
        }
        widest = std::max(widest, x);
    }
    out.width = int(std::ceil(widest)) + 2 * pad + 1;
    out.height = int(std::ceil(float(lines.size() - 1) * lineStep + float(ascent - descent) * scale)) + 2 * pad;
    out.alphaOnly = false;
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    std::vector<unsigned char> bmp;
    for (size_t l = 0; l < lines.size(); ++l) {
        const float baseline = base0 + float(l) * lineStep;
        float x = float(pad);
        Glyph prev{nullptr, 0};
        for (const Glyph& gl : glyphs[l]) {
            const stbtt_fontinfo* face = gl.face;
            const int g = gl.g;
            const float s = scaleOf(face);
            if (prev.g && prev.face == face) x += float(stbtt_GetGlyphKernAdvance(face, prev.g, g)) * s;
            prev = gl;
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(face, g, &adv, &lsb);
            const float fx = x - std::floor(x);
            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBoxSubpixel(face, g, s, s, fx, 0, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                bmp.assign(size_t(gw) * gh, 0);
                stbtt_MakeGlyphBitmapSubpixel(face, bmp.data(), gw, gh, gw, s, s, fx, 0, g);
                const int ox = int(std::floor(x)) + x0, oy = int(std::lround(baseline)) + y0;
                for (int y = 0; y < gh; ++y) {
                    const int ty = oy + y;
                    if (ty < 0 || ty >= out.height) continue;
                    for (int xx = 0; xx < gw; ++xx) {
                        const int tx = ox + xx;
                        if (tx < 0 || tx >= out.width) continue;
                        uint8_t* p = &out.rgba[(size_t(ty) * out.width + tx) * 4];
                        p[0] = p[1] = p[2] = 255;
                        p[3] = std::max(p[3], bmp[size_t(y) * gw + xx]);
                    }
                }
            }
            x += float(adv) * s;
        }
    }
    return true;
}

namespace {
struct OwnFile {
    std::string path;
    bool cjk = false;
};
// (looked at once: each file read to see what it holds)
const std::vector<OwnFile>& ownFiles() {
    static const std::vector<OwnFile> files = [] {
        std::vector<OwnFile> out;
        for (const std::string& folder : gameFolders()) {
            const std::string dir = folder + "font/";
            int count = 0;
            char** names = SDL_GlobDirectory(dir.c_str(), "*", SDL_GLOB_CASEINSENSITIVE, &count);
            std::vector<std::string> paths;
            for (int k = 0; names && k < count; ++k) {
                std::string n = names[k], low = n;
                for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
                const auto ends = [&](const char* e) { return low.size() > 4 && low.compare(low.size() - 4, 4, e) == 0; };
                if (n[0] != '.' && (ends(".ttf") || ends(".otf") || ends(".ttc"))) paths.push_back(dir + n);
            }
            SDL_free(names);
            if (paths.empty()) continue;
            std::sort(paths.begin(), paths.end());
            for (const std::string& path : paths) {
                Font f;
                if (!f.load(path)) {
                    SDL_Log("font/: %s isn't a font this can read", path.c_str());
                    continue;
                }
                out.push_back({path, f.covers("\xe3\x81\x82\xe6\xbc\xa2")}); // (あ漢)
            }
            break; // (the first game folder with font files)
        }
        return out;
    }();
    return files;
}
} // namespace

std::string ownFont(bool cjk) {
    for (const OwnFile& f : ownFiles())
        if (!cjk || f.cjk) return f.path;
    return {};
}
bool isOwnFont(const std::string& path) {
    for (const OwnFile& f : ownFiles())
        if (f.path == path) return true;
    return false;
}

#ifdef SDL_PLATFORM_APPLE
bool loadAppleFont(Font& font, const char* postScriptName) {
    CFStringRef name = CFStringCreateWithCString(nullptr, postScriptName, kCFStringEncodingUTF8);
    if (!name) return false;
    std::string path, found;
    if (CTFontDescriptorRef desc = CTFontDescriptorCreateWithNameAndSize(name, 0)) {
        // (CoreText answers an unknown name with a stand-in: its own name checked)
        if (CFStringRef got = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute))) {
            char buf[256];
            if (CFStringGetCString(got, buf, sizeof buf, kCFStringEncodingUTF8)) found = buf;
            CFRelease(got);
        }
        if (CFURLRef url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute))) {
            char buf[1024];
            if (CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buf), sizeof buf)) path = buf;
            CFRelease(url);
        }
        CFRelease(desc);
    }
    CFRelease(name);
    if (path.empty() || found != postScriptName) return false;
    if (font.loaded() && font.path() == path) return font.selectPostScript(postScriptName) || true;
    if (!font.load(path)) return false;
    font.selectPostScript(postScriptName); // (a collection's: that face)
    return true;
}
#else
bool loadAppleFont(Font&, const char*) { return false; }
#endif

// Android's fonts beyond the AOSP file names: a phone maker's system may name or place its CJK font otherwise.
// The system's own font list - /system/etc/fonts.xml, font_fallback.xml from Android 15, the product / vendor
// partitions' - names each family's files and languages: Japanese's first, then Chinese (Simplified,
// Traditional) and Korean, then the default sans-serif family; after them, any font in the font folders,
// CJK-named ones first. In the order to try
std::vector<std::string> androidFonts(const std::string& root) {
    static const char* const kDirs[] = {"/system/fonts/", "/product/fonts/", "/system_ext/fonts/", "/vendor/fonts/"};
    auto locate = [&root](const std::string& name) -> std::string {
        if (!name.empty() && name[0] == '/') return SDL_GetPathInfo((root + name).c_str(), nullptr) ? root + name : std::string();
        for (const char* d : kDirs)
            if (const std::string p = root + d + name; SDL_GetPathInfo(p.c_str(), nullptr)) return p;
        return {};
    };
    auto attr = [](const std::string& tag, const char* key) -> std::string {
        const std::string k = std::string(" ") + key + "=\"";
        const size_t at = tag.find(k);
        if (at == std::string::npos) return {};
        const size_t b = at + k.size(), e = tag.find('"', b);
        return e == std::string::npos ? std::string() : tag.substr(b, e - b);
    };
    // (a family's rank: 0 Japanese, 1 Simplified Chinese, 2 Traditional, 3 Korean, 4 the default family, -1 none)
    auto rank = [](const std::string& lang, const std::string& name) {
        int best = -1;
        size_t i = 0;
        while (i < lang.size()) {
            size_t e = lang.find_first_of(" ,", i);
            if (e == std::string::npos) e = lang.size();
            const std::string t = lang.substr(i, e - i);
            i = e + 1;
            int r = -1;
            if (t.rfind("ja", 0) == 0 || t.find("Jpan") != std::string::npos) r = 0;
            else if (t.rfind("zh-Hans", 0) == 0 || t.find("Hans") != std::string::npos || t == "zh" || t == "zh-CN") r = 1;
            else if (t.rfind("zh-Hant", 0) == 0 || t.find("Hant") != std::string::npos || t == "zh-TW") r = 2;
            else if (t.rfind("ko", 0) == 0 || t.find("Kore") != std::string::npos) r = 3;
            if (r >= 0 && (best < 0 || r < best)) best = r;
        }
        if (best < 0 && name == "sans-serif") best = 4;
        return best;
    };
    std::vector<std::string> ranked[5];
    for (const char* xml : {"/product/etc/fonts_customization.xml", "/system/etc/font_fallback.xml", "/system/etc/fonts.xml",
                            "/vendor/etc/fonts.xml", "/system/etc/fallback_fonts.xml", "/system/etc/system_fonts.xml"}) {
        std::vector<uint8_t> data;
        if (!readFile(root + xml, data)) continue;
        std::string s(data.begin(), data.end());
        // (comments out: a maker's list keeps families it turned off in them)
        for (size_t c = s.find("<!--"); c != std::string::npos; c = s.find("<!--", c)) {
            const size_t e = s.find("-->", c + 4);
            s.erase(c, e == std::string::npos ? std::string::npos : e + 3 - c);
        }
        for (size_t f = s.find("<family"); f != std::string::npos; f = s.find("<family", f + 1)) {
            if (s.compare(f, 10, "<familyset") == 0) continue;
            const size_t tagEnd = s.find('>', f), end = s.find("</family>", f);
            if (tagEnd == std::string::npos || end == std::string::npos) break;
            const std::string tag = s.substr(f, tagEnd - f);
            const int r = rank(attr(tag, "lang"), attr(tag, "name"));
            if (r < 0) continue;
            std::vector<std::string> regular, other; // (the 400 weight first)
            for (size_t t = s.find("<font", tagEnd); t != std::string::npos && t < end; t = s.find("<font", t + 1)) {
                const size_t te = s.find('>', t);
                if (te == std::string::npos || te > end) break;
                if (std::isalpha(static_cast<unsigned char>(s[t + 5])) || s[t + 5] == '-') continue; // (<fontVariation...>)
                const std::string ftag = s.substr(t, te - t);
                std::string file;
                if (s[te - 1] != '/') {
                    size_t a = te + 1, b = s.find('<', a);
                    if (b == std::string::npos) break;
                    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
                    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
                    file = s.substr(a, b - a);
                }
                if (file.empty()) file = attr(ftag, "path");
                // (not italics, nor a serif family's stand-in - fallbackFor="serif")
                if (file.empty() || attr(ftag, "style") == "italic" || !attr(ftag, "fallbackFor").empty()) continue;
                const std::string w = attr(ftag, "weight");
                (w == "400" || w.empty() ? regular : other).push_back(file);
            }
            for (auto* list : {&regular, &other})
                for (const std::string& file : *list)
                    if (const std::string p = locate(file); !p.empty()) ranked[r].push_back(p);
        }
    }
    std::vector<std::string> out;
    auto add = [&out](const std::string& p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    for (const auto& list : ranked)
        for (const std::string& p : list) add(p);
    // The font folders themselves (a list that names none of these): CJK-named first, then sans faces, the rest
    std::vector<std::string> cjk, sans, rest;
    for (const char* d : kDirs) {
        int n = 0;
        char** names = SDL_GlobDirectory((root + d).c_str(), "*", 0, &n);
        for (int i = 0; names && i < n; ++i) {
            const std::string name = names[i];
            std::string lower = name;
            for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            const std::string ext = lower.size() > 4 ? lower.substr(lower.size() - 4) : std::string();
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            auto has = [&lower](const char* k) { return lower.find(k) != std::string::npos; };
            if (has("emoji") || has("symbol") || has("italic")) continue;
            const std::string path = root + d + name;
            if (has("cjk") || has("jp") || has("sc-") || has("tc-") || has("kr") || has("han")) cjk.push_back(path);
            else if (has("roboto") || has("sans")) sans.push_back(path);
            else rest.push_back(path);
        }
        SDL_free(names);
    }
    for (auto* list : {&cjk, &sans, &rest}) {
        std::sort(list->begin(), list->end()); // (the folders' own order is the file system's)
        std::stable_sort(list->begin(), list->end(), [](const std::string& a, const std::string& b) {
            return a.find("Regular") != std::string::npos && b.find("Regular") == std::string::npos;
        });
        for (const std::string& p : *list) add(p);
    }
    return out;
}

bool loadSystemFont(Font& font) {
    std::vector<std::string> candidates;
    if (const char* env = SDL_getenv("CR_FONT")) {
        if (SDL_strcmp(env, "none") == 0) return false; // the 8x8 font only
        candidates.push_back(env);
    }
    if (const std::string own = ownFont(true); !own.empty()) candidates.push_back(own); // (font/: yours)
    for (const char* p : {
             // Linux (Noto CJK under several distributions' paths, then other CJK families)
             "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
             "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
             "/usr/share/fonts/TTF/VL-Gothic-Regular.ttf",
             // macOS
             "/System/Library/Fonts/Hiragino Sans GB.ttc",
             "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
             "/System/Library/Fonts/PingFang.ttc",
             "/System/Library/Fonts/AppleSDGothicNeo.ttc",
             // Windows
             "C:/Windows/Fonts/YuGothM.ttc",
             "C:/Windows/Fonts/msgothic.ttc",
             "C:/Windows/Fonts/meiryo.ttc",
             "C:/Windows/Fonts/msyh.ttc",
             // Android
             "/system/fonts/NotoSansCJK-Regular.ttc",
             "/system/fonts/NotoSansSC-Regular.otf",
         })
        candidates.push_back(p);
    for (const std::string& c : candidates)
        if (font.load(c)) return true;
#ifdef SDL_PLATFORM_ANDROID
    // (a system without the AOSP files: its own list's, the first that draws Latin text)
    if (!SDL_getenv("CR_FONT")) {
        for (const std::string& c : androidFonts(""))
            if (font.load(c) && font.covers("Aa")) return true;
        font.load(std::string()); // (none: no face left loaded - the 8x8 font)
    }
#endif
#ifdef SDL_PLATFORM_IOS
    // (iOS: none at the paths above - macOS's -: the system's own faces through CoreText; applyLanguage
    // picks each language's)
    if (!SDL_getenv("CR_FONT"))
        for (const char* ps : {"HiraKakuProN-W6", "HiraginoSans-W6", "HiraKakuProN-W3"})
            if (loadAppleFont(font, ps)) return true;
#endif
    return false;
}

} // namespace cr
