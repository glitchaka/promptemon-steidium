#pragma once

#include "bch/BchScene.h"

namespace promptemon::demo {

struct ArenaView {
    bch::Vec3 center{};
    float halfX = 1.0f;
    float halfZ = 1.0f;
    float floorY = 0.0f;
};

class CameraController {
public:
    void reset(const ArenaView& arena);
    void drag(float dx, float dy);
    void zoom(int wheelDelta, const ArenaView& arena);
    bool updatePan(float dt,
                   const bool keys[256],
                   const ArenaView& arena,
                   const bch::Vec3& stageMin,
                   const bch::Vec3& stageMax);
    void apply(int width,
               int height,
               const ArenaView& arena,
               const bch::Vec3& stageMin,
               const bch::Vec3& stageMax) const;

    float yawDegrees() const { return yawDeg_; }

private:
    float distance_ = 1.0f;
    float yawDeg_ = 45.0f;
    float pitchDeg_ = 20.0f;
    float panX_ = 0.0f;
    float panZ_ = 0.0f;
};

} // namespace promptemon::demo
