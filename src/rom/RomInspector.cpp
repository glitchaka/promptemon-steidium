#include "rom/RomInspector.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace promptemon::rom {
namespace {

std::string trimField(const unsigned char* data, std::size_t length) {
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = data[i];
        if (c == 0x00 || c == 0xFF) {
            break;
        }
        if (c >= 0x20 && c <= 0x7E) {
            out.push_back(static_cast<char>(c));
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string identifyPokemonGame(const std::string& code) {
    static const std::unordered_map<std::string, std::string> known = {
        {"BPRE", "Pokemon FireRed (USA)"},
        {"BPGE", "Pokemon LeafGreen (USA)"},
        {"BPEE", "Pokemon Emerald (USA)"},
        {"AXVE", "Pokemon Ruby (USA)"},
        {"AXPE", "Pokemon Sapphire (USA)"},
        {"APAE", "Pokemon Pearl (USA)"},
        {"ADAE", "Pokemon Diamond (USA)"},
        {"CPUE", "Pokemon Platinum (USA)"},
        {"IPKE", "Pokemon HeartGold (USA)"},
        {"IPGE", "Pokemon SoulSilver (USA)"},
        {"IRBO", "Pokemon Black (Europe)"},
        {"IRAO", "Pokemon White (Europe)"},
        {"IRBE", "Pokemon Black (USA)"},
        {"IRAE", "Pokemon White (USA)"},
        {"IREO", "Pokemon Black 2 (Europe)"},
        {"IRDO", "Pokemon White 2 (Europe)"},
        {"IREE", "Pokemon Black 2 (USA)"},
        {"IRDE", "Pokemon White 2 (USA)"},
    };

    const auto it = known.find(code);
    return it == known.end() ? std::string{} : it->second;
}

bool looksLikeGba(const std::vector<unsigned char>& header) {
    return header.size() >= 0xC0 && header[0xB2] == 0x96;
}

bool looksLikeNds(const std::vector<unsigned char>& header, std::uintmax_t fileSize) {
    if (header.size() < 0x200 || fileSize < 0x4000) {
        return false;
    }

    const auto le32 = [&header](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(header[offset]) |
               (static_cast<std::uint32_t>(header[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(header[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(header[offset + 3]) << 24);
    };

    const std::uint32_t arm9Offset = le32(0x20);
    const std::uint32_t arm7Offset = le32(0x30);
    return arm9Offset >= 0x200 && arm7Offset >= 0x200 &&
           arm9Offset < fileSize && arm7Offset < fileSize;
}

} // namespace

RomInfo RomInspector::inspect(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("ROM file does not exist: " + path.string());
    }

    RomInfo info;
    info.path = path;
    info.size = std::filesystem::file_size(path);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open ROM: " + path.string());
    }

    const std::size_t bytesToRead = static_cast<std::size_t>(std::min<std::uintmax_t>(info.size, 0x200));
    std::vector<unsigned char> header(bytesToRead);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));

    if (looksLikeGba(header)) {
        info.format = RomFormat::Gba;
        info.title = trimField(header.data() + 0xA0, 12);
        info.gameCode = trimField(header.data() + 0xAC, 4);
        info.makerCode = trimField(header.data() + 0xB0, 2);
    } else if (looksLikeNds(header, info.size)) {
        info.format = RomFormat::NintendoDs;
        info.title = trimField(header.data() + 0x00, 12);
        info.gameCode = trimField(header.data() + 0x0C, 4);
        info.makerCode = trimField(header.data() + 0x10, 2);
    }

    info.detectedGame = identifyPokemonGame(info.gameCode);
    return info;
}

const char* RomInspector::formatName(RomFormat format) noexcept {
    switch (format) {
        case RomFormat::Gba: return "Game Boy Advance";
        case RomFormat::NintendoDs: return "Nintendo DS";
        default: return "Unknown";
    }
}

} // namespace promptemon::rom
