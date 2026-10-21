#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>

namespace promptemon::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool hasAny(const std::string& value, const std::vector<std::string>& tokens) {
    const std::string p = lowerAscii(value);
    for (const auto& token : tokens) {
        if (p.find(lowerAscii(token)) != std::string::npos) return true;
    }
    return false;
}

bool collisionLike(const std::string& path) {
    const std::string p = lowerAscii(path);
    const auto slash = p.find_last_of('/');
    const std::string leaf = p.substr(slash == std::string::npos ? 0 : slash + 1);
    return leaf.rfind("attr", 0) == 0 || leaf.rfind("col_", 0) == 0 ||
           leaf.find("collision") != std::string::npos || leaf.find("collider") != std::string::npos;
}

std::string parentVirtualPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - .5f;
    const float y = uv.y - .5f;
    return {(x * c - y * s + .5f) * material->uvScaleU - material->uvTranslateU,
            (x * s + y * c + .5f) * material->uvScaleV - material->uvTranslateV};
}

void drawMeshes(bch::Scene& scene,
                const std::unordered_map<std::string, unsigned int>& textures,
                const bch::Animation* animation,
                float frame) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        const auto* tex = scene.textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < scene.materials().size()
            ? &scene.materials()[mesh.materialId]
            : nullptr;

        GLuint id = 0;
        if (tex) {
            const auto found = textures.find(tex->name);
            if (found != textures.end()) id = static_cast<GLuint>(found->second);
        }

        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.86f, .87f, .84f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (id) {
                const auto uv = materialUv(material, vertex.uv);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

std::string bySuffix(storage::RomAssetCache* assets, const std::vector<std::string>& suffixes) {
    if (!assets) return {};
    const auto models = assets->modelResources();
    for (const auto& wanted : suffixes) {
        const std::string suffix = lowerAscii(wanted);
        for (const auto& path : models) {
            if (endsWith(lowerAscii(path), suffix)) return path;
        }
    }
    return {};
}

std::string byTokens(storage::RomAssetCache* assets,
                     const std::vector<std::string>& required,
                     const std::vector<std::string>& preferred = {}) {
    if (!assets) return {};
    int bestScore = std::numeric_limits<int>::min();
    std::string best;
    for (const auto& path : assets->modelResources()) {
        const std::string p = lowerAscii(path);
        bool ok = true;
        for (const auto& token : required) {
            if (p.find(lowerAscii(token)) == std::string::npos) {
                ok = false;
                break;
            }
        }
        if (!ok || collisionLike(path)) continue;
        int score = 0;
        for (const auto& token : preferred) if (p.find(lowerAscii(token)) != std::string::npos) score += 30;
        if (p.find("ground") != std::string::npos) score += 40;
        if (p.find("floor") != std::string::npos) score += 30;
        if (p.find("stage") != std::string::npos) score += 20;
        if (score > bestScore) {
            bestScore = score;
            best = path;
        }
    }
    return best;
}

std::string canonical(storage::RomAssetCache* assets, const std::string& requested, const std::wstring& name) {
    if (name == L"Rattata") { auto p = bySuffix(assets,{"/koratta.bch","/koratta_f.bch"}); if (!p.empty()) return p; }
    if (name == L"Pidgey")  { auto p = bySuffix(assets,{"/poppo.bch"}); if (!p.empty()) return p; }
    if (name == L"Caterpie"){ auto p = bySuffix(assets,{"/kyatapi.bch","/kyatapii.bch"}); if (!p.empty()) return p; }
    if (name == L"Weedle")  { auto p = bySuffix(assets,{"/biidoru.bch"}); if (!p.empty()) return p; }
    if (name == L"Zubat")   { auto p = bySuffix(assets,{"/zubatto.bch","/zubatto_f.bch"}); if (!p.empty()) return p; }
    if (name == L"Geodude") { auto p = bySuffix(assets,{"/isitubute.bch","/ishitsubute.bch"}); if (!p.empty()) return p; }
    if (name == L"Gastly")  { auto p = bySuffix(assets,{"/goosu.bch"}); if (!p.empty()) return p; }
    if (name == L"Machop")  { auto p = bySuffix(assets,{"/wanrikii.bch"}); if (!p.empty()) return p; }
    return requested;
}

float yawToward(float x, float z, float tx, float tz) {
    return std::atan2(tx - x, -(tz - z)) * 180.0f / kPi;
}

void drawFlatBar(float x, float y, float w, float h, float ratio,
                 float r, float g, float b, float alpha = 1.0f) {
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(.04f, .05f, .05f, .84f * alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x + 2, y + 2); glVertex2f(x + 2 + (w - 4) * ratio, y + 2);
    glVertex2f(x + 2 + (w - 4) * ratio, y + h - 2); glVertex2f(x + 2, y + h - 2);
    glEnd();
}

} // namespace

PrototypeRomRenderer::~PrototypeRomRenderer() { destroy(); }

bool PrototypeRomRenderer::create(HWND parent, storage::RomAssetCache* assets) {
    destroy();
    parent_ = parent;
    archive_ = assets;
    if (!archive_ || !archive_->isOpen()) return false;

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

    window_ = CreateWindowExW(0, L"PromptemonGameSurface", L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                              0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!window_ || !initializeOpenGL()) return false;

    initializeFonts();
    if (!uiSkin_.nativeSkinReady()) {
        destroy();
        return false;
    }
    lastTick_ = std::chrono::steady_clock::now();
    return true;
}

void PrototypeRomRenderer::destroy() {
    if (gl_) {
        wglMakeCurrent(dc_, gl_);
        releaseStageComposition();
        releaseVisual(avatar_);
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
    PIXELFORMATDESCRIPTOR p{};
    p.nSize = sizeof(p);
    p.nVersion = 1;
    p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    p.iPixelType = PFD_TYPE_RGBA;
    p.cColorBits = 32;
    p.cDepthBits = 24;
    p.iLayerType = PFD_MAIN_PLANE;
    const int f = ChoosePixelFormat(dc_, &p);
    if (!f || !SetPixelFormat(dc_, f, &p)) return false;
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
    if (archive_ && gl_) {
        wglMakeCurrent(dc_, gl_);
        uiSkin_.load(*archive_);
    }
}

void PrototypeRomRenderer::releaseFonts() { uiSkin_.release(); }

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string& modelPath) const {
    if (!archive_) return {};
    const std::string p = lowerAscii(modelPath);
    if (p.find("mii/") != std::string::npos) {
        for (const auto& resource : archive_->resources()) {
            const auto q = lowerAscii(resource);
            if (endsWith(q, "/mii_anim.bch") || endsWith(q, "/mii_anim.bch.cx")) {
                try { return archive_->extractBch(resource); } catch (...) {}
            }
        }
    }

    const std::string stem = lowerAscii(std::filesystem::path(modelPath).stem().string());
    for (const auto& resource : archive_->resources()) {
        const auto q = lowerAscii(resource);
        if (q == p || q.find(stem) == std::string::npos) continue;
        if (q.find("anim") == std::string::npos && q.find("motion") == std::string::npos) continue;
        try { return archive_->extractBch(resource); } catch (...) {}
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
        const auto motion = loadAnimations ? animationForModel(modelPath) : std::vector<std::uint8_t>{};
        auto scene = std::make_unique<bch::Scene>(motion.empty()
            ? bch::Scene::parse(model)
            : bch::Scene::parse(model, motion));
        if (scene->meshes().empty()) return false;

        target.modelPath = modelPath;
        target.scene = std::move(scene);
        target.frame = 0.0f;
        if (loadAnimations) {
            target.idle = target.scene->findAnimation("Pii_anim_nomove_kamae");
            if (!target.idle) target.idle = chooseAnimation(target,{"idle","wait","stand","nomove","kamae","fly"},false);
            if (!target.idle) {
                for (const auto& clip : target.scene->animations()) {
                    if (clip.loop) { target.idle = &clip; break; }
                }
            }
            target.current = target.idle;
        }
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
    target = {};
}

void PrototypeRomRenderer::uploadTextures(Visual& target) {
    if (!target.scene) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto& texture : target.scene->textures()) {
        if (texture.width <= 0 || texture.height <= 0 || texture.rgba.empty()) continue;
        GLuint id = 0;
        glGenTextures(1, &id);
        if (!id) continue;
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
    if (!target.scene) return;
    bch::Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX};
    bch::Vec3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    const auto pose = target.scene->samplePose(nullptr, 0.0f);
    for (const auto& mesh : target.scene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = target.scene->skin(vertex, pose);
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
            found = true;
        }
    }
    if (!found) return;
    target.boundsMin = mn;
    target.boundsMax = mx;
    target.center = {(mn.x + mx.x) * .5f, (mn.y + mx.y) * .5f, (mn.z + mx.z) * .5f};
    const float extent = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
    if (extent > 1e-5f) target.fitScale = 1.65f / extent;
}

void PrototypeRomRenderer::releaseStageComposition() {
    for (auto& p : stageParts_) releaseVisual(p);
    stageParts_.clear();
    stageDirectory_.clear();
    stageBoundsMin_ = {};
    stageBoundsMax_ = {};
}

bool PrototypeRomRenderer::loadStageComposition(const std::string& requested) {
    releaseStageComposition();
    if (!archive_) return false;

    std::string ground = requested;
    if (ground.empty()) ground = byTokens(archive_, {"stage","grass"}, {"ground","field","floor"});
    if (ground.empty()) ground = byTokens(archive_, {"stage"}, {"ground","field","floor"});
    if (ground.empty()) return false;

    stageDirectory_ = parentVirtualPath(lowerAscii(ground));
    std::set<std::string> candidates;
    candidates.insert(ground);

    for (const auto& resource : archive_->modelResources()) {
        const std::string p = lowerAscii(resource);
        if (!stageDirectory_.empty() && p.rfind(stageDirectory_, 0) == 0 && !collisionLike(resource)) {
            candidates.insert(resource);
            if (candidates.size() >= 18) break;
        }
    }

    for (const auto& resource : candidates) {
        Visual part;
        if (loadVisual(part, resource, false)) stageParts_.push_back(std::move(part));
    }
    if (stageParts_.empty()) return false;
    stagePath_ = ground;
    updateStageCompositionBounds();
    return true;
}

void PrototypeRomRenderer::updateStageCompositionBounds() {
    if (stageParts_.empty()) return;
    bch::Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX};
    bch::Vec3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const auto& p : stageParts_) {
        mn.x = std::min(mn.x, p.boundsMin.x); mn.y = std::min(mn.y, p.boundsMin.y); mn.z = std::min(mn.z, p.boundsMin.z);
        mx.x = std::max(mx.x, p.boundsMax.x); mx.y = std::max(mx.y, p.boundsMax.y); mx.z = std::max(mx.z, p.boundsMax.z);
    }
    stageBoundsMin_ = mn;
    stageBoundsMax_ = mx;
}

void PrototypeRomRenderer::drawStageComposition() {
    if (stageParts_.empty()) return;
    const float sx = std::max(.001f, stageBoundsMax_.x - stageBoundsMin_.x);
    const float sz = std::max(.001f, stageBoundsMax_.z - stageBoundsMin_.z);
    const float cx = (stageBoundsMin_.x + stageBoundsMax_.x) * .5f;
    const float cz = (stageBoundsMin_.z + stageBoundsMax_.z) * .5f;
    const float fy = stageBoundsMin_.y;
    const float scale = std::min(15.0f / sx, 11.5f / sz);

    glPushMatrix();
    glScalef(scale, scale, scale);
    glTranslatef(-cx, -fy, -cz);
    for (auto& p : stageParts_) if (p.scene) drawMeshes(*p.scene, p.textures, nullptr, 0.0f);
    glPopMatrix();
}

const bch::Animation* PrototypeRomRenderer::chooseAnimation(Visual& visual,
                                                             const std::vector<std::string>& tokens,
                                                             bool preferNonLoop) const {
    if (!visual.scene) return nullptr;
    const bch::Animation* best = nullptr;
    int bestScore = -99999;
    for (const auto& clip : visual.scene->animations()) {
        const std::string name = lowerAscii(clip.name);
        int score = 0;
        bool matched = false;
        for (const auto& token : tokens) {
            if (name.find(lowerAscii(token)) != std::string::npos) { matched = true; score += 100; }
        }
        if (!matched) continue;
        if (preferNonLoop && !clip.loop) score += 40;
        if (preferNonLoop && clip.loop) score -= 20;
        if (clip.frames > 5) score += 10;
        if (score > bestScore) { bestScore = score; best = &clip; }
    }
    return best;
}

void PrototypeRomRenderer::playAnimation(Visual& visual,
                                         const std::vector<std::string>& tokens,
                                         bool preferNonLoop) {
    if (const auto* animation = chooseAnimation(visual, tokens, preferNonLoop)) {
        visual.current = animation;
        visual.frame = 0.0f;
    }
}

void PrototypeRomRenderer::setStageModel(const std::string& path) {
    stagePath_ = path;
    loadStageComposition(path);
}

void PrototypeRomRenderer::setEffectModel(const std::string& path) {
    effectPath_ = path;
    if (!path.empty()) ensureVisual(effect_, path, true);
}

void PrototypeRomRenderer::showMainMenu() {
    page_ = GamePage::MainMenu;
    std::string plaza = byTokens(archive_, {"stage","castle"}, {"ground","floor"});
    if (plaza.empty()) plaza = byTokens(archive_, {"stage","courtyard"}, {"ground","floor"});
    if (plaza.empty()) plaza = byTokens(archive_, {"stage","town"}, {"ground","floor"});
    if (plaza.empty()) plaza = byTokens(archive_, {"stage","plaza"}, {"ground","floor"});
    if (!plaza.empty() && parentVirtualPath(lowerAscii(plaza)) != stageDirectory_) loadStageComposition(plaza);

    const auto mii = bySuffix(archive_, {"/mii_normal.bch","/mii_bare.bch"});
    ensureVisual(avatar_, mii, true);
    ensureVisual(player_, canonical(archive_, {}, L"Rattata"), true);
    ensureVisual(wild_, canonical(archive_, {}, L"Pidgey"), true);
    ensureVisual(third_, canonical(archive_, {}, L"Caterpie"), true);
    render();
}

void PrototypeRomRenderer::showIntro() {
    page_ = GamePage::Intro;
    const auto mii = bySuffix(archive_, {"/mii_normal.bch","/mii_bare.bch"});
    ensureVisual(avatar_, mii, true);
    render();
}

void PrototypeRomRenderer::showOptions(int volume, bool fullscreen) {
    page_ = GamePage::Options;
    volume_ = volume;
    fullscreen_ = fullscreen;
    render();
}

bool PrototypeRomRenderer::showStarterSelect(const std::string& a, const std::string& b, const std::string& c) {
    page_ = GamePage::StarterSelect;
    const auto mii = bySuffix(archive_, {"/mii_normal.bch","/mii_bare.bch"});
    ensureVisual(avatar_, mii, true);
    const bool x = ensureVisual(player_, canonical(archive_, a, L"Rattata"), true);
    const bool y = ensureVisual(wild_, canonical(archive_, b, L"Pidgey"), true);
    const bool z = ensureVisual(third_, canonical(archive_, c, L"Caterpie"), true);
    render();
    return x && y && z;
}

bool PrototypeRomRenderer::showHub(const std::string& model, const std::wstring& name,
                                   int level, int xp, int wins, int losses,
                                   const std::wstring& iv) {
    page_ = GamePage::Hub;
    playerName_ = name;
    playerLevel_ = level;
    xp_ = xp;
    wins_ = wins;
    losses_ = losses;
    ivSummary_ = iv;
    std::string plaza = byTokens(archive_, {"stage","castle"}, {"ground","floor"});
    if (plaza.empty()) plaza = byTokens(archive_, {"stage","courtyard"}, {"ground","floor"});
    if (plaza.empty()) plaza = byTokens(archive_, {"stage","town"}, {"ground","floor"});
    if (!plaza.empty() && parentVirtualPath(lowerAscii(plaza)) != stageDirectory_) loadStageComposition(plaza);
    const auto mii = bySuffix(archive_, {"/mii_normal.bch","/mii_bare.bch"});
    ensureVisual(avatar_, mii, true);
    const bool ok = ensureVisual(player_, canonical(archive_, model, name), true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showHunt(const std::string& model, const std::wstring& name,
                                    int level, const std::wstring& iv, bool shiny) {
    page_ = GamePage::Hunt;
    wildName_ = name;
    wildLevel_ = level;
    ivReward_ = iv;
    shiny_ = shiny;
    std::string grass = byTokens(archive_, {"stage","grass"}, {"ground","floor"});
    if (grass.empty()) grass = byTokens(archive_, {"stage","forest"}, {"ground","floor"});
    if (grass.empty()) grass = byTokens(archive_, {"stage","field"}, {"ground","floor"});
    if (grass.empty()) grass = byTokens(archive_, {"stage"}, {"ground","floor"});
    if (!grass.empty() && parentVirtualPath(lowerAscii(grass)) != stageDirectory_) loadStageComposition(grass);
    const bool ok = ensureVisual(wild_, canonical(archive_, model, name), true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showBattle(const std::string& playerModel, const std::string& wildModel,
                                      const std::wstring& playerName, int playerLevel, int playerHp, int playerMaxHp,
                                      const std::wstring& wildName, int wildLevel, int wildHp, int wildMaxHp,
                                      const std::wstring& ivReward, bool shiny,
                                      const std::wstring& move1, const std::wstring& move2,
                                      const std::wstring& battleLog, bool battleOver, bool battleWon) {
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
    playerFieldX_ = -2.2f;
    playerFieldZ_ = 1.8f;
    wildFieldX_ = 2.0f;
    wildFieldZ_ = -1.35f;
    const bool a = ensureVisual(player_, canonical(archive_, playerModel, playerName), true);
    const bool b = ensureVisual(wild_, canonical(archive_, wildModel, wildName), true);
    render();
    return a && b;
}

void PrototypeRomRenderer::playPlayerAction() {
    playAnimation(player_, {"waza","attack","rush","move"}, true);
    effectTargetsWild_ = true;
    effectTimer_ = .80f;
}
void PrototypeRomRenderer::playWildAction() {
    playAnimation(wild_, {"waza","attack","rush","move"}, true);
    effectTargetsWild_ = false;
    effectTimer_ = .80f;
}
void PrototypeRomRenderer::playPlayerHit() { playAnimation(player_, {"damage","hit","yarare"}, true); }
void PrototypeRomRenderer::playWildHit() { playAnimation(wild_, {"damage","hit","yarare"}, true); }
void PrototypeRomRenderer::playPlayerFaint() { playAnimation(player_, {"down","dead","faint"}, true); }
void PrototypeRomRenderer::playWildFaint() { playAnimation(wild_, {"down","dead","faint"}, true); }

void PrototypeRomRenderer::setupPerspective(int w, int h) {
    glViewport(0, 0, std::max(1, w), std::max(1, h));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(std::max(1, w)) / static_cast<double>(std::max(1, h));
    const double top = .31;
    glFrustum(-top * aspect, top * aspect, -top, top, .55, 300.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void PrototypeRomRenderer::setupOverlay(int w, int h) {
    glViewport(0, 0, std::max(1, w), std::max(1, h));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

bool PrototypeRomRenderer::playerInAttackRange() const {
    return page_ == GamePage::Battle && !battleOver_;
}

void PrototypeRomRenderer::updateBattleInput(float) {
    if (!playerInAttackRange()) return;
    const bool a = (GetAsyncKeyState('Z') & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000);
    const bool b = (GetAsyncKeyState('X') & 0x8000);
    if (a && !attack1Latch_ && parent_ && effectTimer_ <= 0)
        SendMessageW(parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(GameAction::Move1), BN_CLICKED), 0);
    if (b && !attack2Latch_ && parent_ && effectTimer_ <= 0)
        SendMessageW(parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(GameAction::Move2), BN_CLICKED), 0);
    attack1Latch_ = a;
    attack2Latch_ = b;
}

void PrototypeRomRenderer::drawVisual(Visual& v, float x, float y, float z, float scale, float yaw) {
    if (!v.scene) return;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw, 0, 1, 0);
    const float fit = v.fitScale * scale;
    glScalef(fit, fit, fit);
    glTranslatef(-v.center.x, -v.center.y, -v.center.z);
    drawMeshes(*v.scene, v.textures, v.current, v.frame);
    glPopMatrix();
}

void PrototypeRomRenderer::drawShadow(float x, float y, float z, float rx, float rz, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(.02f, .02f, .02f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 28; ++i) {
        const float t = i / 28.0f * 2.0f * kPi;
        glVertex3f(x + std::cos(t) * rx, y, z + std::sin(t) * rz);
    }
    glEnd();
}

void PrototypeRomRenderer::drawFallbackEffect(float x, float y, float z, float phase) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float wave = std::sin(std::clamp(phase, 0.0f, 1.0f) * kPi);
    const float radius = .12f + wave * .7f;
    glColor4f(1.0f, .86f, .2f, 1.0f - phase);
    glBegin(GL_LINES);
    for (int i = 0; i < 14; ++i) {
        const float a = i / 14.0f * 2.0f * kPi;
        glVertex3f(x, y, z);
        glVertex3f(x + std::cos(a) * radius, y + .28f, z + std::sin(a) * radius);
    }
    glEnd();
}

void PrototypeRomRenderer::drawMenuBattleEffect(float x, float y, float z, float phase) {
    drawFallbackEffect(x, y, z, phase);
}

void PrototypeRomRenderer::drawAttackEffect() {
    if (effectTimer_ <= 0.0f) return;
    const float phase = 1.0f - effectTimer_ / .80f;
    const float x = effectTargetsWild_ ? wildFieldX_ : playerFieldX_;
    const float z = effectTargetsWild_ ? wildFieldZ_ : playerFieldZ_;
    if (effect_.scene) drawVisual(effect_, x, .45f, z, .38f, 0.0f);
    else drawFallbackEffect(x, .45f, z, phase);
}

void PrototypeRomRenderer::renderWorld(int w, int h) {
    setupPerspective(w, h);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float t = elapsedSeconds_;
    const float sway = std::sin(t * .18f) * 1.2f;
    const float bob = std::sin(t * .35f) * .06f;

    glPushMatrix();
    glTranslatef(0.0f, -2.15f + bob, -16.4f);
    glRotatef(53.0f, 1, 0, 0);
    glRotatef(-7.0f + sway, 0, 1, 0);
    drawStageComposition();

    if (page_ == GamePage::MainMenu) {
        drawShadow(-2.8f,.03f,1.6f,.40f,.24f,.22f);
        drawShadow(-.8f,.03f,-.2f,.38f,.22f,.20f);
        drawShadow(1.0f,.03f,.55f,.38f,.22f,.20f);
        drawShadow(2.8f,.03f,-1.2f,.38f,.22f,.20f);
        drawVisual(avatar_,-2.8f,.45f,1.6f,.72f,165.0f);
        drawVisual(player_,-.8f,.42f,-.2f,.50f,-20.0f);
        drawVisual(wild_,1.0f,.42f,.55f,.50f,35.0f);
        drawVisual(third_,2.8f,.42f,-1.2f,.50f,-35.0f);
    } else if (page_ == GamePage::Intro) {
        drawShadow(0,.03f,.6f,.42f,.24f,.24f);
        drawVisual(avatar_,0,.47f,.6f,.78f,180.0f);
    } else if (page_ == GamePage::StarterSelect) {
        drawShadow(0,.03f,2.15f,.40f,.22f,.23f);
        drawVisual(avatar_,0,.46f,2.15f,.72f,180.0f);
        drawShadow(-2.6f,.03f,-.25f,.40f,.23f,.24f);
        drawShadow(0,.03f,-1.05f,.40f,.23f,.24f);
        drawShadow(2.6f,.03f,-.25f,.40f,.23f,.24f);
        drawVisual(player_,-2.6f,.42f,-.25f,.56f,10.0f);
        drawVisual(wild_,0,.42f,-1.05f,.56f,0.0f);
        drawVisual(third_,2.6f,.42f,-.25f,.56f,-10.0f);
    } else if (page_ == GamePage::Hub) {
        drawShadow(-.9f,.03f,.75f,.42f,.24f,.23f);
        drawShadow(1.1f,.03f,-.15f,.36f,.22f,.20f);
        drawVisual(player_,-.9f,.44f,.75f,.58f,165.0f);
        drawVisual(avatar_,1.1f,.46f,-.15f,.68f,-20.0f);
    } else if (page_ == GamePage::Hunt) {
        drawShadow(-2.1f,.03f,1.5f,.42f,.24f,.24f);
        drawShadow(2.1f,.03f,-1.0f,.42f,.24f,.24f);
        drawVisual(player_,-2.1f,.44f,1.5f,.58f,yawToward(-2.1f,1.5f,2.1f,-1.0f));
        drawVisual(wild_,2.1f,.44f,-1.0f,.58f,yawToward(2.1f,-1.0f,-2.1f,1.5f));
    } else if (page_ == GamePage::Battle) {
        float px = playerFieldX_, pz = playerFieldZ_, wx = wildFieldX_, wz = wildFieldZ_;
        if (effectTimer_ > 0.0f) {
            const float phase = 1.0f - effectTimer_ / .80f;
            const float q = std::sin(phase * kPi);
            const float dx = wx - px, dz = wz - pz;
            const float len = std::max(.001f, std::sqrt(dx * dx + dz * dz));
            if (effectTargetsWild_) { px += dx / len * q * 1.05f; pz += dz / len * q * 1.05f; }
            else { wx -= dx / len * q * 1.05f; wz -= dz / len * q * 1.05f; }
        }
        drawShadow(px,.03f,pz,.44f,.25f,.28f);
        drawShadow(wx,.03f,wz,.44f,.25f,.28f);
        drawVisual(player_,px,.44f,pz,.61f,yawToward(px,pz,wx,wz));
        drawVisual(wild_,wx,.44f,wz,.61f,yawToward(wx,wz,px,pz));
        drawAttackEffect();
    }

    glPopMatrix();
}

void PrototypeRomRenderer::drawPanel(float x, float y, float w, float h, float alpha) {
    uiSkin_.drawPanel(x, y, w, h, alpha);
}

float PrototypeRomRenderer::textWidth(const std::wstring& text, int kind) const {
    return uiSkin_.textWidth(text, kind);
}

void PrototypeRomRenderer::drawText(float x, float y, const std::wstring& text, int kind, bool centered) {
    uiSkin_.drawText(x, y, text, kind, centered);
}

void PrototypeRomRenderer::addHitTarget(GameAction action, const RectF& rect, bool enabled) {
    if (enabled && action != GameAction::None) hitTargets_.push_back({action, rect});
}

void PrototypeRomRenderer::drawButton(const RectF& rect, const std::wstring& label,
                                      GameAction action, bool enabled) {
    uiSkin_.drawButton(rect.x, rect.y, rect.w, rect.h, enabled && action == hoverAction_, enabled);
    drawText(rect.x + rect.w * .5f, rect.y + rect.h * .64f, label, 1, true);
    addHitTarget(action, rect, enabled);
}

void PrototypeRomRenderer::drawHpPanel(const RectF& rect, const std::wstring& name,
                                       int level, int hp, int maxHp, bool shiny, bool) {
    const float ratio = std::clamp(static_cast<float>(hp) / std::max(1, maxHp), 0.0f, 1.0f);
    drawText(rect.x + rect.w * .5f, rect.y + 17.0f,
             (shiny ? L"★ " : L"") + name + L"  Nv." + std::to_wstring(level), 0, true);
    const float red = ratio < .25f ? .95f : .20f;
    const float green = ratio < .25f ? .20f : (ratio < .50f ? .72f : .86f);
    drawFlatBar(rect.x + 8, rect.y + 25, rect.w - 16, 10, ratio, red, green, .32f);
}

std::wstring PrototypeRomRenderer::cropText(const std::wstring& value, std::size_t maxChars) const {
    if (value.size() <= maxChars) return value;
    return maxChars < 4 ? value.substr(0, maxChars) : value.substr(0, maxChars - 3) + L"...";
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
            drawPanel(W - 386, 38, 336, 370, .94f);
            drawText(W - 218, 86, L"PROMPTEMON", 2, true);
            drawText(W - 218, 116, L"STEIDIUM", 1, true);
            float y = 150;
            drawButton({W - 352, y, 268, 42}, L"Nueva partida", GameAction::NewGame, true); y += 48;
            drawButton({W - 352, y, 268, 42}, L"Cargar partida", GameAction::None, false); y += 48;
            drawButton({W - 352, y, 268, 42}, L"Borrar partida", GameAction::None, false); y += 48;
            drawButton({W - 352, y, 268, 42}, L"Exportar partida", GameAction::None, false); y += 48;
            drawButton({W - 352, y, 268, 42}, L"Opciones", GameAction::Options, true);
            break;
        }
        case GamePage::Intro: {
            drawPanel(48, H - 174, W - 96, 118, .93f);
            drawText(76, H - 135, L"Bienvenido al mundo de los Juguetes Pokemon.", 1, false);
            drawText(76, H - 105, L"Elige un companero y empieza a formar tu equipo.", 0, false);
            drawButton({W - 246, H - 106, 166, 38}, L"Continuar", GameAction::IntroContinue, true);
            break;
        }
        case GamePage::StarterSelect: {
            drawPanel(W * .22f, 24, W * .56f, 58, .90f);
            drawText(W * .5f, 61, L"ELIGE TU PRIMER JUGUETE POKEMON", 1, true);
            const float cardW = std::min(285.0f, (W - 72.0f) / 3.0f);
            const float gap = 12.0f;
            const float total = cardW * 3.0f + gap * 2.0f;
            const float x = (W - total) * .5f;
            drawButton({x, H - 78, cardW, 46}, L"Rattata  |  IV Velocidad", GameAction::StarterRattata, true);
            drawButton({x + cardW + gap, H - 78, cardW, 46}, L"Pidgey  |  IV Velocidad", GameAction::StarterPidgey, true);
            drawButton({x + (cardW + gap) * 2, H - 78, cardW, 46}, L"Caterpie  |  IV PS", GameAction::StarterCaterpie, true);
            break;
        }
        case GamePage::Hub: {
            drawPanel(28, 26, 338, 108, .91f);
            drawText(48, 59, playerName_ + L"  Nv." + std::to_wstring(playerLevel_), 1, false);
            drawText(48, 87, L"XP " + std::to_wstring(xp_) + L"/100", 0, false);
            drawText(48, 112, L"Victorias " + std::to_wstring(wins_) + L"   Derrotas " + std::to_wstring(losses_), 0, false);
            drawButton({W - 234, H - 62, 202, 38}, L"Ir a Caza", GameAction::Hunt, true);
            break;
        }
        case GamePage::Hunt: {
            drawPanel(28, 26, 325, 91, .91f);
            drawText(48, 58, (shiny_ ? L"★ " : L"") + wildName_ + L"  Nv." + std::to_wstring(wildLevel_), 1, false);
            drawText(48, 88, L"Aporta IV: " + ivReward_, 0, false);
            drawButton({W - 222, H - 62, 190, 38}, L"Combatir", GameAction::StartBattle, true);
            break;
        }
        case GamePage::Battle: {
            const float playerCx = W * .39f;
            const float enemyCx = W * .61f;
            drawHpPanel({playerCx - 92, H * .63f, 184, 38}, playerName_, playerLevel_, playerHp_, playerMaxHp_, false, false);
            drawHpPanel({enemyCx - 92, H * .36f, 184, 38}, wildName_, wildLevel_, wildHp_, wildMaxHp_, shiny_, true);

            if (!battleLog_.empty()) {
                drawPanel(24, 20, std::min(W - 48.0f, 600.0f), 52, .84f);
                drawText(42, 53, cropText(battleLog_, 62), 0, false);
            }

            drawPanel(W - 252, 20, 220, 55, .88f);
            drawText(W - 142, 53, L"IV al vencer: " + ivReward_, 0, true);

            if (!battleOver_) {
                drawButton({W * .5f - 198, H - 66, 188, 42}, L"A  " + move1_, GameAction::Move1, effectTimer_ <= 0.0f);
                drawButton({W * .5f + 10, H - 66, 188, 42}, L"B  " + move2_, GameAction::Move2, effectTimer_ <= 0.0f);
            } else {
                drawPanel(W * .5f - 210, H - 116, 420, 88, .93f);
                drawText(W * .5f, H - 84, battleWon_ ? L"VICTORIA" : L"DERROTA", 1, true);
                drawButton({W * .5f - 190, H - 68, 178, 34}, L"Volver al hub", GameAction::ReturnHub, true);
                drawButton({W * .5f + 12, H - 68, 178, 34}, L"Siguiente", GameAction::NextBattle, true);
            }
            break;
        }
        case GamePage::Options: {
            drawPanel(W * .5f - 270, H * .5f - 205, 540, 410, .95f);
            drawText(W * .5f, H * .5f - 151, L"OPCIONES", 2, true);
            drawText(W * .5f - 150, H * .5f - 78, L"Volumen  " + std::to_wstring(volume_) + L"%", 1, false);
            drawButton({W * .5f - 68, H * .5f - 55, 58, 40}, L"-", GameAction::VolumeDown, volume_ > 0);
            drawButton({W * .5f + 10, H * .5f - 55, 58, 40}, L"+", GameAction::VolumeUp, volume_ < 100);
            drawText(W * .5f - 150, H * .5f + 32, L"Pantalla", 1, false);
            drawButton({W * .5f - 150, H * .5f + 55, 138, 40}, L"Completa", GameAction::Fullscreen, !fullscreen_);
            drawButton({W * .5f + 12, H * .5f + 55, 138, 40}, L"Ventana", GameAction::Windowed, fullscreen_);
            drawButton({W * .5f - 90, H * .5f + 126, 180, 40}, L"Volver", GameAction::Back, true);
            break;
        }
    }

    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::render() {
    if (!window_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);
    RECT r{};
    GetClientRect(window_, &r);
    const int w = std::max(1, static_cast<int>(r.right - r.left));
    const int h = std::max(1, static_cast<int>(r.bottom - r.top));
    glViewport(0, 0, w, h);
    glClearColor(.67f, .84f, .94f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderWorld(w, h);
    renderOverlay(w, h);
    SwapBuffers(dc_);
}

void PrototypeRomRenderer::tick() {
    if (!gl_) return;
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    dt = std::clamp(dt, 0.0f, .1f);
    elapsedSeconds_ += dt;
    effectTimer_ = std::max(0.0f, effectTimer_ - dt);
    updateBattleInput(dt);

    auto advance = [dt](Visual& v) {
        if (!v.current) return;
        v.frame += dt * 30.0f;
        if (v.current->frames > 0 && v.frame >= v.current->frames) {
            if (v.current->loop) v.frame = std::fmod(v.frame, v.current->frames);
            else { v.current = v.idle; v.frame = 0.0f; }
        }
    };
    advance(avatar_);
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

LRESULT CALLBACK PrototypeRomRenderer::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PrototypeRomRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<PrototypeRomRenderer*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT p{};
            BeginPaint(hwnd, &p);
            if (self) self->render();
            EndPaint(hwnd, &p);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (self) self->render();
            return 0;
        case WM_MOUSEMOVE:
            if (self) {
                const auto action = self->hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (action != self->hoverAction_) {
                    self->hoverAction_ = action;
                    self->render();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (self && self->parent_) {
                const auto action = self->hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (action != GameAction::None)
                    SendMessageW(self->parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(action), BN_CLICKED), 0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace promptemon::demo
