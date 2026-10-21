#pragma once

#include <windows.h>
#include <windowsx.h>

#include "bch/BchScene.h"
#include "rom/RomModelArchive.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace promptemon::demo {

enum class GamePage {
    MainMenu,
    Intro,
    StarterSelect,
    Hub,
    Hunt,
    Battle,
    Options
};

enum class GameAction : int {
    None = 0,
    NewGame = 100,
    Options = 101,
    Back = 102,
    IntroContinue = 103,
    StarterRattata = 104,
    StarterPidgey = 105,
    StarterCaterpie = 106,
    Hunt = 107,
    StartBattle = 108,
    Move1 = 109,
    Move2 = 110,
    ReturnHub = 111,
    NextBattle = 112,
    VolumeDown = 113,
    VolumeUp = 114,
    Fullscreen = 115,
    Windowed = 116
};

class PrototypeRomRenderer {
public:
    PrototypeRomRenderer() = default;
    ~PrototypeRomRenderer();

    bool create(HWND parent, rom::RomModelArchive* archive);
    void destroy();
    void show(bool visible);
    void move(int x, int y, int width, int height);
    void tick();

    void setStageModel(const std::string& modelPath);
    void setEffectModel(const std::string& modelPath);

    void showMainMenu();
    void showIntro();
    void showOptions(int volume, bool fullscreen);

    bool showStarterSelect(const std::string& rattataModel,
                           const std::string& pidgeyModel,
                           const std::string& caterpieModel);

    bool showHub(const std::string& playerModel,
                 const std::wstring& playerName,
                 int level,
                 int xp,
                 int wins,
                 int losses,
                 const std::wstring& ivSummary);

    bool showHunt(const std::string& wildModel,
                  const std::wstring& wildName,
                  int level,
                  const std::wstring& ivReward,
                  bool shiny);

    bool showBattle(const std::string& playerModel,
                    const std::string& wildModel,
                    const std::wstring& playerName,
                    int playerLevel,
                    int playerHp,
                    int playerMaxHp,
                    const std::wstring& wildName,
                    int wildLevel,
                    int wildHp,
                    int wildMaxHp,
                    const std::wstring& ivReward,
                    bool shiny,
                    const std::wstring& move1,
                    const std::wstring& move2,
                    const std::wstring& battleLog,
                    bool battleOver,
                    bool battleWon);

    void playPlayerAction();
    void playWildAction();
    void playPlayerHit();
    void playWildHit();
    void playPlayerFaint();
    void playWildFaint();

private:
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

    struct RectF {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool initializeOpenGL();
    void shutdownOpenGL();
    bool ensureVisual(Visual& target, const std::string& modelPath, bool loadAnimations = true);
    bool loadVisual(Visual& target, const std::string& modelPath, bool loadAnimations);
    void releaseVisual(Visual& target);
    void uploadTextures(Visual& target);
    void updateBounds(Visual& target);
    std::vector<std::uint8_t> animationForModel(const std::string& modelPath) const;

    const bch::Animation* chooseAnimation(Visual& visual,
                                          const std::vector<std::string>& preferredTokens,
                                          bool preferNonLoop) const;
    void playAnimation(Visual& visual,
                       const std::vector<std::string>& preferredTokens,
                       bool preferNonLoop);

    void initializeFonts();
    void releaseFonts();
    void render();
    void renderWorld(int width, int height);
    void renderOverlay(int width, int height);
    void setupPerspective(int width, int height);
    void setupOverlay(int width, int height);

    void drawVisual(Visual& visual, float x, float y, float z, float scale, float yawDegrees);
    void drawStage();
    void drawShadow(float x, float y, float z, float radiusX, float radiusZ, float alpha);
    void drawAttackEffect();
    void drawFallbackEffect(float x, float y, float z, float phase);

    void drawPanel(float x, float y, float w, float h, float alpha = 0.86f);
    void drawButton(const RectF& rect, const std::wstring& label, GameAction action, bool enabled = true);
    void drawHpPanel(const RectF& rect,
                     const std::wstring& name,
                     int level,
                     int hp,
                     int maxHp,
                     bool shiny,
                     bool enemy);
    void drawText(float x, float y, const std::wstring& text, int fontKind = 0, bool centered = false);
    float textWidth(const std::wstring& text, int fontKind) const;

    GameAction hitTest(int x, int y) const;
    void addHitTarget(GameAction action, const RectF& rect, bool enabled = true);
    std::wstring cropText(const std::wstring& value, std::size_t maxChars) const;

    HWND window_ = nullptr;
    HWND parent_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC gl_ = nullptr;
    rom::RomModelArchive* archive_ = nullptr;

    Visual stage_;
    Visual player_;
    Visual wild_;
    Visual third_;
    Visual effect_;
    std::string stagePath_;
    std::string effectPath_;

    GamePage page_ = GamePage::MainMenu;
    std::vector<std::pair<GameAction, RectF>> hitTargets_;
    GameAction hoverAction_ = GameAction::None;

    std::wstring playerName_;
    std::wstring wildName_;
    std::wstring ivSummary_;
    std::wstring ivReward_;
    std::wstring move1_;
    std::wstring move2_;
    std::wstring battleLog_;
    int playerLevel_ = 1;
    int wildLevel_ = 1;
    int playerHp_ = 1;
    int playerMaxHp_ = 1;
    int wildHp_ = 1;
    int wildMaxHp_ = 1;
    int xp_ = 0;
    int wins_ = 0;
    int losses_ = 0;
    int volume_ = 70;
    bool fullscreen_ = false;
    bool shiny_ = false;
    bool battleOver_ = false;
    bool battleWon_ = false;

    unsigned int fontSmall_ = 0;
    unsigned int fontMedium_ = 0;
    unsigned int fontLarge_ = 0;
    HFONT hFontSmall_ = nullptr;
    HFONT hFontMedium_ = nullptr;
    HFONT hFontLarge_ = nullptr;

    float elapsedSeconds_ = 0.0f;
    float effectTimer_ = 0.0f;
    bool effectTargetsWild_ = true;
    std::chrono::steady_clock::time_point lastTick_{};
};

} // namespace promptemon::demo
