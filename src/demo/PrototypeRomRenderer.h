#pragma once

#include <windows.h>

#include "bch/BchScene.h"
#include "rom/RomModelArchive.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::demo {

class PrototypeRomRenderer {
public:
    PrototypeRomRenderer() = default;
    ~PrototypeRomRenderer();

    bool create(HWND parent, rom::RomModelArchive* archive);
    void destroy();

    HWND window() const noexcept { return window_; }
    void show(bool visible);
    void move(int x, int y, int width, int height);

    bool setSingleModel(const std::string& modelPath);
    bool setBattleModels(const std::string& playerModelPath, const std::string& wildModelPath);
    void clearModels();
    void tick();

private:
    struct Visual {
        std::unique_ptr<bch::Scene> scene;
        const bch::Animation* idle = nullptr;
        const bch::Animation* current = nullptr;
        float frame = 0.0f;
        bch::Vec3 center{};
        float fitScale = 1.0f;
        std::unordered_map<std::string, unsigned int> textures;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool initializeOpenGL();
    void shutdownOpenGL();
    bool loadVisual(Visual& target, const std::string& modelPath);
    void releaseVisual(Visual& target);
    void uploadTextures(Visual& target);
    void updateBounds(Visual& target);
    std::vector<std::uint8_t> animationForModel(const std::string& modelPath) const;

    void render();
    void drawVisual(Visual& visual, float x, float y, float z, float scale, float yawDegrees);
    void setupProjection(int width, int height);

    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC gl_ = nullptr;
    rom::RomModelArchive* archive_ = nullptr;
    Visual player_;
    Visual wild_;
    bool battleMode_ = false;
    std::chrono::steady_clock::time_point lastTick_{};
};

} // namespace promptemon::demo
