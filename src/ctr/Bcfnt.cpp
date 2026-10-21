#include "ctr/Bcfnt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace promptemon::ctr {
namespace {

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

int signedByte(std::uint8_t value) {
    return static_cast<int>(static_cast<std::int8_t>(value));
}

std::size_t pointedSection(std::uint32_t pointer, std::size_t size) {
    if (pointer < 8) return static_cast<std::size_t>(-1);
    const std::size_t position = static_cast<std::size_t>(pointer - 8);
    if (position + 8 > size) return static_cast<std::size_t>(-1);
    return position;
}

} // namespace

const Glyph* BitmapFont::findGlyph(std::uint32_t codepoint) const noexcept {
    if (const auto found = glyphs.find(codepoint); found != glyphs.end()) return &found->second;
    if (const auto question = glyphs.find(static_cast<std::uint32_t>('?')); question != glyphs.end()) return &question->second;
    for (const auto& item : glyphs) {
        if (item.second.index == fallbackGlyphIndex) return &item.second;
    }
    return glyphs.empty() ? nullptr : &glyphs.begin()->second;
}

bool isBcfnt(const std::vector<std::uint8_t>& data) noexcept {
    if (data.size() < 0x34) return false;
    return magicAt(data, 0, "CFNT") || magicAt(data, 0, "FFNT");
}

BitmapFont decodeBcfnt(const std::vector<std::uint8_t>& data) {
    BitmapFont font;
    if (!isBcfnt(data)) return font;
    if (u16(data, 0x04) != 0xFEFFu) return font;

    const std::uint16_t headerSize = u16(data, 0x06);
    if (headerSize < 0x14 || static_cast<std::size_t>(headerSize) + 0x20 > data.size()) return font;

    const std::size_t finf = headerSize;
    if (!magicAt(data, finf, "FINF")) return font;

    // Pokemon Rumble World is a CTR title and uses the version-3 CFNT layout.
    font.lineFeed = static_cast<int>(data[finf + 0x09]);
    font.fallbackGlyphIndex = u16(data, finf + 0x0A);
    font.defaultMetrics.left = signedByte(data[finf + 0x0C]);
    font.defaultMetrics.glyphWidth = signedByte(data[finf + 0x0D]);
    font.defaultMetrics.charWidth = signedByte(data[finf + 0x0E]);

    const std::uint32_t tglpPointer = u32(data, finf + 0x10);
    const std::uint32_t cwdhPointer = u32(data, finf + 0x14);
    const std::uint32_t cmapPointer = u32(data, finf + 0x18);

    const std::size_t tglp = pointedSection(tglpPointer, data.size());
    if (tglp == static_cast<std::size_t>(-1) || tglp + 0x20 > data.size() || !magicAt(data, tglp, "TGLP")) return {};

    font.cellWidth = static_cast<int>(data[tglp + 0x08]);
    font.cellHeight = static_cast<int>(data[tglp + 0x09]);
    font.baseline = static_cast<int>(data[tglp + 0x0A]);
    const std::uint32_t sheetSize = u32(data, tglp + 0x0C);
    const int sheetCount = static_cast<int>(u16(data, tglp + 0x10));
    const int format = static_cast<int>(u16(data, tglp + 0x12));
    font.columns = static_cast<int>(u16(data, tglp + 0x14));
    font.rows = static_cast<int>(u16(data, tglp + 0x16));
    font.sheetWidth = static_cast<int>(u16(data, tglp + 0x18));
    font.sheetHeight = static_cast<int>(u16(data, tglp + 0x1A));
    const std::uint32_t sheetDataOffset = u32(data, tglp + 0x1C);

    if (font.cellWidth <= 0 || font.cellHeight <= 0 || font.columns <= 0 || font.rows <= 0 ||
        font.sheetWidth <= 0 || font.sheetHeight <= 0 || sheetCount <= 0 || sheetCount > 64 ||
        sheetSize == 0 || sheetDataOffset >= data.size()) {
        return {};
    }

    font.sheets.reserve(static_cast<std::size_t>(sheetCount));
    for (int i = 0; i < sheetCount; ++i) {
        const std::uint64_t offset64 = static_cast<std::uint64_t>(sheetDataOffset) +
                                       static_cast<std::uint64_t>(sheetSize) * static_cast<std::uint64_t>(i);
        if (offset64 >= data.size()) return {};
        Image sheet = decodeTexture(data, static_cast<std::size_t>(offset64), format, font.sheetWidth, font.sheetHeight);
        if (!sheet.valid()) return {};
        font.sheets.push_back(std::move(sheet));
    }

    std::unordered_map<std::uint16_t, GlyphMetrics> widths;
    std::uint32_t nextCwdh = cwdhPointer;
    int cwdhGuard = 0;
    while (nextCwdh != 0 && ++cwdhGuard < 1024) {
        const std::size_t section = pointedSection(nextCwdh, data.size());
        if (section == static_cast<std::size_t>(-1) || section + 0x10 > data.size() || !magicAt(data, section, "CWDH")) break;
        const std::uint16_t first = u16(data, section + 0x08);
        const std::uint16_t last = u16(data, section + 0x0A);
        nextCwdh = u32(data, section + 0x0C);
        if (last < first) continue;
        const std::size_t count = static_cast<std::size_t>(last - first) + 1u;
        const std::size_t values = section + 0x10;
        if (values + count * 3u > data.size()) break;
        for (std::size_t i = 0; i < count; ++i) {
            GlyphMetrics metrics;
            metrics.left = signedByte(data[values + i * 3u]);
            metrics.glyphWidth = signedByte(data[values + i * 3u + 1u]);
            metrics.charWidth = signedByte(data[values + i * 3u + 2u]);
            widths[static_cast<std::uint16_t>(first + i)] = metrics;
        }
    }

    std::unordered_map<std::uint32_t, std::uint16_t> mapping;
    std::uint32_t nextCmap = cmapPointer;
    int cmapGuard = 0;
    while (nextCmap != 0 && ++cmapGuard < 1024) {
        const std::size_t section = pointedSection(nextCmap, data.size());
        if (section == static_cast<std::size_t>(-1) || section + 0x14 > data.size() || !magicAt(data, section, "CMAP")) break;
        const std::uint16_t firstCode = u16(data, section + 0x08);
        const std::uint16_t lastCode = u16(data, section + 0x0A);
        const std::uint16_t method = u16(data, section + 0x0C);
        nextCmap = u32(data, section + 0x10);
        const std::size_t values = section + 0x14;
        if (lastCode < firstCode) continue;

        if (method == 0) {
            if (values + 2 > data.size()) break;
            const std::uint16_t indexOffset = u16(data, values);
            for (std::uint32_t code = firstCode; code <= lastCode; ++code) {
                mapping[code] = static_cast<std::uint16_t>(indexOffset + (code - firstCode));
            }
        } else if (method == 1) {
            const std::size_t count = static_cast<std::size_t>(lastCode - firstCode) + 1u;
            if (values + count * 2u > data.size()) break;
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint16_t index = u16(data, values + i * 2u);
                if (index != 0xFFFFu) mapping[static_cast<std::uint32_t>(firstCode) + i] = index;
            }
        } else if (method == 2) {
            if (values + 2 > data.size()) break;
            const std::uint16_t count = u16(data, values);
            if (values + 2u + static_cast<std::size_t>(count) * 4u > data.size()) break;
            for (std::uint16_t i = 0; i < count; ++i) {
                const std::size_t entry = values + 2u + static_cast<std::size_t>(i) * 4u;
                mapping[u16(data, entry)] = u16(data, entry + 2u);
            }
        }
    }

    const int glyphsPerSheet = font.columns * font.rows;
    if (glyphsPerSheet <= 0) return {};
    const int strideX = font.cellWidth + 1;
    const int strideY = font.cellHeight + 1;

    for (const auto& [codepoint, index] : mapping) {
        const int sheet = static_cast<int>(index) / glyphsPerSheet;
        const int local = static_cast<int>(index) % glyphsPerSheet;
        if (sheet < 0 || sheet >= static_cast<int>(font.sheets.size())) continue;
        const int column = local % font.columns;
        const int row = local / font.columns;

        Glyph glyph;
        glyph.index = index;
        glyph.metrics = font.defaultMetrics;
        if (const auto found = widths.find(index); found != widths.end()) glyph.metrics = found->second;
        glyph.sheet = sheet;
        glyph.x = column * strideX + 1;
        glyph.y = row * strideY + 1;
        if (glyph.x + font.cellWidth > font.sheetWidth || glyph.y + font.cellHeight > font.sheetHeight) continue;
        font.glyphs[codepoint] = glyph;
    }

    if (!font.valid() || font.glyphs.empty()) return {};
    return font;
}

} // namespace promptemon::ctr
