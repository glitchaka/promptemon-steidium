#include "bch/BchScene.h"
#include "rom/RumbleWorldArchive.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using promptemon::bch::Animation;
using promptemon::bch::Scene;
using promptemon::bch::Vec3;

constexpr int ID_IDLE = 1001;
constexpr int ID_ACTION1 = 1002;
constexpr int ID_ACTION2 = 1003;

HWND gWindow = nullptr;
HDC gDC = nullptr;
HGLRC gGL = nullptr;
std::unique_ptr<Scene> gScene;
const Animation* gIdle = nullptr;
const Animation* gCurrent = nullptr;
float gFrame = 0.0f;
float gYaw = 0.0f;
float gPitch = -8.0f;
float gZoom = 1.0f;
bool gDragging = false;
POINT gLastMouse{};
std::chrono::steady_clock::time_point gLastTick;

void setStatus(const std::string& text) {
    if (!gWindow) return;
    const std::string title = "promptemon-steidium - " + text;
    SetWindowTextA(gWindow, title.c_str());
}

const Animation* chooseAction(const char* preferred, const char* contains) {
    if (!gScene) return nullptr;
    if (auto* a = gScene->findAnimation(preferred)) return a;
    for (const auto& a : gScene->animations()) {
        if (!a.loop && a.frames > 2.0f && a.name.find(contains) != std::string::npos) return &a;
    }
    return nullptr;
}

void play(const Animation* anim) {
    if (!anim) return;
    gCurrent = anim;
    gFrame = 0.0f;
    setStatus(anim->name);
}

void setupProjection(int width, int height) {
    if (height < 1) height = 1;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double top = 0.42;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, 0.8, 20.0);
    glMatrixMode(GL_MODELVIEW);
}

void drawGround() {
    glDisable(GL_CULL_FACE);
    glColor3f(0.18f, 0.20f, 0.22f);
    glBegin(GL_QUADS);
    glVertex3f(-1.8f, -0.02f, -1.8f);
    glVertex3f( 1.8f, -0.02f, -1.8f);
    glVertex3f( 1.8f, -0.02f,  1.8f);
    glVertex3f(-1.8f, -0.02f,  1.8f);
    glEnd();
}

void drawScene() {
    if (!gScene || !gWindow) return;

    RECT rc{};
    GetClientRect(gWindow, &rc);
    setupProjection(rc.right - rc.left, rc.bottom - rc.top);

    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glLoadIdentity();
    glTranslatef(0.0f, -0.55f, -3.2f / std::max(0.35f, gZoom));
    glRotatef(gPitch, 1.0f, 0.0f, 0.0f);
    glRotatef(gYaw, 0.0f, 1.0f, 0.0f);
    glScalef(4.2f, 4.2f, 4.2f);

    drawGround();

    const auto matrices = gScene->samplePose(gCurrent, gFrame);
    for (const auto& mesh : gScene->meshes()) {
        if (mesh.materialId == 0) glColor3f(0.12f, 0.10f, 0.08f);
        else glColor3f(0.98f, 0.78f, 0.08f);

        glBegin(GL_TRIANGLES);
        for (const auto& v : mesh.vertices) {
            const Vec3 p = gScene->skin(v, matrices);
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }

    SwapBuffers(gDC);
}

void tick() {
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - gLastTick).count();
    gLastTick = now;
    dt = std::clamp(dt, 0.0f, 0.1f);

    if (gCurrent) {
        gFrame += dt * 30.0f;
        if (gCurrent->frames > 0.0f && gFrame > gCurrent->frames) {
            if (gCurrent->loop) {
                gFrame = std::fmod(gFrame, gCurrent->frames);
            } else {
                gCurrent = gIdle;
                gFrame = 0.0f;
                if (gIdle) setStatus(gIdle->name);
            }
        }
    }
    drawScene();
}

void initOpenGL(HWND hwnd) {
    gDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int pf = ChoosePixelFormat(gDC, &pfd);
    if (!pf || !SetPixelFormat(gDC, pf, &pfd)) throw std::runtime_error("Could not set OpenGL pixel format");
    gGL = wglCreateContext(gDC);
    if (!gGL || !wglMakeCurrent(gDC, gGL)) throw std::runtime_error("Could not create OpenGL context");
}

void shutdownOpenGL() {
    if (gGL) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gGL);
        gGL = nullptr;
    }
    if (gDC && gWindow) {
        ReleaseDC(gWindow, gDC);
        gDC = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateWindowA("BUTTON", "Idle", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          12, 12, 95, 30, hwnd, reinterpret_cast<HMENU>(ID_IDLE), nullptr, nullptr);
            CreateWindowA("BUTTON", "Action 1", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          115, 12, 95, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION1), nullptr, nullptr);
            CreateWindowA("BUTTON", "Action 2", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          218, 12, 95, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION2), nullptr, nullptr);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_IDLE) play(gIdle);
            else if (LOWORD(wp) == ID_ACTION1) play(chooseAction("Pii_ROOT_attack_2__attack", "attack"));
            else if (LOWORD(wp) == ID_ACTION2) play(chooseAction("Pii_ROOT_attack_TUNODORIRU__attack", "attack_"));
            return 0;
        case WM_LBUTTONDOWN:
            gDragging = true;
            gLastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            gDragging = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (gDragging) {
                POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                gYaw += (p.x - gLastMouse.x) * 0.55f;
                gPitch += (p.y - gLastMouse.y) * 0.35f;
                gPitch = std::clamp(gPitch, -70.0f, 70.0f);
                gLastMouse = p;
            }
            return 0;
        case WM_MOUSEWHEEL:
            gZoom *= GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.1f : 0.9f;
            gZoom = std::clamp(gZoom, 0.45f, 2.25f);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

std::filesystem::path findCiaInRomDir(const std::filesystem::path& base) {
    const auto romDir = base / "roms";
    std::error_code ec;
    if (!std::filesystem::is_directory(romDir, ec)) return {};

    for (const auto& entry : std::filesystem::directory_iterator(romDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".cia") return entry.path();
    }
    return {};
}

std::filesystem::path findCiaFromAncestors(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) return {};

    for (;;) {
        if (auto cia = findCiaInRomDir(start); !cia.empty()) return cia;
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path ciaFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        const std::filesystem::path explicitPath = argv[1];
        LocalFree(argv);
        return explicitPath;
    }
    if (argv) LocalFree(argv);

    if (auto cia = findCiaFromAncestors(std::filesystem::current_path()); !cia.empty()) return cia;
    if (auto cia = findCiaFromAncestors(executableDirectory()); !cia.empty()) return cia;

    throw std::runtime_error(
        "No CIA found. Put your Pokemon Rumble World .cia in the repository roms folder: "
        "promptemon-steidium\\roms\\"
    );
}

void loadPokemon(const std::filesystem::path& cia) {
    promptemon::rom::RumbleWorldArchive archive;
    if (!archive.open(cia)) throw std::runtime_error("Could not open CIA: " + cia.string());
    const auto model = archive.extractBch("pii/PIKATYUU_M.bch.cx");
    const auto anim = archive.extractBch("pii/anim.bch.cx");
    gScene = std::make_unique<Scene>(Scene::parse(model, anim));
    gIdle = gScene->findAnimation("Pii_anim_nomove_kamae");
    if (!gIdle) {
        for (const auto& a : gScene->animations()) {
            if (a.loop) { gIdle = &a; break; }
        }
    }
    gCurrent = gIdle;
    gFrame = 0.0f;
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        const auto cia = ciaFromCommandLine();
        loadPokemon(cia);

        WNDCLASSA wc{};
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = "PromptemonSteidiumDemo";
        if (!RegisterClassA(&wc)) throw std::runtime_error("Could not register window class");

        gWindow = CreateWindowExA(0, wc.lpszClassName, "promptemon-steidium",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 720,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow) throw std::runtime_error("Could not create window");
        initOpenGL(gWindow);
        ShowWindow(gWindow, show);
        UpdateWindow(gWindow);
        setStatus(gScene ? gScene->modelName() + " | drag: rotate | wheel: zoom" : "No model");
        gLastTick = std::chrono::steady_clock::now();

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (running) tick();
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "promptemon-steidium demo error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
