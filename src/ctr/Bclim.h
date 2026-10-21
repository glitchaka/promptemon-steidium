#pragma once

#include "ctr/CtrTexture.h"

#include <cstdint>
#include <vector>

namespace promptemon::ctr {

bool isBclim(const std::vector<std::uint8_t>& data) noexcept;
Image decodeBclim(const std::vector<std::uint8_t>& data);

} // namespace promptemon::ctr
