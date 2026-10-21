#include "storage/RomAssetCache.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace promptemon::storage {
namespace {

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::string utf16ZAscii(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t limit) {
    std::string out;
    while (offset + 1 < data.size() && offset + 1 < limit) {
        const std::uint16_t ch = static_cast<std::uint16_t>(data[offset]) |
                                 static_cast<std::uint16_t>(data[offset + 1] << 8);
        offset += 2;
        if (ch == 0) break;
        out.push_back(ch <= 0x7Fu ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length <= 0) return {};
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return {};
    return bytes;
}

std::string sanitize(std::string value) {
    for (char& c : value) {
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return value;
}

void indexDarc(const std::filesystem::path& directory,
               const std::filesystem::path& source,
               const std::string& outerVirtual,
               std::unordered_map<std::string, std::filesystem::path>& loaded) {
    const auto data = readBytes(source);
    if (data.size() < 0x1C || !magicAt(data, 0, "darc")) return;
    if (!(data[4] == 0xFF && data[5] == 0xFE)) return;

    const std::uint32_t tableOffset = u32(data, 0x10);
    const std::uint32_t tableSize = u32(data, 0x14);
    if (tableOffset > data.size() || tableSize > data.size() - tableOffset || tableSize < 12) return;

    const std::uint32_t rootLength = u32(data, tableOffset + 8);
    if (rootLength == 0 || rootLength > 0x100000u) return;
    const std::uint64_t entriesBytes = static_cast<std::uint64_t>(rootLength) * 12u;
    if (entriesBytes > tableSize) return;

    const std::size_t namesOffset = static_cast<std::size_t>(tableOffset + entriesBytes);
    const std::size_t namesLimit = static_cast<std::size_t>(tableOffset + tableSize);

    struct DirectoryFrame {
        std::string path;
        std::uint32_t endIndex = 0;
    };

    std::vector<DirectoryFrame> directories;
    directories.push_back({"", rootLength});

    const auto expandedRoot = directory / ".installed-expanded" / std::filesystem::u8path(sanitize(outerVirtual));
    std::error_code ec;

    for (std::uint32_t i = 1; i < rootLength; ++i) {
        while (!directories.empty() && i >= directories.back().endIndex) directories.pop_back();
        const std::size_t entryOffset = static_cast<std::size_t>(tableOffset) + static_cast<std::size_t>(i) * 12u;
        if (entryOffset + 12 > data.size()) break;

        const std::uint32_t flagsAndName = u32(data, entryOffset);
        const bool isDirectory = (flagsAndName & 0x01000000u) != 0;
        const std::uint32_t nameOffset = flagsAndName & 0x00FFFFFFu;
        const std::size_t namePos = namesOffset + nameOffset;
        if (namePos >= namesLimit) continue;

        const std::string name = utf16ZAscii(data, namePos, namesLimit);
        const std::string parent = directories.empty() ? std::string{} : directories.back().path;
        const std::string fullPath = parent.empty() ? name : parent + "/" + name;

        const std::uint32_t dataOffset = u32(data, entryOffset + 4);
        const std::uint32_t length = u32(data, entryOffset + 8);

        if (isDirectory) {
            directories.push_back({(!name.empty() && name != ".") ? fullPath : parent, length});
            continue;
        }

        if (fullPath.empty() || static_cast<std::uint64_t>(dataOffset) + length > data.size()) continue;

        const auto destination = expandedRoot / std::filesystem::u8path(fullPath);
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            ec.clear();
            continue;
        }

        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) continue;
        if (length != 0) {
            output.write(reinterpret_cast<const char*>(data.data() + dataOffset), static_cast<std::streamsize>(length));
            if (!output) continue;
        }
        output.close();

        const auto relative = std::filesystem::relative(destination, directory, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        loaded[outerVirtual + "::" + fullPath] = relative;
    }
}

} // namespace

bool RomAssetCache::openInstalledDirectory(const std::filesystem::path& directory) {
    close();

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(directory / "bundle_manifest.tsv", ec) || ec) return false;

    const std::filesystem::path romfs = directory / "files" / "romfs";
    ec.clear();
    if (!std::filesystem::is_directory(romfs, ec) || ec) return false;

    std::filesystem::remove_all(directory / ".installed-expanded", ec);
    ec.clear();

    std::unordered_map<std::string, std::filesystem::path> loaded;
    std::vector<std::pair<std::filesystem::path, std::string>> containers;

    for (std::filesystem::recursive_directory_iterator it(romfs, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const auto virtualRelative = std::filesystem::relative(it->path(), romfs, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const auto localRelative = std::filesystem::relative(it->path(), directory, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const std::string virtualPath = virtualRelative.generic_u8string();
        if (virtualPath.empty()) continue;

        loaded[virtualPath] = localRelative;

        const auto ext = it->path().extension().u8string();
        if (ext == ".arc" || ext == ".darc" || ext == ".carc") {
            containers.emplace_back(it->path(), virtualPath);
        }
    }

    for (const auto& [source, virtualPath] : containers) {
        indexDarc(directory, source, virtualPath, loaded);
    }

    if (loaded.empty()) return false;
    root_ = directory;
    signature_ = "promptemon-resource-bundle-v1";
    files_ = std::move(loaded);
    return true;
}

} // namespace promptemon::storage
