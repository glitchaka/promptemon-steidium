#include "storage/RomAssetCache.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace promptemon::storage {

bool RomAssetCache::openInstalledDirectory(const std::filesystem::path& directory) {
    close();

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(directory / "bundle_manifest.tsv", ec) || ec) return false;

    const std::filesystem::path romfs = directory / "files" / "romfs";
    ec.clear();
    if (!std::filesystem::is_directory(romfs, ec) || ec) return false;

    std::unordered_map<std::string, std::filesystem::path> loaded;
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
        if (!virtualPath.empty()) loaded[virtualPath] = localRelative;
    }

    if (loaded.empty()) return false;
    root_ = directory;
    signature_ = "promptemon-resource-bundle-v1";
    files_ = std::move(loaded);
    return true;
}

} // namespace promptemon::storage
