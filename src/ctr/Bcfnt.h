#pragma once

#include "ctr/CtrTexture.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace promptemon::ctr {

struct GlyphMetrics {
    int left = 0;
    int glyphWidth = 0;
    int charWidth = 0;
};

struct Glyph {
    std::uint16_t index = 0;
    GlyphMetrics metrics{};
    int sheet = 0;
    int x = 0;
    int y = 0;
};

struct BitmapFont {
    int cellWidth = 0;
    int cellHeight = 0;
    int baseline = 0;
    int lineFeed = 0;
    int columns = 0;
    int rows = 0;
    int sheetWidth = 0;
    int sheetHeight = 0;
    GlyphMetrics defaultMetrics{};
    std::uint16_t fallbackGlyphIndex = 0;
    std::vector<Image> sheets;
    std::unordered_map<std::uint32_t, Glyph> glyphs;

    bool valid() const noexcept {
        return cellWidth > 0 && cellHeight > 0 && columns > 0 && rows > 0 && !sheets.empty();
    }

    const Glyph* findGlyph(std::uint32_t codepoint) const noexcept;
};

bool isBcfnt(const std::vector<std::uint8_t>& data) noexcept;
BitmapFont decodeBcfnt(const std::vector<std::uint8_t>& data);

} // namespace promptemon::ctr
