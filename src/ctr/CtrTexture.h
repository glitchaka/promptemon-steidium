#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace promptemon::ctr {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const noexcept {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

Image decodeTexture(const std::vector<std::uint8_t>& data,
                    std::size_t dataOffset,
                    int format,
                    int width,
                    int height);

} // namespace promptemon::ctr
