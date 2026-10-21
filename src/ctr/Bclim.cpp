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

std::size_t findFooter(const std::vector<std::uint8_t>& data) {
    if (data.size() < 0x28) return static_cast<std::size_t>(-1);
    const std::size_t begin = data.size() > 0x100 ? data.size() - 0x100 : 0;
    for (std::size_t pos = data.size() - 0x28;; --pos) {
        if (magicAt(data, pos, "CLIM") && pos + 0x28 <= data.size()) return pos;
        if (pos == begin || pos == 0) break;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace

bool isBclim(const std::vector<std::uint8_t>& data) noexcept {
    return findFooter(data) != static_cast<std::size_t>(-1);
}

Image decodeBclim(const std::vector<std::uint8_t>& data) {
    const std::size_t clim = findFooter(data);
    if (clim == static_cast<std::size_t>(-1)) return {};

    const std::uint16_t headerSize = u16(data, clim + 0x06);
    if (headerSize < 0x14 || clim + headerSize + 0x14 > data.size()) return {};

    const std::size_t imag = clim + headerSize;
    if (!magicAt(data, imag, "imag")) return {};

    const std::uint32_t imagSize = u32(data, imag + 0x04);
    if (imagSize < 0x10 || imag + imagSize > data.size()) return {};

    const int width = static_cast<int>(u16(data, imag + 0x08));
    const int height = static_cast<int>(u16(data, imag + 0x0A));
    const int format = static_cast<int>(u32(data, imag + 0x0C) & 0x0f);
    const std::uint32_t declaredImageSize = imagSize >= 0x14 ? u32(data, imag + 0x10) : static_cast<std::uint32_t>(clim);

    std::size_t imageOffset = 0;
    if (declaredImageSize != 0 && declaredImageSize <= clim) imageOffset = clim - declaredImageSize;

    Image image = decodeTexture(data, imageOffset, format, width, height);
    if (!image.valid()) return {};
    return image;
}

} // namespace promptemon::ctr
