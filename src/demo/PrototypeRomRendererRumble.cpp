#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
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
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsAny(const std::string& value, const std::vector<std::string>& tokens) {
    const std::string p = lowerAscii(value);
    for (const auto& token : tokens) {
        if (p.find(lowerAscii(token)) != std::string::npos) return true;
    }
    return false;
}

bool sceneryMesh(const std::string& name) {
    const std::string n = lowerAscii(name);
    return n.find("sky") != std::string::npos ||
           n.find("cloud") != std::string::npos ||
           n.find("horizon") != std::string::npos ||
           n.find("far") != std::string::npos ||
           n.find("backdrop") != std::string::npos ||
           n.find("background") != std::string::npos;
}

bool collisionResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    const std::string name = std::filesystem::path(p).filename().string();
    return name.rfind("attr", 0) == 0 ||
           name.find("collision") != std::string::npos ||
           name.find("collider") != std::string::npos ||
           name.rfind("col_", 0) == 0;
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
                     const std::unordered_map<std::string, unsigned int>& textures,
                     const bch::Animation* animation,
                     float frame,
                     bool skipScenery = false) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        if (skipScenery && sceneryMesh(mesh.name)) continue;

        const auto* texture = scene.textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < scene.materials().size()
            ? &scene.materials()[mesh.materialId]
            : nullptr;
        GLuint textureId = 0;
        if (texture) {
            const auto found = textures.find(texture->name);
            if (found != textures.end()) textureId = static_cast<GLuint>(found->second);
        }

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.82f, .84f, .82f, 1.0f);
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

std::string findModelByBasename(storage::RomAssetCache* archive,
                                const std::vector<std::string>& basenames) {
    if (!archive) return {};
    const auto models = archive->modelResources();
    for (const auto& base : basenames) {
        const std::string suffix = "/" + lowerAscii(base);
        for (const auto& resource : models) {
            const std::string p = lowerAscii(resource);
            if (p.find("pii/") != std::string::npos && endsWith(p, suffix)) return resource;
        }
        for (const auto& resource : models) {
            if (endsWith(lowerAscii(resource), suffix)) return resource;
        }
    }
    return {};
}

std::string resolveCanonicalModel(storage::RomAssetCache* archive,
                                  const std::string& requested,
                                  const std::wstring& displayName) {
    const char* base = canonicalModelBase(displayName);
    if (!base) return requested;
    const auto resolved = findModelByBasename(archive, {base});
    return resolved.empty() ? requested : resolved;
}

float yawToward(float fromX, float fromZ, float toX, float toZ) {
    return std::atan2(toX - fromX, -(toZ - fromZ)) * 180.0f / kPi;
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
        releaseStageComposition();
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

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string& modelPath) const {
    if (!archive_) return {};

    std::string stem = lowerAscii(std::filesystem::path(modelPath).stem().string());
    for (const auto& resource : archive_->modelResources()) {
        const std::string p = lowerAscii(resource);
        if (p == lowerAscii(modelPath)) continue;
        if (p.find(stem) == std::string::npos) continue;
        if (p.find("anim") == std::string::npos && p.find("motion") == std::string::npos) continue;
        try {
            return archive_->extractBch(resource);
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
        std::unique_ptr<bch::Scene> scene;
        if (!animation.empty()) scene = std::make_unique<bch::Scene>(bch::Scene::parse(model, animation));
        else scene = std::make_unique<bch::Scene>(bch::Scene::parse(model));
        if (scene->meshes().empty()) return false;

        target.modelPath = modelPath;
        target.scene = std::move(scene);
        target.frame = 0.0f;
        target.idle = target.scene->findAnimation("Pii_anim_nomove_kamae");
        if (!target.idle) target.idle = chooseAnimation(target, {"nomove", "kamae", "idle", "wait", "stand", "fly"}, false);
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
    const auto pose = target.scene->samplePose(nullptr, 0.0f);
    bool found = false;

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
    if (extent > 1e-5f) target.fitScale = 1.75f / extent;
}

void PrototypeRomRenderer::releaseStageComposition() {
    for (auto& part : stageParts_) releaseVisual(part);
    stageParts_.clear();
    stageDirectory_.clear();
    stageBoundsMin_ = {};
    stageBoundsMax_ = {};
}

bool PrototypeRomRenderer::loadStageComposition(const std::string& requestedModel) {
    if (!archive_) return false;
    releaseStageComposition();

    const auto models = archive_->modelResources();
    std::map<std::string, int> scores;
    std::map<std::string, std::vector<std::string>> groups;

    for (const auto& resource : models) {
        const std::string p = lowerAscii(resource);
        if (p.find("stage/") == std::string::npos || !endsWith(p, ".bch")) continue;
        if (collisionResource(p)) continue;

        const auto slash = p.find_last_of('/');
        if (slash == std::string::npos) continue;
        const std::string directory = p.substr(0, slash + 1);
        groups[directory].push_back(resource);

        int score = 10;
        if (p.find("grass") != std::string::npos) score += 80;
        if (p.find("forest") != std::string::npos) score += 70;
        if (p.find("field") != std::string::npos) score += 60;
        if (p.find("beach") != std::string::npos) score += 45;
        if (p.find("town") != std::string::npos) score += 20;
        if (p.find("miiinfo") != std::string::npos) score -= 200;
        scores[directory] += score;
    }

    std::string requestedDirectory;
    const std::string requestedLower = lowerAscii(requestedModel);
    if (const auto slash = requestedLower.find_last_of('/'); slash != std::string::npos)
        requestedDirectory = requestedLower.substr(0, slash + 1);

    std::string selected;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto& [directory, paths] : groups) {
        int score = scores[directory] + static_cast<int>(paths.size()) * 25;
        if (!requestedDirectory.empty() && directory == requestedDirectory) score += 60;
        if (score > bestScore) {
            bestScore = score;
            selected = directory;
        }
    }

    if (selected.empty()) return false;
    stageDirectory_ = selected;

    auto paths = groups[selected];
    std::sort(paths.begin(), paths.end());
    if (paths.size() > 48) paths.resize(48);

    for (const auto& path : paths) {
        Visual part;
        if (loadVisual(part, path, false)) stageParts_.push_back(std::move(part));
    }

    if (stageParts_.empty()) return false;
    updateStageCompositionBounds();
    return true;
}

void PrototypeRomRenderer::updateStageCompositionBounds() {
    bch::Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    bch::Vec3 mx{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    bool found = false;

    for (auto& part : stageParts_) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            if (sceneryMesh(mesh.name)) continue;
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                found = true;
            }
        }
    }

    if (!found) {
        mn = stageParts_.front().boundsMin;
        mx = stageParts_.front().boundsMax;
    }
    stageBoundsMin_ = mn;
    stageBoundsMax_ = mx;
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
        if (preferNonLoop && !clip.loop) score += 45;
        if (preferNonLoop && clip.loop) score -= 20;
        if (clip.frames > 4.0f) score += 10;
        if (score > bestScore) {
            bestScore = score;
            best = &clip;
        }
    }
    return best;
}

void PrototypeRomRenderer::playAnimation(
    Visual& visual, const std::vector<std::string>& tokens, bool preferNonLoop) {
    if (const auto* clip = chooseAnimation(visual, tokens, preferNonLoop)) {
        visual.current = clip;
        visual.frame = 0.0f;
    }
}

void PrototypeRomRenderer::setStageModel(const std::string& modelPath) {
    stagePath_ = modelPath;
    loadStageComposition(modelPath);
}

void PrototypeRomRenderer::setEffectModel(const std::string& modelPath) {
    effectPath_ = modelPath;
    const std::string p = lowerAscii(modelPath);
    if (p.find(".bch") == std::string::npos) {
        effectPath_.clear();
        releaseVisual(effect_);
        return;
    }
    ensureVisual(effect_, effectPath_, true);
}

void PrototypeRomRenderer::showMainMenu() {
    page_ = GamePage::MainMenu;
    if (stageParts_.empty()) loadStageComposition(stagePath_);

    const auto first = findModelByBasename(archive_, {
        "houou.bch", "rayquaza.bch", "groudon.bch", "zekrom.bch", "reshiram.bch", "mewtwo.bch"
    });
    const auto second = findModelByBasename(archive_, {
        "rugia.bch", "lugia.bch", "kyogre.bch", "xerneas.bch", "yveltal.bch", "dialga.bch"
    });

    if (!first.empty()) ensureVisual(player_, first, true);
    if (!second.empty()) ensureVisual(wild_, second, true);
    menuAttackBeat_ = -1;
    render();
}

void PrototypeRomRenderer::showIntro() {
    page_ = GamePage::Intro;
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
    const bool entering = page_ != GamePage::Battle;
    page_ = GamePage::Battle;
    playerName_ = pn; playerLevel_ = pl; playerHp_ = php; playerMaxHp_ = std::max(1, pmax);
    wildName_ = wn; wildLevel_ = wl; wildHp_ = whp; wildMaxHp_ = std::max(1, wmax);
    ivReward_ = iv; shiny_ = shiny; move1_ = m1; move2_ = m2; battleLog_ = log;
    battleOver_ = over; battleWon_ = won;

    const bool a = ensureVisual(player_, resolveCanonicalModel(archive_, pm, pn), true);
    const bool b = ensureVisual(wild_, resolveCanonicalModel(archive_, wm, wn), true);
    if (entering) {
        playerFieldX_ = -3.6f;
        playerFieldZ_ = 2.2f;
        wildFieldX_ = 3.2f;
        wildFieldZ_ = -1.8f;
        playerYaw_ = yawToward(playerFieldX_, playerFieldZ_, wildFieldX_, wildFieldZ_);
        attack1Latch_ = attack2Latch_ = false;
        if (window_) SetFocus(window_);
    }
    render();
    return a && b;
}

void PrototypeRomRenderer::playPlayerAction() {
    playAnimation(player_, {"waza", "attack", "rush", "tackle", "bite", "move", "action"}, true);
    effectTargetsWild_ = true;
    effectTimer_ = 1.0f;
}

void PrototypeRomRenderer::playWildAction() {
    playAnimation(wild_, {"waza", "attack", "rush", "tackle", "bite", "move", "action"}, true);
    effectTargetsWild_ = false;
    effectTimer_ = 1.0f;
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
    const double top = .36;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, .55, 300.0);
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

bool PrototypeRomRenderer::playerInAttackRange() const {
    return page_ == GamePage::Battle && !battleOver_;
}

void PrototypeRomRenderer::updateBattleInput(float dt) {
    (void)dt;
    if (page_ != GamePage::Battle || battleOver_) return;
    const bool attack1 = (GetAsyncKeyState('Z') & 0x8000) ||
                         (GetAsyncKeyState('J') & 0x8000) ||
                         (GetAsyncKeyState(VK_SPACE) & 0x8000);
    const bool attack2 = (GetAsyncKeyState('X') & 0x8000) ||
                         (GetAsyncKeyState('K') & 0x8000);

    if (attack1 && !attack1Latch_ && parent_ && effectTimer_ <= 0.0f)
        SendMessageW(parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(GameAction::Move1), BN_CLICKED), 0);
    if (attack2 && !attack2Latch_ && parent_ && effectTimer_ <= 0.0f)
        SendMessageW(parent_, WM_COMMAND, MAKEWPARAM(static_cast<int>(GameAction::Move2), BN_CLICKED), 0);

    attack1Latch_ = attack1;
    attack2Latch_ = attack2;
}

void PrototypeRomRenderer::drawVisual(Visual& visual, float x, float y, float z, float scale, float yaw) {
    if (!visual.scene) return;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw, 0, 1, 0);
    const float fit = visual.fitScale * scale;
    glScalef(fit, fit, fit);
    glTranslatef(-visual.center.x, -visual.center.y, -visual.center.z);
    drawSceneMeshes(*visual.scene, visual.textures, visual.current, visual.frame, false);
    glPopMatrix();
}

void PrototypeRomRenderer::drawStageComposition() {
    if (stageParts_.empty()) return;

    const float spanX = std::max(.001f, stageBoundsMax_.x - stageBoundsMin_.x);
    const float spanZ = std::max(.001f, stageBoundsMax_.z - stageBoundsMin_.z);
    const float centerX = (stageBoundsMin_.x + stageBoundsMax_.x) * .5f;
    const float centerZ = (stageBoundsMin_.z + stageBoundsMax_.z) * .5f;
    const float floorY = stageBoundsMin_.y;

    // Deliberately fill the viewport. A Rumble stage is the world around the camera,
    // never a miniature that must fit completely inside the screen.
    const float targetX = page_ == GamePage::MainMenu ? 34.0f : 31.0f;
    const float targetZ = page_ == GamePage::MainMenu ? 26.0f : 23.0f;
    const float worldScale = std::max(targetX / spanX, targetZ / spanZ);

    glPushMatrix();
    glScalef(worldScale, worldScale, worldScale);
    glTranslatef(-centerX, -floorY, -centerZ);
    for (auto& part : stageParts_) {
        if (!part.scene) continue;
        drawSceneMeshes(*part.scene, part.textures, nullptr, 0.0f, true);
    }
    glPopMatrix();
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
    const float radius = .18f + pulse * .95f;
    const float alpha = std::max(0.0f, 1.0f - phase);
    glColor4f(1.0f, .90f, .28f, alpha);
    glBegin(GL_LINES);
    for (int i = 0; i < 18; ++i) {
        const float a = i / 18.0f * 2.0f * kPi;
        glVertex3f(x + std::cos(a) * .12f, y + std::sin(a) * .12f, z);
        glVertex3f(x + std::cos(a) * radius, y + std::sin(a) * radius, z);
    }
    glEnd();
}

void PrototypeRomRenderer::drawMenuBattleEffect(float x, float y, float z, float phase) {
    drawFallbackEffect(x, y, z, phase);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const float p = std::sin(std::clamp(phase, 0.0f, 1.0f) * kPi);
    glColor4f(.65f, .88f, 1.0f, .65f * (1.0f - phase));
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 24; ++i) {
        const float a = i / 24.0f * 2.0f * kPi;
        glVertex3f(x + std::cos(a) * (.18f + p * .55f), y, z + std::sin(a) * (.18f + p * .55f));
    }
    glEnd();
}

void PrototypeRomRenderer::drawAttackEffect() {
    if (effectTimer_ <= 0.0f) return;
    const float phase = 1.0f - effectTimer_;
    const float x = effectTargetsWild_ ? wildFieldX_ : playerFieldX_;
    const float z = effectTargetsWild_ ? wildFieldZ_ : playerFieldZ_;
    if (effect_.scene) drawVisual(effect_, x, .55f, z, .50f, 0.0f);
    else drawFallbackEffect(x, .55f, z, phase);
}

void PrototypeRomRenderer::renderWorld(int width, int height) {
    setupPerspective(width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float t = elapsedSeconds_;
    const float cameraSway = std::sin(t * .36f) * 4.2f;
    const float cameraLift = std::sin(t * .48f) * .22f;
    const float cameraSlide = std::sin(t * .27f) * .75f;

    glPushMatrix();

    if (page_ == GamePage::MainMenu || page_ == GamePage::Intro || page_ == GamePage::Options) {
        glTranslatef(cameraSlide, -3.25f + cameraLift, -16.2f);
        glRotatef(37.5f + std::sin(t * .22f) * 1.4f, 1, 0, 0);
        glRotatef(cameraSway, 0, 1, 0);
        drawStageComposition();

        const float cycle = std::fmod(t, 6.0f);
        float ax = -4.6f;
        float az = 1.1f;
        float bx = 4.4f;
        float bz = -1.0f;
        const float bobA = .58f + std::sin(t * 2.15f) * .16f;
        const float bobB = .62f + std::sin(t * 2.05f + 1.4f) * .18f;

        if (cycle < 1.25f) {
            const float p = std::sin((cycle / 1.25f) * kPi);
            ax += p * 3.0f;
            az -= p * 1.25f;
            drawMenuBattleEffect(bx, .75f, bz, std::clamp(cycle / 1.25f, 0.0f, 1.0f));
        } else if (cycle >= 3.0f && cycle < 4.25f) {
            const float p = std::sin(((cycle - 3.0f) / 1.25f) * kPi);
            bx -= p * 3.0f;
            bz += p * 1.25f;
            drawMenuBattleEffect(ax, .75f, az, std::clamp((cycle - 3.0f) / 1.25f, 0.0f, 1.0f));
        }

        drawShadow(ax, .02f, az, .72f, .42f, .30f);
        drawShadow(bx, .02f, bz, .72f, .42f, .30f);
        drawVisual(player_, ax, bobA, az, 1.05f, yawToward(ax, az, bx, bz));
        drawVisual(wild_, bx, bobB, bz, 1.05f, yawToward(bx, bz, ax, az));

        glPopMatrix();
        return;
    }

    if (page_ == GamePage::StarterSelect) {
        glTranslatef(0.0f, -3.0f, -15.5f);
        glRotatef(38.0f, 1, 0, 0);
        drawStageComposition();
        drawShadow(-3.4f, .02f, 0.7f, .45f, .27f, .30f);
        drawShadow(0.0f, .02f, 0.0f, .45f, .27f, .30f);
        drawShadow(3.4f, .02f, -0.7f, .45f, .27f, .30f);
        drawVisual(player_, -3.4f, .43f + std::sin(t * 2.0f) * .06f, .7f, .58f, 18.0f);
        drawVisual(wild_, 0.0f, .43f + std::sin(t * 2.0f + .8f) * .06f, 0.0f, .58f, 0.0f);
        drawVisual(third_, 3.4f, .43f + std::sin(t * 2.0f + 1.6f) * .06f, -.7f, .58f, -18.0f);
        glPopMatrix();
        return;
    }

    glTranslatef(cameraSlide * .25f, -3.35f, -16.0f);
    glRotatef(39.0f, 1, 0, 0);
    glRotatef(cameraSway * .25f, 0, 1, 0);
    drawStageComposition();

    switch (page_) {
        case GamePage::Hub:
            drawShadow(0.0f, .02f, 1.4f, .42f, .25f, .30f);
            drawVisual(player_, 0.0f, .42f + std::sin(t * 2.1f) * .05f, 1.4f, .55f, 180.0f + std::sin(t) * 4.0f);
            break;

        case GamePage::Hunt:
            drawShadow(-2.8f, .02f, 2.0f, .42f, .25f, .30f);
            drawShadow(2.6f, .02f, -1.5f, .42f, .25f, .30f);
            drawVisual(player_, -2.8f, .42f + std::sin(t * 2.2f) * .05f, 2.0f, .55f, 35.0f);
            drawVisual(wild_, 2.6f, .42f + std::sin(t * 2.0f + 1.0f) * .05f, -1.5f, .55f, -35.0f);
            break;

        case GamePage::Battle: {
            float px = playerFieldX_, pz = playerFieldZ_;
            float wx = wildFieldX_, wz = wildFieldZ_;
            if (effectTimer_ > 0.0f) {
                const float phase = 1.0f - effectTimer_;
                const float pulse = std::sin(std::clamp(phase, 0.0f, 1.0f) * kPi);
                float dx = wx - px, dz = wz - pz;
                const float dist = std::max(.001f, std::sqrt(dx * dx + dz * dz));
                dx /= dist; dz /= dist;
                if (effectTargetsWild_) { px += dx * pulse * 1.65f; pz += dz * pulse * 1.65f; }
                else { wx -= dx * pulse * 1.65f; wz -= dz * pulse * 1.65f; }
            }

            drawShadow(px, .02f, pz, .46f, .28f, .34f);
            drawShadow(wx, .02f, wz, .46f, .28f, .34f);
            drawVisual(player_, px, .44f + std::sin(t * 2.0f) * .04f, pz, .60f, yawToward(px, pz, wx, wz));
            drawVisual(wild_, wx, .44f + std::sin(t * 2.0f + 1.0f) * .04f, wz, .60f, yawToward(wx, wz, px, pz));
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

float PrototypeRomRenderer::textWidth(const std::wstring& t, int k) const {
    return uiSkin_.textWidth(t, k);
}

void PrototypeRomRenderer::drawText(float x, float y, const std::wstring& t, int k, bool c) {
    uiSkin_.drawText(x, y, t, k, c);
}

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
            const float bw = std::min(330.0f, W * .30f);
            const float x = 54.0f;
            float y = H * .42f;
            drawButton({x, y, bw, 48}, L"Nueva partida", GameAction::NewGame, true); y += 56;
            drawButton({x, y, bw, 48}, L"Cargar partida", GameAction::None, false); y += 56;
            drawButton({x, y, bw, 48}, L"Borrar partida", GameAction::None, false); y += 56;
            drawButton({x, y, bw, 48}, L"Exportar partida", GameAction::None, false); y += 56;
            drawButton({x, y, bw, 48}, L"Opciones", GameAction::Options, true);
            drawText(58, 88, L"PROMPTEMON", 2, false);
            drawText(60, 120, L"STEIDIUM", 1, false);
            break;
        }
        case GamePage::Intro:
            drawPanel(50, H - 214, W - 100, 154, .88f);
            drawText(78, H - 174, L"Bienvenido a Promptemon Steidium", 1, false);
            drawText(78, H - 138, L"Caza, entrena tus IV y encuentra variantes shiny.", 0, false);
            drawButton({W - 270, H - 112, 200, 44}, L"Continuar", GameAction::IntroContinue, true);
            break;
        case GamePage::StarterSelect: {
            drawPanel(40, 28, W - 80, 68, .82f);
            drawText(W * .5f, 72, L"ELIGE TU PRIMER COMPANERO", 2, true);
            const float cw = W / 3;
            drawButton({18, H - 88, cw - 30, 52}, L"Rattata | IV Velocidad", GameAction::StarterRattata, true);
            drawButton({cw + 6, H - 88, cw - 30, 52}, L"Pidgey | IV Velocidad", GameAction::StarterPidgey, true);
            drawButton({cw * 2 + 6, H - 88, cw - 24, 52}, L"Caterpie | IV PS", GameAction::StarterCaterpie, true);
            break;
        }
        case GamePage::Hub:
            drawPanel(28, 24, std::min(470.0f, W * .42f), 106, .80f);
            drawText(48, 58, playerName_ + L"  Nv. " + std::to_wstring(playerLevel_), 1, false);
            drawText(48, 90, L"XP " + std::to_wstring(xp_) + L"/100   Victorias " + std::to_wstring(wins_) + L"   Derrotas " + std::to_wstring(losses_), 0, false);
            drawButton({W - 286, H - 76, 246, 46}, L"Entrar a Caza", GameAction::Hunt, true);
            drawButton({36, H - 72, 154, 40}, L"Menu", GameAction::Back, true);
            break;
        case GamePage::Hunt:
            drawPanel(28, 24, 350, 82, .80f);
            drawText(46, 54, shiny_ ? L"[SHINY] " + wildName_ : wildName_, 1, false);
            drawText(46, 82, L"Nv. " + std::to_wstring(wildLevel_) + L"  IV: " + ivReward_, 0, false);
            drawButton({W - 238, H - 68, 200, 40}, L"Entrar", GameAction::StartBattle, true);
            drawButton({34, H - 64, 152, 36}, L"Volver", GameAction::ReturnHub, true);
            break;
        case GamePage::Battle: {
            const float panelW = std::min(320.0f, W * .28f);
            drawHpPanel({24, H - 100, panelW, 72}, playerName_, playerLevel_, playerHp_, playerMaxHp_, false, false);
            drawHpPanel({W - panelW - 24, 22, panelW, 72}, wildName_, wildLevel_, wildHp_, wildMaxHp_, shiny_, true);
            if (!battleLog_.empty()) {
                drawPanel(W * .5f - 220, 20, 440, 46, .68f);
                drawText(W * .5f, 50, cropText(battleLog_, 58), 0, true);
            }
            if (!battleOver_) {
                const float bw = std::min(190.0f, W * .17f);
                drawButton({W - bw * 2 - 40, H - 88, bw, 48}, move1_, GameAction::Move1, effectTimer_ <= 0.0f);
                drawButton({W - bw - 24, H - 88, bw, 48}, move2_, GameAction::Move2, effectTimer_ <= 0.0f);
            } else {
                drawPanel(W * .5f - 180, H * .5f - 48, 360, 96, .84f);
                drawText(W * .5f, H * .5f - 10, battleWon_ ? L"Pokemon derrotado" : L"Has sido derrotado", 1, true);
                drawButton({W * .5f - 160, H * .5f + 10, 150, 34}, L"Otro encuentro", GameAction::NextBattle, true);
                drawButton({W * .5f + 10, H * .5f + 10, 150, 34}, L"Volver al Hub", GameAction::ReturnHub, true);
            }
            break;
        }
        case GamePage::Options:
            drawPanel(W * .18f, H * .16f, W * .64f, H * .66f, .88f);
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
    glClearColor(.49f, .78f, .94f, 1.0f);
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

    if (page_ == GamePage::MainMenu) {
        const int beat = static_cast<int>(std::floor(std::fmod(elapsedSeconds_, 6.0f)));
        if (beat != menuAttackBeat_) {
            if (beat == 0) playAnimation(player_, {"waza", "attack", "rush", "move", "action"}, true);
            if (beat == 3) playAnimation(wild_, {"waza", "attack", "rush", "move", "action"}, true);
            menuAttackBeat_ = beat;
        }
    }

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
