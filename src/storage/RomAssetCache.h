#pragma once

#include "rom/RomModelArchive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::storage {

class RomAssetCache {
public:
    bool prepare(rom::RomModelArchive& archive, const std::filesystem::path& baseDirectory);
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
    static std::string cacheFileName(const std::string& virtualPath);
    static std::string categoryFor(const std::string& virtualPath);

    bool loadManifest(const std::filesystem::path& directory, const std::string& expectedSignature);
    bool buildManifest(rom::RomModelArchive& archive,
                       const std::filesystem::path& directory,
                       const std::string& signature);

    std::filesystem::path root_;
    std::string signature_;
    std::unordered_map<std::string, std::filesystem::path> files_;
};

} // namespace promptemon::storage
