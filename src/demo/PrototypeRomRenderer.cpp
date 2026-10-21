#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace promptemon::demo {
namespace {

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float cosine = std::cos(material->uvRotation);
    const float sine = std::sin(material->uvRotation);
    const float x = uv.x - 0.5f;
    const float y = uv.y - 0.5f;
    return {
        (x * cosine - y * sine + 0.5f) * material->uvScaleU - material->uvTranslateU,
        (x * sine + y * cosine + 0.5f) * material->uvScaleV - material->uvTranslateV
    };
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const std::string& value, const std::vector<std::string>& tokens) {
    const std::string lowered = lower(value);
    for (const auto& token : tokens) {
        if (lowered.find(lower(token)) != std::string::npos) return true;
    }
    return false;
}

} // namespace

PrototypeRomRenderer::~PrototypeRomRenderer() {
    destroy();
}

bool PrototypeRomRenderer::create(HWND parent, rom::RomModelArchive* archive) {
    destroy();
    parent_ = parent;
    archive_ = archive;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptemonGameSurface";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    window_ = CreateWindowExW(0,
                              L"PromptemonGameSurface",
                              L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                              0, 0, 1, 1,
                              parent,
                              nullptr,
                              GetModuleHandleW(nullptr),
                              this);
    if (!window_) return false;

    if (!initializeOpenGL()) {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    initializeFonts();
    lastTick_ = std::chrono::steady_clock::now();
    return true;
}

void PrototypeRomRenderer::destroy() {
    if (gl_) {
        wglMakeCurrent(dc_, gl_);
        releaseVisual(stage_);
        releaseVisual(player_);
        releaseVisual(wild_);
        releaseVisual(third_);
        releaseVisual(effect_);
        releaseFonts();
    }
    shutdownOpenGL();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    parent_ = nullptr;
    archive_ = nullptr;
}

void PrototypeRomRenderer::show(bool visible) {
    if (window_) ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

void PrototypeRomRenderer::move(int x, int y, int width, int height) {
    if (window_) MoveWindow(window_, x, y, std::max(1, width), std::max(1, height), TRUE);
}

bool PrototypeRomRenderer::initializeOpenGL() {
    dc_ = GetDC(window_);
    if (!dc_) return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(dc_, &pfd);
    if (!format || !SetPixelFormat(dc_, format, &pfd)) return false;
    gl_ = wglCreateContext(dc_);
    return gl_ && wglMakeCurrent(dc_, gl_) == TRUE;
}

void PrototypeRomRenderer::shutdownOpenGL() {
    if (gl_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gl_);
        gl_ = nullptr;
    }
    if (dc_ && window_) {
        ReleaseDC(window_, dc_);
        dc_ = nullptr;
    }
}

void PrototypeRomRenderer::initializeFonts() {
    if (!dc_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);

    auto makeFont = [&](int height, int weight, HFONT& handle, unsigned int& base) {
        handle = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FF_DONTCARE, L"Segoe UI");
        SelectObject(dc_, handle);
        base = glGenLists(256);
        wglUseFontBitmapsW(dc_, 0, 256, base);
    };

    makeFont(17, FW_NORMAL, hFontSmall_, fontSmall_);
    makeFont(23, FW_SEMIBOLD, hFontMedium_, fontMedium_);
    makeFont(38, FW_BOLD, hFontLarge_, fontLarge_);
}

void PrototypeRomRenderer::releaseFonts() {
    if (fontSmall_) glDeleteLists(fontSmall_, 256);
    if (fontMedium_) glDeleteLists(fontMedium_, 256);
    if (fontLarge_) glDeleteLists(fontLarge_, 256);
    fontSmall_ = fontMedium_ = fontLarge_ = 0;
    if (hFontSmall_) DeleteObject(hFontSmall_);
    if (hFontMedium_) DeleteObject(hFontMedium_);
    if (hFontLarge_) DeleteObject(hFontLarge_);
    hFontSmall_ = hFontMedium_ = hFontLarge_ = nullptr;
}

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string& modelPath) const {
    if (!archive_) return {};
    std::vector<std::string> candidates;
    const auto slash = modelPath.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string directory = modelPath.substr(0, slash);
        candidates.push_back(directory + "/anim.bch.cx");
        candidates.push_back(directory + "/anim.bch");
        candidates.push_back(directory + "/animation.bch.cx");
        candidates.push_back(directory + "/animation.bch");
        candidates.push_back(directory + "/mii_anim.bch.cx");
        candidates.push_back(directory + "/mii_anim.bch");
    }

    for (const auto& candidate : candidates) {
        if (!archive_->has(candidate)) continue;
        try {
            return archive_->extractBch(candidate);
        } catch (...) {
        }
    }
    return {};
}

bool PrototypeRomRenderer::ensureVisual(Visual& target, const std::string& modelPath, bool loadAnimations) {
    if (modelPath.empty()) return false;
    if (target.scene && target.modelPath == modelPath) return true;
    return loadVisual(target, modelPath, loadAnimations);
}

bool PrototypeRomRenderer::loadVisual(Visual& target, const std::string& modelPath, bool loadAnimations) {
    if (!archive_ || modelPath.empty()) return false;
    wglMakeCurrent(dc_, gl_);
    releaseVisual(target);
    try {
        const auto model = archive_->extractBch(modelPath);
        const auto animation = loadAnimations ? animationForModel(modelPath) : std::vector<std::uint8_t>{};
        auto scene = std::make_unique<bch::Scene>(bch::Scene::parse(model, animation));
        if (scene->meshes().empty()) return false;

        target.modelPath = modelPath;
        target.scene = std::move(scene);
        target.frame = 0.0f;
        target.idle = target.scene->findAnimation("Pii_anim_nomove_kamae");
        if (!target.idle) {
            target.idle = chooseAnimation(target, {"nomove", "kamae", "idle", "wait"}, false);
        }
        if (!target.idle) {
            for (const auto& clip : target.scene->animations()) {
                if (clip.loop) {
                    target.idle = &clip;
                    break;
                }
            }
        }
        target.current = target.idle;
        updateBounds(target);
        uploadTextures(target);
        return true;
    } catch (...) {
        releaseVisual(target);
        return false;
    }
}

void PrototypeRomRenderer::releaseVisual(Visual& target) {
    for (const auto& texture : target.textures) {
        GLuint id = static_cast<GLuint>(texture.second);
        if (id) glDeleteTextures(1, &id);
    }
    target.textures.clear();
    target.modelPath.clear();
    target.scene.reset();
    target.idle = nullptr;
    target.current = nullptr;
    target.frame = 0.0f;
    target.center = {};
    target.fitScale = 1.0f;
}

void PrototypeRomRenderer::uploadTextures(Visual& target) {
    if (!target.scene) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto& texture : target.scene->textures()) {
        if (texture.width <= 0 || texture.height <= 0 || texture.rgba.empty()) continue;
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture.rgba.data());
        target.textures[texture.name] = id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PrototypeRomRenderer::updateBounds(Visual& target) {
    if (!target.scene || target.scene->meshes().empty()) return;
    bch::Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    bch::Vec3 maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    const auto pose = target.scene->samplePose(nullptr, 0.0f);
    bool found = false;

    for (const auto& mesh : target.scene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto point = target.scene->skin(vertex, pose);
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
            found = true;
        }
    }
    if (!found) return;

    target.center = {(minimum.x + maximum.x) * 0.5f,
                     (minimum.y + maximum.y) * 0.5f,
                     (minimum.z + maximum.z) * 0.5f};
    const float extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    if (extent > 1e-5f) target.fitScale = 1.75f / extent;
}

const bch::Animation* PrototypeRomRenderer::chooseAnimation(Visual& visual,
                                                            const std::vector<std::string>& preferredTokens,
                                                            bool preferNonLoop) const {
    if (!visual.scene) return nullptr;
    for (const auto& clip : visual.scene->animations()) {
        if (preferNonLoop && clip.loop) continue;
        if (containsAny(clip.name, preferredTokens)) return &clip;
    }
    if (preferNonLoop) {
        for (const auto& clip : visual.scene->animations()) {
            if (!clip.loop && clip.frames > 2.0f) return &clip;
        }
    }
    return nullptr;
}

void PrototypeRomRenderer::playAnimation(Visual& visual,
                                         const std::vector<std::string>& preferredTokens,
                                         bool preferNonLoop) {
    if (const auto* clip = chooseAnimation(visual, preferredTokens, preferNonLoop)) {
        visual.current = clip;
        visual.frame = 0.0f;
    }
}

void PrototypeRomRenderer::setStageModel(const std::string& modelPath) {
    stagePath_ = modelPath;
    if (!stagePath_.empty()) ensureVisual(stage_, stagePath_, false);
}

void PrototypeRomRenderer::setEffectModel(const std::string& modelPath) {
    effectPath_ = modelPath;
    if (!effectPath_.empty()) ensureVisual(effect_, effectPath_, true);
}

void PrototypeRomRenderer::showMainMenu() {
    page_ = GamePage::MainMenu;
    ensureVisual(stage_, stagePath_, false);
    render();
}

void PrototypeRomRenderer::showIntro() {
    page_ = GamePage::Intro;
    ensureVisual(stage_, stagePath_, false);
    render();
}

void PrototypeRomRenderer::showOptions(int volume, bool fullscreen) {
    page_ = GamePage::Options;
    volume_ = volume;
    fullscreen_ = fullscreen;
    render();
}

bool PrototypeRomRenderer::showStarterSelect(const std::string& rattataModel,
                                              const std::string& pidgeyModel,
                                              const std::string& caterpieModel) {
    page_ = GamePage::StarterSelect;
    ensureVisual(stage_, stagePath_, false);
    const bool a = ensureVisual(player_, rattataModel, true);
    const bool b = ensureVisual(wild_, pidgeyModel, true);
    const bool c = ensureVisual(third_, caterpieModel, true);
    render();
    return a && b && c;
}

bool PrototypeRomRenderer::showHub(const std::string& playerModel,
                                    const std::wstring& playerName,
                                    int level,
                                    int xp,
                                    int wins,
                                    int losses,
                                    const std::wstring& ivSummary) {
    page_ = GamePage::Hub;
    playerName_ = playerName;
    playerLevel_ = level;
    xp_ = xp;
    wins_ = wins;
    losses_ = losses;
    ivSummary_ = ivSummary;
    ensureVisual(stage_, stagePath_, false);
    const bool ok = ensureVisual(player_, playerModel, true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showHunt(const std::string& wildModel,
                                     const std::wstring& wildName,
                                     int level,
                                     const std::wstring& ivReward,
                                     bool shiny) {
    page_ = GamePage::Hunt;
    wildName_ = wildName;
    wildLevel_ = level;
    ivReward_ = ivReward;
    shiny_ = shiny;
    ensureVisual(stage_, stagePath_, false);
    const bool ok = ensureVisual(wild_, wildModel, true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showBattle(const std::string& playerModel,
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
                                       bool battleWon) {
    page_ = GamePage::Battle;
    playerName_ = playerName;
    playerLevel_ = playerLevel;
    playerHp_ = playerHp;
    playerMaxHp_ = std::max(1, playerMaxHp);
    wildName_ = wildName;
    wildLevel_ = wildLevel;
    wildHp_ = wildHp;
    wildMaxHp_ = std::max(1, wildMaxHp);
    ivReward_ = ivReward;
    shiny_ = shiny;
    move1_ = move1;
    move2_ = move2;
    battleLog_ = battleLog;
    battleOver_ = battleOver;
    battleWon_ = battleWon;

    ensureVisual(stage_, stagePath_, false);
    const bool a = ensureVisual(player_, playerModel, true);
    const bool b = ensureVisual(wild_, wildModel, true);
    render();
    return a && b;
}

void PrototypeRomRenderer::playPlayerAction() {
    playAnimation(player_, {"waza", "attack", "atack", "action", "move"}, true);
    effectTargetsWild_ = true;
    effectTimer_ = 0.72f;
    if (!effectPath_.empty()) {
        ensureVisual(effect_, effectPath_, true);
        playAnimation(effect_, {"effect", "hit", "start", "attack"}, true);
    }
}

void PrototypeRomRenderer::playWildAction() {
    playAnimation(wild_, {"waza", "attack", "atack", "action", "move"}, true);
    effectTargetsWild_ = false;
    effectTimer_ = 0.72f;
    if (!effectPath_.empty()) {
        ensureVisual(effect_, effectPath_, true);
        playAnimation(effect_, {"effect", "hit", "start", "attack"}, true);
    }
}

void PrototypeRomRenderer::playPlayerHit() {
    playAnimation(player_, {"damage", "dmg", "hit", "yarare"}, true);
}

void PrototypeRomRenderer::playWildHit() {
    playAnimation(wild_, {"damage", "dmg", "hit", "yarare"}, true);
}

void PrototypeRomRenderer::playPlayerFaint() {
    playAnimation(player_, {"down", "dead", "faint", "die"}, true);
}

void PrototypeRomRenderer::playWildFaint() {
    playAnimation(wild_, {"down", "dead", "faint", "die"}, true);
}

void PrototypeRomRenderer::setupPerspective(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(std::max(1, width)) / static_cast<double>(std::max(1, height));
    const double top = 0.52;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, 0.8, 80.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void PrototypeRomRenderer::setupOverlay(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void PrototypeRomRenderer::drawVisual(Visual& visual, float x, float y, float z, float scale, float yawDegrees) {
    if (!visual.scene) return;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yawDegrees, 0.0f, 1.0f, 0.0f);
    const float fit = visual.fitScale * scale;
    glScalef(fit, fit, fit);
    glTranslatef(-visual.center.x, -visual.center.y, -visual.center.z);

    const auto pose = visual.scene->samplePose(visual.current, visual.frame);
    for (const auto& mesh : visual.scene->meshes()) {
        const auto* texture = visual.scene->textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < visual.scene->materials().size()
            ? &visual.scene->materials()[mesh.materialId]
            : nullptr;
        GLuint textureId = 0;
        if (texture) {
            auto found = visual.textures.find(texture->name);
            if (found != visual.textures.end()) textureId = found->second;
        }

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.82f, 0.82f, 0.84f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = visual.scene->skin(vertex, pose);
            if (textureId) {
                const auto uv = materialUv(material, vertex.uv);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void PrototypeRomRenderer::drawStage() {
    if (!stage_.scene) return;
    drawVisual(stage_, 0.0f, -0.65f, -10.5f, 5.4f, 0.0f);
}

void PrototypeRomRenderer::drawShadow(float x, float y, float z, float radiusX, float radiusZ, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.04f, 0.05f, 0.04f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 32; ++i) {
        const float a = static_cast<float>(i) / 32.0f * 6.2831853f;
        glVertex3f(x + std::cos(a) * radiusX, y, z + std::sin(a) * radiusZ);
    }
    glEnd();
}

void PrototypeRomRenderer::drawFallbackEffect(float x, float y, float z, float phase) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    for (int ring = 0; ring < 3; ++ring) {
        const float radius = 0.12f + (phase + ring * 0.17f) * 0.55f;
        const float alpha = std::max(0.0f, 1.0f - phase - ring * 0.12f);
        glColor4f(1.0f, 0.92f, 0.42f, alpha);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 24; ++i) {
            const float a = static_cast<float>(i) / 24.0f * 6.2831853f;
            glVertex3f(x + std::cos(a) * radius, y + std::sin(a) * radius, z);
        }
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::drawAttackEffect() {
    if (effectTimer_ <= 0.0f) return;
    const float phase = 1.0f - effectTimer_ / 0.72f;
    const float x = effectTargetsWild_ ? 1.15f : -1.15f;
    const float y = effectTargetsWild_ ? 0.05f : -0.10f;
    const float z = effectTargetsWild_ ? -4.9f : -4.35f;
    if (effect_.scene) {
        drawVisual(effect_, x, y, z, 1.25f + phase * 0.35f, 0.0f);
    } else {
        drawFallbackEffect(x, y, z + 0.25f, phase);
    }
}

void PrototypeRomRenderer::renderWorld(int width, int height) {
    setupPerspective(width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawStage();

    switch (page_) {
        case GamePage::StarterSelect:
            drawShadow(-1.65f, -0.63f, -5.0f, 0.58f, 0.28f, 0.34f);
            drawShadow(0.0f, -0.63f, -5.0f, 0.58f, 0.28f, 0.34f);
            drawShadow(1.65f, -0.63f, -5.0f, 0.58f, 0.28f, 0.34f);
            drawVisual(player_, -1.65f, -0.38f, -5.0f, 1.38f, 12.0f);
            drawVisual(wild_, 0.0f, -0.38f, -5.0f, 1.38f, 0.0f);
            drawVisual(third_, 1.65f, -0.38f, -5.0f, 1.38f, -12.0f);
            break;
        case GamePage::Hub:
            drawShadow(0.0f, -0.63f, -4.8f, 0.70f, 0.32f, 0.34f);
            drawVisual(player_, 0.0f, -0.30f, -4.8f, 1.58f, -8.0f);
            break;
        case GamePage::Hunt:
            drawShadow(0.45f, -0.63f, -4.8f, 0.72f, 0.34f, 0.36f);
            drawVisual(wild_, 0.45f, -0.26f, -4.8f, 1.66f, -14.0f);
            break;
        case GamePage::Battle:
            drawShadow(-1.18f, -0.63f, -4.35f, 0.70f, 0.32f, 0.34f);
            drawShadow(1.18f, -0.63f, -4.95f, 0.70f, 0.32f, 0.34f);
            drawVisual(player_, -1.18f, -0.27f, -4.35f, 1.52f, 24.0f);
            drawVisual(wild_, 1.18f, -0.22f, -4.95f, 1.52f, -24.0f);
            drawAttackEffect();
            break;
        default:
            break;
    }
}

void PrototypeRomRenderer::drawPanel(float x, float y, float w, float h, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.035f, 0.075f, 0.12f, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glColor4f(0.80f, 0.91f, 0.98f, std::min(1.0f, alpha + 0.08f));
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

float PrototypeRomRenderer::textWidth(const std::wstring& text, int fontKind) const {
    const float charWidth = fontKind == 2 ? 21.0f : (fontKind == 1 ? 12.5f : 9.0f);
    return static_cast<float>(text.size()) * charWidth;
}

void PrototypeRomRenderer::drawText(float x, float y, const std::wstring& text, int fontKind, bool centered) {
    unsigned int base = fontKind == 2 ? fontLarge_ : (fontKind == 1 ? fontMedium_ : fontSmall_);
    if (!base || text.empty()) return;
    std::vector<unsigned char> glyphs;
    glyphs.reserve(text.size());
    for (wchar_t ch : text) glyphs.push_back(static_cast<unsigned char>(ch <= 255 ? ch : '?'));
    if (centered) x -= textWidth(text, fontKind) * 0.5f;
    glColor4f(0.96f, 0.98f, 1.0f, 1.0f);
    glRasterPos2f(x, y);
    glListBase(base);
    glCallLists(static_cast<GLsizei>(glyphs.size()), GL_UNSIGNED_BYTE, glyphs.data());
}

void PrototypeRomRenderer::addHitTarget(GameAction action, const RectF& rect, bool enabled) {
    if (enabled && action != GameAction::None) hitTargets_.push_back({action, rect});
}

void PrototypeRomRenderer::drawButton(const RectF& rect, const std::wstring& label, GameAction action, bool enabled) {
    const bool hover = enabled && action == hoverAction_;
    glDisable(GL_TEXTURE_2D);
    if (!enabled) glColor4f(0.15f, 0.18f, 0.20f, 0.78f);
    else if (hover) glColor4f(0.18f, 0.55f, 0.92f, 0.94f);
    else glColor4f(0.08f, 0.31f, 0.58f, 0.92f);
    glBegin(GL_QUADS);
    glVertex2f(rect.x, rect.y); glVertex2f(rect.x + rect.w, rect.y);
    glVertex2f(rect.x + rect.w, rect.y + rect.h); glVertex2f(rect.x, rect.y + rect.h);
    glEnd();
    glColor4f(0.85f, 0.94f, 1.0f, enabled ? 1.0f : 0.45f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(rect.x, rect.y); glVertex2f(rect.x + rect.w, rect.y);
    glVertex2f(rect.x + rect.w, rect.y + rect.h); glVertex2f(rect.x, rect.y + rect.h);
    glEnd();
    drawText(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.62f, label, 1, true);
    addHitTarget(action, rect, enabled);
}

void PrototypeRomRenderer::drawHpPanel(const RectF& rect,
                                        const std::wstring& name,
                                        int level,
                                        int hp,
                                        int maxHp,
                                        bool shiny,
                                        bool enemy) {
    drawPanel(rect.x, rect.y, rect.w, rect.h, 0.88f);
    std::wstring title = shiny ? L"[SHINY] " + name : name;
    drawText(rect.x + 18.0f, rect.y + 30.0f, title, 1, false);
    drawText(rect.x + rect.w - 92.0f, rect.y + 30.0f, L"Nv. " + std::to_wstring(level), 0, false);

    const float barX = rect.x + 18.0f;
    const float barY = rect.y + 50.0f;
    const float barW = rect.w - 36.0f;
    const float ratio = std::clamp(static_cast<float>(hp) / static_cast<float>(std::max(1, maxHp)), 0.0f, 1.0f);
    glColor4f(0.08f, 0.11f, 0.13f, 0.95f);
    glBegin(GL_QUADS); glVertex2f(barX,barY); glVertex2f(barX+barW,barY); glVertex2f(barX+barW,barY+14); glVertex2f(barX,barY+14); glEnd();
    if (ratio > 0.5f) glColor4f(0.25f, 0.86f, 0.36f, 1.0f);
    else if (ratio > 0.2f) glColor4f(0.95f, 0.75f, 0.18f, 1.0f);
    else glColor4f(0.92f, 0.25f, 0.22f, 1.0f);
    glBegin(GL_QUADS); glVertex2f(barX,barY); glVertex2f(barX+barW*ratio,barY); glVertex2f(barX+barW*ratio,barY+14); glVertex2f(barX,barY+14); glEnd();
    drawText(rect.x + 18.0f, rect.y + 86.0f,
             L"PS " + std::to_wstring(hp) + L" / " + std::to_wstring(maxHp), 0, false);
    (void)enemy;
}

std::wstring PrototypeRomRenderer::cropText(const std::wstring& value, std::size_t maxChars) const {
    if (value.size() <= maxChars) return value;
    if (maxChars < 4) return value.substr(0, maxChars);
    return value.substr(0, maxChars - 3) + L"...";
}

void PrototypeRomRenderer::renderOverlay(int width, int height) {
    setupOverlay(width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    hitTargets_.clear();

    const float W = static_cast<float>(width);
    const float H = static_cast<float>(height);

    switch (page_) {
        case GamePage::MainMenu: {
            drawPanel(42, 42, std::min(520.0f, W * 0.48f), 118, 0.82f);
            drawText(64, 92, L"PROMPTEMON", 2, false);
            drawText(66, 132, L"STEIDIUM", 1, false);
            const float bw = std::min(360.0f, W * 0.34f);
            float y = H * 0.37f;
            drawButton({54,y,bw,54},L"Nueva partida",GameAction::NewGame,true); y += 64;
            drawButton({54,y,bw,54},L"Cargar partida",GameAction::None,false); y += 64;
            drawButton({54,y,bw,54},L"Borrar partida",GameAction::None,false); y += 64;
            drawButton({54,y,bw,54},L"Exportar partida",GameAction::None,false); y += 64;
            drawButton({54,y,bw,54},L"Opciones",GameAction::Options,true);
            break;
        }
        case GamePage::Intro: {
            drawPanel(50, H - 240, W - 100, 180, 0.90f);
            drawText(78, H - 198, L"Bienvenido a Promptemon Steidium", 1, false);
            drawText(78, H - 158, L"La ROM construye este mundo: escenario, Pokemon y animaciones.", 0, false);
            drawText(78, H - 128, L"Caza, entrena tus IV y encuentra variantes shiny.", 0, false);
            drawButton({W - 270,H - 116,200,46},L"Continuar",GameAction::IntroContinue,true);
            break;
        }
        case GamePage::StarterSelect: {
            drawPanel(40, 30, W - 80, 78, 0.80f);
            drawText(W * 0.5f, 78, L"ELIGE TU PRIMER COMPANERO", 2, true);
            const float cardW = W / 3.0f;
            drawButton({18,H-100,cardW-30,62},L"Rattata  |  IV Velocidad",GameAction::StarterRattata,true);
            drawButton({cardW+6,H-100,cardW-30,62},L"Pidgey  |  IV Velocidad",GameAction::StarterPidgey,true);
            drawButton({cardW*2+6,H-100,cardW-24,62},L"Caterpie  |  IV PS",GameAction::StarterCaterpie,true);
            break;
        }
        case GamePage::Hub: {
            drawPanel(34, 30, std::min(540.0f,W*0.48f), 142, 0.86f);
            drawText(56, 68, playerName_ + L"  Nv. " + std::to_wstring(playerLevel_), 1, false);
            drawText(56, 100, L"XP " + std::to_wstring(xp_) + L"/100    Victorias " + std::to_wstring(wins_) + L"    Derrotas " + std::to_wstring(losses_), 0, false);
            drawText(56, 132, cropText(ivSummary_, 66), 0, false);
            drawButton({W-320,H-90,270,54},L"Entrar a Caza",GameAction::Hunt,true);
            drawButton({40,H-82,180,44},L"Menu",GameAction::Back,true);
            break;
        }
        case GamePage::Hunt: {
            drawPanel(36, 32, std::min(470.0f,W*0.42f), 126, 0.86f);
            drawText(58, 72, shiny_ ? L"[SHINY] " + wildName_ : wildName_, 1, false);
            drawText(58, 105, L"Nv. " + std::to_wstring(wildLevel_) + L"   Aporta IV: " + ivReward_, 0, false);
            drawButton({W-304,H-92,254,56},L"Combatir",GameAction::StartBattle,true);
            drawButton({40,H-84,190,46},L"Volver al Hub",GameAction::ReturnHub,true);
            break;
        }
        case GamePage::Battle: {
            const float panelW = std::min(410.0f, W * 0.36f);
            drawHpPanel({34,28,panelW,104},playerName_,playerLevel_,playerHp_,playerMaxHp_,false,false);
            drawHpPanel({W-panelW-34,28,panelW,104},wildName_,wildLevel_,wildHp_,wildMaxHp_,shiny_,true);

            drawPanel(34,H-176,W-68,142,0.90f);
            drawText(58,H-140,cropText(battleLog_,96),0,false);
            drawText(W-350,H-140,L"Aporta IV: " + ivReward_,0,false);
            if (!battleOver_) {
                const float moveW = (W - 142) * 0.5f;
                drawButton({54,H-104,moveW,54},move1_,GameAction::Move1,true);
                drawButton({76+moveW,H-104,moveW,54},move2_,GameAction::Move2,true);
            } else {
                drawButton({54,H-104,240,54},L"Otro encuentro",GameAction::NextBattle,true);
                drawButton({W-294,H-104,240,54},L"Volver al Hub",GameAction::ReturnHub,true);
            }
            break;
        }
        case GamePage::Options: {
            drawPanel(W*0.18f,H*0.16f,W*0.64f,H*0.66f,0.91f);
            drawText(W*0.5f,H*0.24f,L"OPCIONES",2,true);
            drawText(W*0.34f,H*0.36f,L"Volumen de audio: " + std::to_wstring(volume_) + L"%",1,false);
            drawButton({W*0.33f,H*0.40f,90,48},L"-",GameAction::VolumeDown,volume_>0);
            drawButton({W*0.58f,H*0.40f,90,48},L"+",GameAction::VolumeUp,volume_<100);
            drawText(W*0.34f,H*0.54f,L"Modo de pantalla",1,false);
            drawButton({W*0.26f,H*0.59f,220,50},L"Pantalla completa",GameAction::Fullscreen,!fullscreen_);
            drawButton({W*0.55f,H*0.59f,180,50},L"Ventana",GameAction::Windowed,fullscreen_);
            drawButton({W*0.5f-90,H*0.73f,180,46},L"Volver",GameAction::Back,true);
            break;
        }
    }

    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::render() {
    if (!window_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);
    RECT client{};
    GetClientRect(window_, &client);
    const int width = std::max(1, client.right - client.left);
    const int height = std::max(1, client.bottom - client.top);

    glViewport(0, 0, width, height);
    glClearColor(0.38f, 0.66f, 0.86f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderWorld(width, height);
    renderOverlay(width, height);
    SwapBuffers(dc_);
}

void PrototypeRomRenderer::tick() {
    if (!gl_) return;
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    elapsedSeconds_ += dt;
    effectTimer_ = std::max(0.0f, effectTimer_ - dt);

    auto advance = [dt](Visual& visual) {
        if (!visual.current) return;
        visual.frame += dt * 30.0f;
        if (visual.current->frames > 0.0f && visual.frame > visual.current->frames) {
            if (visual.current->loop) visual.frame = std::fmod(visual.frame, visual.current->frames);
            else {
                visual.current = visual.idle;
                visual.frame = 0.0f;
            }
        }
    };

    advance(player_);
    advance(wild_);
    advance(third_);
    advance(effect_);
    render();
}

GameAction PrototypeRomRenderer::hitTest(int x, int y) const {
    for (auto it = hitTargets_.rbegin(); it != hitTargets_.rend(); ++it) {
        const auto& r = it->second;
        if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) return it->first;
    }
    return GameAction::None;
}

LRESULT CALLBACK PrototypeRomRenderer::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PrototypeRomRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PrototypeRomRenderer*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            if (self) self->render();
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (self) self->render();
            return 0;
        case WM_MOUSEMOVE:
            if (self) {
                const auto action = self->hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (action != self->hoverAction_) {
                    self->hoverAction_ = action;
                    self->render();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (self && self->parent_) {
                const auto action = self->hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (action != GameAction::None) {
                    SendMessageW(self->parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(action), BN_CLICKED), 0);
                }
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace promptemon::demo
