#include "demo/StageWorld.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

namespace promptemon::demo {
namespace {

bool containsAny(const std::string& value, const std::initializer_list<const char*> tokens) {
    const std::string p = lowerAscii(value);
    for (const char* token : tokens) if (p.find(token) != std::string::npos) return true;
    return false;
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
        if (isStageResource(path) && p.find("n001") != std::string::npos && preferredGround(path) && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) if (isStageResource(path) && preferredGround(path) && !rejectStageGeometry(path)) return path;
    for (const auto& path : models) if (isStageResource(path) && !rejectStageGeometry(path)) return path;
    return {};
}

} // namespace

bool StageWorld::load(storage::RomAssetCache& assets) {
    destroy();
    const auto models = assets.modelResources();
    const std::string anchor = chooseStageAnchor(models);
    if (anchor.empty()) return false;
    const std::string directory = parentPath(lowerAscii(anchor));

    min_ = {FLT_MAX, FLT_MAX, FLT_MAX};
    max_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    std::vector<std::string> candidates;
    for (const auto& path : models) if (lowerAscii(path).rfind(directory, 0) == 0) candidates.push_back(path);
    if (candidates.empty()) candidates.push_back(anchor);

    std::stable_sort(candidates.begin(), candidates.end(), [&](const std::string& a, const std::string& b) {
        if (a == anchor) return true;
        if (b == anchor) return false;
        return preferredGround(a) && !preferredGround(b);
    });

    bool haveAnchor = false;
    float anchorX = 0.0f, anchorZ = 0.0f;
    bch::Vec3 anchorCenter{};

    for (const auto& path : candidates) {
        try {
            const auto raw = assets.extractBch(path);
            auto scene = std::make_unique<bch::Scene>(bch::Scene::parse(raw));
            uploadSceneTextures(textures_, *scene);
            if (scene->meshes().empty() || rejectStageGeometry(path)) continue;

            Part part;
            part.path = path;
            part.materialTextures = extractMaterialTextureNames(raw);
            part.scene = std::move(scene);
            if (!computeSceneBounds(*part.scene, part.min, part.max, false)) continue;

            const float sx = std::max(.001f, part.max.x - part.min.x);
            const float sz = std::max(.001f, part.max.z - part.min.z);
            const bch::Vec3 center{(part.min.x + part.max.x) * .5f,
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
                if (sx > anchorX * 4.0f || sz > anchorZ * 4.0f || dx > anchorX * 2.5f || dz > anchorZ * 2.5f) continue;
            }

            min_.x = std::min(min_.x, part.min.x);
            min_.y = std::min(min_.y, part.min.y);
            min_.z = std::min(min_.z, part.min.z);
            max_.x = std::max(max_.x, part.max.x);
            max_.y = std::max(max_.y, part.max.y);
            max_.z = std::max(max_.z, part.max.z);
            parts_.push_back(std::move(part));
        } catch (...) {
        }
    }

    if (parts_.empty()) return false;
    loadExternalTextures(assets, textures_, isStageResource);
    computeArena();
    return true;
}

void StageWorld::destroy() {
    destroyTexturePool(textures_);
    parts_.clear();
}

void StageWorld::draw() const {
    for (const auto& part : parts_) {
        if (part.scene) drawScene(*part.scene, textures_, part.materialTextures);
    }
}

void StageWorld::computeArena() {
    const float sx = std::max(.001f, max_.x - min_.x);
    const float sy = std::max(.001f, max_.y - min_.y);
    const float sz = std::max(.001f, max_.z - min_.z);
    const float cx = (min_.x + max_.x) * .5f;
    const float cz = (min_.z + max_.z) * .5f;

    arena_.halfX = sx * .24f;
    arena_.halfZ = sz * .24f;
    arena_.center = {cx, 0.0f, cz};

    const float quantum = std::max(.0001f, sy * .001f);
    struct Bucket { int count = 0; double sum = 0.0; };
    std::unordered_map<long long, Bucket> buckets;

    for (const auto& part : parts_) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                if (std::abs(p.x - cx) > arena_.halfX * .86f || std::abs(p.z - cz) > arena_.halfZ * .86f) continue;
                const long long key = static_cast<long long>(std::llround(p.y / quantum));
                auto& bucket = buckets[key];
                ++bucket.count;
                bucket.sum += p.y;
            }
        }
    }

    int bestCount = 0;
    float bestY = min_.y;
    for (const auto& entry : buckets) {
        if (entry.second.count > bestCount) {
            bestCount = entry.second.count;
            bestY = static_cast<float>(entry.second.sum / entry.second.count);
        }
    }
    arena_.floorY = bestY;
    arena_.center.y = bestY;
}

} // namespace promptemon::demo
