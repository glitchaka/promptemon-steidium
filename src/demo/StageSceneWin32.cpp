#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "bch/BchScene.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Texture;
using promptemon::bch::Vec2;
using promptemon::bch::Vec3;
using promptemon::storage::RomAssetCache;

constexpr float kPi = 3.14159265358979323846f;

struct StagePart {
    std::string path;
    std::unique_ptr<Scene> scene;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

struct Actor {
    std::string path;
    std::unique_ptr<Scene> scene;
    std::unordered_map<std::string, GLuint> textures;
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    Vec3 position{};
    float yawDeg = 0.0f;
};

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;

    std::vector<StagePart> parts;
    std::unordered_map<std::string, GLuint> stageTextureStorage;
    std::unordered_map<std::string, GLuint> stageTextureAliases;
    Vec3 stageMin{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 stageMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    Actor player;
    Actor opponent;
    Vec3 arenaCenter{};
    float arenaHalfX = 1.0f;
    float arenaHalfZ = 1.0f;
    float arenaFloorY = 0.0f;

    float zoom = 8.0f;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float panX = 0.0f;
    float panZ = 0.0f;
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
    const auto slash = value.find_last_of("/\\");
    return lowerAscii(value.substr(slash == std::string::npos ? 0 : slash + 1));
}

std::string stemName(const std::string& value) {
    std::string leaf = leafName(value);
    const auto dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return leaf;
}

bool isStageResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    return p.rfind("stage/", 0) == 0 || p.find("/stage/") != std::string::npos;
}

bool isPokemonResource(const std::string& path) {
    const std::string p = lowerAscii(path);
    return p.rfind("pii/", 0) == 0 || p.find("/pii/") != std::string::npos;
}

bool rejectStageGeometry(const std::string& path) {
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
            preferredGround(path) && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (isStageResource(path) && p.find("n001") != std::string::npos && !rejectStageGeometry(path))
            return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && preferredGround(path) && !rejectStageGeometry(path)) return path;
    }
    for (const auto& path : models) {
        if (isStageResource(path) && !rejectStageGeometry(path)) return path;
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

bool computeSceneBounds(const Scene& scene, Vec3& minOut, Vec3& maxOut) {
    minOut = {FLT_MAX, FLT_MAX, FLT_MAX};
    maxOut = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    const auto pose = scene.samplePose(nullptr, 0.0f);
    for (const auto& mesh : scene.meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            minOut.x = std::min(minOut.x, p.x);
            minOut.y = std::min(minOut.y, p.y);
            minOut.z = std::min(minOut.z, p.z);
            maxOut.x = std::max(maxOut.x, p.x);
            maxOut.y = std::max(maxOut.y, p.y);
            maxOut.z = std::max(maxOut.z, p.z);
            found = true;
        }
    }
    return found;
}

void deleteTextureMap(std::unordered_map<std::string, GLuint>& textures) {
    for (const auto& entry : textures) {
        GLuint id = entry.second;
        if (id) glDeleteTextures(1, &id);
    }
    textures.clear();
}

void uploadTextureList(const std::vector<Texture>& textures,
                       std::unordered_map<std::string, GLuint>& destination) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto& texture : textures) {
        if (texture.width <= 0 || texture.height <= 0 || texture.rgba.empty()) continue;
        const std::string key = lowerAscii(texture.name);
        if (destination.find(key) != destination.end()) continue;

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
        destination[key] = id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void rebuildStageTextureAliases() {
    gApp.stageTextureAliases.clear();
    for (const auto& entry : gApp.stageTextureStorage) {
        gApp.stageTextureAliases.emplace(entry.first, entry.second);
        gApp.stageTextureAliases.emplace(leafName(entry.first), entry.second);
        gApp.stageTextureAliases.emplace(stemName(entry.first), entry.second);
    }
}

GLuint lookupTexture(const std::unordered_map<std::string, GLuint>& map,
                     const std::string& requested) {
    if (requested.empty()) return 0;
    const std::string exact = lowerAscii(requested);
    const std::string leaf = leafName(requested);
    const std::string stem = stemName(requested);

    auto it = map.find(exact);
    if (it != map.end()) return it->second;
    it = map.find(leaf);
    if (it != map.end()) return it->second;
    it = map.find(stem);
    if (it != map.end()) return it->second;

    for (const auto& entry : map) {
        if (leafName(entry.first) == leaf || stemName(entry.first) == stem) return entry.second;
    }
    return 0;
}

void destroyStage() {
    deleteTextureMap(gApp.stageTextureStorage);
    gApp.stageTextureAliases.clear();
    gApp.parts.clear();
}

void destroyActor(Actor& actor) {
    deleteTextureMap(actor.textures);
    actor.scene.reset();
    actor.path.clear();
}

bool loadStage() {
    const auto models = gApp.assets.modelResources();
    const std::string anchor = chooseStageAnchor(models);
    if (anchor.empty()) return false;

    const std::string directory = parentPath(lowerAscii(anchor));
    std::vector<std::string> candidates;
    for (const auto& path : models) {
        const std::string p = lowerAscii(path);
        if (p.rfind(directory, 0) == 0) candidates.push_back(path);
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
    bool haveGeometry = false;
    float anchorX = 0.0f;
    float anchorZ = 0.0f;
    Vec3 anchorCenter{};

    for (const auto& path : candidates) {
        try {
            auto scene = std::make_unique<Scene>(Scene::parse(gApp.assets.extractBch(path)));

            // Keep textures from every BCH in the stage directory even when its geometry
            // is a sky/collision/oversized shell. Other meshes can legitimately reference them.
            uploadTextureList(scene->textures(), gApp.stageTextureStorage);

            if (scene->meshes().empty() || rejectStageGeometry(path)) continue;

            StagePart part;
            part.path = path;
            part.scene = std::move(scene);
            if (!computeSceneBounds(*part.scene, part.min, part.max)) continue;

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
                    dx > anchorX * 2.5f || dz > anchorZ * 2.5f) continue;
            }

            gApp.stageMin.x = std::min(gApp.stageMin.x, part.min.x);
            gApp.stageMin.y = std::min(gApp.stageMin.y, part.min.y);
            gApp.stageMin.z = std::min(gApp.stageMin.z, part.min.z);
            gApp.stageMax.x = std::max(gApp.stageMax.x, part.max.x);
            gApp.stageMax.y = std::max(gApp.stageMax.y, part.max.y);
            gApp.stageMax.z = std::max(gApp.stageMax.z, part.max.z);
            gApp.parts.push_back(std::move(part));
            haveGeometry = true;
        } catch (...) {
        }
    }

    rebuildStageTextureAliases();
    return haveGeometry;
}

void computeArena() {
    const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
    const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
    const float cx = (gApp.stageMin.x + gApp.stageMax.x) * .5f;
    const float cz = (gApp.stageMin.z + gApp.stageMax.z) * .5f;

    gApp.arenaHalfX = sx * .20f;
    gApp.arenaHalfZ = sz * .20f;
    gApp.arenaCenter = {cx, 0.0f, cz};

    std::vector<float> floorSamples;
    for (const auto& part : gApp.parts) {
        if (!part.scene) continue;
        const auto pose = part.scene->samplePose(nullptr, 0.0f);
        for (const auto& mesh : part.scene->meshes()) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = part.scene->skin(vertex, pose);
                if (std::abs(p.x - cx) <= gApp.arenaHalfX * .90f &&
                    std::abs(p.z - cz) <= gApp.arenaHalfZ * .90f) {
                    floorSamples.push_back(p.y);
                }
            }
        }
    }

    if (!floorSamples.empty()) {
        std::sort(floorSamples.begin(), floorSamples.end());
        // The ring floor is the broad lower horizontal surface; use a low percentile,
        // not the previous one-third sample which could select a wall/railing height.
        const std::size_t index = std::min(floorSamples.size() - 1, floorSamples.size() / 12);
        gApp.arenaFloorY = floorSamples[index];
    } else {
        gApp.arenaFloorY = gApp.stageMin.y;
    }
    gApp.arenaCenter.y = gApp.arenaFloorY;
}

std::size_t vertexCount(const Scene& scene) {
    std::size_t total = 0;
    for (const auto& mesh : scene.meshes()) total += mesh.vertices.size();
    return total;
}

bool loadActor(Actor& actor, const std::initializer_list<const char*> keys) {
    destroyActor(actor);
    const auto models = gApp.assets.modelResources();

    std::vector<std::string> candidates;
    for (const auto& path : models) {
        if (!isPokemonResource(path)) continue;
        const std::string p = lowerAscii(path);
        for (const char* key : keys) {
            if (p.find(key) != std::string::npos) {
                candidates.push_back(path);
                break;
            }
        }
    }
    if (candidates.empty()) return false;

    std::size_t bestVertices = 0;
    for (const auto& path : candidates) {
        try {
            auto scene = std::make_unique<Scene>(Scene::parse(gApp.assets.extractBch(path)));
            const std::size_t count = vertexCount(*scene);
            if (count > bestVertices) {
                bestVertices = count;
                actor.path = path;
                actor.scene = std::move(scene);
            }
        } catch (...) {
        }
    }
    if (!actor.scene || bestVertices == 0) return false;
    if (!computeSceneBounds(*actor.scene, actor.min, actor.max)) return false;

    uploadTextureList(actor.scene->textures(), actor.textures);
    for (const auto& path : candidates) {
        if (path == actor.path) continue;
        try {
            Scene companion = Scene::parse(gApp.assets.extractBch(path));
            uploadTextureList(companion.textures(), actor.textures);
        } catch (...) {
        }
    }
    return true;
}

void placeActors() {
    // Rumble World stage and Pokemon resources share their native scene units.
    // Do not normalize Pokemon to the ring size: render them at native scale.
    gApp.player.position = {
        gApp.arenaCenter.x - gApp.arenaHalfX * .42f,
        gApp.arenaFloorY,
        gApp.arenaCenter.z
    };
    gApp.player.yawDeg = 90.0f;

    gApp.opponent.position = {
        gApp.arenaCenter.x + gApp.arenaHalfX * .42f,
        gApp.arenaFloorY,
        gApp.arenaCenter.z
    };
    gApp.opponent.yawDeg = -90.0f;
}

GLuint materialTextureId(const Scene& scene,
                         const std::unordered_map<std::string, GLuint>& textures,
                         std::uint16_t materialId) {
    if (materialId >= scene.materials().size()) return 0;
    const Material& material = scene.materials()[materialId];
    if (const auto* texture = scene.textureForMaterial(materialId)) {
        if (const GLuint id = lookupTexture(textures, texture->name)) return id;
    }
    return lookupTexture(textures, material.textureName);
}

void drawScene(const Scene& scene,
               const std::unordered_map<std::string, GLuint>& textures) {
    const auto pose = scene.samplePose(nullptr, 0.0f);
    for (const auto& mesh : scene.meshes()) {
        const Material* material = mesh.materialId < scene.materials().size()
            ? &scene.materials()[mesh.materialId]
            : nullptr;
        const GLuint textureId = materialTextureId(scene, textures, mesh.materialId);

        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.72f, .72f, .72f, 1.0f);
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

void drawStagePart(StagePart& part) {
    if (part.scene) drawScene(*part.scene, gApp.stageTextureAliases);
}

void drawActor(Actor& actor) {
    if (!actor.scene) return;
    const float cx = (actor.min.x + actor.max.x) * .5f;
    const float cz = (actor.min.z + actor.max.z) * .5f;

    glPushMatrix();
    glTranslatef(actor.position.x, actor.position.y, actor.position.z);
    glRotatef(actor.yawDeg, 0, 1, 0);
    glTranslatef(-cx, -actor.min.y, -cz);
    drawScene(*actor.scene, actor.textures);
    glPopMatrix();
}

void resetView() {
    gApp.zoom = 8.0f;
    gApp.yawDeg = 0.0f;
    gApp.pitchDeg = 0.0f;
    gApp.panX = 0.0f;
    gApp.panZ = 0.0f;
}

void panCamera(float rightAmount, float forwardAmount) {
    const float sx = std::max(.001f, gApp.stageMax.x - gApp.stageMin.x);
    const float sz = std::max(.001f, gApp.stageMax.z - gApp.stageMin.z);
    const float baseStep = std::min(sx, sz) * .03f;
    const float step = baseStep / std::max(1.0f, std::sqrt(gApp.zoom / 8.0f));
    const float angle = (-45.0f + gApp.yawDeg) * kPi / 180.0f;

    const Vec3 right{std::cos(angle), 0.0f, std::sin(angle)};
    const Vec3 forward{std::sin(angle), 0.0f, -std::cos(angle)};
    gApp.panX += (right.x * rightAmount + forward.x * forwardAmount) * step;
    gApp.panZ += (right.z * rightAmount + forward.z * forwardAmount) * step;

    gApp.panX = std::clamp(gApp.panX, -sx, sx);
    gApp.panZ = std::clamp(gApp.panZ, -sz, sz);
}

void movePlayer(float rightAmount, float forwardAmount) {
    if (!gApp.player.scene) return;

    const float angle = (-45.0f + gApp.yawDeg) * kPi / 180.0f;
    const Vec3 right{std::cos(angle), 0.0f, std::sin(angle)};
    const Vec3 forward{std::sin(angle), 0.0f, -std::cos(angle)};
    const float step = std::max(.01f, std::min(gApp.arenaHalfX, gApp.arenaHalfZ) * .075f);

    const float dx = right.x * rightAmount + forward.x * forwardAmount;
    const float dz = right.z * rightAmount + forward.z * forwardAmount;
    gApp.player.position.x += dx * step;
    gApp.player.position.z += dz * step;

    const float marginX = gApp.arenaHalfX * .10f;
    const float marginZ = gApp.arenaHalfZ * .10f;
    gApp.player.position.x = std::clamp(gApp.player.position.x,
        gApp.arenaCenter.x - gApp.arenaHalfX + marginX,
        gApp.arenaCenter.x + gApp.arenaHalfX - marginX);
    gApp.player.position.z = std::clamp(gApp.player.position.z,
        gApp.arenaCenter.z - gApp.arenaHalfZ + marginZ,
        gApp.arenaCenter.z + gApp.arenaHalfZ - marginZ);

    if (dx != 0.0f || dz != 0.0f) gApp.player.yawDeg = std::atan2(dx, dz) * 180.0f / kPi;
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
    glOrtho(-halfW, halfW, -halfH, halfH, -5000.0f, 5000.0f);

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

        const float cx = (gApp.stageMin.x + gApp.stageMax.x) * .5f + gApp.panX;
        const float cy = (gApp.stageMin.y + gApp.stageMax.y) * .5f;
        const float cz = (gApp.stageMin.z + gApp.stageMax.z) * .5f + gApp.panZ;

        glScalef(fit, fit, fit);
        glRotatef(55.0f + gApp.pitchDeg, 1, 0, 0);
        glRotatef(-45.0f + gApp.yawDeg, 0, 1, 0);
        glTranslatef(-cx, -cy, -cz);

        for (auto& part : gApp.parts) drawStagePart(part);
        drawActor(gApp.player);
        drawActor(gApp.opponent);
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
        destroyActor(gApp.player);
        destroyActor(gApp.opponent);
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

int mouseX(LPARAM lParam) {
    return static_cast<int>(static_cast<short>(LOWORD(lParam)));
}

int mouseY(LPARAM lParam) {
    return static_cast<int>(static_cast<short>(HIWORD(lParam)));
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

            computeArena();
            if (!loadActor(gApp.player, {"koratta", "rattata"}) ||
                !loadActor(gApp.opponent, {"poppo", "pidgey"})) {
                MessageBoxW(hwnd, L"No se pudieron cargar los Pokemon Rumble requeridos para la arena.",
                            L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            placeActors();
            resetView();
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
        case WM_LBUTTONDOWN:
            gApp.dragging = true;
            gApp.lastMouse = {mouseX(lParam), mouseY(lParam)};
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (gApp.dragging) {
                const POINT now{mouseX(lParam), mouseY(lParam)};
                const int dx = now.x - gApp.lastMouse.x;
                const int dy = now.y - gApp.lastMouse.y;
                gApp.lastMouse = now;
                gApp.yawDeg += static_cast<float>(dx) * .35f;
                gApp.pitchDeg = std::clamp(gApp.pitchDeg + static_cast<float>(dy) * .30f, -50.0f, 35.0f);
                render();
            }
            return 0;
        case WM_LBUTTONUP:
            gApp.dragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            gApp.dragging = false;
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            gApp.zoom = std::clamp(gApp.zoom * (delta > 0 ? 1.25f : .80f), .20f, 400.0f);
            render();
            return 0;
        }
        case WM_KEYDOWN:
            switch (wParam) {
                // WASD pans the inspection camera across the stage.
                case 'W': panCamera(0.0f, 1.0f); render(); return 0;
                case 'S': panCamera(0.0f, -1.0f); render(); return 0;
                case 'A': panCamera(-1.0f, 0.0f); render(); return 0;
                case 'D': panCamera(1.0f, 0.0f); render(); return 0;
                // Arrow keys move the player's Pokemon inside the arena.
                case VK_UP:    movePlayer(0.0f, 1.0f); render(); return 0;
                case VK_DOWN:  movePlayer(0.0f, -1.0f); render(); return 0;
                case VK_LEFT:  movePlayer(-1.0f, 0.0f); render(); return 0;
                case VK_RIGHT: movePlayer(1.0f, 0.0f); render(); return 0;
                case 'R': resetView(); render(); return 0;
                case VK_ESCAPE: DestroyWindow(hwnd); return 0;
            }
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
