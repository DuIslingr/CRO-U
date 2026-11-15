#include "assets/hd_texture.hpp"

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "bcdec/bcdec.h" // (iOrange's block-compression decoder: MIT / Unlicense)

#include <algorithm>

#include <SDL3/SDL.h>

#include <cstring>
#include <vector>

#ifdef CR_HAVE_ZLIB
#include <zlib.h>
#endif

namespace cr {

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

} // namespace

// An upscale's see-through pixels' colour: drawn filtered (the originals nearest), a cut-out's
// edge shows the colour under its part-transparent pixels - which the networks leave dark (a
// black fringe round hair and ornaments). Each pixel short of opaque takes the nearest opaque
// one's colour (a breadth-first fill from them), its alpha kept.
void fillSeeThrough(Image& img) {
    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || img.alphaOnly) return;
    std::vector<uint8_t>& px = img.rgba;
    constexpr uint8_t kOpaque = 250;
    std::vector<uint8_t> done(size_t(w) * size_t(h), 0);
    std::vector<int> queue;
    queue.reserve(size_t(w) * size_t(h) / 4);
    size_t solid = 0;
    for (int i = 0; i < w * h; ++i)
        if (px[size_t(i) * 4 + 3] >= kOpaque) done[size_t(i)] = 1, ++solid;
    if (solid == 0 || solid == size_t(w) * size_t(h)) return;
    for (int y = 0; y < h; ++y) // (the opaque pixels at the edge: where the fill starts)
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (!done[size_t(i)]) continue;
            if ((x > 0 && !done[size_t(i - 1)]) || (x + 1 < w && !done[size_t(i + 1)]) || (y > 0 && !done[size_t(i - w)]) ||
                (y + 1 < h && !done[size_t(i + w)]))
                queue.push_back(i);
        }
    for (size_t q = 0; q < queue.size(); ++q) {
        const int i = queue[q], x = i % w, y = i / w;
        const int next[4] = {x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1};
        for (int n : next) {
            if (n < 0 || done[size_t(n)]) continue;
            done[size_t(n)] = 1;
            for (int c = 0; c < 3; ++c) px[size_t(n) * 4 + size_t(c)] = px[size_t(i) * 4 + size_t(c)];
            queue.push_back(n);
        }
    }
}

uint64_t imageHash(const Image& img) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    const uint32_t head[3] = {uint32_t(img.width), uint32_t(img.height), img.alphaOnly ? 1u : 0u};
    mix(reinterpret_cast<const uint8_t*>(head), sizeof head);
    mix(img.rgba.data(), img.rgba.size());
    return h;
}

std::string hashName(uint64_t hash) {
    char s[17];
    SDL_snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(hash));
    return s;
}

bool saveRawImage(const std::string& path, const Image& img) {
    std::vector<uint8_t> out = {'C', 'R', 'R', 'A', 'W', '1'};
    put32(out, uint32_t(img.width));
    put32(out, uint32_t(img.height));
    put32(out, (img.alphaOnly ? 1u : 0u) | uint32_t(img.storage) << 1);
    out.insert(out.end(), img.rgba.begin(), img.rgba.end());
    return SDL_SaveFile(path.c_str(), out.data(), out.size());
}

namespace {

// A replacement left out: why ("<hash>.png: ..."), and false
bool leftOut(std::string* why, const std::string& file, const std::string& what) {
    if (why) *why = file + ": " + what;
    return false;
}

std::string sizesText(int w, int h, int originalW, int originalH) {
    return std::to_string(w) + "x" + std::to_string(h) + " for a " + std::to_string(originalW) + "x" + std::to_string(originalH) +
           " picture (its shape, at least its size)";
}

// PNG's chunk check (CRC-32, the IEEE polynomial)
uint32_t crc32Of(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 3; i >= 0; --i) out.push_back(uint8_t(v >> (8 * i)));
}

// A .dds replacement's top picture as RGBA8, top row first - the DDS's own order. Block compression BC1 (DXT1),
// BC2 (DXT3), BC3 (DXT5), BC4 / BC5 (unsigned: ATI1 / ATI2, BC4U / BC5U) and BC7 (sRGB or not: the same bytes), and
// uncompressed 32-bit RGBA / BGRA (a DX10 header's R8G8B8A8 / B8G8R8A8 / B8G8R8X8, or a legacy one's masks). A plain
// 2D texture only - its first mip; a cube, volume or array is refused. BC4 is a grey (its value on R, G and B),
// BC5 red and green. (After metastigma's fork's PNG / DDS replacements, CRO-U-metastigma#10.) False and `what`
// set when it can't be read
bool decodeDds(const uint8_t* p, size_t n, Image& out, std::string& what) {
    if (n < 128 || std::memcmp(p, "DDS ", 4) != 0 || get32(p + 4) != 124 || get32(p + 76) != 32) return what = "not a DDS", false;
    const uint32_t h = get32(p + 12), w = get32(p + 16), pfFlags = get32(p + 80), fourCC = get32(p + 84);
    const uint32_t caps2 = get32(p + 112);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return what = "no picture, or over 8192", false;
    if (caps2 & (0x200u | 0x200000u)) return what = "a cube map or a volume, not a picture", false;
    enum Kind { kNone, kBc1, kBc2, kBc3, kBc4, kBc5, kBc7, kRgba, kBgra, kBgrx, kMasks } kind = kNone;
    size_t at = 128;
    auto cc = [](const char* s) { return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24; };
    if ((pfFlags & 4u) && fourCC == cc("DX10")) {
        if (n < 148) return what = "a DX10 header cut short", false;
        const uint32_t dxgi = get32(p + 128), dim = get32(p + 132), misc = get32(p + 136), array = get32(p + 140);
        at = 148;
        if (dim != 3 || (misc & 4u) || array > 1) return what = "not a single 2D picture (a cube, an array or a volume)", false;
        switch (dxgi) {
        case 71: case 72: kind = kBc1; break;
        case 74: case 75: kind = kBc2; break;
        case 77: case 78: kind = kBc3; break;
        case 80: kind = kBc4; break;
        case 83: kind = kBc5; break;
        case 98: case 99: kind = kBc7; break;
        case 28: case 29: kind = kRgba; break;
        case 87: case 91: kind = kBgra; break;
        case 88: case 93: kind = kBgrx; break;
        default: return what = "DXGI format " + std::to_string(dxgi) + " (BC1-5, BC7 or 32-bit RGBA / BGRA read)", false;
        }
    } else if (pfFlags & 4u) {
        if (fourCC == cc("DXT1")) kind = kBc1;
        else if (fourCC == cc("DXT2") || fourCC == cc("DXT3")) kind = kBc2;
        else if (fourCC == cc("DXT4") || fourCC == cc("DXT5")) kind = kBc3;
        else if (fourCC == cc("ATI1") || fourCC == cc("BC4U")) kind = kBc4;
        else if (fourCC == cc("ATI2") || fourCC == cc("BC5U")) kind = kBc5;
        else return what = "its compression (DXT1 / 3 / 5, ATI1 / 2, BC4U / BC5U or a DX10 header read)", false;
    } else if ((pfFlags & 0x40u) && get32(p + 88) == 32) kind = kMasks; // (RGB, 32 bits a pixel: by its masks)
    else return what = "its pixel format (block-compressed or 32-bit read)", false;
    out.width = int(w), out.height = int(h);
    out.rgba.assign(size_t(w) * h * 4, 0);
    if (kind == kRgba || kind == kBgra || kind == kBgrx || kind == kMasks) {
        if (n - at < size_t(w) * h * 4) return what = "its pixels cut short", false;
        uint32_t m[4] = {0xffu, 0xff00u, 0xff0000u, 0xff000000u}; // (R, G, B, A in a little-endian 32-bit pixel)
        if (kind == kBgra || kind == kBgrx) m[0] = 0xff0000u, m[2] = 0xffu;
        if (kind == kBgrx) m[3] = 0;
        if (kind == kMasks) {
            for (int c = 0; c < 4; ++c) m[c] = get32(p + 92 + 4 * c);
            if (!(pfFlags & 1u)) m[3] = 0; // (no alpha: opaque)
        }
        auto channel = [](uint32_t v, uint32_t mask) -> uint8_t {
            if (!mask) return 255;
            int shift = 0;
            while (!((mask >> shift) & 1u)) ++shift;
            const uint32_t top = mask >> shift;
            return uint8_t(((v & mask) >> shift) * 255u / top);
        };
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const uint32_t v = get32(p + at + i * 4);
            for (int c = 0; c < 4; ++c) out.rgba[i * 4 + size_t(c)] = channel(v, m[c]);
        }
        return true;
    }
    const size_t block = kind == kBc1 || kind == kBc4 ? 8 : 16;
    const uint32_t bx = (w + 3) / 4, by = (h + 3) / 4;
    if ((n - at) / block < size_t(bx) * by) return what = "its blocks cut short", false;
    uint8_t tile[4 * 4 * 4];
    for (uint32_t y = 0; y < by; ++y)
        for (uint32_t x = 0; x < bx; ++x) {
            const uint8_t* b = p + at + (size_t(y) * bx + x) * block;
            switch (kind) {
            case kBc1: bcdec_bc1(b, tile, 16); break;
            case kBc2: bcdec_bc2(b, tile, 16); break;
            case kBc3: bcdec_bc3(b, tile, 16); break;
            case kBc7: bcdec_bc7(b, tile, 16); break;
            case kBc4: { // (one channel: a grey)
                uint8_t r[16];
                bcdec_bc4(b, r, 4);
                for (int i = 0; i < 16; ++i) tile[i * 4] = tile[i * 4 + 1] = tile[i * 4 + 2] = r[i], tile[i * 4 + 3] = 255;
                break;
            }
            default: { // kBc5: red and green
                uint8_t rg[32];
                bcdec_bc5(b, rg, 8);
                for (int i = 0; i < 16; ++i) tile[i * 4] = rg[i * 2], tile[i * 4 + 1] = rg[i * 2 + 1], tile[i * 4 + 2] = 0, tile[i * 4 + 3] = 255;
                break;
            }
            }
            for (uint32_t ty = 0; ty < 4 && y * 4 + ty < h; ++ty)
                for (uint32_t tx = 0; tx < 4 && x * 4 + tx < w; ++tx)
                    std::memcpy(&out.rgba[((size_t(y) * 4 + ty) * w + x * 4 + tx) * 4], &tile[(ty * 4 + tx) * 4], 4);
        }
    return true;
}

} // namespace

bool hdShapeFits(int w, int h, int originalW, int originalH) {
    if (originalW <= 0 || originalH <= 0 || w < originalW || h < originalH || w > 8192 || h > 8192) return false;
    // (one scale s for both: w = originalW s + a, h = originalH s + b, each rounded, |a|, |b| <= 1/2)
    const int64_t off = int64_t(w) * originalH - int64_t(h) * originalW;
    return 2 * (off < 0 ? -off : off) <= int64_t(originalW) + originalH;
}

bool savePngImage(const std::string& path, const Image& img) {
    const size_t n = size_t(std::max(img.width, 0)) * size_t(std::max(img.height, 0)) * 4;
    if (!n || img.rgba.size() < n) return false;
    std::vector<uint8_t> rows(img.rgba.begin(), img.rgba.begin() + std::ptrdiff_t(n));
    std::vector<uint8_t> png;
    stbi_write_png_to_func(
        [](void* to, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(to);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, img.width, img.height, 4, rows.data(), img.width * 4);
    if (png.size() < 33 || std::memcmp(png.data() + 12, "IHDR", 4) != 0) return false;
    // the .crraw's flags for tools/hd_textures.py (which upscaler), after IHDR
    const std::string text = std::string("cr-flags") + '\0' + std::to_string((img.alphaOnly ? 1u : 0u) | uint32_t(img.storage) << 1);
    std::vector<uint8_t> chunk;
    putBe32(chunk, uint32_t(text.size()));
    chunk.insert(chunk.end(), {'t', 'E', 'X', 't'});
    chunk.insert(chunk.end(), text.begin(), text.end());
    putBe32(chunk, crc32Of(chunk.data() + 4, chunk.size() - 4));
    png.insert(png.begin() + 33, chunk.begin(), chunk.end());
    return SDL_SaveFile(path.c_str(), png.data(), png.size());
}

bool loadHdImage(const std::string& dir, const Image& original, Image& out, std::string* why) {
    if (why) why->clear();
    if (dir.empty() || original.width <= 0) return false;
    const std::string name = hashName(imageHash(original));
    size_t size = 0;
#ifdef CR_HAVE_ZLIB
    if (void* data = SDL_LoadFile((dir + name + ".crhd").c_str(), &size)) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        std::string what;
        if (size <= 26 || std::memcmp(p, "CRHD1", 6) != 0) what = "not a .crhd";
        const uint32_t w = what.empty() ? get32(p + 6) : 0, h = what.empty() ? get32(p + 10) : 0;
        if (what.empty() && (get32(p + 14) != uint32_t(original.width) || get32(p + 18) != uint32_t(original.height)))
            what = "made for a picture of another size";
        if (what.empty() && !hdShapeFits(int(std::min(w, 65536u)), int(std::min(h, 65536u)), original.width, original.height))
            what = sizesText(int(std::min(w, 65536u)), int(std::min(h, 65536u)), original.width, original.height);
        if (what.empty()) {
            out.width = int(w);
            out.height = int(h);
            out.alphaOnly = (get32(p + 22) & 1) != 0;
            out.rgba.resize(size_t(w) * h * 4);
            uLongf len = uLongf(out.rgba.size());
            if (uncompress(out.rgba.data(), &len, p + 26, uLong(size - 26)) != Z_OK || len != out.rgba.size()) what = "its pixels don't unpack";
        }
        SDL_free(data);
        return what.empty() || leftOut(why, name + ".crhd", what);
    }
#endif
    if (void* data = SDL_LoadFile((dir + name + ".png").c_str(), &size)) { // (any upscale or edit: the original's shape)
        int w = 0, h = 0, channels = 0;
        stbi_uc* px = stbi_load_from_memory(static_cast<const stbi_uc*>(data), int(std::min(size, size_t(1) << 30)), &w, &h, &channels, 4);
        SDL_free(data);
        if (!px) return leftOut(why, name + ".png", "not a PNG this reads");
        if (!hdShapeFits(w, h, original.width, original.height)) {
            stbi_image_free(px);
            return leftOut(why, name + ".png", sizesText(w, h, original.width, original.height));
        }
        out.width = w;
        out.height = h;
        out.alphaOnly = original.alphaOnly;
        out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
        stbi_image_free(px);
        return true;
    }
    if (void* data = SDL_LoadFile((dir + name + ".dds").c_str(), &size)) { // (a texture tool's: BC1-5, BC7, 32-bit)
        std::string what;
        Image img;
        const bool read = decodeDds(static_cast<const uint8_t*>(data), size, img, what);
        SDL_free(data);
        if (!read) return leftOut(why, name + ".dds", what);
        if (!hdShapeFits(img.width, img.height, original.width, original.height))
            return leftOut(why, name + ".dds", sizesText(img.width, img.height, original.width, original.height));
        out = std::move(img);
        out.alphaOnly = original.alphaOnly;
        return true;
    }
    return false;
}

} // namespace cr
