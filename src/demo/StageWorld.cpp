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

struct PlanePatch {
    int triangles = 0;
    double sumY = 0.0;
    double area = 0.0;
    float minX = FLT_MAX;
    float maxX = -FLT_MAX;
    float minZ = FLT_MAX;
    float maxZ = -FLT_MAX;
};

void includePoint(PlanePatch& patch, const bch::Vec3& p) {
    patch.minX = std::min(patch.minX, p.x);
    patch.maxX = std::max(patch.maxX, p.x);
    patch.minZ = std::min(patch.minZ, p.z);
    patch.maxZ = std::max(patch.maxZ, p.z);
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
    const float worldCx = (min_.x + max_.x) * .5f;
    const float worldCz = (min_.z + max_.z) * .5f;
    const float quantum = std::max(.0001f, sy * .003f);

    bool found = false;
    float bestScore = -FLT_MAX;
    PlanePatch bestPatch{};
    float bestFloorY = min_.y;

    for (const auto& part : parts_) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);

        for (const auto& mesh : part.scene->meshes()) {
            std::unordered_map<long long, PlanePatch> planes;
            const auto& vertices = mesh.vertices;

            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
                const bch::Vec3 a = part.scene->skin(vertices[i], pose);
                const bch::Vec3 b = part.scene->skin(vertices[i + 1], pose);
                const bch::Vec3 c = part.scene->skin(vertices[i + 2], pose);

                const float abx = b.x - a.x;
                const float aby = b.y - a.y;
                const float abz = b.z - a.z;
                const float acx = c.x - a.x;
                const float acy = c.y - a.y;
                const float acz = c.z - a.z;

                const float nx = aby * acz - abz * acy;
                const float ny = abz * acx - abx * acz;
                const float nz = abx * acy - aby * acx;
                const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (normalLength <= 1.0e-7f) continue;

                // Arena floor triangles are almost horizontal. Walls, ramps and the
                // exterior shell are deliberately ignored here.
                if (std::abs(ny) / normalLength < .94f) continue;

                const float averageY = (a.y + b.y + c.y) / 3.0f;
                const long long key = static_cast<long long>(std::llround(averageY / quantum));
                auto& patch = planes[key];
                ++patch.triangles;
                patch.sumY += averageY;
                patch.area += normalLength * .5;
                includePoint(patch, a);
                includePoint(patch, b);
                includePoint(patch, c);
            }

            for (const auto& [key, patch] : planes) {
                (void)key;
                if (patch.triangles < 2) continue;

                const float width = patch.maxX - patch.minX;
                const float depth = patch.maxZ - patch.minZ;
                if (width <= 0.0f || depth <= 0.0f) continue;

                const float rx = width / sx;
                const float rz = depth / sz;
                if (rx < .08f || rz < .08f || rx > .70f || rz > .70f) continue;

                const float cx = (patch.minX + patch.maxX) * .5f;
                const float cz = (patch.minZ + patch.maxZ) * .5f;
                const float dx = std::abs(cx - worldCx) / (sx * .5f);
                const float dz = std::abs(cz - worldCz) / (sz * .5f);
                if (dx > .38f || dz > .38f) continue;

                const float distance = std::sqrt(dx * dx + dz * dz);
                const float centrality = 1.0f - std::clamp(distance, 0.0f, 1.0f);
                const float footprint = rx * rz;

                // The playable rectangle is a central inset of the stadium, not the
                // outer deck. Prefer a medium central footprint instead of the largest
                // horizontal surface in the model.
                const float sizeFit = 1.0f - std::min(1.0f, std::abs(footprint - .16f) / .16f);
                const float triangleDensity = std::min(1.0f, static_cast<float>(patch.triangles) / 64.0f);
                const float floorY = static_cast<float>(patch.sumY / patch.triangles);
                const float elevation = std::clamp((floorY - min_.y) / sy, 0.0f, 1.0f);
                const float score = centrality * 8.0f + sizeFit * 4.0f + triangleDensity + elevation * .5f;

                if (!found || score > bestScore) {
                    found = true;
                    bestScore = score;
                    bestPatch = patch;
                    bestFloorY = floorY;
                }
            }
        }
    }

    if (found) {
        const float width = bestPatch.maxX - bestPatch.minX;
        const float depth = bestPatch.maxZ - bestPatch.minZ;
        arena_.center = {(bestPatch.minX + bestPatch.maxX) * .5f,
                         bestFloorY,
                         (bestPatch.minZ + bestPatch.maxZ) * .5f};
        // Keep actors clear of the black rim/walls surrounding the actual floor.
        arena_.halfX = width * .44f;
        arena_.halfZ = depth * .44f;
        arena_.floorY = bestFloorY;
        return;
    }

    // Conservative fallback only if the model does not expose a usable planar mesh.
    arena_.center = {worldCx, min_.y, worldCz};
    arena_.halfX = sx * .12f;
    arena_.halfZ = sz * .12f;
    arena_.floorY = min_.y;
}

} // namespace promptemon::demo
