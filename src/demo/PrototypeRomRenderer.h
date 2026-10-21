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

    bool setSingleModel(const std::string& modelPath, bool shiny = false);
    bool setBattleModels(const std::string& playerModelPath,
                         const std::string& wildModelPath,
                         bool wildShiny = false);
    bool setStarterModels(const std::string& leftModelPath,
                          const std::string& centerModelPath,
                          const std::string& rightModelPath);

    void playPlayerAction();
    void playWildAction();
    void clearModels();
    void tick();

private:
    enum class Layout {
        Single,
        Battle,
        Starters
    };

    struct Visual {
        std::string modelPath;
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
    bool ensureVisual(Visual& target, const std::string& modelPath);
    bool loadVisual(Visual& target, const std::string& modelPath);
    void releaseVisual(Visual& target);
    void uploadTextures(Visual& target);
    void updateBounds(Visual& target);
    std::vector<std::uint8_t> animationForModel(const std::string& modelPath) const;
    const bch::Animation* firstAction(Visual& visual) const;

    void render();
    void drawBackdrop();
    void drawShadow(float x, float y, float z, float radiusX, float radiusY);
    void drawSparkles(float x, float y, float z, float phase);
    void drawVisual(Visual& visual, float x, float y, float z, float scale, float yawDegrees);
    void setupProjection(int width, int height);

    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC gl_ = nullptr;
    rom::RomModelArchive* archive_ = nullptr;

    Visual player_;
    Visual wild_;
    Visual third_;
    Layout layout_ = Layout::Single;
    bool singleShiny_ = false;
    bool wildShiny_ = false;
    float elapsedSeconds_ = 0.0f;
    std::chrono::steady_clock::time_point lastTick_{};
};

} // namespace promptemon::demo
