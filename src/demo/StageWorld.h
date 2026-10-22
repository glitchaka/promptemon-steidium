#pragma once

#include "demo/CameraController.h"
#include "demo/SceneRenderSupport.h"

#include <memory>
#include <string>
#include <vector>

namespace promptemon::demo {

class StageWorld {
public:
    bool load(storage::RomAssetCache& assets);
    void destroy();
    void draw() const;

    const ArenaView& arena() const { return arena_; }
    const bch::Vec3& minBounds() const { return min_; }
    const bch::Vec3& maxBounds() const { return max_; }

private:
    struct Part {
        std::string path;
        std::unique_ptr<bch::Scene> scene;
        std::vector<MaterialTextureNames> materialTextures;
        bch::Vec3 min{};
        bch::Vec3 max{};
    };

    void computeArena();

    std::vector<Part> parts_;
    TexturePool textures_;
    bch::Vec3 min_{};
    bch::Vec3 max_{};
    ArenaView arena_{};
};

} // namespace promptemon::demo
