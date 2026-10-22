#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "bch/BchScene.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <array>
#include <cctype>
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

constexpr float kPi = 3.14159265358979323846f;

struct StagePart {
    std::string path;
    std::unique_ptr<Scene> scene;
    std::unordered_map<std::string, GLuint> textures;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

struct CollisionTriangle {
    Vec3 a{};
    Vec3 b{};
    Vec3 c{};
};

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;
    std::vector<StagePart> parts;
    std::vector<CollisionTriangle> collision;

    Vec3 stageMin{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 stageMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vec3 focusMin{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 focusMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    float yawDeg = 0.0f;
    float pitchDeg = 22.0f;
    float distanceScale = 1.0f;
    bool dragging = false;
    POINT lastMouse{};
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
           containsAny(leaf, {"collision", "collider", "sky", "skydome", "background",
                              "backdrop", "farbg", "far_", "dummy", "debug"});
}

bool preferredGround(const std::string& path) {
    return containsAny(path, {"ground", "floor", "terrain", "land", "field", "map"});
}

std::string chooseStageAnchor(const std::vector<std::string>& models) {
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos &&
            preferredGround(path) && !rejectStagePart(path)) return path;
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

Vec3 subtract(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 multiply(Vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float vectorLength(Vec3 v) {
    return std::sqrt(dot(v, v));
}

Vec3 normalize(Vec3 v) {
    const float len = vectorLength(v);
    if (len <= .000001f) return {0, 0, 0};
    return multiply(v, 1.0f / len);
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

void appendCollision(const StagePart& part) {
    if (!part.scene) return;
    const auto pose = part.scene->samplePose(nullptr, 0.0f);
    for (const auto& mesh : part.scene->meshes()) {
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            gApp.collision.push_back({
                part.scene->skin(mesh.vertices[i], pose),
                part.scene->skin(mesh.vertices[i + 1], pose),
                part.scene->skin(mesh.vertices[i + 2], pose)
            });
        }
    }
}

void destroyStage() {
    for (auto& part : gApp.parts) {
        for (const auto& entry : part.textures) {
            GLuint id = entry.second;
            if (id) glDeleteTextures(1, &id);
        }
    }
    gApp.parts.clear();
    gApp.collision.clear();
}

void computeCameraFocus() {
    constexpr int Grid = 20;
    std::array<std::size_t, Grid * Grid> density{};

    const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
    const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);

    auto cellFor = [&](const Vec3& p, int& ix, int& iz) {
        const float nx = std::clamp((p.x - gApp.stageMin.x) / sx, 0.0f, .999999f);
        const float nz = std::clamp((p.z - gApp.stageMin.z) / sz, 0.0f, .999999f);
        ix = std::clamp(static_cast<int>(nx * Grid), 0, Grid - 1);
        iz = std::clamp(static_cast<int>(nz * Grid), 0, Grid - 1);
    };

    for (auto& part : gApp.parts) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                int ix = 0, iz = 0;
                cellFor(p, ix, iz);
                ++density[iz * Grid + ix];
            }
        }
    }

    std::size_t bestCount = 0;
    int bestX = Grid / 2;
    int bestZ = Grid / 2;
    for (int z = 1; z < Grid - 1; ++z) {
        for (int x = 1; x < Grid - 1; ++x) {
            std::size_t count = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    count += density[(z + dz) * Grid + (x + dx)];
            if (count > bestCount) {
                bestCount = count;
                bestX = x;
                bestZ = z;
            }
        }
    }

    gApp.focusMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    gApp.focusMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    std::size_t accepted = 0;

    for (auto& part : gApp.parts) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                int ix = 0, iz = 0;
                cellFor(p, ix, iz);
                if (std::abs(ix - bestX) > 2 || std::abs(iz - bestZ) > 2) continue;
                gApp.focusMin.x = std::min(gApp.focusMin.x, p.x);
                gApp.focusMin.y = std::min(gApp.focusMin.y, p.y);
                gApp.focusMin.z = std::min(gApp.focusMin.z, p.z);
                gApp.focusMax.x = std::max(gApp.focusMax.x, p.x);
                gApp.focusMax.y = std::max(gApp.focusMax.y, p.y);
                gApp.focusMax.z = std::max(gApp.focusMax.z, p.z);
                ++accepted;
            }
        }
    }

    if (accepted < 64 || gApp.focusMin.x == FLT_MAX) {
        gApp.focusMin = gApp.stageMin;
        gApp.focusMax = gApp.stageMax;
    }
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

            const float px = std::max(.001f, part.max.x - part.min.x);
            const float pz = std::max(.001f, part.max.z - part.min.z);
            const Vec3 center{(part.min.x + part.max.x) * .5f,
                              (part.min.y + part.max.y) * .5f,
                              (part.min.z + part.max.z) * .5f};

            if (!haveAnchorBounds) {
                anchorX = px;
                anchorZ = pz;
                anchorCenter = center;
                haveAnchorBounds = true;
            } else {
                const float dx = std::abs(center.x - anchorCenter.x);
                const float dz = std::abs(center.z - anchorCenter.z);
                if (px > anchorX * 4.0f || pz > anchorZ * 4.0f ||
                    dx > anchorX * 2.5f || dz > anchorZ * 2.5f) continue;
            }

            uploadTextures(part);
            appendCollision(part);
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

    if (gApp.parts.empty()) return false;
    computeCameraFocus();
    return true;
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

bool rayTriangle(Vec3 origin, Vec3 dir, const CollisionTriangle& tri, float& tOut) {
    constexpr float Epsilon = 0.000001f;
    const Vec3 edge1 = subtract(tri.b, tri.a);
    const Vec3 edge2 = subtract(tri.c, tri.a);
    const Vec3 p = cross(dir, edge2);
    const float det = dot(edge1, p);
    if (std::abs(det) < Epsilon) return false;

    const float invDet = 1.0f / det;
    const Vec3 s = subtract(origin, tri.a);
    const float u = dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    const Vec3 q = cross(s, edge1);
    const float v = dot(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = dot(edge2, q) * invDet;
    if (t <= Epsilon) return false;
    tOut = t;
    return true;
}

Vec3 collideCamera(Vec3 target, Vec3 desiredEye, float span) {
    Vec3 ray = subtract(desiredEye, target);
    const float desiredDistance = vectorLength(ray);
    if (desiredDistance <= .0001f || gApp.collision.empty()) return desiredEye;

    const Vec3 dir = multiply(ray, 1.0f / desiredDistance);
    const float ignoreNearTarget = std::max(span * .10f, desiredDistance * .05f);
    float nearest = desiredDistance;

    for (const auto& tri : gApp.collision) {
        float t = 0.0f;
        if (!rayTriangle(target, dir, tri, t)) continue;
        if (t <= ignoreNearTarget || t >= nearest) continue;
        nearest = t;
    }

    if (nearest >= desiredDistance) return desiredEye;
    const float padding = std::max(span * .025f, desiredDistance * .015f);
    const float safeDistance = std::max(ignoreNearTarget, nearest - padding);
    return add(target, multiply(dir, safeDistance));
}

void applyLookAt(Vec3 eye, Vec3 target) {
    const Vec3 forward = normalize(subtract(target, eye));
    Vec3 side = normalize(cross(forward, {0, 1, 0}));
    if (vectorLength(side) < .0001f) side = {1, 0, 0};
    const Vec3 up = cross(side, forward);

    // glMultMatrixf is column-major.
    const GLfloat matrix[16] = {
        side.x, side.y, side.z, 0,
        up.x, up.y, up.z, 0,
        -forward.x, -forward.y, -forward.z, 0,
        0, 0, 0, 1
    };
    glMultMatrixf(matrix);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void resetCamera() {
    gApp.yawDeg = 0.0f;
    gApp.pitchDeg = 22.0f;
    gApp.distanceScale = 1.0f;
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

    if (!gApp.parts.empty()) {
        const float fx = std::max(.001f, gApp.focusMax.x - gApp.focusMin.x);
        const float fy = std::max(.001f, gApp.focusMax.y - gApp.focusMin.y);
        const float fz = std::max(.001f, gApp.focusMax.z - gApp.focusMin.z);
        const float span = std::max({fx, fz, .001f});

        const Vec3 target{
            (gApp.focusMin.x + gApp.focusMax.x) * .5f,
            gApp.focusMin.y + std::max(fy * .18f, span * .025f),
            (gApp.focusMin.z + gApp.focusMax.z) * .5f
        };

        const float yaw = gApp.yawDeg * kPi / 180.0f;
        const float pitch = gApp.pitchDeg * kPi / 180.0f;
        const float distance = std::max(span * 1.55f, .5f) * gApp.distanceScale;
        const float horizontal = std::cos(pitch) * distance;
        const Vec3 desiredEye{
            target.x + std::sin(yaw) * horizontal,
            target.y + std::sin(pitch) * distance,
            target.z + std::cos(yaw) * horizontal
        };
        const Vec3 eye = collideCamera(target, desiredEye, span);

        const float stageX = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
        const float stageY = std::max(.001f, gApp.stageMax.y - gApp.stageMin.y);
        const float stageZ = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
        const float stageDiag = std::sqrt(stageX * stageX + stageY * stageY + stageZ * stageZ);
        const float eyeDistance = std::max(.01f, vectorLength(subtract(eye, target)));

        const float fovY = 52.0f;
        const float nearPlane = std::max(.005f, eyeDistance * .005f);
        const float farPlane = std::max(eyeDistance + stageDiag * 3.0f, 100.0f);
        const float top = nearPlane * std::tan(fovY * kPi / 360.0f);
        const float right = top * aspect;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-right, right, -top, top, nearPlane, farPlane);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        applyLookAt(eye, target);

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
            resetCamera();
            return 0;
        }
        case WM_SIZE:
            render();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            gApp.dragging = true;
            gApp.lastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!gApp.dragging) return 0;
            POINT now{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int dx = now.x - gApp.lastMouse.x;
            const int dy = now.y - gApp.lastMouse.y;
            gApp.lastMouse = now;
            gApp.yawDeg += static_cast<float>(dx) * .35f;
            gApp.pitchDeg = std::clamp(gApp.pitchDeg - static_cast<float>(dy) * .28f, 5.0f, 72.0f);
            render();
            return 0;
        }
        case WM_LBUTTONUP:
            gApp.dragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            gApp.dragging = false;
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float factor = delta > 0 ? .86f : 1.16f;
            gApp.distanceScale = std::clamp(gApp.distanceScale * factor, .22f, 4.0f);
            render();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == 'R') {
                resetCamera();
                render();
                return 0;
            }
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
