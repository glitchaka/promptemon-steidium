#include "rom/RumbleWorldArchive.h"

#include "rom/Lzh8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace promptemon::rom {
namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Unexpected end of structure");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t u64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 8 > data.size()) throw std::runtime_error("Unexpected end of structure");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[offset + i]) << (8u * i);
    }
    return value;
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

std::string utf16Ascii(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t byteLength) {
    if (offset + byteLength > data.size() || (byteLength & 1u) != 0) {
        throw std::runtime_error("Invalid RomFS UTF-16 name");
    }

    std::string out;
    out.reserve(byteLength / 2u);
    for (std::uint32_t i = 0; i < byteLength; i += 2) {
        const std::uint16_t ch = static_cast<std::uint16_t>(data[offset + i]) |
                                 static_cast<std::uint16_t>(data[offset + i + 1] << 8);
        out.push_back(ch <= 0x7Fu ? static_cast<char>(ch) : '?');
    }
    return out;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct DirectoryEntry {
    std::uint32_t sibling = kNone;
    std::uint32_t child = kNone;
    std::uint32_t firstFile = kNone;
    std::string name;
};

struct FileEntry {
    std::uint32_t sibling = kNone;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataSize = 0;
    std::string name;
};

} // namespace

bool RumbleWorldArchive::open(const std::filesystem::path& ciaPath) {
    close();
    path_ = ciaPath;

    if (!std::filesystem::exists(path_) || !std::filesystem::is_regular_file(path_)) {
        close();
        return false;
    }

    try {
        const auto ciaHeader = read(0, 0x20);
        const std::uint32_t headerSize = u32(ciaHeader, 0x00);
        const std::uint32_t certificateSize = u32(ciaHeader, 0x08);
        const std::uint32_t ticketSize = u32(ciaHeader, 0x0C);
        const std::uint32_t tmdSize = u32(ciaHeader, 0x10);

        if (headerSize < 0x20) throw std::runtime_error("Invalid CIA header");

        std::uint64_t contentOffset = alignUp(headerSize, 0x40);
        contentOffset = alignUp(contentOffset + certificateSize, 0x40);
        contentOffset = alignUp(contentOffset + ticketSize, 0x40);
        contentOffset = alignUp(contentOffset + tmdSize, 0x40);

        const auto ncch = read(contentOffset, 0x200);
        if (ncch[0x100] != 'N' || ncch[0x101] != 'C' || ncch[0x102] != 'C' || ncch[0x103] != 'H') {
            throw std::runtime_error("First CIA content is not a readable NCCH partition");
        }

        const std::uint64_t romFsOffset = static_cast<std::uint64_t>(u32(ncch, 0x1B0)) * 0x200u;
        const std::uint64_t romFsSize = static_cast<std::uint64_t>(u32(ncch, 0x1B4)) * 0x200u;
        if (romFsOffset == 0 || romFsSize == 0) throw std::runtime_error("NCCH has no RomFS");

        const std::uint64_t romFsBase = contentOffset + romFsOffset;
        const auto ivfc = read(romFsBase, 4);
        if (ivfc[0] != 'I' || ivfc[1] != 'V' || ivfc[2] != 'F' || ivfc[3] != 'C') {
            throw std::runtime_error("RomFS is encrypted or unsupported");
        }

        indexRomFs(romFsBase, romFsSize);
        return !files_.empty();
    } catch (...) {
        close();
        throw;
    }
}

void RumbleWorldArchive::close() {
    files_.clear();
    path_.clear();
}

bool RumbleWorldArchive::isOpen() const noexcept {
    return !path_.empty() && !files_.empty();
}

std::vector<std::string> RumbleWorldArchive::pokemonModels() const {
    std::vector<std::string> result;
    for (const auto& [path, record] : files_) {
        (void)record;
        if (path.rfind("pii/", 0) == 0 && path != "pii/anim.bch.cx" && endsWith(path, ".bch.cx")) {
            result.push_back(path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool RumbleWorldArchive::has(const std::string& romfsPath) const {
    return files_.find(romfsPath) != files_.end();
}

std::vector<std::uint8_t> RumbleWorldArchive::extract(const std::string& romfsPath) const {
    const auto it = files_.find(romfsPath);
    if (it == files_.end()) throw std::runtime_error("RomFS file not found: " + romfsPath);
    if (it->second.size > static_cast<std::uint64_t>(static_cast<std::size_t>(-1))) {
        throw std::runtime_error("RomFS file is too large to extract");
    }
    return read(it->second.absoluteOffset, static_cast<std::size_t>(it->second.size));
}

std::vector<std::uint8_t> RumbleWorldArchive::extractBch(const std::string& romfsPath) const {
    auto data = extract(romfsPath);
    if (Lzh8::isCompressed(data)) data = Lzh8::decompress(data);
    if (data.size() < 4 || data[0] != 'B' || data[1] != 'C' || data[2] != 'H' || data[3] != 0) {
        throw std::runtime_error("Extracted resource is not BCH: " + romfsPath);
    }
    return data;
}

std::vector<std::uint8_t> RumbleWorldArchive::read(std::uint64_t offset, std::size_t size) const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open CIA: " + path_.string());

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) throw std::runtime_error("Could not seek in CIA");

    std::vector<std::uint8_t> data(size);
    if (size != 0) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            throw std::runtime_error("Unexpected end of CIA");
        }
    }
    return data;
}

void RumbleWorldArchive::indexRomFs(std::uint64_t romFsBase, std::uint64_t romFsSize) {
    const std::size_t probeSize = static_cast<std::size_t>(std::min<std::uint64_t>(romFsSize, 0x20000u));
    const auto probe = read(romFsBase, probeSize);

    std::size_t level3Offset = static_cast<std::size_t>(-1);
    for (std::size_t pos = 0; pos + 0x28 <= probe.size(); pos += 4) {
        if (u32(probe, pos) != 0x28u) continue;

        const std::uint32_t dirHashOffset = u32(probe, pos + 0x04);
        const std::uint32_t dirHashSize = u32(probe, pos + 0x08);
        const std::uint32_t dirMetaOffset = u32(probe, pos + 0x0C);
        const std::uint32_t dirMetaSize = u32(probe, pos + 0x10);
        const std::uint32_t fileHashOffset = u32(probe, pos + 0x14);
        const std::uint32_t fileHashSize = u32(probe, pos + 0x18);
        const std::uint32_t fileMetaOffset = u32(probe, pos + 0x1C);
        const std::uint32_t fileMetaSize = u32(probe, pos + 0x20);
        const std::uint32_t dataOffset = u32(probe, pos + 0x24);

        const bool sequential =
            dirHashOffset == 0x28u &&
            dirMetaOffset == dirHashOffset + dirHashSize &&
            fileHashOffset == dirMetaOffset + dirMetaSize &&
            fileMetaOffset == fileHashOffset + fileHashSize &&
            dataOffset >= fileMetaOffset + fileMetaSize;
        if (sequential && static_cast<std::uint64_t>(pos) + dataOffset < romFsSize) {
            level3Offset = pos;
            break;
        }
    }

    if (level3Offset == static_cast<std::size_t>(-1)) {
        throw std::runtime_error("Could not locate RomFS level-3 header");
    }

    const std::uint32_t dirMetaOffset = u32(probe, level3Offset + 0x0C);
    const std::uint32_t dirMetaSize = u32(probe, level3Offset + 0x10);
    const std::uint32_t fileMetaOffset = u32(probe, level3Offset + 0x1C);
    const std::uint32_t fileMetaSize = u32(probe, level3Offset + 0x20);
    const std::uint32_t dataOffset = u32(probe, level3Offset + 0x24);

    const std::uint64_t level3Base = romFsBase + level3Offset;
    const auto directories = read(level3Base + dirMetaOffset, dirMetaSize);
    const auto fileMetadata = read(level3Base + fileMetaOffset, fileMetaSize);
    const std::uint64_t fileDataBase = level3Base + dataOffset;

    const auto parseDirectory = [&directories](std::uint32_t offset) -> DirectoryEntry {
        if (static_cast<std::uint64_t>(offset) + 0x18u > directories.size()) {
            throw std::runtime_error("Invalid RomFS directory metadata offset");
        }
        DirectoryEntry entry;
        entry.sibling = u32(directories, offset + 0x04);
        entry.child = u32(directories, offset + 0x08);
        entry.firstFile = u32(directories, offset + 0x0C);
        const std::uint32_t nameLength = u32(directories, offset + 0x14);
        entry.name = utf16Ascii(directories, offset + 0x18, nameLength);
        return entry;
    };

    const auto parseFile = [&fileMetadata](std::uint32_t offset) -> FileEntry {
        if (static_cast<std::uint64_t>(offset) + 0x20u > fileMetadata.size()) {
            throw std::runtime_error("Invalid RomFS file metadata offset");
        }
        FileEntry entry;
        entry.sibling = u32(fileMetadata, offset + 0x04);
        entry.dataOffset = u64(fileMetadata, offset + 0x08);
        entry.dataSize = u64(fileMetadata, offset + 0x10);
        const std::uint32_t nameLength = u32(fileMetadata, offset + 0x1C);
        entry.name = utf16Ascii(fileMetadata, offset + 0x20, nameLength);
        return entry;
    };

    std::unordered_set<std::uint32_t> visitedDirectories;
    std::function<void(std::uint32_t, const std::string&)> walk;
    walk = [&](std::uint32_t directoryOffset, const std::string& parentPath) {
        if (!visitedDirectories.insert(directoryOffset).second) return;
        const DirectoryEntry directory = parseDirectory(directoryOffset);
        const std::string currentPath = directoryOffset == 0
            ? parentPath
            : (parentPath.empty() ? directory.name : parentPath + "/" + directory.name);

        std::unordered_set<std::uint32_t> visitedFiles;
        for (std::uint32_t fileOffset = directory.firstFile;
             fileOffset != kNone && visitedFiles.insert(fileOffset).second;) {
            const FileEntry file = parseFile(fileOffset);
            const std::string fullPath = currentPath.empty() ? file.name : currentPath + "/" + file.name;
            files_[fullPath] = FileRecord{fileDataBase + file.dataOffset, file.dataSize};
            fileOffset = file.sibling;
        }

        std::unordered_set<std::uint32_t> visitedSiblings;
        for (std::uint32_t childOffset = directory.child;
             childOffset != kNone && visitedSiblings.insert(childOffset).second;) {
            const DirectoryEntry child = parseDirectory(childOffset);
            walk(childOffset, currentPath);
            childOffset = child.sibling;
        }
    };

    walk(0, "");
}

} // namespace promptemon::rom
