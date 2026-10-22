#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "demo/CameraController.h"
#include "demo/PokemonActor.h"
#include "demo/StageWorld.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <filesystem>

namespace {

using promptemon::demo::CameraController;
using promptemon::demo::PokemonActor;
using promptemon::demo::StageWorld;
using promptemon::storage::RomAssetCache;

constexpr UINT_PTR kUpdateTimer = 1;

struct App {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC gl = nullptr;
    RomAssetCache assets;
    StageWorld world;
    CameraController camera;
    PokemonActor player;
    PokemonActor opponent;
    bool keys[256]{};
    bool dragging = false;
    POINT lastMouse{};
    ULONGLONG lastTick = 0;
};

App gApp;

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

void shutdownOpenGL() {
    if (gApp.gl) {
        wglMakeCurrent(gApp.dc, gApp.gl);
        gApp.player.destroy();
        gApp.opponent.destroy();
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

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || len >= buffer.size()) return {};
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
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
    gApp.player.draw();
    gApp.opponent.draw();
    SwapBuffers(gApp.dc);
}

void update(float dt) {
    bool changed = false;
    changed |= gApp.camera.updatePan(dt, gApp.keys, gApp.world.arena(), gApp.world.minBounds(), gApp.world.maxBounds());

    float right = 0.0f;
    float forward = 0.0f;
    if (gApp.keys[VK_LEFT]) right -= 1.0f;
    if (gApp.keys[VK_RIGHT]) right += 1.0f;
    if (gApp.keys[VK_UP]) forward += 1.0f;
    if (gApp.keys[VK_DOWN]) forward -= 1.0f;
    changed |= gApp.player.move(right, forward, gApp.camera.yawDegrees(), dt, gApp.world.arena());

    changed |= gApp.player.update(dt);
    changed |= gApp.opponent.update(dt);
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
                MessageBoxW(hwnd, L"No se pudo abrir assets\\resources-tx.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!gApp.world.load(gApp.assets)) {
                MessageBoxW(hwnd, L"El bundle no contiene un stage BCH utilizable.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }
            if (!gApp.player.load(gApp.assets, {"/koratta.bch", "/koratta_f.bch"}, {"koratta", "rattata"}) ||
                !gApp.opponent.load(gApp.assets, {"/poppo.bch"}, {"poppo", "pidgey"})) {
                MessageBoxW(hwnd,
                            L"No se pudieron cargar los modelos y animaciones BCH de Rattata/Pidgey.",
                            L"Promptemon Steidium", MB_OK | MB_ICONERROR);
                return -1;
            }

            gApp.player.place(gApp.world.arena(), -1.0f, 90.0f);
            gApp.opponent.place(gApp.world.arena(), 1.0f, -90.0f);
            gApp.camera.reset(gApp.world.arena());
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

        case WM_LBUTTONDOWN:
            gApp.dragging = true;
            gApp.lastMouse = {mouseX(lParam), mouseY(lParam)};
            SetCapture(hwnd);
            return 0;

        case WM_MOUSEMOVE:
            if (gApp.dragging) {
                const POINT now{mouseX(lParam), mouseY(lParam)};
                gApp.camera.drag(static_cast<float>(now.x - gApp.lastMouse.x),
                                 static_cast<float>(now.y - gApp.lastMouse.y));
                gApp.lastMouse = now;
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

        case WM_MOUSEWHEEL:
            gApp.camera.zoom(GET_WHEEL_DELTA_WPARAM(wParam), gApp.world.arena());
            render();
            return 0;

        case WM_KEYDOWN:
            if (wParam < 256) gApp.keys[wParam] = true;
            if (wParam == 'R') {
                gApp.camera.reset(gApp.world.arena());
                render();
            }
            if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;

        case WM_KEYUP:
            if (wParam < 256) gApp.keys[wParam] = false;
            return 0;

        case WM_KILLFOCUS:
            std::fill(std::begin(gApp.keys), std::end(gApp.keys), false);
            return 0;

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
