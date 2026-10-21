#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path diagnosticBundlePath() {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (!length || length >= std::size(buffer)) return {};
    return std::filesystem::path(buffer).parent_path() / L"assets" / L"resources-tx";
}

void dumpUiScanEarly() {
    const auto bundle = diagnosticBundlePath();
    const auto scan = bundle / L"ui_scan.tsv";
    std::cout << "\n[UI] Diagnostico inmediato: " << scan.u8string() << '\n';

    std::ifstream input(scan, std::ios::binary);
    if (!input) {
        std::cout << "[UI][ERROR] ui_scan.tsv no existe. La carga fallo antes de completar RomUiSkin::load().\n";
        return;
    }

    std::string line;
    bool foundSummary = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::cout << "[UI] " << line << '\n';
        if (line.rfind("SUMMARY\t", 0) == 0) foundSummary = true;
    }
    if (!foundSummary) {
        std::cout << "[UI][ERROR] ui_scan.tsv existe pero no contiene SUMMARY; la exploracion se interrumpio antes de terminar.\n";
    }
    std::cout.flush();
}

int WINAPI PromptemonDiagnosticMessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    if (text && wcsstr(text, L"recursos de interfaz") != nullptr) {
        std::cout << "[RENDER][ERROR] RomUiSkin no quedo listo. Volcando diagnostico antes del cuadro de error...\n";
        dumpUiScanEarly();
    }
    return ::MessageBoxW(hwnd, text, caption, type);
}

} // namespace

#define MessageBoxW PromptemonDiagnosticMessageBoxW
#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain
#undef MessageBoxW

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace {

void openDiagnosticConsole() {
    if (!AllocConsole()) return;

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"Promptemon Steidium - Monitor y consola de debug");

    std::cout << "========================================\n";
    std::cout << " Promptemon Steidium - Monitor de carga\n";
    std::cout << "========================================\n";
}

void focusDebugConsole() {
    HWND console = GetConsoleWindow();
    if (!console) return;
    ShowWindow(console, SW_RESTORE);
    SetForegroundWindow(console);
    BringWindowToTop(console);
    std::cout << "\n[CONSOLA] Debug activo. Escribe 'help'.\n> " << std::flush;
}

LRESULT CALLBACK InstalledWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == 'C' &&
        (GetKeyState(VK_CONTROL) & 0x8000) &&
        (GetKeyState(VK_SHIFT) & 0x8000)) {
        focusDebugConsole();
        return 0;
    }
    return WndProc(hwnd, msg, wp, lp);
}

void logPath(const char* label, const std::filesystem::path& path) {
    std::cout << "[RUTA] " << label << ": " << path.u8string() << '\n';
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::size_t countContaining(const std::vector<std::string>& resources, const std::string& token) {
    const std::string needle = lowerAscii(token);
    std::size_t count = 0;
    for (const auto& resource : resources) {
        if (lowerAscii(resource).find(needle) != std::string::npos) ++count;
    }
    return count;
}

void dumpUiScan(const std::filesystem::path& bundle) {
    const auto scan = bundle / "ui_scan.tsv";
    std::cout << "\n[DIAGNOSTICO UI] " << scan.u8string() << '\n';

    std::ifstream input(scan, std::ios::binary);
    if (!input) {
        std::cout << "[UI][ERROR] ui_scan.tsv no existe.\n";
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::cout << "[UI] " << line << '\n';
    }
}

void logInventory(const std::vector<std::string>& resources) {
    std::cout << "[INVENTARIO] total=" << resources.size()
              << " font=" << countContaining(resources, "font")
              << " cfnt=" << countContaining(resources, "cfnt")
              << " bclim=" << countContaining(resources, "bclim")
              << " ui=" << countContaining(resources, "ui")
              << " arc=" << countContaining(resources, ".arc")
              << " stage=" << countContaining(resources, "stage")
              << " town=" << countContaining(resources, "town")
              << " pii=" << countContaining(resources, "pii/")
              << '\n';
}

void logPokemonModels() {
    std::cout << "[POKEMON] Modelos resueltos:\n";
    for (const auto& species : kSpecies) {
        const std::string& model = modelFor(species);
        std::wcout << L"  dex=" << species.dex << L" " << species.name << L" -> ";
        if (model.empty()) std::wcout << L"<NO ENCONTRADO>\n";
        else std::cout << model << '\n';
    }
}

void logStageCandidates(std::size_t maxItems = 10) {
    struct Candidate { int score; std::string path; };
    std::vector<Candidate> candidates;
    for (const auto& path : gModelResources) {
        const int score = stageScore(path);
        if (score > 0) candidates.push_back({score, path});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });

    std::cout << "[STAGE] Candidatos:\n";
    const std::size_t limit = std::min(maxItems, candidates.size());
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  [" << i + 1 << "] score=" << candidates[i].score << " " << candidates[i].path << '\n';
    }
    if (limit == 0) std::cout << "  <NINGUNO>\n";
}

void printResourceInfo(const std::string& path) {
    if (!gAssets.hasResource(path)) {
        std::cout << "[RESOURCE] No existe: " << path << '\n';
        return;
    }
    try {
        const auto bytes = gAssets.extractResource(path);
        std::cout << "[RESOURCE] " << path << " size=" << bytes.size() << " bytes header=";
        const std::size_t n = std::min<std::size_t>(16, bytes.size());
        for (std::size_t i = 0; i < n; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << ' ';
        }
        std::cout << std::dec << '\n';
    } catch (const std::exception& e) {
        std::cout << "[RESOURCE][ERROR] " << e.what() << '\n';
    } catch (...) {
        std::cout << "[RESOURCE][ERROR] excepcion desconocida\n";
    }
}

void printDebugHelp() {
    std::cout << "\nComandos disponibles:\n"
              << "  help                 muestra esta ayuda\n"
              << "  status               estado general del runtime\n"
              << "  ui                   vuelca ui_scan.tsv\n"
              << "  find <texto>         busca recursos por nombre/ruta\n"
              << "  models               modelos resueltos de Pokemon\n"
              << "  model <dex>          modelo resuelto para un dex\n"
              << "  stages               candidatos de escenario y score\n"
              << "  resource <ruta>      tamano y cabecera de un recurso\n"
              << "  action <id>          envia un GameAction (100-116)\n"
              << "  quit                 cierra el juego y el proceso\n\n";
}

void debugCommandLoop(std::filesystem::path bundle) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << "> " << std::flush;
            continue;
        }

        std::istringstream input(line);
        std::string command;
        input >> command;
        command = lowerAscii(command);

        if (command == "help") {
            printDebugHelp();
        } else if (command == "status") {
            const auto resources = gAssets.resources();
            std::cout << "[STATUS] screen=" << static_cast<int>(gScreen)
                      << " resources=" << resources.size()
                      << " models=" << gModelResources.size() << '\n';
            std::cout << "[STATUS] stage=" << (gStageModel.empty() ? "<NONE>" : gStageModel) << '\n';
            std::cout << "[STATUS] effect=" << (gEffectModel.empty() ? "<NONE>" : gEffectModel) << '\n';
            logInventory(resources);
        } else if (command == "ui") {
            dumpUiScan(bundle);
        } else if (command == "find") {
            std::string term;
            std::getline(input >> std::ws, term);
            term = lowerAscii(term);
            if (term.empty()) {
                std::cout << "uso: find <texto>\n";
            } else {
                std::size_t shown = 0;
                for (const auto& resource : gAssets.resources()) {
                    if (lowerAscii(resource).find(term) == std::string::npos) continue;
                    std::cout << "  " << resource << '\n';
                    if (++shown >= 50) {
                        std::cout << "  ... limite de 50 resultados\n";
                        break;
                    }
                }
                std::cout << "[FIND] mostrados=" << shown << '\n';
            }
        } else if (command == "models") {
            logPokemonModels();
        } else if (command == "model") {
            int dex = 0;
            input >> dex;
            bool found = false;
            for (const auto& species : kSpecies) {
                if (species.dex != dex) continue;
                const auto& model = modelFor(species);
                std::cout << "[MODEL] dex=" << dex << " -> " << (model.empty() ? "<NO ENCONTRADO>" : model) << '\n';
                found = true;
                break;
            }
            if (!found) std::cout << "[MODEL] dex no registrado en kSpecies\n";
        } else if (command == "stages") {
            logStageCandidates();
        } else if (command == "resource") {
            std::string path;
            std::getline(input >> std::ws, path);
            if (path.empty()) std::cout << "uso: resource <ruta virtual>\n";
            else printResourceInfo(path);
        } else if (command == "action") {
            int id = 0;
            input >> id;
            if (id < 100 || id > 116 || !gWindow) {
                std::cout << "[ACTION] id invalido; rango actual 100-116\n";
            } else {
                PostMessageW(gWindow, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
                std::cout << "[ACTION] enviado " << id << '\n';
            }
        } else if (command == "quit" || command == "exit") {
            if (gWindow) PostMessageW(gWindow, WM_CLOSE, 0, 0);
            break;
        } else {
            std::cout << "[CONSOLA] comando desconocido: " << command << " (usa help)\n";
        }

        std::cout << "> " << std::flush;
    }
}

[[noreturn]] void fatalExit(int code) {
    std::cout << "[PROCESO] Terminando proceso. Codigo " << code << '\n';
    std::cout.flush();
    FreeConsole();
    ExitProcess(static_cast<UINT>(code));
}

bool registerInstalledGameWindowClass(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = InstalledWndProc;
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
        fatalExit(1);
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
        fatalExit(1);
    }

    const auto allResources = gAssets.resources();
    std::cout << "[BUNDLE] OK - " << allResources.size() << " recursos indexados.\n";
    logInventory(allResources);

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
        fatalExit(1);
    }

    logPokemonModels();
    logStageCandidates();

    std::cout << "[WIN32] Registrando ventana principal...\n";
    if (!registerInstalledGameWindowClass(instance)) {
        std::cout << "[ERROR] RegisterClassW fallo. GetLastError=" << GetLastError() << '\n';
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }
    std::cout << "[WIN32] OK\n";

    std::cout << "[RENDER] Creando ventana y cargando OpenGL/UI/fuente...\n";
    HWND window = createInstalledGameWindow(instance, showCommand);
    if (!window) {
        std::cout << "[ERROR] createInstalledGameWindow fallo. GetLastError=" << GetLastError() << '\n';
        dumpUiScan(bundle);
        std::cout << "\n[DETENIDO] El renderizador fallo antes de crear la ventana.\n";
        MessageBoxW(nullptr,
                    L"No se pudo iniciar el renderizador.\n"
                    L"Revisa el monitor de carga para ver el fallo exacto.",
                    L"Error de renderizado", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }

    std::cout << "[RENDER] OK - ventana inicializada.\n";
    std::cout << "[RENDER] stage seleccionado: " << (gStageModel.empty() ? "<NONE>" : gStageModel) << '\n';
    std::cout << "[RENDER] effect seleccionado: " << (gEffectModel.empty() ? "<NONE>" : gEffectModel) << '\n';
    dumpUiScan(bundle);

    std::cout << "[JUEGO] Promptemon Steidium ejecutando.\n";
    std::cout << "[CONSOLA] Ctrl+Shift+C trae esta consola al frente. Escribe 'help'.\n> " << std::flush;

    std::thread(debugCommandLoop, bundle).detach();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    std::cout << "\n[SALIDA] Codigo " << static_cast<int>(message.wParam) << '\n';
    std::cout.flush();
    if (uninitializeOle) OleUninitialize();
    FreeConsole();
    return static_cast<int>(message.wParam);
}
