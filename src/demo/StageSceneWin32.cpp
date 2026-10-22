#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include "bch/BchScene.h"
#include "ctr/Bclim.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Texture;
using promptemon::bch::Vec2;
using promptemon::bch::Vec3;
using promptemon::storage::RomAssetCache;

constexpr float kPi = 3.14159265358979323846f;
constexpr UINT_PTR kUpdateTimer = 1;

struct TexturePool {
    std::vector<GLuint> owned;
    std::unordered_map<std::string, GLuint> aliases;
};

using MaterialTextureNames = std::array<std::string, 3>;

struct StagePart {
    std::string path;
    std::unique_ptr<Scene> scene;
    std::vector<MaterialTextureNames> materialTextures;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

struct Actor {
    std::string path;
    std::unique_ptr<Scene> scene;
    std::vector<MaterialTextureNames> materialTextures;
    TexturePool textures;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vec3 position{};
    float scale = 1.0f;
    float yawDeg = 0.0f;
};

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;

    std::vector<StagePart> parts;
    TexturePool stageTextures;
    Vec3 stageMin{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 stageMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    Actor player;
    Actor opponent;
    Vec3 arenaCenter{};
    float arenaHalfX = 1.0f;
    float arenaHalfZ = 1.0f;
    float arenaFloorY = 0.0f;

    float cameraDistance = 1.0f;
    float cameraYawDeg = 45.0f;
    float cameraPitchDeg = 28.0f;
    float panX = 0.0f;
    float panZ = 0.0f;
    bool dragging = false;
    POINT lastMouse{};
    bool keys[256]{};
    ULONGLONG lastTick = 0;
};

App gApp;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsAny(const std::string& value, const std::initializer_list<const char*> tokens) {
    const std::string p = lowerAscii(value);
    for (const char* token : tokens) {
        if (p.find(token) != std::string::npos) return true;
    }
    return false;
}

std::string parentPath(const std::string& value) {
    const auto slash = value.find_last_of('/');
    return slash == std::string::npos ? std::string{} : value.substr(0, slash + 1);
}

std::string leafName(const std::string& value) {
    const auto slash = value.find_last_of("/\\");
    return lowerAscii(value.substr(slash == std::string::npos ? 0 : slash + 1));
}

std::string stemName(const std::string& value) {
    std::string leaf = leafName(value);
    const auto dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return leaf;
}

bool isStageResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    return p.rfind("stage/", 0) == 0 || p.find("/stage/") != std::string::npos;
}

bool isPokemonResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    return p.rfind("pii/", 0) == 0 || p.find("/pii/") != std::string::npos;
}

bool rejectStageGeometry(const std::string& path) {
    const std::string leaf = leafName(path);
    return leaf.rfind("attr", 0) == 0 || leaf.rfind("col_", 0) == 0 ||
           containsAny(leaf, {"collision", "collider", "sky", "skydome", "background",
                              "backdrop", "farbg", "far_", "dummy", "debug"});
}

bool preferredGround(const std::string& path) {
    return containsAny(path, {"ground", "floor", "terrain", "land", "field", "map"});
}

std::string chooseStageAnchor(const std::vector<std::string>& models) {
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos &&
            preferredGround(path) && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && preferredGround(path) && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && !rejectStageGeometry(path)) return path;
    }
    return {};
}

Vec2 materialUv(const Material* material, Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - .5f;
    const float y = uv.y - .5f;
    return {(x * c - y * s + .5f) * material->uvScaleU - material->uvTranslateU,
            (x * s + y * c + .5f) * material->uvScaleV - material->uvTranslateV};
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void writeU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > data.size()) return;
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::string bchString(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (!offset || offset >= data.size()) return {};
    std::size_t end = offset;
    while (end < data.size() && data[end] != 0) ++end;
    return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
}

std::vector<MaterialTextureNames> extractMaterialTextureNames(const std::vector<std::uint8_t>& raw) {
    std::vector<MaterialTextureNames> out;
    if (raw.size() < 0x40 || raw[0] != 'B' || raw[1] != 'C' || raw[2] != 'H') return out;

    auto data = raw;
    const std::uint8_t bc = data[4];
    const std::uint32_t mainOffset = readU32(data, 8);
    const std::uint32_t stringOffset = readU32(data, 12);
    const std::uint32_t relocationOffset = readU32(data, bc > 0x20 ? 28 : 24);
    const std::uint32_t relocationLength = readU32(data, bc > 0x20 ? 52 : 44);

    const std::uint64_t relocationEnd = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(relocationOffset) + relocationLength, data.size());
    for (std::uint32_t o = relocationOffset; static_cast<std::uint64_t>(o) + 4 <= relocationEnd; o += 4) {
        const std::uint32_t value = readU32(data, o);
        const std::uint32_t offset = value & 0x1ffffffu;
        const std::uint32_t flags = value >> 25;
        if (flags == 0) {
            const std::size_t patch = static_cast<std::size_t>(offset) * 4u + mainOffset;
            if (patch + 4 <= data.size()) writeU32(data, patch, readU32(data, patch) + mainOffset);
        } else if (flags == 1) {
            const std::size_t patch = static_cast<std::size_t>(offset) + mainOffset;
            if (patch + 4 <= data.size()) writeU32(data, patch, readU32(data, patch) + stringOffset);
        }
    }

    if (static_cast<std::size_t>(mainOffset) + 8 > data.size()) return out;
    const std::uint32_t modelTable = readU32(data, mainOffset);
    const std::uint32_t modelCount = readU32(data, mainOffset + 4);
    if (!modelTable || !modelCount || static_cast<std::size_t>(modelTable) + 4 > data.size()) return out;
    const std::uint32_t modelHeader = readU32(data, modelTable);
    if (!modelHeader || static_cast<std::size_t>(modelHeader) + 64 > data.size()) return out;

    const std::size_t materialsHeader = static_cast<std::size_t>(modelHeader) + 4u + 48u;
    const std::uint32_t materialsOffset = readU32(data, materialsHeader);
    const std::uint32_t materialCount = std::min<std::uint32_t>(readU32(data, materialsHeader + 4), 4096u);
    const std::size_t stride = bc < 0x21 ? 0x58u : 0x2cu;
    const std::size_t name0Offset = bc < 0x21 ? 0x48u : 0x1cu;

    out.reserve(materialCount);
    for (std::uint32_t i = 0; i < materialCount; ++i) {
        const std::size_t materialOffset = static_cast<std::size_t>(materialsOffset) + static_cast<std::size_t>(i) * stride;
        if (materialOffset + stride > data.size()) break;
        MaterialTextureNames names{};
        for (std::size_t slot = 0; slot < names.size(); ++slot) {
            names[slot] = bchString(data, readU32(data, materialOffset + name0Offset + slot * 4u));
        }
        out.push_back(std::move(names));
    }
    return out;
}

bool computeSceneBounds(const Scene& scene, Vec3& minOut, Vec3& maxOut) {
    minOut = {FLT_MAX, FLT_MAX, FLT_MAX};
    maxOut = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    const auto pose = scene.samplePose(nullptr, 0.0f);
    for (const auto& mesh : scene.meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
            minOut.x = std::min(minOut.x, p.x);
            minOut.y = std::min(minOut.y, p.y);
            minOut.z = std::min(minOut.z, p.z);
            maxOut.x = std::max(maxOut.x, p.x);
            maxOut.y = std::max(maxOut.y, p.y);
            maxOut.z = std::max(maxOut.z, p.z);
            found = true;
        }
    }
    return found;
}

bool computeRobustSceneBounds(const Scene& scene, Vec3& minOut, Vec3& maxOut) {
    std::vector<float> xs, ys, zs;
    const auto pose = scene.samplePose(nullptr, 0.0f);
    for (const auto& mesh : scene.meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
            xs.push_back(p.x);
            ys.push_back(p.y);
            zs.push_back(p.z);
        }
    }
    if (xs.empty()) return false;
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    std::sort(zs.begin(), zs.end());

    const std::size_t trim = xs.size() >= 100 ? xs.size() / 50 : 0;
    const std::size_t hi = xs.size() - 1 - trim;
    minOut = {xs[trim], ys[trim], zs[trim]};
    maxOut = {xs[hi], ys[hi], zs[hi]};
    return maxOut.x > minOut.x && maxOut.y > minOut.y && maxOut.z > minOut.z;
}

void destroyTexturePool(TexturePool& pool) {
    for (GLuint id : pool.owned) if (id) glDeleteTextures(1, &id);
    pool.owned.clear();
    pool.aliases.clear();
}

void registerAliases(TexturePool& pool, const std::string& key, GLuint id) {
    const std::string exact = lowerAscii(key);
    if (exact.empty()) return;
    pool.aliases.emplace(exact, id);
    pool.aliases.emplace(leafName(exact), id);
    pool.aliases.emplace(stemName(exact), id);
}

void uploadRgba(TexturePool& pool, const std::string& key, int width, int height,
                const std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) return;
    if (pool.aliases.find(lowerAscii(key)) != pool.aliases.end()) return;

    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return;
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    pool.owned.push_back(id);
    registerAliases(pool, key, id);
}

void uploadSceneTextures(TexturePool& pool, const Scene& scene) {
    for (const Texture& texture : scene.textures()) {
        uploadRgba(pool, texture.name, texture.width, texture.height, texture.rgba);
    }
}

void loadExternalTexturesForStage(TexturePool& pool) {
    for (const auto& resource : gApp.assets.resources()) {
        if (!isStageResource(resource)) continue;
        try {
            const auto bytes = gApp.assets.extractResource(resource);
            if (!promptemon::ctr::isBclim(bytes)) continue;
            const auto image = promptemon::ctr::decodeBclim(bytes);
            if (image.valid()) uploadRgba(pool, resource, image.width, image.height, image.rgba);
        } catch (...) {
        }
    }
}

void loadExternalTexturesForPokemon(const std::string& modelPath, TexturePool& pool) {
    const std::string directory = parentPath(lowerAscii(modelPath));
    for (const auto& resource : gApp.assets.resources()) {
        const std::string p = lowerAscii(resource);
        if (!isPokemonResource(resource) || p.rfind(directory, 0) != 0) continue;
        try {
            const auto bytes = gApp.assets.extractResource(resource);
            if (!promptemon::ctr::isBclim(bytes)) continue;
            const auto image = promptemon::ctr::decodeBclim(bytes);
            if (image.valid()) uploadRgba(pool, resource, image.width, image.height, image.rgba);
        } catch (...) {
        }
    }
}

GLuint lookupTexture(const TexturePool& pool, const std::string& requested) {
    if (requested.empty()) return 0;
    const std::string exact = lowerAscii(requested);
    const std::string leaf = leafName(exact);
    const std::string stem = stemName(exact);

    auto it = pool.aliases.find(exact);
    if (it != pool.aliases.end()) return it->second;
    it = pool.aliases.find(leaf);
    if (it != pool.aliases.end()) return it->second;
    it = pool.aliases.find(stem);
    if (it != pool.aliases.end()) return it->second;

    if (stem.size() >= 4) {
        for (const auto& entry : pool.aliases) {
            const std::string other = stemName(entry.first);
            if (other.size() >= 4 && (other.find(stem) != std::string::npos || stem.find(other) != std::string::npos))
                return entry.second;
        }
    }
    return 0;
}

void destroyActor(Actor& actor) {
    destroyTexturePool(actor.textures);
    actor.scene.reset();
    actor.materialTextures.clear();
    actor.path.clear();
}

void destroyStage() {
    destroyTexturePool(gApp.stageTextures);
    gApp.parts.clear();
}

bool loadStage() {
    const auto models = gApp.assets.modelResources();
    const std::string anchor = chooseStageAnchor(models);
    if (anchor.empty()) return false;
    const std::string directory = parentPath(lowerAscii(anchor));

    destroyStage();
    gApp.stageMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    gApp.stageMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    std::vector<std::string> candidates;
    for (const auto& path : models) {
        if (lowerAscii(path).rfind(directory, 0) == 0) candidates.push_back(path);
    }
    if (candidates.empty()) candidates.push_back(anchor);

    std::stable_sort(candidates.begin(), candidates.end(), [&](const std::string& a, const std::string& b) {
        if (a == anchor) return true;
        if (b == anchor) return false;
        return preferredGround(a) && !preferredGround(b);
    });

    bool haveAnchor = false;
    bool haveGeometry = false;
    float anchorX = 0.0f, anchorZ = 0.0f;
    Vec3 anchorCenter{};

    for (const auto& path : candidates) {
        try {
            const auto raw = gApp.assets.extractBch(path);
            auto scene = std::make_unique<Scene>(Scene::parse(raw));
            uploadSceneTextures(gApp.stageTextures, *scene);
            if (scene->meshes().empty() || rejectStageGeometry(path)) continue;

            StagePart part;
            part.path = path;
            part.materialTextures = extractMaterialTextureNames(raw);
            part.scene = std::move(scene);
            if (!computeSceneBounds(*part.scene, part.min, part.max)) continue;

            const float sx = std::max(.001f, part.max.x - part.min.x);
            const float sz = std::max(.001f, part.max.z - part.min.z);
            const Vec3 center{(part.min.x + part.max.x) * .5f,
                              (part.min.y + part.max.y) * .5f,
                              (part.min.z + part.max.z) * .5f};

            if (!haveAnchor) {
                anchorX = sx;
                anchorZ = sz;
                anchorCenter = center;
                haveAnchor = true;
            } else {
                const float dx = std::abs(center.x - anchorCenter.x);
                const float dz = std::abs(center.z - anchorCenter.z);
                if (sx > anchorX * 4.0f || sz > anchorZ * 4.0f || dx > anchorX * 2.5f || dz > anchorZ * 2.5f)
                    continue;
            }

            gApp.stageMin.x = std::min(gApp.stageMin.x, part.min.x);
            gApp.stageMin.y = std::min(gApp.stageMin.y, part.min.y);
            gApp.stageMin.z = std::min(gApp.stageMin.z, part.min.z);
            gApp.stageMax.x = std::max(gApp.stageMax.x, part.max.x);
            gApp.stageMax.y = std::max(gApp.stageMax.y, part.max.y);
            gApp.stageMax.z = std::max(gApp.stageMax.z, part.max.z);
            gApp.parts.push_back(std::move(part));
            haveGeometry = true;
        } catch (...) {
        }
    }

    loadExternalTexturesForStage(gApp.stageTextures);
    return haveGeometry;
}

void computeArena() {
    const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
    const float sy = std::max(.001f, gApp.stageMax.y - gApp.stageMin.y);
    const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
    const float cx = (gApp.stageMin.x + gApp.stageMax.x) * .5f;
    const float cz = (gApp.stageMin.z + gApp.stageMax.z) * .5f;

    gApp.arenaHalfX = sx * .24f;
    gApp.arenaHalfZ = sz * .24f;
    gApp.arenaCenter = {cx, 0.0f, cz};

    const float quantum = std::max(.0001f, sy * .001f);
    struct Bucket { int count = 0; double sum = 0.0; };
    std::unordered_map<long long, Bucket> buckets;

    for (const auto& part : gApp.parts) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                if (std::abs(p.x - cx) > gApp.arenaHalfX * .86f || std::abs(p.z - cz) > gApp.arenaHalfZ * .86f) continue;
                const long long key = static_cast<long long>(std::llround(p.y / quantum));
                auto& bucket = buckets[key];
                ++bucket.count;
                bucket.sum += p.y;
            }
        }
    }

    int bestCount = 0;
    float bestY = gApp.stageMin.y;
    for (const auto& entry : buckets) {
        if (entry.second.count > bestCount) {
            bestCount = entry.second.count;
            bestY = static_cast<float>(entry.second.sum / entry.second.count);
        }
    }
    gApp.arenaFloorY = bestY;
    gApp.arenaCenter.y = bestY;
}

std::string findCanonicalPokemon(const std::vector<std::string>& models,
                                 const std::initializer_list<const char*> suffixes,
                                 const std::initializer_list<const char*> tokens) {
    for (const char* suffixRaw : suffixes) {
        const std::string suffix = lowerAscii(suffixRaw);
        for (const auto& path : models) {
            const std::string p = lowerAscii(path);
            if (isPokemonResource(path) && endsWith(p, suffix)) return path;
        }
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (!isPokemonResource(path)) continue;
        for (const char* token : tokens) if (p.find(token) != std::string::npos) return path;
    }
    return {};
}

bool loadActor(Actor& actor,
               const std::initializer_list<const char*> suffixes,
               const std::initializer_list<const char*> tokens) {
    destroyActor(actor);
    const auto models = gApp.assets.modelResources();
    const std::string modelPath = findCanonicalPokemon(models, suffixes, tokens);
    if (modelPath.empty()) return false;

    try {
        const auto raw = gApp.assets.extractBch(modelPath);
        actor.path = modelPath;
        actor.materialTextures = extractMaterialTextureNames(raw);
        actor.scene = std::make_unique<Scene>(Scene::parse(raw));
        if (!actor.scene || actor.scene->meshes().empty()) return false;
        if (!computeRobustSceneBounds(*actor.scene, actor.min, actor.max) &&
            !computeSceneBounds(*actor.scene, actor.min, actor.max)) return false;
        uploadSceneTextures(actor.textures, *actor.scene);

        const std::string directory = parentPath(lowerAscii(modelPath));
        for (const auto& path : models) {
            if (lowerAscii(path).rfind(directory, 0) != 0 || path == modelPath) continue;
            try {
                Scene companion = Scene::parse(gApp.assets.extractBch(path));
                uploadSceneTextures(actor.textures, companion);
            } catch (...) {
            }
        }
        loadExternalTexturesForPokemon(modelPath, actor.textures);
        return true;
    } catch (...) {
        destroyActor(actor);
        return false;
    }
}

void placeActors() {
    const float arenaWidth = std::max(.001f, gApp.arenaHalfX * 2.0f);
    const float arenaDepth = std::max(.001f, gApp.arenaHalfZ * 2.0f);
    const float targetHeight = std::min(arenaWidth, arenaDepth) * .16f;

    auto place = [&](Actor& actor, float side, float yaw) {
        const float height = std::max(.001f, actor.max.y - actor.min.y);
        actor.scale = targetHeight / height;
        actor.position = {gApp.arenaCenter.x + side * gApp.arenaHalfX * .36f,
                          gApp.arenaFloorY + targetHeight * .02f,
                          gApp.arenaCenter.z};
        actor.yawDeg = yaw;
    };
    place(gApp.player, -1.0f, 90.0f);
    place(gApp.opponent, 1.0f, -90.0f);
}

GLuint materialTextureId(const Scene& scene,
                         const TexturePool& pool,
                         const std::vector<MaterialTextureNames>& bindings,
                         std::uint16_t materialId) {
    if (materialId < bindings.size()) {
        for (const auto& name : bindings[materialId]) {
            if (const GLuint id = lookupTexture(pool, name)) return id;
        }
    }
    if (materialId < scene.materials().size()) {
        const Material& material = scene.materials()[materialId];
        if (const GLuint id = lookupTexture(pool, material.textureName)) return id;
    }
    if (const auto* texture = scene.textureForMaterial(materialId)) {
        if (const GLuint id = lookupTexture(pool, texture->name)) return id;
    }
    return 0;
}

void drawScene(const Scene& scene,
               const TexturePool& pool,
               const std::vector<MaterialTextureNames>& bindings) {
    const auto pose = scene.samplePose(nullptr, 0.0f);
    for (const auto& mesh : scene.meshes()) {
        const Material* material = mesh.materialId < scene.materials().size() ? &scene.materials()[mesh.materialId] : nullptr;
        const GLuint textureId = materialTextureId(scene, pool, bindings, mesh.materialId);
        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.58f, .58f, .58f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (textureId) {
                const auto uv = materialUv(material, vertex.uv);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void drawActor(const Actor& actor) {
    if (!actor.scene) return;
    const float cx = (actor.min.x + actor.max.x) * .5f;
    const float cz = (actor.min.z + actor.max.z) * .5f;
    glPushMatrix();
    glTranslatef(actor.position.x, actor.position.y, actor.position.z);
    glRotatef(actor.yawDeg, 0, 1, 0);
    glScalef(actor.scale, actor.scale, actor.scale);
    glTranslatef(-cx, -actor.min.y, -cz);
    drawScene(*actor.scene, actor.textures, actor.materialTextures);
    glPopMatrix();
}

float arenaSpan() {
    return std::max(.001f, std::max(gApp.arenaHalfX * 2.0f, gApp.arenaHalfZ * 2.0f));
}

void resetView() {
    gApp.cameraDistance = arenaSpan() * 1.45f;
    gApp.cameraYawDeg = 45.0f;
    gApp.cameraPitchDeg = 27.0f;
    gApp.panX = 0.0f;
    gApp.panZ = 0.0f;
}

void updateContinuous(float dt) {
    bool changed = false;
    const float span = arenaSpan();
    const float yaw = gApp.cameraYawDeg * kPi / 180.0f;
    const Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const Vec3 forward{-std::sin(yaw), 0.0f, -std::cos(yaw)};

    float camR = 0.0f, camF = 0.0f;
    if (gApp.keys['A']) camR -= 1.0f;
    if (gApp.keys['D']) camR += 1.0f;
    if (gApp.keys['W']) camF += 1.0f;
    if (gApp.keys['S']) camF -= 1.0f;
    if (camR != 0.0f || camF != 0.0f) {
        const float speed = span * .75f * std::max(.45f, gApp.cameraDistance / (span * 1.45f));
        gApp.panX += (right.x * camR + forward.x * camF) * speed * dt;
        gApp.panZ += (right.z * camR + forward.z * camF) * speed * dt;
        const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
        const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
        gApp.panX = std::clamp(gApp.panX, -sx * .45f, sx * .45f);
        gApp.panZ = std::clamp(gApp.panZ, -sz * .45f, sz * .45f);
        changed = true;
    }

    float actorR = 0.0f, actorF = 0.0f;
    if (gApp.keys[VK_LEFT]) actorR -= 1.0f;
    if (gApp.keys[VK_RIGHT]) actorR += 1.0f;
    if (gApp.keys[VK_UP]) actorF += 1.0f;
    if (gApp.keys[VK_DOWN]) actorF -= 1.0f;
    if ((actorR != 0.0f || actorF != 0.0f) && gApp.player.scene) {
        const float speed = span * .34f;
        const float dx = right.x * actorR + forward.x * actorF;
        const float dz = right.z * actorR + forward.z * actorF;
        gApp.player.position.x += dx * speed * dt;
        gApp.player.position.z += dz * speed * dt;
        gApp.player.position.x = std::clamp(gApp.player.position.x,
            gApp.arenaCenter.x - gApp.arenaHalfX * .84f,
            gApp.arenaCenter.x + gApp.arenaHalfX * .84f);
        gApp.player.position.z = std::clamp(gApp.player.position.z,
            gApp.arenaCenter.z - gApp.arenaHalfZ * .84f,
            gApp.arenaCenter.z + gApp.arenaHalfZ * .84f);
        gApp.player.yawDeg = std::atan2(dx, dz) * 180.0f / kPi;
        changed = true;
    }

    if (changed) InvalidateRect(gApp.hwnd, nullptr, FALSE);
}

void render() {
    if (!gApp.hwnd || !gApp.gl) return;
    wglMakeCurrent(gApp.dc, gApp.gl);

    RECT rc{};
    GetClientRect(gApp.hwnd, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(1L, rc.bottom - rc.top);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);

    glViewport(0, 0, width, height);
    glClearColor(.55f, .70f, .80f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float stageSpan = std::max({
        std::max(.001f, gApp.stageMax.x - gApp.stageMin.x),
        std::max(.001f, gApp.stageMax.y - gApp.stageMin.y),
        std::max(.001f, gApp.stageMax.z - gApp.stageMin.z)
    });
    const float nearPlane = std::max(.001f, arenaSpan() * .002f);
    const float farPlane = std::max(stageSpan * 30.0f, gApp.cameraDistance * 30.0f);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(58.0, aspect, nearPlane, farPlane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const float yaw = gApp.cameraYawDeg * kPi / 180.0f;
    const float pitch = gApp.cameraPitchDeg * kPi / 180.0f;
    const float cp = std::cos(pitch);
    const float targetLift = std::min(gApp.arenaHalfX, gApp.arenaHalfZ) * .16f;
    const Vec3 target{
        gApp.arenaCenter.x + gApp.panX,
        gApp.arenaFloorY + targetLift,
        gApp.arenaCenter.z + gApp.panZ
    };
    const Vec3 eye{
        target.x + std::sin(yaw) * cp * gApp.cameraDistance,
        target.y + std::sin(pitch) * gApp.cameraDistance,
        target.z + std::cos(yaw) * cp * gApp.cameraDistance
    };

    gluLookAt(eye.x, eye.y, eye.z,
              target.x, target.y, target.z,
              0.0, 1.0, 0.0);

    for (auto& part : gApp.parts) {
        if (part.scene) drawScene(*part.scene, gApp.stageTextures, part.materialTextures);
    }
    drawActor(gApp.player);
    drawActor(gApp.opponent);

    SwapBuffers(gApp.dc);
}

bool initOpenGL(HWND hwnd) {
    gApp.dc = GetDC(hwnd);
    if (!gApp.dc) return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(gApp.dc, &pfd);
    if (!format || !SetPixelFormat(gApp.dc, format, &pfd)) return false;
    gApp.gl = wglCreateContext(gApp.dc);
    return gApp.gl && wglMakeCurrent(gApp.dc, gApp.gl);
}

void shutdownOpenGL() {
    if (gApp.gl) {
        wglMakeCurrent(gApp.dc, gApp.gl);
        destroyActor(gApp.player);
        destroyActor(gApp.opponent);
        destroyStage();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gApp.gl);
        gApp.gl = nullptr;
    }
    if (gApp.dc && gApp.hwnd) {
        ReleaseDC(gApp.hwnd, gApp.dc);
        gApp.dc = nullptr;
    }
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || len >= buffer.size()) return {};
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
}

int mouseX(LPARAM lParam) { return static_cast<int>(static_cast<short>(LOWORD(lParam))); }
int mouseY(LPARAM lParam) { return static_cast<int>(static_cast<short>(HIWORD(lParam))); }

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            gApp.hwnd = hwnd;
            if (!initOpenGL(hwnd)) return -1;
            const auto bundle = executableDirectory() / L"assets" / L"resources-tx";
            if (!gApp.assets.openInstalledDirectory(bundle)) {
                MessageBoxW(hwnd, L"No se pudo abrir assets\\resources-tx.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!loadStage()) {
                MessageBoxW(hwnd, L"El bundle no contiene un stage BCH utilizable.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            computeArena();
            if (!loadActor(gApp.player, {"/koratta.bch", "/koratta_f.bch"}, {"koratta", "rattata"}) ||
                !loadActor(gApp.opponent, {"/poppo.bch"}, {"poppo", "pidgey"})) {
                MessageBoxW(hwnd, L"No se pudieron cargar los Pokemon Rumble requeridos para la arena.",
                            L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            placeActors();
            resetView();
            gApp.lastTick = GetTickCount64();
            SetTimer(hwnd, kUpdateTimer, 16, nullptr);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kUpdateTimer) {
                const ULONGLONG now = GetTickCount64();
                const float dt = std::clamp(static_cast<float>(now - gApp.lastTick) / 1000.0f, 0.0f, .05f);
                gApp.lastTick = now;
                updateContinuous(dt);
            }
            return 0;
        case WM_SIZE:
            render();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            gApp.dragging = true;
            gApp.lastMouse = {mouseX(lParam), mouseY(lParam)};
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (gApp.dragging) {
                const POINT now{mouseX(lParam), mouseY(lParam)};
                const int dx = now.x - gApp.lastMouse.x;
                const int dy = now.y - gApp.lastMouse.y;
                gApp.lastMouse = now;
                gApp.cameraYawDeg += static_cast<float>(dx) * .35f;
                gApp.cameraPitchDeg = std::clamp(gApp.cameraPitchDeg - static_cast<float>(dy) * .28f, 4.0f, 76.0f);
                render();
            }
            return 0;
        case WM_LBUTTONUP:
            gApp.dragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            gApp.dragging = false;
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float span = arenaSpan();
            const float minDistance = std::max(.01f, span * .18f);
            const float maxDistance = std::max(span * 10.0f,
                std::max(gApp.stageMax.x - gApp.stageMin.x, gApp.stageMax.z - gApp.stageMin.z) * 3.0f);
            gApp.cameraDistance = std::clamp(gApp.cameraDistance * (delta > 0 ? .82f : 1.22f), minDistance, maxDistance);
            render();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam < 256) gApp.keys[wParam] = true;
            if (wParam == 'R') { resetView(); render(); }
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_KEYUP:
            if (wParam < 256) gApp.keys[wParam] = false;
            return 0;
        case WM_KILLFOCUS:
            std::fill_n(gApp.keys, 256, false);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kUpdateTimer);
            shutdownOpenGL();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSW wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"PromptemonStageScene";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Promptemon Steidium",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
