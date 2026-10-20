#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace promptemon::rom {

enum class RomFormat {
    Unknown,
    Gba,
    NintendoDs,
};

struct RomInfo {
    RomFormat format = RomFormat::Unknown;
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::string title;
    std::string gameCode;
    std::string makerCode;
    std::string detectedGame;
};

class RomInspector {
public:
    static RomInfo inspect(const std::filesystem::path& path);
    static const char* formatName(RomFormat format) noexcept;
};

} // namespace promptemon::rom
