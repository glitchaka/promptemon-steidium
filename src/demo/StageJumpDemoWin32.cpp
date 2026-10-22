#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "demo/CameraController.h"
#include "demo/JumpController.h"
#include "demo/PokemonActor.h"
#include "demo/StageWorld.h"
#include "net/CncLanSession.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

using promptemon::demo::CameraController;
using promptemon::demo::JumpController;
using promptemon::demo::PokemonActor;
using promptemon::demo::StageWorld;
using promptemon::net::CncLanSession;
using promptemon::storage::RomAssetCache;

constexpr UINT_PTR kUpdateTimer = 1;
constexpr float kPi = 3.14159265358979323846f;

struct RemoteVisual {
    std::unique_ptr<PokemonActor> actor;
    std::uint8_t slot = 0;
};

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;
    StageWorld world;
    CameraController camera;
    PokemonActor pokemon;
    JumpController jump;
    CncLanSession lan;
    std::unordered_map<std::uint32_t, RemoteVisual> remotes;
    bool rotating = false;
    POINT lastMouse{};
    ULONGLONG lastTick = 0;
    std::size_t lastPeerCount = static_cast<std::size_t>(-1);
};

App gApp;

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || len >= buffer.size()) return {};
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
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
    return gApp.gl && wglMakeCurrent(gApp.dc, gApp.gl);
}

void destroyRemoteVisuals() {
    for (auto& entry : gApp.remotes) {
        if (entry.second.actor) entry.second.actor->destroy();
    }
    gApp.remotes.clear();
}

void shutdownOpenGL() {
    gApp.lan.stop();
    if (gApp.gl) {
        wglMakeCurrent(gApp.dc, gApp.gl);
        destroyRemoteVisuals();
        gApp.pokemon.destroy();
        gApp.world.destroy();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gApp.gl);
        gApp.gl = nullptr;
    }
    if (gApp.dc && gApp.hwnd) {
        ReleaseDC(gApp.hwnd, gApp.dc);
        gApp.dc = nullptr;
    }
}

void updateWindowTitle(std::size_t peerCount) {
    if (!gApp.hwnd || peerCount == gApp.lastPeerCount) return;
    gApp.lastPeerCount = peerCount;
    const std::wstring title = L"Promptemon - Stage Jump LAN | slot " +
        std::to_wstring(static_cast<unsigned>(gApp.lan.localSlot()) + 1u) +
        L" | jugadores " + std::to_wstring(peerCount + 1u);
    SetWindowTextW(gApp.hwnd, title.c_str());
}

void placeRemote(PokemonActor& actor, std::uint8_t slot) {
    const float angle = (static_cast<float>(slot) / 8.0f) * (2.0f * kPi);
    const float x = std::cos(angle) * .55f;
    const float z = std::sin(angle) * .55f;
    const float yaw = std::atan2(-x, -z) * 180.0f / kPi;
    actor.placeAt(gApp.world.arena(), x, z, yaw);
}

bool syncRemotePlayers(float dt) {
    bool changed = false;
    const auto peers = gApp.lan.peers();
    std::unordered_set<std::uint32_t> seen;
    seen.reserve(peers.size());

    for (const auto& peer : peers) {
        seen.insert(peer.id);
        auto found = gApp.remotes.find(peer.id);
        if (found == gApp.remotes.end()) {
            auto actor = std::make_unique<PokemonActor>();
            if (!actor->load(gApp.assets, {"/koratta.bch", "/koratta_f.bch"}, {"koratta", "rattata"})) {
                continue;
            }
            placeRemote(*actor, peer.slot);
            RemoteVisual visual;
            visual.slot = peer.slot;
            visual.actor = std::move(actor);
            found = gApp.remotes.emplace(peer.id, std::move(visual)).first;
            changed = true;
        } else if (found->second.slot != peer.slot) {
            found->second.slot = peer.slot;
            placeRemote(*found->second.actor, peer.slot);
            changed = true;
        }

        found->second.actor->setVerticalOffset(peer.jumpOffset);
        changed |= found->second.actor->update(dt);
    }

    for (auto it = gApp.remotes.begin(); it != gApp.remotes.end();) {
        if (seen.find(it->first) == seen.end()) {
            if (it->second.actor) it->second.actor->destroy();
            it = gApp.remotes.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    updateWindowTitle(peers.size());
    return changed;
}

void render() {
    if (!gApp.hwnd || !gApp.gl) return;
    wglMakeCurrent(gApp.dc, gApp.gl);

    RECT rc{};
    GetClientRect(gApp.hwnd, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(1L, rc.bottom - rc.top);

    glViewport(0, 0, width, height);
    glClearColor(.55f, .70f, .80f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gApp.camera.apply(width, height, gApp.world.arena(), gApp.world.minBounds(), gApp.world.maxBounds());
    gApp.world.draw();
    gApp.pokemon.draw();
    for (const auto& entry : gApp.remotes) {
        if (entry.second.actor) entry.second.actor->draw();
    }
    SwapBuffers(gApp.dc);
}

void update(float dt) {
    bool changed = gApp.pokemon.update(dt);

    if (gApp.jump.update(dt)) changed = true;
    gApp.pokemon.setVerticalOffset(gApp.jump.offset());
    gApp.lan.setLocalJumpOffset(gApp.jump.offset());
    changed |= gApp.lan.update(dt);
    changed |= syncRemotePlayers(dt);

    if (changed) InvalidateRect(gApp.hwnd, nullptr, FALSE);
}

int mouseX(LPARAM lParam) { return static_cast<int>(static_cast<short>(LOWORD(lParam))); }
int mouseY(LPARAM lParam) { return static_cast<int>(static_cast<short>(HIWORD(lParam))); }

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            gApp.hwnd = hwnd;
            if (!initOpenGL(hwnd)) return -1;

            const auto bundle = executableDirectory() / L"assets" / L"resources-tx";
            if (!gApp.assets.openInstalledDirectory(bundle)) {
                MessageBoxW(hwnd, L"No se pudo abrir assets\\resources-tx.", L"Promptemon Stage Jump", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!gApp.world.load(gApp.assets)) {
                MessageBoxW(hwnd, L"No se pudo cargar el stage BCH.", L"Promptemon Stage Jump", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!gApp.pokemon.load(gApp.assets, {"/koratta.bch", "/koratta_f.bch"}, {"koratta", "rattata"})) {
                MessageBoxW(hwnd, L"No se pudo cargar Rattata con sus animaciones BCH.", L"Promptemon Stage Jump", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!gApp.lan.start()) {
                MessageBoxW(hwnd,
                            L"No se pudo iniciar la sesion LAN UDP de C&C (puertos 8088-8095).",
                            L"Promptemon Stage Jump", MB_OK | MB_ICONERROR);
                return -1;
            }

            // El jugador local siempre arranca exactamente en el centro del ring, 1:1.
            gApp.pokemon.placeAt(gApp.world.arena(), 0.0f, 0.0f, 180.0f);
            gApp.jump.reset(gApp.pokemon.nativeHeight());
            gApp.lan.setLocalJumpOffset(0.0f);

            // Vista cercana dentro del estadio: mover la camara, no escalar el stage.
            gApp.camera.reset(gApp.world.arena());
            for (int i = 0; i < 7; ++i) gApp.camera.zoom(WHEEL_DELTA, gApp.world.arena());

            updateWindowTitle(0);
            gApp.lastTick = GetTickCount64();
            SetTimer(hwnd, kUpdateTimer, 16, nullptr);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kUpdateTimer) {
                const ULONGLONG now = GetTickCount64();
                const float dt = std::clamp(static_cast<float>(now - gApp.lastTick) / 1000.0f, 0.0f, .05f);
                gApp.lastTick = now;
                update(dt);
            }
            return 0;

        case WM_LBUTTONDOWN:
            gApp.jump.trigger();
            return 0;

        case WM_RBUTTONDOWN:
            gApp.rotating = true;
            gApp.lastMouse = {mouseX(lParam), mouseY(lParam)};
            SetCapture(hwnd);
            return 0;

        case WM_MOUSEMOVE:
            if (gApp.rotating) {
                const POINT now{mouseX(lParam), mouseY(lParam)};
                gApp.camera.drag(static_cast<float>(now.x - gApp.lastMouse.x),
                                 static_cast<float>(now.y - gApp.lastMouse.y));
                gApp.lastMouse = now;
                render();
            }
            return 0;

        case WM_RBUTTONUP:
            gApp.rotating = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;

        case WM_CAPTURECHANGED:
            gApp.rotating = false;
            return 0;

        case WM_MOUSEWHEEL:
            gApp.camera.zoom(GET_WHEEL_DELTA_WPARAM(wParam), gApp.world.arena());
            render();
            return 0;

        case WM_KEYDOWN:
            if (wParam == 'R') {
                gApp.camera.reset(gApp.world.arena());
                for (int i = 0; i < 7; ++i) gApp.camera.zoom(WHEEL_DELTA, gApp.world.arena());
                render();
            }
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;

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

        case WM_DESTROY:
            KillTimer(hwnd, kUpdateTimer);
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
    wc.lpszClassName = L"PromptemonStageJumpDemo";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Promptemon - Stage Jump LAN",
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
