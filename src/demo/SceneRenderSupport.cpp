#include "demo/SceneRenderSupport.h"

#include "ctr/Bclim.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace promptemon::demo {
namespace {

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
    while (end < data.size() && data[end]) ++end;
    return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
}

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - .5f;
    const float y = uv.y - .5f;
    return {(x * c - y * s + .5f) * material->uvScaleU - material->uvTranslateU,
            (x * s + y * c + .5f) * material->uvScaleV - material->uvTranslateV};
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

GLuint materialTextureId(const bch::Scene& scene,
                         const TexturePool& pool,
                         const std::vector<MaterialTextureNames>& bindings,
                         std::uint16_t materialId) {
    if (materialId < bindings.size()) {
        for (const auto& name : bindings[materialId]) {
            if (const GLuint id = lookupTexture(pool, name)) return id;
        }
    }
    if (materialId < scene.materials().size()) {
        if (const GLuint id = lookupTexture(pool, scene.materials()[materialId].textureName)) return id;
    }
    if (const auto* texture = scene.textureForMaterial(materialId)) {
        if (const GLuint id = lookupTexture(pool, texture->name)) return id;
    }
    return 0;
}

} // namespace

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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
        for (std::size_t slot = 0; slot < names.size(); ++slot)
            names[slot] = bchString(data, readU32(data, materialOffset + name0Offset + slot * 4u));
        out.push_back(std::move(names));
    }
    return out;
}

bool computeSceneBounds(const bch::Scene& scene, bch::Vec3& minOut, bch::Vec3& maxOut, bool robust) {
    std::vector<float> xs, ys, zs;
    minOut = {FLT_MAX, FLT_MAX, FLT_MAX};
    maxOut = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const auto pose = scene.samplePose(nullptr, 0.0f);

    for (const auto& mesh : scene.meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
            if (robust) {
                xs.push_back(p.x); ys.push_back(p.y); zs.push_back(p.z);
            } else {
                minOut.x = std::min(minOut.x, p.x); minOut.y = std::min(minOut.y, p.y); minOut.z = std::min(minOut.z, p.z);
                maxOut.x = std::max(maxOut.x, p.x); maxOut.y = std::max(maxOut.y, p.y); maxOut.z = std::max(maxOut.z, p.z);
            }
        }
    }

    if (!robust) return minOut.x != FLT_MAX;
    if (xs.empty()) return false;
    std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end()); std::sort(zs.begin(), zs.end());
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

void uploadSceneTextures(TexturePool& pool, const bch::Scene& scene) {
    for (const auto& texture : scene.textures())
        uploadRgba(pool, texture.name, texture.width, texture.height, texture.rgba);
}

void loadExternalTextures(storage::RomAssetCache& assets,
                          TexturePool& pool,
                          bool (*accept)(const std::string&),
                          const std::string& requiredPrefix) {
    const std::string prefix = lowerAscii(requiredPrefix);
    for (const auto& resource : assets.resources()) {
        if (accept && !accept(resource)) continue;
        const std::string p = lowerAscii(resource);
        if (!prefix.empty() && p.rfind(prefix, 0) != 0) continue;
        try {
            const auto bytes = assets.extractResource(resource);
            if (!ctr::isBclim(bytes)) continue;
            const auto image = ctr::decodeBclim(bytes);
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
    for (const auto& key : {exact, leaf, stem}) {
        const auto it = pool.aliases.find(key);
        if (it != pool.aliases.end()) return it->second;
    }
    if (stem.size() >= 4) {
        for (const auto& entry : pool.aliases) {
            const std::string other = stemName(entry.first);
            if (other.size() >= 4 && (other.find(stem) != std::string::npos || stem.find(other) != std::string::npos))
                return entry.second;
        }
    }
    return 0;
}

void drawScene(const bch::Scene& scene,
               const TexturePool& pool,
               const std::vector<MaterialTextureNames>& bindings,
               const bch::Animation* animation,
               float frame) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        const bch::Material* material = mesh.materialId < scene.materials().size() ? &scene.materials()[mesh.materialId] : nullptr;
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

} // namespace promptemon::demo
