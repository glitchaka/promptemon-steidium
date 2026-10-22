#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "bch/BchScene.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Vec2;
using promptemon::bch::Vec3;
using promptemon::storage::RomAssetCache;

struct StagePart {
    std::string path;
    std::unique_ptr<Scene> scene;
    std::unordered_map<std::string, GLuint> textures;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;
    std::vector<StagePart> parts;
    Vec3 stageMin{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 stageMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    float zoom = 3.0f;
};

App gApp;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const std::string& value, const std::initializer_list<const char*> tokens) {
    const std::string p = lowerAscii(value);
    for (const char* token : tokens) {
        if (p.find(token) != std::string::npos) return true;
    }
    return false;
}

std::string parentPath(const std::string& value) {
    const auto slash = value.find_last_of('/');
    return slash == std::string::npos ? std::string{} : value.substr(0, slash + 1);
}

std::string leafName(const std::string& value) {
    const auto slash = value.find_last_of('/');
    return lowerAscii(value.substr(slash == std::string::npos ? 0 : slash + 1));
}

bool isStageResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    return p.rfind("stage/", 0) == 0 || p.find("/stage/") != std::string::npos;
}

bool rejectStagePart(const std::string& path) {
    const std::string leaf = leafName(path);
    return leaf.rfind("attr", 0) == 0 ||
           leaf.rfind("col_", 0) == 0 ||
           containsAny(leaf, {"collision", "collider", "sky", "skydome", "background", "backdrop", "farbg", "far_", "dummy", "debug"});
}

bool preferredGround(const std::string& path) {
    return containsAny(path, {"ground", "floor", "terrain", "land", "field", "map"});
}

std::string chooseStageAnchor(const std::vector<std::string>& models) {
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos && preferredGround(path) && !rejectStagePart(path))
            return path;
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos && !rejectStagePart(path))
            return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && preferredGround(path) && !rejectStagePart(path)) return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && !rejectStagePart(path)) return path;
    }
    return {};
}

Vec2 materialUv(const Material* material, Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - .5f;
    const float y = uv.y - .5f;
    return {(x * c - y * s + .5f) * material->uvScaleU - material->uvTranslateU,
            (x * s + y * c + .5f) * material->uvScaleV - material->uvTranslateV};
}

bool computeBounds(StagePart& part) {
    if (!part.scene) return false;
    bool found = false;
    const auto pose = part.scene->samplePose(nullptr, 0.0f);
    for (const auto& mesh : part.scene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = part.scene->skin(vertex, pose);
            part.min.x = std::min(part.min.x, p.x);
            part.min.y = std::min(part.min.y, p.y);
            part.min.z = std::min(part.min.z, p.z);
            part.max.x = std::max(part.max.x, p.x);
            part.max.y = std::max(part.max.y, p.y);
            part.max.z = std::max(part.max.z, p.z);
            found = true;
        }
    }
    return found;
}

void uploadTextures(StagePart& part) {
    if (!part.scene) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto& texture : part.scene->textures()) {
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
        part.textures[texture.name] = id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void destroyStage() {
    for (auto& part : gApp.parts) {
        for (const auto& entry : part.textures) {
            GLuint id = entry.second;
            if (id) glDeleteTextures(1, &id);
        }
    }
    gApp.parts.clear();
}

bool loadStage() {
    const auto models = gApp.assets.modelResources();
    const std::string anchor = chooseStageAnchor(models);
    if (anchor.empty()) return false;

    const std::string directory = parentPath(lowerAscii(anchor));
    std::vector<std::string> candidates;
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (p.rfind(directory, 0) != 0 || rejectStagePart(path)) continue;
        candidates.push_back(path);
    }
    if (candidates.empty()) candidates.push_back(anchor);

    std::stable_sort(candidates.begin(), candidates.end(), [&](const std::string& a, const std::string& b) {
        if (a == anchor) return true;
        if (b == anchor) return false;
        return preferredGround(a) && !preferredGround(b);
    });

    destroyStage();
    gApp.stageMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    gApp.stageMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    bool haveAnchorBounds = false;
    float anchorX = 0.0f;
    float anchorZ = 0.0f;
    Vec3 anchorCenter{};

    for (const auto& path : candidates) {
        try {
            auto scene = std::make_unique<Scene>(Scene::parse(gApp.assets.extractBch(path)));
            if (scene->meshes().empty()) continue;

            StagePart part;
            part.path = path;
            part.scene = std::move(scene);
            if (!computeBounds(part)) continue;

            const float sx = std::max(0.001f, part.max.x - part.min.x);
            const float sz = std::max(0.001f, part.max.z - part.min.z);
            const Vec3 center{(part.min.x + part.max.x) * .5f,
                              (part.min.y + part.max.y) * .5f,
                              (part.min.z + part.max.z) * .5f};

            if (!haveAnchorBounds) {
                anchorX = sx;
                anchorZ = sz;
                anchorCenter = center;
                haveAnchorBounds = true;
            } else {
                const float dx = std::abs(center.x - anchorCenter.x);
                const float dz = std::abs(center.z - anchorCenter.z);
                if (sx > anchorX * 4.0f || sz > anchorZ * 4.0f ||
                    dx > anchorX * 2.5f || dz > anchorZ * 2.5f) {
                    continue;
                }
            }

            uploadTextures(part);
            gApp.stageMin.x = std::min(gApp.stageMin.x, part.min.x);
            gApp.stageMin.y = std::min(gApp.stageMin.y, part.min.y);
            gApp.stageMin.z = std::min(gApp.stageMin.z, part.min.z);
            gApp.stageMax.x = std::max(gApp.stageMax.x, part.max.x);
            gApp.stageMax.y = std::max(gApp.stageMax.y, part.max.y);
            gApp.stageMax.z = std::max(gApp.stageMax.z, part.max.z);
            gApp.parts.push_back(std::move(part));
        } catch (...) {
        }
    }

    return !gApp.parts.empty();
}

void drawPart(StagePart& part) {
    if (!part.scene) return;
    const auto pose = part.scene->samplePose(nullptr, 0.0f);
    for (const auto& mesh : part.scene->meshes()) {
        const auto* texture = part.scene->textureForMaterial(mesh.materialId);
        const Material* material = mesh.materialId < part.scene->materials().size()
            ? &part.scene->materials()[mesh.materialId]
            : nullptr;

        GLuint textureId = 0;
        if (texture) {
            const auto found = part.textures.find(texture->name);
            if (found != part.textures.end()) textureId = found->second;
        }

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.82f, .84f, .80f, 1.0f);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = part.scene->skin(vertex, pose);
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

void render() {
    if (!gApp.hwnd || !gApp.gl) return;
    wglMakeCurrent(gApp.dc, gApp.gl);

    RECT rc{};
    GetClientRect(gApp.hwnd, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(1L, rc.bottom - rc.top);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    glViewport(0, 0, width, height);
    glClearColor(.55f, .78f, .91f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float halfH = 10.0f;
    const float halfW = halfH * aspect;
    glOrtho(-halfW, halfW, -halfH, halfH, -200.0f, 200.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (!gApp.parts.empty()) {
        const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
        const float sy = std::max(.001f, gApp.stageMax.y - gApp.stageMin.y);
        const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
        const float projectedHeight = std::max(.001f, sy * .58f + sz * .82f);

        const float fitX = (halfW * 2.0f * .96f) / sx;
        const float fitY = (halfH * 2.0f * .94f) / projectedHeight;
        const float fit = std::min(fitX, fitY) * gApp.zoom;

        const float cx = (gApp.stageMin.x + gApp.stageMax.x) * .5f;
        const float cy = (gApp.stageMin.y + gApp.stageMax.y) * .5f;
        const float cz = (gApp.stageMin.z + gApp.stageMax.z) * .5f;

        glScalef(fit, fit, fit);
        glRotatef(55.0f, 1, 0, 0);
        glRotatef(-45.0f, 0, 1, 0);
        glTranslatef(-cx, -cy, -cz);

        for (auto& part : gApp.parts) drawPart(part);
    }

    SwapBuffers(gApp.dc);
}

bool initOpenGL(HWND hwnd) {
    gApp.dc = GetDC(hwnd);
    if (!gApp.dc) return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(gApp.dc, &pfd);
    if (!format || !SetPixelFormat(gApp.dc, format, &pfd)) return false;

    gApp.gl = wglCreateContext(gApp.dc);
    if (!gApp.gl || !wglMakeCurrent(gApp.dc, gApp.gl)) return false;
    return true;
}

void shutdownOpenGL() {
    if (gApp.gl) {
        wglMakeCurrent(gApp.dc, gApp.gl);
        destroyStage();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gApp.gl);
        gApp.gl = nullptr;
    }
    if (gApp.dc && gApp.hwnd) {
        ReleaseDC(gApp.hwnd, gApp.dc);
        gApp.dc = nullptr;
    }
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || len >= buffer.size()) return {};
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            gApp.hwnd = hwnd;
            if (!initOpenGL(hwnd)) return -1;

            const auto bundle = executableDirectory() / L"assets" / L"resources-tx";
            if (!gApp.assets.openInstalledDirectory(bundle)) {
                MessageBoxW(hwnd, L"No se pudo abrir assets\\resources-tx.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!loadStage()) {
                MessageBoxW(hwnd, L"El bundle no contiene un stage BCH utilizable.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            return 0;
        }
        case WM_SIZE:
        case WM_PAINT: {
            if (message == WM_PAINT) {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd, &ps);
                render();
                EndPaint(hwnd, &ps);
            } else {
                render();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            gApp.zoom = std::clamp(gApp.zoom * (delta > 0 ? 1.12f : .89f), .75f, 6.0f);
            render();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSW wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"PromptemonStageScene";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Promptemon Steidium",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
