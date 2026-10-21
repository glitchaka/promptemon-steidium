#include "storage/RomAssetCache.h"
#include "rom/Lz11.h"
#include "rom/Lzh8.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace promptemon::storage {
namespace {

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

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset, bool bigEndian) {
    return bigEndian ? u16be(data, offset) : u16le(data, offset);
}

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset, bool bigEndian) {
    return bigEndian ? u32be(data, offset) : u32le(data, offset);
}

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::size_t findMagic(const std::vector<std::uint8_t>& data, const char* magic, std::size_t limit) {
    const std::size_t maxOffset = std::min(limit, data.size());
    for (std::size_t i = 0; i + 4 <= maxOffset; ++i) {
        if (magicAt(data, i, magic)) return i;
    }
    return static_cast<std::size_t>(-1);
}

std::string utf16ZAscii(const std::vector<std::uint8_t>& data,
                        std::size_t offset,
                        std::size_t limit,
                        bool bigEndian) {
    std::string out;
    while (offset + 1 < data.size() && offset + 1 < limit) {
        const std::uint16_t ch = bigEndian
            ? static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1])
            : static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
        offset += 2;
        if (ch == 0) break;
        out.push_back(ch <= 0x7Fu ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::string asciiZ(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t limit) {
    std::string out;
    while (offset < data.size() && offset < limit) {
        const char ch = static_cast<char>(data[offset++]);
        if (!ch) break;
        if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7E) return {};
        out.push_back(ch);
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

std::vector<std::uint8_t> decodeKnown(std::vector<std::uint8_t> data) {
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (promptemon::rom::Lzh8::isCompressed(data)) {
            data = promptemon::rom::Lzh8::decompress(data);
            continue;
        }
        if (promptemon::rom::Lz11::isCompressed(data)) {
            data = promptemon::rom::Lz11::decompress(data);
            continue;
        }
        break;
    }
    return data;
}

std::string sanitize(std::string value) {
    for (char& c : value) {
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return value;
}

bool writeExpanded(const std::filesystem::path& directory,
                   const std::filesystem::path& expandedRoot,
                   const std::string& outerVirtual,
                   const std::string& innerPath,
                   const std::vector<std::uint8_t>& bytes,
                   std::unordered_map<std::string, std::filesystem::path>& loaded) {
    if (innerPath.empty() || bytes.empty()) return false;
    std::error_code ec;
    const auto destination = expandedRoot / std::filesystem::u8path(innerPath);
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return false;

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) return false;
    output.close();

    const auto relative = std::filesystem::relative(destination, directory, ec);
    if (ec) return false;
    loaded[outerVirtual + "::" + innerPath] = relative;
    return true;
}

void indexContainer(const std::filesystem::path& directory,
                    const std::filesystem::path& source,
                    const std::string& outerVirtual,
                    std::unordered_map<std::string, std::filesystem::path>& loaded,
                    unsigned depth);

void indexSarc(const std::filesystem::path& directory,
               const std::filesystem::path& source,
               const std::string& outerVirtual,
               std::unordered_map<std::string, std::filesystem::path>& loaded,
               unsigned depth) {
    auto data = decodeKnown(readBytes(source));
    if (data.size() < 0x20 || !magicAt(data, 0, "SARC")) return;

    bool bigEndian = false;
    if (data[6] == 0xFF && data[7] == 0xFE) bigEndian = false;
    else if (data[6] == 0xFE && data[7] == 0xFF) bigEndian = true;
    else return;

    const std::uint16_t sarcHeaderSize = u16(data, 4, bigEndian);
    const std::uint32_t dataOffset = u32(data, 0x0C, bigEndian);
    if (sarcHeaderSize < 0x14 || dataOffset >= data.size()) return;
    if (!magicAt(data, sarcHeaderSize, "SFAT")) return;

    const std::uint16_t sfatHeaderSize = u16(data, sarcHeaderSize + 4, bigEndian);
    const std::uint16_t nodeCount = u16(data, sarcHeaderSize + 6, bigEndian);
    if (sfatHeaderSize < 0x0C || nodeCount > 0x4000) return;

    const std::size_t nodesOffset = static_cast<std::size_t>(sarcHeaderSize) + sfatHeaderSize;
    const std::size_t nodesSize = static_cast<std::size_t>(nodeCount) * 0x10u;
    const std::size_t sfntOffset = nodesOffset + nodesSize;
    if (sfntOffset + 8 > data.size() || !magicAt(data, sfntOffset, "SFNT")) return;

    const std::uint16_t sfntHeaderSize = u16(data, sfntOffset + 4, bigEndian);
    const std::size_t namesOffset = sfntOffset + sfntHeaderSize;
    if (namesOffset > data.size()) return;

    const auto expandedRoot = directory / ".installed-expanded" / std::filesystem::u8path(sanitize(outerVirtual));

    for (std::uint16_t i = 0; i < nodeCount; ++i) {
        const std::size_t node = nodesOffset + static_cast<std::size_t>(i) * 0x10u;
        if (node + 0x10 > data.size()) break;

        const std::uint32_t nameAttributes = u32(data, node + 4, bigEndian);
        const bool hasName = (nameAttributes & 0x01000000u) != 0;
        if (!hasName) continue;
        const std::uint32_t nameUnits = nameAttributes & 0x00FFFFFFu;
        const std::size_t namePos = namesOffset + static_cast<std::size_t>(nameUnits) * 4u;
        if (namePos >= data.size()) continue;
        const std::string name = asciiZ(data, namePos, dataOffset);
        if (name.empty()) continue;

        const std::uint32_t start = u32(data, node + 8, bigEndian);
        const std::uint32_t end = u32(data, node + 12, bigEndian);
        if (end < start) continue;
        const std::size_t absoluteStart = static_cast<std::size_t>(dataOffset) + start;
        const std::size_t absoluteEnd = static_cast<std::size_t>(dataOffset) + end;
        if (absoluteEnd > data.size() || absoluteStart >= absoluteEnd) continue;

        std::vector<std::uint8_t> leaf(data.begin() + static_cast<std::ptrdiff_t>(absoluteStart),
                                       data.begin() + static_cast<std::ptrdiff_t>(absoluteEnd));
        try {
            leaf = decodeKnown(std::move(leaf));
        } catch (...) {
            continue;
        }
        if (!writeExpanded(directory, expandedRoot, outerVirtual, name, leaf, loaded)) continue;

        if (depth < 4 && leaf.size() >= 4 && (magicAt(leaf, 0, "SARC") || magicAt(leaf, 0, "darc"))) {
            const auto nestedSource = expandedRoot / std::filesystem::u8path(name);
            indexContainer(directory, nestedSource, outerVirtual + "::" + name, loaded, depth + 1);
        }
    }
}

void indexDarc(const std::filesystem::path& directory,
               const std::filesystem::path& source,
               const std::string& outerVirtual,
               std::unordered_map<std::string, std::filesystem::path>& loaded,
               unsigned depth) {
    auto data = decodeKnown(readBytes(source));
    if (data.size() < 0x1C) return;

    if (!magicAt(data, 0, "darc")) {
        const std::size_t embedded = findMagic(data, "darc", 0x2000);
        if (embedded == static_cast<std::size_t>(-1)) return;
        data = std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(embedded), data.end());
        if (data.size() < 0x1C) return;
    }

    bool bigEndian = false;
    if (data[4] == 0xFF && data[5] == 0xFE) bigEndian = false;
    else if (data[4] == 0xFE && data[5] == 0xFF) bigEndian = true;
    else return;

    const std::uint32_t tableOffset = u32(data, 0x10, bigEndian);
    const std::uint32_t tableSize = u32(data, 0x14, bigEndian);
    if (tableOffset > data.size() || tableSize > data.size() - tableOffset || tableSize < 12) return;

    const std::uint32_t rootLength = u32(data, tableOffset + 8, bigEndian);
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

    for (std::uint32_t i = 1; i < rootLength; ++i) {
        while (!directories.empty() && i >= directories.back().endIndex) directories.pop_back();
        const std::size_t entryOffset = static_cast<std::size_t>(tableOffset) + static_cast<std::size_t>(i) * 12u;
        if (entryOffset + 12 > data.size()) break;

        const std::uint32_t flagsAndName = u32(data, entryOffset, bigEndian);
        const bool isDirectory = (flagsAndName & 0x01000000u) != 0;
        const std::uint32_t nameOffset = flagsAndName & 0x00FFFFFFu;
        const std::size_t namePos = namesOffset + nameOffset;
        if (namePos >= namesLimit) continue;

        const std::string name = utf16ZAscii(data, namePos, namesLimit, bigEndian);
        const std::string parent = directories.empty() ? std::string{} : directories.back().path;
        const std::string fullPath = parent.empty() ? name : parent + "/" + name;

        const std::uint32_t dataOffset = u32(data, entryOffset + 4, bigEndian);
        const std::uint32_t length = u32(data, entryOffset + 8, bigEndian);

        if (isDirectory) {
            directories.push_back({(!name.empty() && name != ".") ? fullPath : parent, length});
            continue;
        }
        if (fullPath.empty() || static_cast<std::uint64_t>(dataOffset) + length > data.size()) continue;

        std::vector<std::uint8_t> leaf(data.begin() + dataOffset, data.begin() + dataOffset + length);
        try {
            leaf = decodeKnown(std::move(leaf));
        } catch (...) {
            continue;
        }
        if (!writeExpanded(directory, expandedRoot, outerVirtual, fullPath, leaf, loaded)) continue;

        if (depth < 4 && leaf.size() >= 4 && (magicAt(leaf, 0, "SARC") || magicAt(leaf, 0, "darc"))) {
            const auto nestedSource = expandedRoot / std::filesystem::u8path(fullPath);
            indexContainer(directory, nestedSource, outerVirtual + "::" + fullPath, loaded, depth + 1);
        }
    }
}

void indexContainer(const std::filesystem::path& directory,
                    const std::filesystem::path& source,
                    const std::string& outerVirtual,
                    std::unordered_map<std::string, std::filesystem::path>& loaded,
                    unsigned depth) {
    if (depth > 4) return;
    auto data = decodeKnown(readBytes(source));
    if (data.size() < 4) return;
    if (magicAt(data, 0, "SARC")) indexSarc(directory, source, outerVirtual, loaded, depth);
    else if (magicAt(data, 0, "darc") || findMagic(data, "darc", 0x2000) != static_cast<std::size_t>(-1))
        indexDarc(directory, source, outerVirtual, loaded, depth);
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

        const auto ext = lowerAscii(it->path().extension().u8string());
        if (ext == ".arc" || ext == ".darc" || ext == ".carc")
            containers.emplace_back(it->path(), virtualPath);
    }

    for (const auto& [source, virtualPath] : containers)
        indexContainer(directory, source, virtualPath, loaded, 0);

    if (loaded.empty()) return false;
    root_ = directory;
    signature_ = "promptemon-resource-bundle-v1";
    files_ = std::move(loaded);
    return true;
}

} // namespace promptemon::storage
