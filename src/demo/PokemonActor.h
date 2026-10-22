#pragma once

#include "demo/CameraController.h"
#include "demo/SceneRenderSupport.h"

#include <memory>
#include <string>
#include <vector>

namespace promptemon::demo {

class PokemonActor {
public:
    bool load(storage::RomAssetCache& assets,
              const std::vector<std::string>& preferredSuffixes,
              const std::vector<std::string>& tokens);
    void destroy();

    void place(const ArenaView& arena, float side, float yawDegrees);
    bool update(float dt);
    bool move(float rightAmount, float forwardAmount, float cameraYawDegrees, float dt, const ArenaView& arena);
    void draw() const;

    void setVerticalOffset(float offset) { verticalOffset_ = offset; }
    float nativeHeight() const { return scene_ ? (max_.y - min_.y) : 0.0f; }

    bool ready() const { return scene_ != nullptr; }
    bool hasAnimations() const { return scene_ && !scene_->animations().empty(); }
    const std::string& path() const { return path_; }
    std::size_t animationCount() const { return scene_ ? scene_->animations().size() : 0; }

private:
    void configureAnimationCycle();
    void switchToIdle();
    void switchToNextAlternate();
    const bch::Animation* currentAnimation() const;

    std::string path_;
    std::unique_ptr<bch::Scene> scene_;
    std::vector<MaterialTextureNames> materialTextures_;
    TexturePool textures_;
    bch::Vec3 min_{};
    bch::Vec3 max_{};
    bch::Vec3 position_{};
    float yawDeg_ = 0.0f;
    float verticalOffset_ = 0.0f;

    std::size_t idleIndex_ = 0;
    std::vector<std::size_t> alternates_;
    std::size_t nextAlternate_ = 0;
    std::size_t currentIndex_ = 0;
    float frame_ = 0.0f;
    float stateTime_ = 0.0f;
    bool playingIdle_ = true;
};

} // namespace promptemon::demo
