#pragma once

namespace promptemon::demo {

class JumpController {
public:
    void reset(float actorHeight);
    void trigger();
    bool update(float dt);

    float offset() const { return offset_; }
    bool active() const { return active_; }

private:
    float actorHeight_ = 1.0f;
    float elapsed_ = 0.0f;
    float offset_ = 0.0f;
    bool active_ = false;
};

} // namespace promptemon::demo
