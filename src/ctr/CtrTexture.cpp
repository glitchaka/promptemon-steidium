#include "ctr/CtrTexture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace promptemon::ctr {
namespace {

constexpr std::array<int, 64> kTileOrder = {
    0,1,8,9,2,3,10,11,16,17,24,25,18,19,26,27,
    4,5,12,13,6,7,14,15,20,21,28,29,22,23,30,31,
    32,33,40,41,34,35,42,43,48,49,56,57,50,51,58,59,
    36,37,44,45,38,39,46,47,52,53,60,61,54,55,62,63
};

std::uint64_t u64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 8 > data.size()) return 0;
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[offset + i]) << (8 * i);
    return value;
}

int bytesPerPixel(int format) {
    switch (format) {
        case 0: return 4;
        case 1: return 3;
        case 2: case 3: case 4: case 5: case 6: return 2;
        case 7: case 8: case 9: return 1;
        default: return 0;
    }
}

std::uint8_t expand4(std::uint32_t value) {
    return static_cast<std::uint8_t>((value & 0x0f) * 17);
}

std::uint8_t expand5(std::uint32_t value) {
    return static_cast<std::uint8_t>((value * 255 + 15) / 31);
}

std::uint8_t expand6(std::uint32_t value) {
    return static_cast<std::uint8_t>((value * 255 + 31) / 63);
}

std::uint8_t clampByte(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void writeRgba(std::vector<std::uint8_t>& out,
               std::size_t pixel,
               std::uint8_t r,
               std::uint8_t g,
               std::uint8_t b,
               std::uint8_t a) {
    const std::size_t offset = pixel * 4u;
    if (offset + 4 > out.size()) return;
    out[offset] = r;
    out[offset + 1] = g;
    out[offset + 2] = b;
    out[offset + 3] = a;
}

Image decodePlain(const std::vector<std::uint8_t>& data,
                  std::size_t dataOffset,
                  int format,
                  int width,
                  int height) {
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 255);

    const int tilesX = (width + 7) / 8;
    const int tilesY = (height + 7) / 8;

    for (int tileY = 0; tileY < tilesY; ++tileY) {
        for (int tileX = 0; tileX < tilesX; ++tileX) {
            for (int k = 0; k < 64; ++k) {
                const int tilePosition = kTileOrder[static_cast<std::size_t>(k)];
                const int pixelX = tileX * 8 + tilePosition % 8;
                const int pixelY = tileY * 8 + tilePosition / 8;
                if (pixelX >= width || pixelY >= height) continue;

                std::uint32_t value = 0;
                std::size_t offset = dataOffset;
                if (format == 10 || format == 11) {
                    offset += static_cast<std::size_t>((tileY * tilesX + tileX) * 32 + k / 2);
                    if (offset >= data.size()) continue;
                    value = (data[offset] >> ((k & 1) ? 4 : 0)) & 0x0f;
                } else {
                    const int bpp = bytesPerPixel(format);
                    if (bpp <= 0) continue;
                    offset += static_cast<std::size_t>((tileY * tilesX + tileX) * 64 * bpp + k * bpp);
                    if (offset + static_cast<std::size_t>(bpp) > data.size()) continue;
                    for (int byte = 0; byte < bpp; ++byte) {
                        value |= static_cast<std::uint32_t>(data[offset + byte]) << (byte * 8);
                    }
                }

                std::uint8_t r = 255;
                std::uint8_t g = 255;
                std::uint8_t b = 255;
                std::uint8_t a = 255;
                switch (format) {
                    case 0:
                        a = static_cast<std::uint8_t>(value & 0xff);
                        b = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        g = static_cast<std::uint8_t>((value >> 16) & 0xff);
                        r = static_cast<std::uint8_t>((value >> 24) & 0xff);
                        break;
                    case 1:
                        b = static_cast<std::uint8_t>(value & 0xff);
                        g = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        r = static_cast<std::uint8_t>((value >> 16) & 0xff);
                        break;
                    case 2:
                        a = (value & 1) ? 255 : 0;
                        r = expand5((value >> 11) & 0x1f);
                        g = expand5((value >> 6) & 0x1f);
                        b = expand5((value >> 1) & 0x1f);
                        break;
                    case 3:
                        r = expand5((value >> 11) & 0x1f);
                        g = expand6((value >> 5) & 0x3f);
                        b = expand5(value & 0x1f);
                        break;
                    case 4:
                        a = expand4(value);
                        b = expand4(value >> 4);
                        g = expand4(value >> 8);
                        r = expand4(value >> 12);
                        break;
                    case 5: {
                        a = static_cast<std::uint8_t>(value & 0xff);
                        const auto luminance = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        r = g = b = luminance;
                        break;
                    }
                    case 6:
                        r = static_cast<std::uint8_t>(value & 0xff);
                        g = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        b = 0;
                        break;
                    case 7:
                        r = g = b = static_cast<std::uint8_t>(value);
                        break;
                    case 8:
                        r = g = b = 255;
                        a = static_cast<std::uint8_t>(value);
                        break;
                    case 9:
                        a = expand4(value);
                        r = g = b = expand4(value >> 4);
                        break;
                    case 10:
                        r = g = b = expand4(value);
                        break;
                    case 11:
                        r = g = b = 255;
                        a = expand4(value);
                        break;
                    default:
                        break;
                }

                writeRgba(image.rgba,
                          static_cast<std::size_t>(pixelY * width + pixelX),
                          r, g, b, a);
            }
        }
    }
    return image;
}

constexpr int kEtcTable[8][4] = {
    {2, 8, -2, -8}, {5, 17, -5, -17}, {9, 29, -9, -29}, {13, 42, -13, -42},
    {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}
};

int signExtend3(int value) {
    return value >= 4 ? value - 8 : value;
}

Image decodeEtc(const std::vector<std::uint8_t>& data,
                std::size_t dataOffset,
                int width,
                int height,
                bool hasAlpha) {
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 255);

    const int blockSize = hasAlpha ? 16 : 8;
    const int tilesX = (width + 7) / 8;

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            const std::size_t relative = static_cast<std::size_t>(
                (((x >> 3) + (y >> 3) * tilesX) * 4 + ((x >> 2) & 1) + ((y >> 2) & 1) * 2) * blockSize);
            const std::size_t offset = dataOffset + relative;
            if (offset + static_cast<std::size_t>(blockSize) > data.size()) continue;

            const std::uint64_t alpha64 = hasAlpha ? u64(data, offset) : std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t rgb64 = u64(data, hasAlpha ? offset + 8 : offset);
            const int flip = static_cast<int>((rgb64 >> 32) & 1);
            const int differential = static_cast<int>((rgb64 >> 33) & 1);

            int r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
            if (differential) {
                const int r1b = static_cast<int>((rgb64 >> 59) & 31);
                const int g1b = static_cast<int>((rgb64 >> 51) & 31);
                const int b1b = static_cast<int>((rgb64 >> 43) & 31);
                r1 = expand5(r1b); g1 = expand5(g1b); b1 = expand5(b1b);
                r2 = expand5(std::clamp(r1b + signExtend3(static_cast<int>((rgb64 >> 56) & 7)), 0, 31));
                g2 = expand5(std::clamp(g1b + signExtend3(static_cast<int>((rgb64 >> 48) & 7)), 0, 31));
                b2 = expand5(std::clamp(b1b + signExtend3(static_cast<int>((rgb64 >> 40) & 7)), 0, 31));
            } else {
                r1 = expand4(static_cast<std::uint32_t>((rgb64 >> 60) & 15));
                g1 = expand4(static_cast<std::uint32_t>((rgb64 >> 52) & 15));
                b1 = expand4(static_cast<std::uint32_t>((rgb64 >> 44) & 15));
                r2 = expand4(static_cast<std::uint32_t>((rgb64 >> 56) & 15));
                g2 = expand4(static_cast<std::uint32_t>((rgb64 >> 48) & 15));
                b2 = expand4(static_cast<std::uint32_t>((rgb64 >> 40) & 15));
            }

            const int table1 = static_cast<int>((rgb64 >> 37) & 7);
            const int table2 = static_cast<int>((rgb64 >> 34) & 7);
            const std::uint32_t raw = static_cast<std::uint32_t>(rgb64);
            const std::uint32_t modulation =
                ((raw & 0xff000000u) >> 24) |
                ((raw & 0x00ff0000u) >> 8) |
                ((raw & 0x0000ff00u) << 8) |
                ((raw & 0x000000ffu) << 24);

            const auto put = [&](int px, int py, int rr, int gg, int bb, int bx, int by, int table) {
                if (px < 0 || py < 0 || px >= width || py >= height) return;
                const int shift = bx * 4 + by;
                int tableIndex = 0;
                if (shift < 8) {
                    tableIndex = static_cast<int>((modulation >> (shift + 24)) & 1) +
                                 static_cast<int>(((modulation << 1) >> (shift + 8)) & 2);
                } else {
                    tableIndex = static_cast<int>((modulation >> (shift + 8)) & 1) +
                                 static_cast<int>(((modulation << 1) >> (shift - 8)) & 2);
                }
                const int modifier = kEtcTable[table][tableIndex];
                const std::uint8_t alpha = hasAlpha
                    ? static_cast<std::uint8_t>(((alpha64 >> (shift * 4)) & 15) * 17)
                    : 255;
                writeRgba(image.rgba,
                          static_cast<std::size_t>(py * width + px),
                          clampByte(rr + modifier),
                          clampByte(gg + modifier),
                          clampByte(bb + modifier),
                          alpha);
            };

            if (flip) {
                for (int by = 0; by < 2; ++by) {
                    for (int bx = 0; bx < 4; ++bx) {
                        put(x + bx, y + by, r1, g1, b1, bx, by, table1);
                        put(x + bx, y + by + 2, r2, g2, b2, bx, by + 2, table2);
                    }
                }
            } else {
                for (int by = 0; by < 4; ++by) {
                    for (int bx = 0; bx < 2; ++bx) {
                        put(x + bx, y + by, r1, g1, b1, bx, by, table1);
                        put(x + bx + 2, y + by, r2, g2, b2, bx + 2, by, table2);
                    }
                }
            }
        }
    }
    return image;
}

} // namespace

Image decodeTexture(const std::vector<std::uint8_t>& data,
                    std::size_t dataOffset,
                    int format,
                    int width,
                    int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return {};
    if (format >= 0 && format <= 11) return decodePlain(data, dataOffset, format, width, height);
    if (format == 12) return decodeEtc(data, dataOffset, width, height, false);
    if (format == 13) return decodeEtc(data, dataOffset, width, height, true);
    return {};
}

} // namespace promptemon::ctr
