#include "ctr/Bclim.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace promptemon::ctr {
namespace {

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

int bflimToCtrFormat(int format) {
    switch (format) {
        case 0x00: return 7;  // L8
        case 0x01: return 8;  // A8
        case 0x02: return 9;  // LA4
        case 0x03: return 5;  // LA8
        case 0x04: return 6;  // HILO8
        case 0x05: return 3;  // RGB565
        case 0x06: return 1;  // RGB8
        case 0x07: return 2;  // RGBA5551
        case 0x08: return 4;  // RGBA4
        case 0x09: return 0;  // RGBA8
        case 0x0A: return 12; // ETC1
        case 0x0B: return 13; // ETC1A4
        case 0x0C: return 10; // L4
        case 0x0D: return 11; // A4
        case 0x13: return 12; // alternate ETC1 id
        default: return -1;
    }
}

struct Footer {
    std::size_t offset = static_cast<std::size_t>(-1);
    bool bigEndian = false;
    bool flim = false;
};

Footer findFooter(const std::vector<std::uint8_t>& data) {
    if (data.size() < 0x28) return {};
    const std::size_t begin = data.size() > 0x100 ? data.size() - 0x100 : 0;
    for (std::size_t pos = data.size() - 0x28;; --pos) {
        const bool clim = magicAt(data, pos, "CLIM");
        const bool flim = magicAt(data, pos, "FLIM");
        if ((clim || flim) && pos + 0x28 <= data.size()) {
            Footer footer;
            footer.offset = pos;
            footer.flim = flim;
            if (data[pos + 4] == 0xFF && data[pos + 5] == 0xFE) footer.bigEndian = false;
            else if (data[pos + 4] == 0xFE && data[pos + 5] == 0xFF) footer.bigEndian = true;
            else return {};
            return footer;
        }
        if (pos == begin || pos == 0) break;
    }
    return {};
}

} // namespace

bool isBclim(const std::vector<std::uint8_t>& data) noexcept {
    return findFooter(data).offset != static_cast<std::size_t>(-1);
}

Image decodeBclim(const std::vector<std::uint8_t>& data) {
    const Footer footer = findFooter(data);
    const std::size_t clim = footer.offset;
    if (clim == static_cast<std::size_t>(-1)) return {};

    const auto read16 = [&](std::size_t offset) {
        return footer.bigEndian ? u16be(data, offset) : u16le(data, offset);
    };
    const auto read32 = [&](std::size_t offset) {
        return footer.bigEndian ? u32be(data, offset) : u32le(data, offset);
    };

    const std::uint16_t headerSize = read16(clim + 0x06);
    if (headerSize < 0x14 || clim + headerSize + 0x10 > data.size()) return {};

    const std::size_t imag = clim + headerSize;
    if (!magicAt(data, imag, "imag")) return {};

    // In Nintendo BFLIM/BCLIM, this field is the parse-info size (normally
    // 0x10), not the physical number of bytes occupied by the whole imag
    // structure. The structure itself is 0x14 bytes when dataSize is present.
    const std::uint32_t imagParseSize = read32(imag + 0x04);
    if (imagParseSize < 0x10 || imag + 0x10 > data.size()) return {};

    int width = 0;
    int height = 0;
    int format = -1;
    std::uint32_t declaredImageSize = static_cast<std::uint32_t>(clim);

    if (footer.flim) {
        // 3DS BFLIM: 4s I H(height) H(width) H(alignment) B(format)
        // B(swizzle) I(dataSize). parseInfoSize is 0x10 even though this
        // complete structure occupies 0x14 bytes.
        if (imag + 0x14 > data.size()) return {};
        height = static_cast<int>(read16(imag + 0x08));
        width = static_cast<int>(read16(imag + 0x0A));
        format = bflimToCtrFormat(static_cast<int>(data[imag + 0x0E]));
        declaredImageSize = read32(imag + 0x10);
    } else {
        // BCLIM uses the CTR texture enum directly.
        width = static_cast<int>(read16(imag + 0x08));
        height = static_cast<int>(read16(imag + 0x0A));
        format = static_cast<int>(read32(imag + 0x0C) & 0x0F);
        if (imag + 0x14 <= data.size()) declaredImageSize = read32(imag + 0x10);
    }

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || format < 0 || format > 13) return {};

    std::size_t imageOffset = 0;
    if (declaredImageSize != 0 && declaredImageSize <= clim) imageOffset = clim - declaredImageSize;

    Image image = decodeTexture(data, imageOffset, format, width, height);
    if (!image.valid()) return {};
    return image;
}

} // namespace promptemon::ctr
