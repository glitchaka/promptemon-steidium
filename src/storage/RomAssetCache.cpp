#include "storage/RomAssetCache.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace promptemon::storage {
namespace {

constexpr const char* kCacheSchema = "promptemon-assets-v2-all-resources";

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

std::uint64_t fnv1a64(const std::string& value, std::uint64_t hash = 1469598103934665603ull) {
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string leafName(const std::string& virtualPath) {
    const auto marker = virtualPath.rfind(" :: ");
    const std::string inner = marker == std::string::npos
        ? virtualPath
        : virtualPath.substr(marker + 4);
    const auto slash = inner.find_last_of("/\\");
    return slash == std::string::npos ? inner : inner.substr(slash + 1);
}

std::string sanitize(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '_' || c == '-')) c = '_';
    }
    if (value.empty()) value = "resource.bin";
    if (value.size() > 96) value.resize(96);
    return value;
}

bool isAnimationLeaf(const std::string& virtualPath) {
    const std::string p = lowerCopy(virtualPath);
    const std::string leaf = lowerCopy(leafName(p));
    return leaf == "anim.bch" || leaf == "anim.bch.cx" ||
           leaf == "mii_anim.bch" || leaf == "mii_anim.bch.cx" ||
           contains(leaf, "animation");
}

} // namespace

void RomAssetCache::close() {
    files_.clear();
    root_.clear();
    signature_.clear();
}

std::string RomAssetCache::computeSignature(const rom::RomModelArchive& archive) {
    // The cache schema is part of the signature so a code update that changes
    // extraction policy cannot silently reuse an old, incomplete manifest.
    std::uint64_t hash = fnv1a64(kCacheSchema);
    for (const auto& entry : archive.library().entries()) {
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(entry.info.path, ec);
        const auto ticks = ec ? 0ll : modified.time_since_epoch().count();
        const std::string component = entry.info.path.filename().u8string() + "|" +
                                      std::to_string(entry.info.size) + "|" +
                                      std::to_string(ticks) + "|" +
                                      entry.info.detectedGame + "|" + entry.info.gameCode;
        hash = fnv1a64(component, hash);
    }
    return hex64(hash);
}

bool RomAssetCache::shouldExport(const std::string& virtualPath) {
    // Do not guess which ROM resources are useful from filenames. Rumble World
    // uses generic names and nested containers for a number of UI assets. The
    // cache is generated once and is persistent, so export every indexed leaf.
    return !virtualPath.empty();
}

bool RomAssetCache::isModelResource(const std::string& virtualPath) {
    const std::string p = lowerCopy(virtualPath);
    if (!(endsWith(p, ".bch") || endsWith(p, ".bch.cx"))) return false;
    return !isAnimationLeaf(virtualPath);
}

std::string RomAssetCache::cacheFileName(const std::string& virtualPath) {
    return hex64(fnv1a64(virtualPath)) + "_" + sanitize(leafName(virtualPath));
}

std::string RomAssetCache::categoryFor(const std::string& virtualPath) {
    const std::string p = lowerCopy(virtualPath);
    if (endsWith(p, ".bcfnt") || endsWith(p, ".bcfnt.cx") ||
        endsWith(p, ".cfnt") || endsWith(p, ".cfnt.cx") || contains(p, "font")) return "fonts";
    if (endsWith(p, ".bclim") || endsWith(p, ".bclim.cx") ||
        endsWith(p, ".clim") || endsWith(p, ".clim.cx") ||
        endsWith(p, ".bclyt") || endsWith(p, ".bclyt.cx") ||
        endsWith(p, ".bclan") || endsWith(p, ".bclan.cx") ||
        endsWith(p, ".bcflan") || endsWith(p, ".bcflan.cx") ||
        contains(p, "/ui/") || contains(p, "::ui/")) return "ui";
    if (contains(p, "effect") || contains(p, "/eff") || contains(p, "::eff")) return "effects";
    if (endsWith(p, ".bch") || endsWith(p, ".bch.cx")) return "bch";
    return "misc";
}

bool RomAssetCache::loadManifest(const std::filesystem::path& directory,
                                 const std::string& expectedSignature) {
    files_.clear();
    std::ifstream input(directory / "manifest.tsv", std::ios::binary);
    if (!input) return false;

    std::string line;
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string prefix = "#signature\t";
    if (line.rfind(prefix, 0) != 0 || line.substr(prefix.size()) != expectedSignature) return false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string virtualPath = line.substr(0, tab);
        const std::filesystem::path relative = std::filesystem::u8path(line.substr(tab + 1));
        const std::filesystem::path full = directory / relative;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec) || ec) continue;
        files_[virtualPath] = relative;
    }

    if (files_.empty()) return false;
    root_ = directory;
    signature_ = expectedSignature;
    return true;
}

bool RomAssetCache::buildManifest(rom::RomModelArchive& archive,
                                  const std::filesystem::path& directory,
                                  const std::string& signature) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;

    const auto all = archive.resources();
    std::unordered_map<std::string, std::filesystem::path> built;
    built.reserve(all.size());

    for (const auto& virtualPath : all) {
        if (!shouldExport(virtualPath)) continue;
        try {
            const auto bytes = archive.extractResource(virtualPath);
            if (bytes.empty()) continue;

            const std::filesystem::path category = std::filesystem::u8path(categoryFor(virtualPath));
            const std::filesystem::path relative = category / std::filesystem::u8path(cacheFileName(virtualPath));
            const std::filesystem::path full = directory / relative;
            std::filesystem::create_directories(full.parent_path(), ec);
            if (ec) {
                ec.clear();
                continue;
            }

            std::ofstream output(full, std::ios::binary | std::ios::trunc);
            if (!output) continue;
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) continue;
            built[virtualPath] = relative;
        } catch (...) {
            // One unsupported leaf must not invalidate extraction of the rest.
        }
    }

    bool hasModel = false;
    for (const auto& [virtualPath, relative] : built) {
        (void)relative;
        if (isModelResource(virtualPath)) {
            hasModel = true;
            break;
        }
    }
    if (!hasModel) return false;

    const std::filesystem::path manifestTmp = directory / "manifest.tsv.tmp";
    const std::filesystem::path manifest = directory / "manifest.tsv";
    {
        std::ofstream output(manifestTmp, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << "#signature\t" << signature << "\n";
        output << "#schema\t" << kCacheSchema << "\n";
        output << "#virtual_path\tlocal_file\n";

        std::vector<std::string> keys;
        keys.reserve(built.size());
        for (const auto& [key, path] : built) {
            (void)path;
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            output << key << '\t' << built.at(key).generic_u8string() << '\n';
        }
        if (!output) return false;
    }

    std::filesystem::remove(manifest, ec);
    ec.clear();
    std::filesystem::rename(manifestTmp, manifest, ec);
    if (ec) return false;

    files_ = std::move(built);
    root_ = directory;
    signature_ = signature;
    return true;
}

bool RomAssetCache::prepare(rom::RomModelArchive& archive,
                            const std::filesystem::path& baseDirectory) {
    close();
    const std::string newSignature = computeSignature(archive);
    const std::filesystem::path directory = baseDirectory / std::filesystem::u8path(newSignature);
    if (loadManifest(directory, newSignature)) return true;
    return buildManifest(archive, directory, newSignature);
}

std::vector<std::string> RomAssetCache::resources() const {
    std::vector<std::string> out;
    out.reserve(files_.size());
    for (const auto& [virtualPath, path] : files_) {
        (void)path;
        out.push_back(virtualPath);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> RomAssetCache::modelResources() const {
    std::vector<std::string> out;
    for (const auto& [virtualPath, path] : files_) {
        (void)path;
        if (isModelResource(virtualPath)) out.push_back(virtualPath);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool RomAssetCache::hasResource(const std::string& virtualPath) const {
    const auto found = files_.find(virtualPath);
    if (found == files_.end()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(root_ / found->second, ec) && !ec;
}

std::vector<std::uint8_t> RomAssetCache::extractResource(const std::string& virtualPath) const {
    const auto found = files_.find(virtualPath);
    if (found == files_.end()) throw std::runtime_error("Exported ROM resource not found: " + virtualPath);
    const std::filesystem::path full = root_ / found->second;
    std::ifstream input(full, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open exported ROM resource: " + full.string());
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) throw std::runtime_error("Could not measure exported ROM resource");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("Could not read exported ROM resource");
    return bytes;
}

std::vector<std::uint8_t> RomAssetCache::extractBch(const std::string& virtualPath) const {
    auto bytes = extractResource(virtualPath);
    if (bytes.size() < 4 || bytes[0] != 'B' || bytes[1] != 'C' || bytes[2] != 'H' || bytes[3] != 0) {
        throw std::runtime_error("Exported resource is not BCH: " + virtualPath);
    }
    return bytes;
}

} // namespace promptemon::storage
