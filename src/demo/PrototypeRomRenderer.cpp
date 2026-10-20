#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace promptemon::demo {
namespace {

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float cosine = std::cos(material->uvRotation);
    const float sine = std::sin(material->uvRotation);
    const float x = uv.x - 0.5f;
    const float y = uv.y - 0.5f;
    const float rotatedU = x * cosine - y * sine + 0.5f;
    const float rotatedV = x * sine + y * cosine + 0.5f;
    return {
        rotatedU * material->uvScaleU - material->uvTranslateU,
        rotatedV * material->uvScaleV - material->uvTranslateV
    };
}

} // namespace

PrototypeRomRenderer::~PrototypeRomRenderer() {
    destroy();
}

bool PrototypeRomRenderer::create(HWND parent, rom::RomModelArchive* archive) {
    destroy();
    archive_ = archive;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptemonPrototypeRomRenderer";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    window_ = CreateWindowExW(
        0,
        L"PromptemonPrototypeRomRenderer",
        L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
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

    lastTick_ = std::chrono::steady_clock::now();
    return true;
}

void PrototypeRomRenderer::destroy() {
    if (gl_) {
        wglMakeCurrent(dc_, gl_);
        releaseVisual(player_);
        releaseVisual(wild_);
    }
    shutdownOpenGL();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    archive_ = nullptr;
}

void PrototypeRomRenderer::show(bool visible) {
    if (window_) ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

void PrototypeRomRenderer::move(int x, int y, int width, int height) {
    if (window_) MoveWindow(window_, x, y, std::max(1, width), std::max(1, height), TRUE);
}

bool PrototypeRomRenderer::initializeOpenGL() {
    if (!window_) return false;
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
    if (!gl_) return false;
    return wglMakeCurrent(dc_, gl_) == TRUE;
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
    for (const auto& path : candidates) {
        if (!archive_->has(path)) continue;
        try {
            return archive_->extractBch(path);
        } catch (...) {
        }
    }
    return {};
}

bool PrototypeRomRenderer::loadVisual(Visual& target, const std::string& modelPath) {
    if (!archive_ || modelPath.empty()) return false;
    try {
        const auto model = archive_->extractBch(modelPath);
        const auto animation = animationForModel(modelPath);
        auto scene = std::make_unique<bch::Scene>(bch::Scene::parse(model, animation));
        if (scene->meshes().empty()) return false;

        target.scene = std::move(scene);
        target.frame = 0.0f;
        target.idle = target.scene->findAnimation("Pii_anim_nomove_kamae");
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
        const GLuint id = static_cast<GLuint>(texture.second);
        if (id) glDeleteTextures(1, &id);
    }
    target.textures.clear();
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture.rgba.data());
        target.textures[texture.name] = static_cast<unsigned int>(id);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PrototypeRomRenderer::updateBounds(Visual& target) {
    target.center = {};
    target.fitScale = 1.0f;
    if (!target.scene || target.scene->meshes().empty()) return;

    bch::Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    bch::Vec3 maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    const auto bindPose = target.scene->samplePose(nullptr, 0.0f);
    bool found = false;

    for (const auto& mesh : target.scene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const bch::Vec3 point = target.scene->skin(vertex, bindPose);
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

    target.center = {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f
    };
    const float extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    if (extent > 1e-5f) target.fitScale = 1.75f / extent;
}

bool PrototypeRomRenderer::setSingleModel(const std::string& modelPath) {
    if (!gl_) return false;
    wglMakeCurrent(dc_, gl_);
    releaseVisual(player_);
    releaseVisual(wild_);
    battleMode_ = false;
    const bool loaded = loadVisual(player_, modelPath);
    render();
    return loaded;
}

bool PrototypeRomRenderer::setBattleModels(const std::string& playerModelPath, const std::string& wildModelPath) {
    if (!gl_) return false;
    wglMakeCurrent(dc_, gl_);
    releaseVisual(player_);
    releaseVisual(wild_);
    battleMode_ = true;
    const bool playerLoaded = loadVisual(player_, playerModelPath);
    const bool wildLoaded = loadVisual(wild_, wildModelPath);
    render();
    return playerLoaded && wildLoaded;
}

void PrototypeRomRenderer::clearModels() {
    if (!gl_) return;
    wglMakeCurrent(dc_, gl_);
    releaseVisual(player_);
    releaseVisual(wild_);
    battleMode_ = false;
    render();
}

void PrototypeRomRenderer::setupProjection(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double top = 0.58;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, 0.9, 40.0);
    glMatrixMode(GL_MODELVIEW);
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
            const auto found = visual.textures.find(texture->name);
            if (found != visual.textures.end()) textureId = static_cast<GLuint>(found->second);
        }

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.80f, 0.80f, 0.82f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const bch::Vec3 point = visual.scene->skin(vertex, pose);
            if (textureId) {
                const bch::Vec2 uv = materialUv(material, vertex.uv);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(point.x, point.y, point.z);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void PrototypeRomRenderer::render() {
    if (!window_ || !gl_) return;
    wglMakeCurrent(dc_, gl_);

    RECT client{};
    GetClientRect(window_, &client);
    setupProjection(client.right - client.left, client.bottom - client.top);

    glClearColor(0.72f, 0.86f, 0.67f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLoadIdentity();

    if (battleMode_) {
        drawVisual(player_, -0.85f, -0.18f, -4.0f, 0.82f, 25.0f);
        drawVisual(wild_, 0.85f, 0.05f, -4.4f, 0.82f, -25.0f);
    } else {
        drawVisual(player_, 0.0f, -0.05f, -3.7f, 1.0f, 0.0f);
    }

    SwapBuffers(dc_);
}

void PrototypeRomRenderer::tick() {
    if (!gl_) return;
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    dt = std::clamp(dt, 0.0f, 0.1f);

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
    render();
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
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace promptemon::demo
