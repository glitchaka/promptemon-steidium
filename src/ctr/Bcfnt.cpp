#include "ctr/Bcfnt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace promptemon::ctr {
namespace {

constexpr std::size_t kInvalid = static_cast<std::size_t>(-1);

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::uint16_t u16le(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint16_t u16be(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(data[offset] << 8) |
           static_cast<std::uint16_t>(data[offset + 1]);
}

std::uint32_t u32le(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint32_t u32be(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

struct Reader {
    const std::vector<std::uint8_t>& data;
    bool bigEndian = false;

    std::uint16_t u16(std::size_t offset) const {
        return bigEndian ? u16be(data, offset) : u16le(data, offset);
    }
    std::uint32_t u32(std::size_t offset) const {
        return bigEndian ? u32be(data, offset) : u32le(data, offset);
    }
};

int signedByte(std::uint8_t value) {
    return static_cast<int>(static_cast<std::int8_t>(value));
}

std::size_t pointedSection(std::uint32_t pointer, std::size_t size) {
    // FINF pointers reference the payload immediately after an 8-byte section
    // header, so the section magic is located at pointer - 8.
    if (pointer < 8) return kInvalid;
    const std::size_t position = static_cast<std::size_t>(pointer - 8);
    return position + 8 <= size ? position : kInvalid;
}

bool byteOrder(const std::vector<std::uint8_t>& data, bool& bigEndian) {
    if (data.size() < 6) return false;
    // Nintendo binary resources write FE FF for BE and FF FE for LE.
    if (data[4] == 0xFE && data[5] == 0xFF) {
        bigEndian = true;
        return true;
    }
    if (data[4] == 0xFF && data[5] == 0xFE) {
        bigEndian = false;
        return true;
    }
    return false;
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
    return magicAt(data, 0, "CFNT") || magicAt(data, 0, "CFNU") ||
           magicAt(data, 0, "FFNT") || magicAt(data, 0, "FFNU");
}

BitmapFont decodeBcfnt(const std::vector<std::uint8_t>& data) {
    BitmapFont font;
    if (!isBcfnt(data)) return font;

    bool bigEndian = false;
    if (!byteOrder(data, bigEndian)) return {};
    const Reader rd{data, bigEndian};

    const bool bffnt = magicAt(data, 0, "FFNT") || magicAt(data, 0, "FFNU");
    const std::uint16_t headerSize = rd.u16(0x06);
    if (headerSize < 0x14 || static_cast<std::size_t>(headerSize) + 0x20 > data.size()) return {};

    const std::size_t finf = headerSize;
    if (!magicAt(data, finf, "FINF")) return {};
    if (rd.u32(finf + 0x04) < 0x20) return {};

    std::uint32_t tglpPointer = 0;
    std::uint32_t cwdhPointer = 0;
    std::uint32_t cmapPointer = 0;

    if (bffnt) {
        // FFNT/BFFNT FINF: 4B, 2H, 4B, 3I after section header.
        font.lineFeed = static_cast<int>(rd.u16(finf + 0x0C));
        font.fallbackGlyphIndex = rd.u16(finf + 0x0E);
        font.defaultMetrics.left = signedByte(data[finf + 0x10]);
        font.defaultMetrics.glyphWidth = static_cast<int>(data[finf + 0x11]);
        font.defaultMetrics.charWidth = static_cast<int>(data[finf + 0x12]);
        tglpPointer = rd.u32(finf + 0x14);
        cwdhPointer = rd.u32(finf + 0x18);
        cmapPointer = rd.u32(finf + 0x1C);
    } else {
        // CFNT/BCFNT FINF: 2B, H, 4B, 3I, 4B after section header.
        font.lineFeed = static_cast<int>(data[finf + 0x09]);
        font.fallbackGlyphIndex = rd.u16(finf + 0x0A);
        font.defaultMetrics.left = signedByte(data[finf + 0x0C]);
        font.defaultMetrics.glyphWidth = static_cast<int>(data[finf + 0x0D]);
        font.defaultMetrics.charWidth = static_cast<int>(data[finf + 0x0E]);
        tglpPointer = rd.u32(finf + 0x10);
        cwdhPointer = rd.u32(finf + 0x14);
        cmapPointer = rd.u32(finf + 0x18);
    }

    const std::size_t tglp = pointedSection(tglpPointer, data.size());
    if (tglp == kInvalid || tglp + 0x20 > data.size() || !magicAt(data, tglp, "TGLP")) return {};

    font.cellWidth = static_cast<int>(data[tglp + 0x08]);
    font.cellHeight = static_cast<int>(data[tglp + 0x09]);

    std::uint32_t sheetSize = rd.u32(tglp + 0x0C);
    int sheetCount = 0;
    int format = 0;
    std::uint32_t sheetDataOffset = rd.u32(tglp + 0x1C);

    if (bffnt) {
        sheetCount = static_cast<int>(data[tglp + 0x0A]);
        font.baseline = static_cast<int>(rd.u16(tglp + 0x10));
    } else {
        font.baseline = static_cast<int>(data[tglp + 0x0A]);
        sheetCount = static_cast<int>(rd.u16(tglp + 0x10));
    }

    format = static_cast<int>(rd.u16(tglp + 0x12));
    font.columns = static_cast<int>(rd.u16(tglp + 0x14));
    font.rows = static_cast<int>(rd.u16(tglp + 0x16));
    font.sheetWidth = static_cast<int>(rd.u16(tglp + 0x18));
    font.sheetHeight = static_cast<int>(rd.u16(tglp + 0x1A));

    if (font.cellWidth <= 0 || font.cellHeight <= 0 || font.columns <= 0 || font.rows <= 0 ||
        font.sheetWidth <= 0 || font.sheetHeight <= 0 || font.sheetWidth > 4096 || font.sheetHeight > 4096 ||
        sheetCount <= 0 || sheetCount > 64 || sheetSize == 0 || format < 0 || format > 13 ||
        sheetDataOffset >= data.size()) {
        return {};
    }

    font.sheets.reserve(static_cast<std::size_t>(sheetCount));
    for (int i = 0; i < sheetCount; ++i) {
        const std::uint64_t offset64 = static_cast<std::uint64_t>(sheetDataOffset) +
                                       static_cast<std::uint64_t>(sheetSize) * static_cast<std::uint64_t>(i);
        if (offset64 >= data.size() || offset64 + sheetSize > data.size()) return {};
        Image sheet = decodeTexture(data, static_cast<std::size_t>(offset64), format,
                                    font.sheetWidth, font.sheetHeight);
        if (!sheet.valid()) return {};
        font.sheets.push_back(std::move(sheet));
    }

    std::unordered_map<std::uint16_t, GlyphMetrics> widths;
    std::uint32_t nextCwdh = cwdhPointer;
    for (int guard = 0; nextCwdh != 0 && guard < 1024; ++guard) {
        const std::size_t section = pointedSection(nextCwdh, data.size());
        if (section == kInvalid || section + 0x10 > data.size() || !magicAt(data, section, "CWDH")) break;
        const std::uint16_t first = rd.u16(section + 0x08);
        const std::uint16_t last = rd.u16(section + 0x0A);
        nextCwdh = rd.u32(section + 0x0C);
        if (last < first) continue;
        const std::size_t count = static_cast<std::size_t>(last - first) + 1u;
        const std::size_t values = section + 0x10;
        if (values + count * 3u > data.size()) break;
        for (std::size_t i = 0; i < count; ++i) {
            GlyphMetrics metrics;
            metrics.left = signedByte(data[values + i * 3u]);
            metrics.glyphWidth = static_cast<int>(data[values + i * 3u + 1u]);
            metrics.charWidth = static_cast<int>(data[values + i * 3u + 2u]);
            widths[static_cast<std::uint16_t>(first + i)] = metrics;
        }
    }

    std::unordered_map<std::uint32_t, std::uint16_t> mapping;
    std::uint32_t nextCmap = cmapPointer;
    for (int guard = 0; nextCmap != 0 && guard < 1024; ++guard) {
        const std::size_t section = pointedSection(nextCmap, data.size());
        if (section == kInvalid || section + 0x14 > data.size() || !magicAt(data, section, "CMAP")) break;
        const std::uint16_t firstCode = rd.u16(section + 0x08);
        const std::uint16_t lastCode = rd.u16(section + 0x0A);
        const std::uint16_t method = rd.u16(section + 0x0C);
        nextCmap = rd.u32(section + 0x10);
        const std::size_t values = section + 0x14;
        if (lastCode < firstCode) continue;

        if (method == 0) {
            if (values + 2 > data.size()) break;
            const std::uint16_t indexOffset = rd.u16(values);
            for (std::uint32_t code = firstCode; code <= lastCode; ++code)
                mapping[code] = static_cast<std::uint16_t>(indexOffset + (code - firstCode));
        } else if (method == 1) {
            const std::size_t count = static_cast<std::size_t>(lastCode - firstCode) + 1u;
            if (values + count * 2u > data.size()) break;
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint16_t index = rd.u16(values + i * 2u);
                if (index != 0xFFFFu) mapping[static_cast<std::uint32_t>(firstCode) + i] = index;
            }
        } else if (method == 2) {
            if (values + 2 > data.size()) break;
            const std::uint16_t count = rd.u16(values);
            if (values + 2u + static_cast<std::size_t>(count) * 4u > data.size()) break;
            for (std::uint16_t i = 0; i < count; ++i) {
                const std::size_t entry = values + 2u + static_cast<std::size_t>(i) * 4u;
                mapping[rd.u16(entry)] = rd.u16(entry + 2u);
            }
        }
    }

    if (mapping.empty()) return {};
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
