#include "demo/JumpController.h"

#include <algorithm>

namespace promptemon::demo {

void JumpController::reset(float actorHeight) {
    actorHeight_ = std::max(.001f, actorHeight);
    elapsed_ = 0.0f;
    offset_ = 0.0f;
    active_ = false;
}

void JumpController::trigger() {
    if (active_) return;
    elapsed_ = 0.0f;
    offset_ = 0.0f;
    active_ = true;
}

bool JumpController::update(float dt) {
    if (!active_) return false;

    constexpr float duration = .55f;
    elapsed_ += std::max(0.0f, dt);
    const float t = std::clamp(elapsed_ / duration, 0.0f, 1.0f);
    const float peak = actorHeight_ * .65f;
    offset_ = 4.0f * peak * t * (1.0f - t);

    if (t >= 1.0f) {
        elapsed_ = 0.0f;
        offset_ = 0.0f;
        active_ = false;
    }
    return true;
}

} // namespace promptemon::demo
