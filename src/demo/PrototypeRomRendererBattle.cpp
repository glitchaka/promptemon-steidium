#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace promptemon::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const std::string& value, const std::vector<std::string>& tokens) {
    const std::string p = lowerAscii(value);
    for (const auto& token : tokens) {
        if (p.find(lowerAscii(token)) != std::string::npos) return true;
    }
    return false;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - 0.5f;
    const float y = uv.y - 0.5f;
    return {
        (x * c - y * s + 0.5f) * material->uvScaleU - material->uvTranslateU,
        (x * s + y * c + 0.5f) * material->uvScaleV - material->uvTranslateV
    };
}

void drawSceneMeshes(bch::Scene& scene,
                     const std::unordered_map<std::string, GLuint>& textures,
                     const bch::Animation* animation,
                     float frame) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        const auto* texture = scene.textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < scene.materials().size()
            ? &scene.materials()[mesh.materialId] : nullptr;
        GLuint textureId = 0;
        if (texture) {
            const auto found = textures.find(texture->name);
            if (found != textures.end()) textureId = found->second;
        }

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.82f, 0.84f, 0.82f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
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
}

const char* canonicalModelBase(const std::wstring& name) {
    if (name == L"Rattata") return "koratta.bch";
    if (name == L"Pidgey") return "poppo.bch";
    if (name == L"Caterpie") return "kyatapi.bch";
    if (name == L"Weedle") return "biidoru.bch";
    if (name == L"Zubat") return "zubatto.bch";
    if (name == L"Geodude") return "ishitsubute.bch";
    if (name == L"Gastly") return "goosu.bch";
    if (name == L"Machop") return "wanrikii.bch";
    return nullptr;
}

std::string resolveCanonicalModel(storage::RomAssetCache* archive,
                                  const std::string& requested,
                                  const std::wstring& displayName) {
    if (!archive) return requested;
    const char* base = canonicalModelBase(displayName);
    if (!base) return requested;
    const std::string suffix = "/" + std::string(base);
    for (const auto& resource : archive->modelResources()) {
        const std::string p = lowerAscii(resource);
        if (p.find("pii/") != std::string::npos && endsWith(p, suffix)) return resource;
    }
    for (const auto& resource : archive->modelResources()) {
        const std::string p = lowerAscii(resource);
        if (endsWith(p, suffix)) return resource;
    }
    return requested;
}

std::string findBattleGround(storage::RomAssetCache* archive, const std::string& requested) {
    if (!archive) return requested;

    // Rumble World contains actual play fields under stage/<biome>/ground_XX.bch.
    // Use one complete field. Never stack every ground variant at the origin.
    const std::vector<std::string> preferred = {
        "stage/grassland/ground_01.bch",
        "stage/grassland/ground_02.bch",
        "stage/forest/ground_01.bch",
        "stage/beach/ground_01.bch"
    };
    for (const auto& needle : preferred) {
        for (const auto& resource : archive->modelResources()) {
            const std::string p = lowerAscii(resource);
            if (endsWith(p, needle)) return resource;
        }
    }

    // If the bundle does not contain grassland, keep a single real ground resource
    // from the selected stage directory instead of building the broken composite.
    const auto slash = requested.find_last_of('/');
    const std::string prefix = slash == std::string::npos ? std::string{} : lowerAscii(requested.substr(0, slash + 1));
    for (const auto& resource : archive->modelResources()) {
        const std::string p = lowerAscii(resource);
        if (!prefix.empty() && p.rfind(prefix, 0) != 0) continue;
        const auto namePos = p.find_last_of('/');
        const std::string name = namePos == std::string::npos ? p : p.substr(namePos + 1);
        if (name.rfind("ground_", 0) == 0 && endsWith(name, ".bch")) return resource;
    }
    return requested;
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
    if (!archive_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);
    uiSkin_.load(*archive_);
}

void PrototypeRomRenderer::releaseFonts() { uiSkin_.release(); }

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string&) const {
    // Rumble World Pokémon BCH files already contain their own skeleton and animation bank.
    // Loading an arbitrary second BCH from pii/ was mixing skeletons and deforming models.
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
        auto scene = std::make_unique<bch::Scene>(bch::Scene::parse(model));
        if (scene->meshes().empty()) return false;

        target.modelPath = modelPath;
        target.scene = std::move(scene);
        target.frame = 0.0f;
        target.idle = nullptr;
        target.current = nullptr;

        if (loadAnimations) {
            target.idle = target.scene->findAnimation("Pii_anim_nomove_kamae");
            if (!target.idle) target.idle = chooseAnimation(target, {"nomove", "kamae", "idle", "wait", "stand"}, false);
            if (!target.idle) {
                for (const auto& clip : target.scene->animations()) {
                    if (clip.loop) {
                        target.idle = &clip;
                        break;
                    }
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
        GLuint id = texture.second;
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
    bch::Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    bch::Vec3 mx{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
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
    target.center = {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
    const float extent = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
    if (extent > 1e-5f) target.fitScale = 1.75f / extent;
}

const bch::Animation* PrototypeRomRenderer::chooseAnimation(
    Visual& visual, const std::vector<std::string>& tokens, bool preferNonLoop) const {
    if (!visual.scene) return nullptr;

    const bch::Animation* best = nullptr;
    int bestScore = -100000;
    for (const auto& clip : visual.scene->animations()) {
        const std::string n = lowerAscii(clip.name);
        int score = 0;
        bool matched = false;
        for (const auto& token : tokens) {
            if (n.find(lowerAscii(token)) != std::string::npos) {
                matched = true;
                score += 100;
            }
        }
        if (!matched) continue;
        if (preferNonLoop && !clip.loop) score += 40;
        if (preferNonLoop && clip.loop) score -= 25;
        if (clip.frames > 5) score += 10;
        if (n.find("damage") != std::string::npos || n.find("yarare") != std::string::npos ||
            n.find("down") != std::string::npos || n.find("dead") != std::string::npos) {
            if (!containsAny(n, tokens)) score -= 200;
        }
        if (score > bestScore) {
            bestScore = score;
            best = &clip;
        }
    }

    if (best) return best;
    if (preferNonLoop) {
        for (const auto& clip : visual.scene->animations()) {
            const std::string n = lowerAscii(clip.name);
            if (!clip.loop && clip.frames > 5 &&
                n.find("damage") == std::string::npos && n.find("yarare") == std::string::npos &&
                n.find("down") == std::string::npos && n.find("dead") == std::string::npos) {
                return &clip;
            }
        }
    }
    return nullptr;
}

void PrototypeRomRenderer::playAnimation(
    Visual& visual, const std::vector<std::string>& tokens, bool preferNonLoop) {
    if (const auto* clip = chooseAnimation(visual, tokens, preferNonLoop)) {
        visual.current = clip;
        visual.frame = 0.0f;
    }
}

void PrototypeRomRenderer::setStageModel(const std::string& modelPath) {
    stagePath_ = findBattleGround(archive_, modelPath);
    if (!stagePath_.empty()) ensureVisual(stage_, stagePath_, false);
}

void PrototypeRomRenderer::setEffectModel(const std::string& modelPath) {
    effectPath_ = modelPath;
    const std::string p = lowerAscii(modelPath);
    if (p.find(".bch") == std::string::npos ||
        (p.find("effect") == std::string::npos && p.find("/eff") == std::string::npos && p.find("waza") == std::string::npos)) {
        effectPath_.clear();
        releaseVisual(effect_);
        return;
    }
    ensureVisual(effect_, effectPath_, true);
}

void PrototypeRomRenderer::showMainMenu() { page_ = GamePage::MainMenu; render(); }
void PrototypeRomRenderer::showIntro() { page_ = GamePage::Intro; render(); }
void PrototypeRomRenderer::showOptions(int volume, bool fullscreen) {
    page_ = GamePage::Options; volume_ = volume; fullscreen_ = fullscreen; render();
}

bool PrototypeRomRenderer::showStarterSelect(const std::string& a, const std::string& b, const std::string& c) {
    page_ = GamePage::StarterSelect;
    const bool x = ensureVisual(player_, resolveCanonicalModel(archive_, a, L"Rattata"), true);
    const bool y = ensureVisual(wild_, resolveCanonicalModel(archive_, b, L"Pidgey"), true);
    const bool z = ensureVisual(third_, resolveCanonicalModel(archive_, c, L"Caterpie"), true);
    render();
    return x && y && z;
}

bool PrototypeRomRenderer::showHub(const std::string& m, const std::wstring& n, int l, int xp, int w, int lo, const std::wstring& iv) {
    page_ = GamePage::Hub;
    playerName_ = n; playerLevel_ = l; xp_ = xp; wins_ = w; losses_ = lo; ivSummary_ = iv;
    const bool ok = ensureVisual(player_, resolveCanonicalModel(archive_, m, n), true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showHunt(const std::string& m, const std::wstring& n, int l, const std::wstring& iv, bool shiny) {
    page_ = GamePage::Hunt;
    wildName_ = n; wildLevel_ = l; ivReward_ = iv; shiny_ = shiny;
    const bool ok = ensureVisual(wild_, resolveCanonicalModel(archive_, m, n), true);
    render();
    return ok;
}

bool PrototypeRomRenderer::showBattle(const std::string& pm, const std::string& wm,
                                      const std::wstring& pn, int pl, int php, int pmax,
                                      const std::wstring& wn, int wl, int whp, int wmax,
                                      const std::wstring& iv, bool shiny,
                                      const std::wstring& m1, const std::wstring& m2,
                                      const std::wstring& log, bool over, bool won) {
    page_ = GamePage::Battle;
    playerName_ = pn; playerLevel_ = pl; playerHp_ = php; playerMaxHp_ = std::max(1, pmax);
    wildName_ = wn; wildLevel_ = wl; wildHp_ = whp; wildMaxHp_ = std::max(1, wmax);
    ivReward_ = iv; shiny_ = shiny; move1_ = m1; move2_ = m2; battleLog_ = log;
    battleOver_ = over; battleWon_ = won;
    const bool a = ensureVisual(player_, resolveCanonicalModel(archive_, pm, pn), true);
    const bool b = ensureVisual(wild_, resolveCanonicalModel(archive_, wm, wn), true);
    render();
    return a && b;
}

void PrototypeRomRenderer::playPlayerAction() {
    playAnimation(player_, {"waza", "attack", "rush", "tackle", "bite", "move", "action"}, true);
    effectTargetsWild_ = true;
    effectTimer_ = 1.05f;
}
void PrototypeRomRenderer::playWildAction() {
    playAnimation(wild_, {"waza", "attack", "rush", "tackle", "bite", "move", "action"}, true);
    effectTargetsWild_ = false;
    effectTimer_ = 1.05f;
}
void PrototypeRomRenderer::playPlayerHit() { playAnimation(player_, {"damage", "hit", "yarare"}, true); }
void PrototypeRomRenderer::playWildHit() { playAnimation(wild_, {"damage", "hit", "yarare"}, true); }
void PrototypeRomRenderer::playPlayerFaint() { playAnimation(player_, {"down", "dead", "faint", "die"}, true); }
void PrototypeRomRenderer::playWildFaint() { playAnimation(wild_, {"down", "dead", "faint", "die"}, true); }

void PrototypeRomRenderer::setupPerspective(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(std::max(1, width)) / std::max(1, height);
    const double top = .42;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, .8, 120.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void PrototypeRomRenderer::setupOverlay(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void PrototypeRomRenderer::drawVisual(Visual& visual, float x, float y, float z, float scale, float yaw) {
    if (!visual.scene) return;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw, 0, 1, 0);
    const float fit = visual.fitScale * scale;
    glScalef(fit, fit, fit);
    glTranslatef(-visual.center.x, -visual.center.y, -visual.center.z);
    drawSceneMeshes(*visual.scene, visual.textures, visual.current, visual.frame);
    glPopMatrix();
}

void PrototypeRomRenderer::drawStage() {
    if (!stage_.scene) return;
    // One real Rumble World field, framed as the play surface rather than as a backdrop.
    drawVisual(stage_, 0.0f, -0.35f, -11.4f, 8.2f, 0.0f);
}

void PrototypeRomRenderer::drawShadow(float x, float y, float z, float rx, float rz, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(.02f, .02f, .02f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 32; ++i) {
        const float a = i / 32.0f * 2.0f * kPi;
        glVertex3f(x + std::cos(a) * rx, y, z + std::sin(a) * rz);
    }
    glEnd();
}

void PrototypeRomRenderer::drawFallbackEffect(float x, float y, float z, float phase) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float pulse = std::sin(std::clamp(phase, 0.0f, 1.0f) * kPi);
    const float r = .20f + pulse * .72f;
    const float alpha = std::max(0.0f, 1.0f - phase);
    glColor4f(1.0f, .88f, .25f, alpha);
    glBegin(GL_LINES);
    for (int i = 0; i < 12; ++i) {
        const float a = i / 12.0f * 2.0f * kPi;
        glVertex3f(x + std::cos(a) * .12f, y + std::sin(a) * .12f, z);
        glVertex3f(x + std::cos(a) * r, y + std::sin(a) * r, z);
    }
    glEnd();
}

void PrototypeRomRenderer::drawAttackEffect() {
    if (effectTimer_ <= 0) return;
    const float phase = 1.0f - effectTimer_ / 1.05f;
    const float x = effectTargetsWild_ ? 2.25f : -2.25f;
    const float y = .05f;
    const float z = effectTargetsWild_ ? -8.8f : -6.9f;
    if (effect_.scene) drawVisual(effect_, x, y, z, .55f, 0);
    else drawFallbackEffect(x, y, z, phase);
}

void PrototypeRomRenderer::renderWorld(int width, int height) {
    setupPerspective(width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Rumble-style elevated gameplay camera: the level is the floor filling the frame,
    // not a mesh standing vertically behind the Pokémon.
    glPushMatrix();
    glTranslatef(0.0f, -2.15f, 0.0f);
    glRotatef(22.0f, 1.0f, 0.0f, 0.0f);
    drawStage();

    switch (page_) {
        case GamePage::StarterSelect:
            drawShadow(-2.2f, .40f, -7.3f, .42f, .24f, .32f);
            drawShadow(0.0f, .40f, -7.7f, .42f, .24f, .32f);
            drawShadow(2.2f, .40f, -7.3f, .42f, .24f, .32f);
            drawVisual(player_, -2.2f, .72f, -7.3f, .52f, 8);
            drawVisual(wild_, 0.0f, .72f, -7.7f, .52f, 0);
            drawVisual(third_, 2.2f, .72f, -7.3f, .52f, -8);
            break;
        case GamePage::Hub:
            drawShadow(0.0f, .40f, -7.2f, .42f, .24f, .32f);
            drawVisual(player_, 0.0f, .72f, -7.2f, .55f, 0);
            break;
        case GamePage::Hunt:
            drawShadow(.55f, .40f, -7.8f, .42f, .24f, .32f);
            drawVisual(wild_, .55f, .72f, -7.8f, .55f, -10);
            break;
        case GamePage::Battle: {
            const float phase = effectTimer_ > 0.0f ? 1.0f - effectTimer_ / 1.05f : 0.0f;
            const float pulse = effectTimer_ > 0.0f ? std::sin(std::clamp(phase, 0.0f, 1.0f) * kPi) : 0.0f;
            const float playerLunge = effectTargetsWild_ ? pulse * .72f : 0.0f;
            const float wildLunge = !effectTargetsWild_ ? pulse * .72f : 0.0f;
            const float playerRecoil = !effectTargetsWild_ ? pulse * .16f : 0.0f;
            const float wildRecoil = effectTargetsWild_ ? pulse * .16f : 0.0f;

            const float px = -2.35f + playerLunge - playerRecoil;
            const float wx = 2.35f - wildLunge + wildRecoil;
            drawShadow(px, .40f, -6.9f, .46f, .26f, .34f);
            drawShadow(wx, .40f, -8.8f, .46f, .26f, .34f);
            drawVisual(player_, px, .72f, -6.9f, .50f, 32);
            drawVisual(wild_, wx, .72f, -8.8f, .50f, -32);
            drawAttackEffect();
            break;
        }
        default:
            break;
    }
    glPopMatrix();
}

void PrototypeRomRenderer::drawPanel(float x, float y, float w, float h, float a) {
    uiSkin_.drawPanel(x, y, w, h, a);
}
float PrototypeRomRenderer::textWidth(const std::wstring& t, int k) const { return uiSkin_.textWidth(t, k); }
void PrototypeRomRenderer::drawText(float x, float y, const std::wstring& t, int k, bool c) { uiSkin_.drawText(x, y, t, k, c); }
void PrototypeRomRenderer::addHitTarget(GameAction a, const RectF& r, bool e) {
    if (e && a != GameAction::None) hitTargets_.push_back({a, r});
}
void PrototypeRomRenderer::drawButton(const RectF& r, const std::wstring& label, GameAction a, bool enabled) {
    const bool hover = enabled && a == hoverAction_;
    uiSkin_.drawButton(r.x, r.y, r.w, r.h, hover, enabled);
    drawText(r.x + r.w * .5f, r.y + r.h * .62f, label, 1, true);
    addHitTarget(a, r, enabled);
}

void PrototypeRomRenderer::drawHpPanel(const RectF& r, const std::wstring& name, int level,
                                       int hp, int maxHp, bool shiny, bool) {
    drawPanel(r.x, r.y, r.w, r.h, .90f);
    drawText(r.x + 16, r.y + 28, shiny ? L"[SHINY] " + name : name, 1, false);
    drawText(r.x + r.w - 86, r.y + 28, L"Nv. " + std::to_wstring(level), 0, false);
    const float bx = r.x + 16, by = r.y + 48, bw = r.w - 32;
    const float ratio = std::clamp(static_cast<float>(hp) / std::max(1, maxHp), 0.0f, 1.0f);
    glDisable(GL_TEXTURE_2D);
    glColor4f(.05f, .07f, .08f, .86f);
    glBegin(GL_QUADS);
    glVertex2f(bx, by); glVertex2f(bx + bw, by); glVertex2f(bx + bw, by + 11); glVertex2f(bx, by + 11);
    glEnd();
    if (ratio > .5f) glColor4f(.25f, .86f, .36f, 1);
    else if (ratio > .2f) glColor4f(.95f, .75f, .18f, 1);
    else glColor4f(.92f, .25f, .22f, 1);
    glBegin(GL_QUADS);
    glVertex2f(bx, by); glVertex2f(bx + bw * ratio, by); glVertex2f(bx + bw * ratio, by + 11); glVertex2f(bx, by + 11);
    glEnd();
}

std::wstring PrototypeRomRenderer::cropText(const std::wstring& v, std::size_t m) const {
    if (v.size() <= m) return v;
    if (m < 4) return v.substr(0, m);
    return v.substr(0, m - 3) + L"...";
}

void PrototypeRomRenderer::renderOverlay(int width, int height) {
    setupOverlay(width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    hitTargets_.clear();
    const float W = static_cast<float>(width), H = static_cast<float>(height);

    switch (page_) {
        case GamePage::MainMenu: {
            drawPanel(42, 42, std::min(520.0f, W * .48f), 118, .93f);
            drawText(64, 92, L"PROMPTEMON", 2, false);
            drawText(66, 132, L"STEIDIUM", 1, false);
            const float bw = std::min(360.0f, W * .34f);
            float y = H * .37f;
            drawButton({54, y, bw, 54}, L"Nueva partida", GameAction::NewGame, true); y += 64;
            drawButton({54, y, bw, 54}, L"Cargar partida", GameAction::None, false); y += 64;
            drawButton({54, y, bw, 54}, L"Borrar partida", GameAction::None, false); y += 64;
            drawButton({54, y, bw, 54}, L"Exportar partida", GameAction::None, false); y += 64;
            drawButton({54, y, bw, 54}, L"Opciones", GameAction::Options, true);
            break;
        }
        case GamePage::Intro:
            drawPanel(50, H - 240, W - 100, 180, .94f);
            drawText(78, H - 198, L"Bienvenido a Promptemon Steidium", 1, false);
            drawText(78, H - 158, L"Caza, entrena tus IV y encuentra variantes shiny.", 0, false);
            drawButton({W - 270, H - 116, 200, 46}, L"Continuar", GameAction::IntroContinue, true);
            break;
        case GamePage::StarterSelect: {
            drawPanel(40, 30, W - 80, 72, .90f);
            drawText(W * .5f, 75, L"ELIGE TU PRIMER COMPANERO", 2, true);
            const float cw = W / 3;
            drawButton({18, H - 94, cw - 30, 56}, L"Rattata | IV Velocidad", GameAction::StarterRattata, true);
            drawButton({cw + 6, H - 94, cw - 30, 56}, L"Pidgey | IV Velocidad", GameAction::StarterPidgey, true);
            drawButton({cw * 2 + 6, H - 94, cw - 24, 56}, L"Caterpie | IV PS", GameAction::StarterCaterpie, true);
            break;
        }
        case GamePage::Hub:
            drawPanel(34, 30, std::min(500.0f, W * .45f), 118, .90f);
            drawText(54, 67, playerName_ + L"  Nv. " + std::to_wstring(playerLevel_), 1, false);
            drawText(54, 99, L"XP " + std::to_wstring(xp_) + L"/100   Victorias " + std::to_wstring(wins_) + L"   Derrotas " + std::to_wstring(losses_), 0, false);
            drawButton({W - 310, H - 82, 260, 48}, L"Entrar a Caza", GameAction::Hunt, true);
            drawButton({40, H - 76, 170, 42}, L"Menu", GameAction::Back, true);
            break;
        case GamePage::Hunt:
            drawPanel(36, 32, std::min(450.0f, W * .40f), 104, .90f);
            drawText(56, 68, shiny_ ? L"[SHINY] " + wildName_ : wildName_, 1, false);
            drawText(56, 99, L"Nv. " + std::to_wstring(wildLevel_) + L"   Aporta IV: " + ivReward_, 0, false);
            drawButton({W - 294, H - 84, 244, 50}, L"Combatir", GameAction::StartBattle, true);
            drawButton({40, H - 78, 180, 44}, L"Volver al Hub", GameAction::ReturnHub, true);
            break;
        case GamePage::Battle: {
            const float pw = std::min(340.0f, W * .30f);
            drawHpPanel({24, 22, pw, 72}, playerName_, playerLevel_, playerHp_, playerMaxHp_, false, false);
            drawHpPanel({W - pw - 24, 22, pw, 72}, wildName_, wildLevel_, wildHp_, wildMaxHp_, shiny_, true);
            drawPanel(24, H - 128, W - 48, 96, .90f);
            drawText(44, H - 98, cropText(battleLog_, 78), 0, false);
            if (!battleOver_) {
                const float mw = (W - 120) * .5f;
                drawButton({40, H - 78, mw, 42}, move1_, GameAction::Move1, true);
                drawButton({80 + mw, H - 78, mw, 42}, move2_, GameAction::Move2, true);
            } else {
                drawButton({40, H - 78, 220, 42}, L"Otro encuentro", GameAction::NextBattle, true);
                drawButton({W - 260, H - 78, 220, 42}, L"Volver al Hub", GameAction::ReturnHub, true);
            }
            break;
        }
        case GamePage::Options:
            drawPanel(W * .18f, H * .16f, W * .64f, H * .66f, .94f);
            drawText(W * .5f, H * .24f, L"OPCIONES", 2, true);
            drawText(W * .34f, H * .36f, L"Volumen de audio: " + std::to_wstring(volume_) + L"%", 1, false);
            drawButton({W * .33f, H * .40f, 90, 48}, L"-", GameAction::VolumeDown, volume_ > 0);
            drawButton({W * .58f, H * .40f, 90, 48}, L"+", GameAction::VolumeUp, volume_ < 100);
            drawText(W * .34f, H * .54f, L"Modo de pantalla", 1, false);
            drawButton({W * .26f, H * .59f, 220, 50}, L"Pantalla completa", GameAction::Fullscreen, !fullscreen_);
            drawButton({W * .55f, H * .59f, 180, 50}, L"Ventana", GameAction::Windowed, fullscreen_);
            drawButton({W * .5f - 90, H * .73f, 180, 46}, L"Volver", GameAction::Back, true);
            break;
    }
    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::render() {
    if (!window_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);
    RECT c{};
    GetClientRect(window_, &c);
    const int w = std::max(1, static_cast<int>(c.right - c.left));
    const int h = std::max(1, static_cast<int>(c.bottom - c.top));
    glViewport(0, 0, w, h);
    glClearColor(.08f, .12f, .16f, 1);
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

    auto advance = [dt](Visual& v) {
        if (!v.current) return;
        v.frame += dt * 30.0f;
        if (v.current->frames > 0 && v.frame >= v.current->frames) {
            if (v.current->loop) v.frame = std::fmod(v.frame, v.current->frames);
            else {
                v.current = v.idle;
                v.frame = 0.0f;
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

LRESULT CALLBACK PrototypeRomRenderer::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PrototypeRomRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* c = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<PrototypeRomRenderer*>(c->lpCreateParams);
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
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (self) self->render();
            return 0;
        case WM_MOUSEMOVE:
            if (self) {
                const auto a = self->hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (a != self->hoverAction_) {
                    self->hoverAction_ = a;
                    self->render();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (self && self->parent_) {
                const auto a = self->hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (a != GameAction::None)
                    SendMessageW(self->parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(a), BN_CLICKED), 0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace promptemon::demo
