#include "demo/CameraController.h"

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <algorithm>
#include <cmath>

namespace promptemon::demo {
namespace {
constexpr float kPi = 3.14159265358979323846f;
float arenaSpan(const ArenaView& arena) {
    return std::max(.001f, std::max(arena.halfX * 2.0f, arena.halfZ * 2.0f));
}
}

void CameraController::reset(const ArenaView& arena) {
    const float span = arenaSpan(arena);
    distance_ = span * .72f;
    yawDeg_ = 45.0f;
    pitchDeg_ = 18.0f;
    panX_ = 0.0f;
    panZ_ = 0.0f;
}

void CameraController::drag(float dx, float dy) {
    yawDeg_ += dx * .32f;
    pitchDeg_ = std::clamp(pitchDeg_ + dy * .24f, -8.0f, 52.0f);
}

void CameraController::zoom(int wheelDelta, const ArenaView& arena) {
    const float span = arenaSpan(arena);
    distance_ *= wheelDelta > 0 ? .82f : 1.22f;
    distance_ = std::clamp(distance_, span * .18f, span * 3.0f);
}

bool CameraController::updatePan(float dt,
                                 const bool keys[256],
                                 const ArenaView& arena,
                                 const bch::Vec3& stageMin,
                                 const bch::Vec3& stageMax) {
    float rightAmount = 0.0f;
    float forwardAmount = 0.0f;
    if (keys['A']) rightAmount -= 1.0f;
    if (keys['D']) rightAmount += 1.0f;
    if (keys['W']) forwardAmount += 1.0f;
    if (keys['S']) forwardAmount -= 1.0f;
    if (rightAmount == 0.0f && forwardAmount == 0.0f) return false;

    const float yaw = yawDeg_ * kPi / 180.0f;
    const bch::Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const bch::Vec3 forward{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const float span = arenaSpan(arena);
    const float speed = span * .65f;

    panX_ += (right.x * rightAmount + forward.x * forwardAmount) * speed * dt;
    panZ_ += (right.z * rightAmount + forward.z * forwardAmount) * speed * dt;

    const float sx = std::max(.001f, stageMax.x - stageMin.x);
    const float sz = std::max(.001f, stageMax.z - stageMin.z);
    panX_ = std::clamp(panX_, -sx * .45f, sx * .45f);
    panZ_ = std::clamp(panZ_, -sz * .45f, sz * .45f);
    return true;
}

void CameraController::apply(int width,
                             int height,
                             const ArenaView& arena,
                             const bch::Vec3& stageMin,
                             const bch::Vec3& stageMax) const {
    const double aspect = static_cast<double>(std::max(1, width)) / static_cast<double>(std::max(1, height));
    const float stageSpan = std::max({
        std::max(.001f, stageMax.x - stageMin.x),
        std::max(.001f, stageMax.y - stageMin.y),
        std::max(.001f, stageMax.z - stageMin.z)
    });
    const float span = arenaSpan(arena);
    const float nearPlane = std::max(.001f, span * .002f);
    const float farPlane = std::max(stageSpan * 30.0f, distance_ * 30.0f);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(62.0, aspect, nearPlane, farPlane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const float yaw = yawDeg_ * kPi / 180.0f;
    const float pitch = pitchDeg_ * kPi / 180.0f;
    const float cp = std::cos(pitch);
    const float lift = std::min(arena.halfX, arena.halfZ) * .13f;
    const bch::Vec3 target{
        arena.center.x + panX_,
        arena.floorY + lift,
        arena.center.z + panZ_
    };
    const bch::Vec3 eye{
        target.x + std::sin(yaw) * cp * distance_,
        target.y + std::sin(pitch) * distance_,
        target.z + std::cos(yaw) * cp * distance_
    };

    gluLookAt(eye.x, eye.y, eye.z,
              target.x, target.y, target.z,
              0.0, 1.0, 0.0);
}

} // namespace promptemon::demo
