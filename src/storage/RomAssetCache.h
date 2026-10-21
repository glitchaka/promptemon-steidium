#pragma once

#include "rom/RomModelArchive.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::storage {

inline std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

class RomAssetCache {
public:
    using ProgressCallback = std::function<void(std::size_t current,
                                                std::size_t total,
                                                const std::string& virtualPath)>;

    bool prepare(rom::RomModelArchive& archive,
                 const std::filesystem::path& baseDirectory,
                 ProgressCallback progress = {});

    // Opens the curated resource bundle produced from resources-tx.
    // Expected layout: <directory>/files/romfs/... + bundle_manifest.tsv.
    bool openInstalledDirectory(const std::filesystem::path& directory);

    void close();

    bool isOpen() const noexcept { return !root_.empty() && !files_.empty(); }
    const std::filesystem::path& root() const noexcept { return root_; }
    const std::string& signature() const noexcept { return signature_; }

    std::vector<std::string> resources() const;
    std::vector<std::string> modelResources() const;

    bool hasResource(const std::string& virtualPath) const;
    bool has(const std::string& virtualPath) const { return hasResource(virtualPath); }
    std::vector<std::uint8_t> extractResource(const std::string& virtualPath) const;
    std::vector<std::uint8_t> extractBch(const std::string& virtualPath) const;

private:
    static std::string computeSignature(const rom::RomModelArchive& archive);
    static bool shouldExport(const std::string& virtualPath);
    static bool isModelResource(const std::string& virtualPath);
    static std::filesystem::path relativePathFor(const std::string& virtualPath);

    bool loadManifest(const std::filesystem::path& directory, const std::string& expectedSignature);
    bool buildManifest(rom::RomModelArchive& archive,
                       const std::filesystem::path& directory,
                       const std::string& signature,
                       const ProgressCallback& progress);

    std::filesystem::path root_;
    std::string signature_;
    std::unordered_map<std::string, std::filesystem::path> files_;
};

} // namespace promptemon::storage
