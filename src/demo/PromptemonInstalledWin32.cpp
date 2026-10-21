#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain

#include <filesystem>

namespace {

bool registerInstalledGameWindowClass(HINSTANCE instance) {
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

HWND createInstalledGameWindow(HINSTANCE instance, int showCommand) {
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

    const std::filesystem::path bundle =
        database.path().parent_path() / L"assets" / L"resources-tx";

    if (!gAssets.openInstalledDirectory(bundle)) {
        MessageBoxW(nullptr,
                    L"Faltan los recursos del juego.\n\n"
                    L"Ejecuta PromptemonResources.exe una vez. Ese archivo instala "
                    L"el subset necesario junto al juego; no necesitas seleccionar la ROM.",
                    L"Promptemon Steidium", MB_OK | MB_ICONWARNING);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    gModelResources = gAssets.modelResources();
    gResolvedModels.clear();
    if (gModelResources.empty()) {
        MessageBoxW(nullptr,
                    L"El bundle instalado no contiene modelos BCH utilizables.\n"
                    L"Vuelve a generar PromptemonResources.exe desde resources-tx.",
                    L"Bundle de recursos invalido", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    if (!registerInstalledGameWindowClass(instance)) {
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    HWND window = createInstalledGameWindow(instance, showCommand);
    if (!window) {
        MessageBoxW(nullptr,
                    L"El bundle fue encontrado, pero faltan recursos necesarios para iniciar "
                    L"el renderizador (UI, fuente o escena).\n"
                    L"Vuelve a generar PromptemonResources.exe desde resources-tx.",
                    L"Recursos incompletos", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (uninitializeOle) OleUninitialize();
    return static_cast<int>(message.wParam);
}
