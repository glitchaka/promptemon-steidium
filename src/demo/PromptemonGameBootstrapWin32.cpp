#include "demo/ExtractionProgressWindow.h"

// Reuse the existing game implementation in the same translation unit, but
// replace only its startup routine. This keeps gameplay/rendering untouched
// while the ROM bootstrap becomes deterministic and self-contained.
#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain

#include <filesystem>
#include <optional>
#include <string>

namespace {

bool prepareAssetCacheVisible(promptemon::storage::LocalDatabase& database,
                              std::wstring& error) {
    promptemon::demo::ExtractionProgressWindow progressWindow;
    bool progressVisible = false;

    try {
        const std::filesystem::path base = database.path().parent_path() / L"assets";
        const auto progress = [&](std::size_t current,
                                  std::size_t total,
                                  const std::string& resourcePath) {
            if (!progressVisible) {
                progressVisible = progressWindow.create();
            }
            if (progressVisible) progressWindow.update(current, total, resourcePath);
        };

        if (!gAssets.prepare(gRomArchive, base, progress)) {
            progressWindow.close();
            error = L"No se pudieron exportar los recursos necesarios de la ROM.";
            return false;
        }

        progressWindow.close();
        gModelResources = gAssets.modelResources();
        if (gModelResources.empty()) {
            error = L"La exportacion termino sin modelos BCH utilizables.";
            return false;
        }

        // Once extraction succeeds, runtime consumes only the local cache.
        gRomArchive.close();
        return true;
    } catch (...) {
        progressWindow.close();
        error = L"Fallo la extraccion de recursos de la ROM.";
        return false;
    }
}

bool registerGameWindowClass(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"PromptemonPlayableGame";
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createGameWindow(HINSTANCE instance, int showCommand) {
    HWND window = CreateWindowExW(0,
                                  L"PromptemonPlayableGame",
                                  L"Promptemon Steidium",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return nullptr;
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    return window;
}

void discardBrokenCache() {
    const std::filesystem::path cacheRoot = gAssets.root();
    gAssets.close();
    gModelResources.clear();
    gResolvedModels.clear();
    if (!cacheRoot.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(cacheRoot, ec);
    }

    // WM_CREATE returning -1 destroys the failed game window and may leave a
    // WM_QUIT posted by WM_DESTROY. Remove it before returning to the selector.
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT ole = OleInitialize(nullptr);
    const bool uninitializeOle = SUCCEEDED(ole);

    promptemon::storage::LocalDatabase database;
    if (!database.open()) {
        MessageBoxW(nullptr,
                    L"No se pudo abrir player.db junto al ejecutable.",
                    L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    if (!registerGameWindowClass(instance)) {
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    const bool forceSelector = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    std::optional<std::filesystem::path> candidate;
    bool trySaved = !forceSelector;

    for (;;) {
        if (trySaved) {
            trySaved = false;
            if (const auto saved = database.setting("rom_directory")) {
                candidate = std::filesystem::path(*saved);
            }
        }

        if (!candidate) {
            candidate = chooseRomDirectory();
            if (!candidate) {
                if (uninitializeOle) OleUninitialize();
                return 0;
            }
        }

        std::wstring error;
        if (!loadRomDirectory(*candidate, error)) {
            MessageBoxW(nullptr, error.c_str(), L"Carpeta de ROMs no valida", MB_OK | MB_ICONWARNING);
            candidate.reset();
            continue;
        }

        // Extraction happens immediately after selecting/validating the ROM,
        // in this same process. Nothing is persisted until this succeeds.
        if (!prepareAssetCacheVisible(database, error)) {
            MessageBoxW(nullptr, error.c_str(), L"No se pudieron extraer los recursos", MB_OK | MB_ICONERROR);
            candidate.reset();
            continue;
        }

        HWND window = createGameWindow(instance, showCommand);
        if (!window) {
            // Renderer validation failed (for example, UI/font was missing).
            // Do not mark this ROM/cache as valid. Purge the incomplete cache
            // and return directly to the ROM selector without restarting.
            discardBrokenCache();
            MessageBoxW(nullptr,
                        L"La ROM se pudo leer, pero la extraccion no produjo todos los recursos necesarios. Selecciona nuevamente la ROM para reintentar la extraccion.",
                        L"Recursos incompletos", MB_OK | MB_ICONWARNING);
            candidate.reset();
            continue;
        }

        database.setSetting("rom_directory", candidate->wstring());
        database.setSetting("asset_cache_directory", gAssets.root().wstring());
        database.setSetting("asset_cache_signature",
                            std::wstring(gAssets.signature().begin(), gAssets.signature().end()));

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (uninitializeOle) OleUninitialize();
        return static_cast<int>(message.wParam);
    }
}
