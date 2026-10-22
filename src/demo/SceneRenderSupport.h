#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "bch/BchScene.h"
#include "storage/RomAssetCache.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::demo {

using MaterialTextureNames = std::array<std::string, 3>;

struct TexturePool {
    std::vector<GLuint> owned;
    std::unordered_map<std::string, GLuint> aliases;
};

std::string lowerAscii(std::string value);
std::string parentPath(const std::string& value);
std::string leafName(const std::string& value);
std::string stemName(const std::string& value);
bool isStageResource(const std::string& path);
bool isPokemonResource(const std::string& path);

std::vector<MaterialTextureNames> extractMaterialTextureNames(const std::vector<std::uint8_t>& raw);

bool computeSceneBounds(const bch::Scene& scene, bch::Vec3& minOut, bch::Vec3& maxOut, bool robust = false);

void destroyTexturePool(TexturePool& pool);
void uploadSceneTextures(TexturePool& pool, const bch::Scene& scene);
void loadExternalTextures(storage::RomAssetCache& assets,
                          TexturePool& pool,
                          bool (*accept)(const std::string&),
                          const std::string& requiredPrefix = {});
GLuint lookupTexture(const TexturePool& pool, const std::string& requested);

void drawScene(const bch::Scene& scene,
               const TexturePool& pool,
               const std::vector<MaterialTextureNames>& bindings,
               const bch::Animation* animation = nullptr,
               float frame = 0.0f);

} // namespace promptemon::demo
