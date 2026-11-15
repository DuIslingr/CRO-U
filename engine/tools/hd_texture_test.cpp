// HD textures: a replacement's shape checked; an exported PNG upright with its flags; a PNG replacement
// read back in the Image's own order, and one of the wrong shape left out with the reason
#include "assets/hd_texture.hpp"
#include "stb/stb_image.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
    namespace fs = std::filesystem;
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) ++problems, std::fprintf(stderr, "hd texture FAILED: %s\n", what);
    };
    check(cr::hdShapeFits(64, 32, 32, 16), "2x");
    check(cr::hdShapeFits(32, 16, 32, 16), "its own size");
    check(cr::hdShapeFits(13, 8, 5, 3), "2.5x, each side rounded");
    check(!cr::hdShapeFits(64, 64, 32, 16), "a square for a 2:1 picture");
    check(!cr::hdShapeFits(32, 64, 64, 32), "a picture turned on its side");
    check(!cr::hdShapeFits(16, 8, 32, 16), "smaller than the original");
    check(!cr::hdShapeFits(16384, 8192, 64, 32), "over 8192");
    const fs::path root = fs::temp_directory_path() /
                          ("cr-hd-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directories(root)) return std::fprintf(stderr, "hd texture: no folder\n"), 1;
    const std::string dir = root.string() + "/";
    cr::Image original;
    original.width = original.height = 2;
    original.rgba = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    cr::Image big; // (its rows numbered: row r's red r)
    big.width = big.height = 4;
    for (int r = 0; r < 4; ++r)
        for (int x = 0; x < 4; ++x) big.rgba.insert(big.rgba.end(), {uint8_t(r), uint8_t(x), 200, 255});
    const std::string name = cr::hashName(cr::imageHash(original));
    check(cr::savePngImage(dir + name + ".png", big), "a PNG written");
    std::vector<uint8_t> file;
    { // (closed before the file is written again: Windows won't replace an open file)
        std::ifstream in(dir + name + ".png", std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    check(std::string(file.begin(), file.end()).find(std::string("cr-flags") + '\0') != std::string::npos, "the flags in a tEXt chunk");
    int w = 0, h = 0, n = 0;
    stbi_uc* px = file.empty() ? nullptr : stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4);
    check(px && w == 4 && h == 4 && px[0] == 0, "the PNG upright: the picture's top row first");
    if (px) stbi_image_free(px);
    cr::Image out;
    std::string why;
    check(cr::loadHdImage(dir, original, out, &why) && out.width == 4 && out.height == 4 && out.rgba == big.rgba,
          "a PNG replacement read in the Image's own order");
    cr::Image wide = big; // (4 x 2 for a 2 x 2 picture)
    wide.height = 2, wide.rgba.resize(4 * 2 * 4);
    check(cr::savePngImage(dir + name + ".png", wide), "a second PNG written");
    check(!cr::loadHdImage(dir, original, out, &why) && why.find("4x2") != std::string::npos, "the wrong shape left out, saying so");
    { // DDS replacements (<hash>.dds): each read format, 4 x 4 for a 2 x 2 picture
        cr::Image orig2;
        orig2.width = orig2.height = 2;
        orig2.rgba = {9, 9, 9, 255, 8, 8, 8, 255, 7, 7, 7, 255, 6, 6, 6, 255};
        const std::string dds = dir + cr::hashName(cr::imageHash(orig2)) + ".dds";
        auto put32 = [](std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + size_t(i)] = uint8_t(x >> (8 * i)); };
        auto header = [&](uint32_t pfFlags, const char* fourCC, size_t extra) {
            std::vector<uint8_t> v(128 + extra, 0);
            std::memcpy(v.data(), "DDS ", 4);
            put32(v, 4, 124), put32(v, 12, 4), put32(v, 16, 4), put32(v, 76, 32), put32(v, 80, pfFlags);
            if (fourCC) std::memcpy(v.data() + 84, fourCC, 4);
            return v;
        };
        auto dx10 = [&](uint32_t dxgi, size_t data) {
            std::vector<uint8_t> v = header(4, "DX10", 20 + data);
            put32(v, 128, dxgi), put32(v, 132, 3), put32(v, 140, 1);
            return v;
        };
        auto write = [&](const std::vector<uint8_t>& v) { std::ofstream(dds, std::ios::binary).write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size())); };
        auto pixel = [&](int i) { return std::vector<uint8_t>(out.rgba.begin() + i * 4, out.rgba.begin() + i * 4 + 4); };
        { // DXT1: colour 0 pure red (565 0xf800), every index 0
            std::vector<uint8_t> v = header(4, "DXT1", 8);
            v[128] = 0x00, v[129] = 0xf8;
            write(v);
            check(cr::loadHdImage(dir, orig2, out, &why) && out.width == 4 && pixel(15) == std::vector<uint8_t>{255, 0, 0, 255}, "a DXT1 replacement");
        }
        { // BC7 mode 6: endpoints (c << 1 | p), the p-bit 1 on every channel - 255 on R and A, 1 on G and B; index 0
            std::vector<uint8_t> v = dx10(98, 16);
            uint8_t* b = v.data() + 148;
            int bit = 0;
            auto bits = [&](uint32_t value, int count) { for (int i = 0; i < count; ++i, ++bit) if ((value >> i) & 1u) b[bit / 8] |= uint8_t(1u << (bit % 8)); };
            bits(1u << 6, 7);                // mode 6
            bits(127, 7), bits(127, 7);      // R0, R1
            bits(0, 7), bits(0, 7);          // G
            bits(0, 7), bits(0, 7);          // B
            bits(127, 7), bits(127, 7);      // A
            bits(1, 1), bits(1, 1);          // p-bits
            write(v);
            check(cr::loadHdImage(dir, orig2, out, &why) && pixel(5) == std::vector<uint8_t>{255, 1, 1, 255}, "a BC7 replacement");
        }
        { // DX10 B8G8R8A8: blue-green-red-alpha bytes
            std::vector<uint8_t> v = dx10(87, 64);
            for (int i = 0; i < 16; ++i) v[148 + size_t(i) * 4] = 10, v[149 + size_t(i) * 4] = 20, v[150 + size_t(i) * 4] = 30, v[151 + size_t(i) * 4] = 40;
            write(v);
            check(cr::loadHdImage(dir, orig2, out, &why) && pixel(3) == std::vector<uint8_t>{30, 20, 10, 40}, "a 32-bit BGRA replacement");
        }
        { // legacy 32-bit RGB by masks, no alpha: opaque
            std::vector<uint8_t> v = header(0x40, nullptr, 64);
            put32(v, 88, 32), put32(v, 92, 0xff), put32(v, 96, 0xff00), put32(v, 100, 0xff0000);
            for (int i = 0; i < 16; ++i) v[128 + size_t(i) * 4] = 50, v[129 + size_t(i) * 4] = 60, v[130 + size_t(i) * 4] = 70, v[131 + size_t(i) * 4] = 0;
            write(v);
            check(cr::loadHdImage(dir, orig2, out, &why) && pixel(0) == std::vector<uint8_t>{50, 60, 70, 255}, "a legacy RGB replacement, opaque");
        }
        { // BC4: a grey - endpoints 200 / 200, its value on R, G and B
            std::vector<uint8_t> v = header(4, "ATI1", 8);
            v[128] = 200, v[129] = 200;
            write(v);
            check(cr::loadHdImage(dir, orig2, out, &why) && pixel(7) == std::vector<uint8_t>{200, 200, 200, 255}, "a BC4 replacement as a grey");
        }
        { // a cube map: left out, saying so
            std::vector<uint8_t> v = header(4, "DXT1", 8 * 6);
            put32(v, 112, 0x200);
            write(v);
            check(!cr::loadHdImage(dir, orig2, out, &why) && why.find("cube") != std::string::npos, "a cube map left out");
        }
        { // cut short: left out
            std::vector<uint8_t> v = header(4, "DXT5", 8);
            write(v);
            check(!cr::loadHdImage(dir, orig2, out, &why) && why.find("short") != std::string::npos, "a DDS cut short left out");
        }
    }
    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("hd texture: %d problems\n", problems);
    return problems ? 1 : 0;
}
