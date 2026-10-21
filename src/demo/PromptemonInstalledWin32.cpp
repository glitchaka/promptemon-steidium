#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void openDiagnosticConsole() {
    if (!AllocConsole()) return;

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"Promptemon Steidium - Monitor de carga");

    std::cout << "========================================\n";
    std::cout << " Promptemon Steidium - Monitor de carga\n";
    std::cout << "========================================\n";
}

void logPath(const char* label, const std::filesystem::path& path) {
    std::cout << "[RUTA] " << label << ": " << path.u8string() << '\n';
}

void dumpUiScan(const std::filesystem::path& bundle) {
    const auto scan = bundle / "ui_scan.tsv";
    std::cout << "\n[DIAGNOSTICO UI] " << scan.u8string() << '\n';

    std::ifstream input(scan, std::ios::binary);
    if (!input) {
        std::cout << "[ERROR] ui_scan.tsv no existe; RomUiSkin fallo antes de generar diagnostico.\n";
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::cout << "  " << line << '\n';
    }
}

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
    openDiagnosticConsole();
    std::cout << "[INICIO] Arrancando Promptemon Steidium...\n";

    const HRESULT ole = OleInitialize(nullptr);
    const bool uninitializeOle = SUCCEEDED(ole);
    std::cout << "[OLE] " << (uninitializeOle ? "OK" : "continuando sin inicializacion completa") << '\n';

    promptemon::storage::LocalDatabase database;
    std::cout << "[DB] Abriendo player.db...\n";
    if (!database.open()) {
        std::cout << "[ERROR] No se pudo abrir player.db.\n";
        MessageBoxW(nullptr,
                    L"No se pudo abrir player.db junto al ejecutable.",
                    L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }
    std::cout << "[DB] OK\n";
    logPath("player.db", database.path());

    const std::filesystem::path bundle =
        database.path().parent_path() / L"assets" / L"resources-tx";
    logPath("bundle", bundle);

    std::cout << "[BUNDLE] Abriendo cache instalada...\n";
    if (!gAssets.openInstalledDirectory(bundle)) {
        std::cout << "[ERROR] No se pudo abrir assets/resources-tx como cache instalada.\n";
        std::cout << "        Verifica bundle_manifest.tsv y files/romfs.\n";
        MessageBoxW(nullptr,
                    L"Faltan los recursos del juego.\n\n"
                    L"Ejecuta PromptemonResources.exe una vez. Ese archivo instala "
                    L"el subset necesario junto al juego; no necesitas seleccionar la ROM.",
                    L"Promptemon Steidium", MB_OK | MB_ICONWARNING);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    const auto allResources = gAssets.resources();
    std::cout << "[BUNDLE] OK - " << allResources.size() << " recursos indexados.\n";

    gModelResources = gAssets.modelResources();
    gResolvedModels.clear();
    std::cout << "[MODELOS] " << gModelResources.size() << " BCH de modelo utilizables.\n";
    if (gModelResources.empty()) {
        std::cout << "[ERROR] El bundle no contiene modelos BCH utilizables.\n";
        MessageBoxW(nullptr,
                    L"El bundle instalado no contiene modelos BCH utilizables.\n"
                    L"Vuelve a generar PromptemonResources.exe desde resources-tx.",
                    L"Bundle de recursos invalido", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    std::cout << "[WIN32] Registrando ventana principal...\n";
    if (!registerInstalledGameWindowClass(instance)) {
        std::cout << "[ERROR] RegisterClassW fallo. GetLastError=" << GetLastError() << '\n';
        if (uninitializeOle) OleUninitialize();
        return 1;
    }
    std::cout << "[WIN32] OK\n";

    std::cout << "[RENDER] Creando ventana y cargando OpenGL/UI/fuente...\n";
    HWND window = createInstalledGameWindow(instance, showCommand);
    if (!window) {
        std::cout << "[ERROR] createInstalledGameWindow fallo. GetLastError=" << GetLastError() << '\n';
        dumpUiScan(bundle);
        std::cout << "\n[DETENIDO] Revisa las lineas anteriores para identificar si fallo fuente, boton o panel.\n";
        MessageBoxW(nullptr,
                    L"El bundle fue encontrado, pero faltan recursos necesarios para iniciar "
                    L"el renderizador (UI, fuente o escena).\n"
                    L"Revisa el monitor de carga para ver el fallo exacto.",
                    L"Recursos incompletos", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        return 1;
    }

    std::cout << "[RENDER] OK - ventana y recursos inicializados.\n";
    std::cout << "[JUEGO] Promptemon Steidium listo.\n\n";

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    std::cout << "[SALIDA] Codigo " << static_cast<int>(message.wParam) << '\n';
    if (uninitializeOle) OleUninitialize();
    return static_cast<int>(message.wParam);
}
