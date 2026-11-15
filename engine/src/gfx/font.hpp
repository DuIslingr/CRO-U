#pragma once
// TrueType / OpenType text through stb_truetype, for the game's dialogue in every language
// (the iOS game draws its text with the system font; font.mct only holds the digits).
// Fonts come from the user's system (Noto Sans CJK, Hiragino, MS Gothic, ...), never shipped.

#include <memory>
#include <string>
#include <vector>

#include "assets/texture.hpp"

namespace cr {

class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Reads a .ttf / .otf / .ttc; false if it isn't one stb_truetype can use.
    bool load(const std::string& path);
    bool loaded() const;
    const std::string& path() const { return path_; }
    int generation() const { return generation_; } // changes with the face
    // In a collection, the face whose name ends with `region` ("JP", "KR", "SC", "TC");
    // keeps the current face when none does. True if the face changed.
    bool selectRegion(const char* region);
    // In a collection, the face with this PostScript name ("HiraKakuProN-W6"); false if none has it
    bool selectPostScript(const char* name);
    // Whether every character of `utf8` has a glyph in the current face.
    bool covers(const std::string& utf8) const;
    // White-on-transparent lines: `em` pixels to the em, baselines `lineStep` apart, `pad`
    // pixels of margin.
    bool rasterise(const std::vector<std::string>& lines, float em, float lineStep, int pad, Image& out) const;
    // The line box (ascent and descent in ems, descent negative) the glyphs are placed in, in place of the
    // face's own (hhea) - a face standing in for another keeps the text where the other put it; 0, 0: the face's
    void setLineBox(float ascentEm, float descentEm) {
        if (ascentEm != boxAscent_ || descentEm != boxDescent_) boxAscent_ = ascentEm, boxDescent_ = descentEm, ++generation_;
    }

private:
    float boxAscent_ = 0, boxDescent_ = 0;

    struct Face;
    std::vector<unsigned char> data_;
    std::unique_ptr<Face> face_;
    std::string path_;
    int faceIndex_ = -1;
    int generation_ = 0;
    bool openFace(int index);
};

// Loads the first usable font with CJK coverage: CR_FONT's (none: none), else the game folder's font/
// one (ownFont), else among the platform's usual locations; false if none.
bool loadSystemFont(Font& font);

// Apple's (iOS, macOS): the system font of this PostScript name ("HiraKakuProN-W6", "PingFangSC-Regular")
// - its file from CoreText, its face in a collection - into `font`; nothing done if it is the face loaded
// already. False elsewhere, or when the system has no such font (iOS has no fonts at fixed paths)
bool loadAppleFont(Font& font, const char* postScriptName);

// Your own fonts: the .ttf / .otf / .ttc files in a game folder's font/ (the first game folder holding
// one; its files by name). The first that reads - with `cjk` the first with Japanese / Chinese / Korean
// glyphs -, "" none
std::string ownFont(bool cjk);
// whether `path` is one of them
bool isOwnFont(const std::string& path);

} // namespace cr
