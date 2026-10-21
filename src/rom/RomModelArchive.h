#pragma once

#include "rom/IRomModelSource.h"
#include "rom/RomLibrary.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace promptemon::rom {

class RomModelArchive {
public:
    bool open(const std::filesystem::path& romDirectory);
    void close();

    const RomLibrary& library() const noexcept { return library_; }
    std::size_t modelSourceCount() const noexcept { return sources_.size(); }
    std::vector<std::string> modelResources() const;

    std::vector<std::string> resources() const {
        std::vector<std::string> out;
        for (const auto& slot : sources_) {
            for (const auto& resource : slot.source->allResources()) {
                out.push_back(slot.label + " :: " + resource);
            }
        }
        return out;
    }

    bool hasResource(const std::string& virtualPath) const {
        const auto decoded = decode(virtualPath);
        return decoded.source && decoded.source->hasResource(decoded.resource);
    }

    std::vector<std::uint8_t> extractResource(const std::string& virtualPath) const {
        const auto decoded = decode(virtualPath);
        if (!decoded.source) throw std::runtime_error("Unknown ROM resource: " + virtualPath);
        return decoded.source->extractResource(decoded.resource);
    }

    bool has(const std::string& virtualPath) const;
    std::vector<std::uint8_t> extractBch(const std::string& virtualPath) const;

private:
    struct SourceSlot {
        std::string label;
        std::unique_ptr<IRomModelSource> source;
    };

    struct DecodedPath {
        const IRomModelSource* source = nullptr;
        std::string resource;
    };

    RomLibrary library_;
    std::vector<SourceSlot> sources_;

    DecodedPath decode(const std::string& virtualPath) const;
};

} // namespace promptemon::rom
