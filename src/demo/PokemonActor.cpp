#include "demo/PokemonActor.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace promptemon::demo {
namespace {
constexpr float kPi = 3.14159265358979323846f;

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isAnimationCandidate(const std::string& path, const std::string& modelPath) {
    const std::string p = lowerAscii(path);
    if (p == lowerAscii(modelPath)) return false;
    if (!(endsWith(p, ".bch") || endsWith(p, ".bch.cx"))) return false;
    return p.rfind(parentPath(lowerAscii(modelPath)), 0) == 0;
}

std::string findModel(storage::RomAssetCache& assets,
                      const std::vector<std::string>& suffixes,
                      const std::vector<std::string>& tokens) {
    const auto models = assets.modelResources();
    for (const auto& suffixRaw : suffixes) {
        const std::string suffix = lowerAscii(suffixRaw);
        for (const auto& path : models) {
            const std::string p = lowerAscii(path);
            if (isPokemonResource(path) && endsWith(p, suffix)) return path;
        }
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (!isPokemonResource(path)) continue;
        for (const auto& tokenRaw : tokens) {
            if (p.find(lowerAscii(tokenRaw)) != std::string::npos) return path;
        }
    }
    return {};
}

bool looksIdle(const std::string& name) {
    const std::string n = lowerAscii(name);
    static const char* tokens[] = {"idle", "wait", "stand", "taiki", "normal", "base"};
    for (const char* token : tokens) if (n.find(token) != std::string::npos) return true;
    return false;
}
}

bool PokemonActor::load(storage::RomAssetCache& assets,
                        const std::vector<std::string>& preferredSuffixes,
                        const std::vector<std::string>& tokens) {
    destroy();
    path_ = findModel(assets, preferredSuffixes, tokens);
    if (path_.empty()) return false;

    try {
        const auto modelRaw = assets.extractBch(path_);
        materialTextures_ = extractMaterialTextureNames(modelRaw);

        std::unique_ptr<bch::Scene> best;
        std::size_t bestAnimations = 0;
        try {
            auto embedded = std::make_unique<bch::Scene>(bch::Scene::parse(modelRaw));
            bestAnimations = embedded->animations().size();
            best = std::move(embedded);
        } catch (...) {
        }

        for (const auto& resource : assets.resources()) {
            if (!isAnimationCandidate(resource, path_)) continue;
            try {
                const auto animationRaw = assets.extractResource(resource);
                auto candidate = std::make_unique<bch::Scene>(bch::Scene::parse(modelRaw, animationRaw));
                if (candidate->animations().size() > bestAnimations) {
                    bestAnimations = candidate->animations().size();
                    best = std::move(candidate);
                }
            } catch (...) {
            }
        }

        if (!best || best->meshes().empty() || bestAnimations == 0) {
            destroy();
            return false;
        }
        scene_ = std::move(best);

        if (!computeSceneBounds(*scene_, min_, max_, true) && !computeSceneBounds(*scene_, min_, max_, false)) {
            destroy();
            return false;
        }

        uploadSceneTextures(textures_, *scene_);
        const std::string directory = parentPath(lowerAscii(path_));
        for (const auto& resource : assets.resources()) {
            const std::string p = lowerAscii(resource);
            if (!isPokemonResource(resource) || p.rfind(directory, 0) != 0) continue;
            if (!(endsWith(p, ".bch") || endsWith(p, ".bch.cx"))) continue;
            try {
                const auto raw = assets.extractResource(resource);
                bch::Scene companion = bch::Scene::parse(raw);
                uploadSceneTextures(textures_, companion);
            } catch (...) {
            }
        }
        loadExternalTextures(assets, textures_, isPokemonResource, directory);
        configureAnimationCycle();
        return true;
    } catch (...) {
        destroy();
        return false;
    }
}

void PokemonActor::destroy() {
    destroyTexturePool(textures_);
    scene_.reset();
    materialTextures_.clear();
    path_.clear();
    alternates_.clear();
    idleIndex_ = currentIndex_ = nextAlternate_ = 0;
    frame_ = stateTime_ = 0.0f;
    playingIdle_ = true;
}

void PokemonActor::configureAnimationCycle() {
    idleIndex_ = 0;
    const auto& clips = scene_->animations();
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (looksIdle(clips[i].name)) {
            idleIndex_ = i;
            break;
        }
    }
    alternates_.clear();
    for (std::size_t i = 0; i < clips.size(); ++i) if (i != idleIndex_) alternates_.push_back(i);
    switchToIdle();
}

void PokemonActor::switchToIdle() {
    currentIndex_ = idleIndex_;
    frame_ = 0.0f;
    stateTime_ = 0.0f;
    playingIdle_ = true;
}

void PokemonActor::switchToNextAlternate() {
    if (alternates_.empty()) {
        switchToIdle();
        return;
    }
    currentIndex_ = alternates_[nextAlternate_ % alternates_.size()];
    nextAlternate_ = (nextAlternate_ + 1) % alternates_.size();
    frame_ = 0.0f;
    stateTime_ = 0.0f;
    playingIdle_ = false;
}

const bch::Animation* PokemonActor::currentAnimation() const {
    if (!scene_ || currentIndex_ >= scene_->animations().size()) return nullptr;
    return &scene_->animations()[currentIndex_];
}

bool PokemonActor::update(float dt) {
    if (!scene_) return false;
    const auto* clip = currentAnimation();
    if (!clip) return false;

    stateTime_ += dt;
    frame_ += dt * 30.0f;
    const float frames = std::max(1.0f, clip->frames);

    if (playingIdle_) {
        if (frame_ >= frames) frame_ = std::fmod(frame_, frames);
        if (stateTime_ >= 3.2f && !alternates_.empty()) switchToNextAlternate();
    } else {
        if (frame_ >= frames || stateTime_ >= std::max(.35f, frames / 30.0f)) switchToIdle();
    }
    return true;
}

void PokemonActor::place(const ArenaView& arena, float side, float yawDegrees) {
    if (!scene_) return;
    const float arenaWidth = std::max(.001f, arena.halfX * 2.0f);
    const float arenaDepth = std::max(.001f, arena.halfZ * 2.0f);
    const float targetHeight = std::min(arenaWidth, arenaDepth) * .18f;
    const float height = std::max(.001f, max_.y - min_.y);
    scale_ = targetHeight / height;
    position_ = {arena.center.x + side * arena.halfX * .36f,
                 arena.floorY + targetHeight * .02f,
                 arena.center.z};
    yawDeg_ = yawDegrees;
}

bool PokemonActor::move(float rightAmount, float forwardAmount, float cameraYawDegrees, float dt, const ArenaView& arena) {
    if (!scene_ || (rightAmount == 0.0f && forwardAmount == 0.0f)) return false;
    const float yaw = cameraYawDegrees * kPi / 180.0f;
    const bch::Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const bch::Vec3 forward{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const float speed = std::max(.001f, std::max(arena.halfX, arena.halfZ) * 1.35f);
    const float dx = right.x * rightAmount + forward.x * forwardAmount;
    const float dz = right.z * rightAmount + forward.z * forwardAmount;
    position_.x += dx * speed * dt;
    position_.z += dz * speed * dt;
    position_.x = std::clamp(position_.x, arena.center.x - arena.halfX * .84f, arena.center.x + arena.halfX * .84f);
    position_.z = std::clamp(position_.z, arena.center.z - arena.halfZ * .84f, arena.center.z + arena.halfZ * .84f);
    yawDeg_ = std::atan2(dx, dz) * 180.0f / kPi;
    return true;
}

void PokemonActor::draw() const {
    if (!scene_) return;
    const float cx = (min_.x + max_.x) * .5f;
    const float cz = (min_.z + max_.z) * .5f;
    glPushMatrix();
    glTranslatef(position_.x, position_.y, position_.z);
    glRotatef(yawDeg_, 0, 1, 0);
    glScalef(scale_, scale_, scale_);
    glTranslatef(-cx, -min_.y, -cz);
    drawScene(*scene_, textures_, materialTextures_, currentAnimation(), frame_);
    glPopMatrix();
}

} // namespace promptemon::demo
