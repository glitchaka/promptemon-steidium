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
    const auto scan = diagnosticBundlePath() / L"ui_scan.tsv";
    std::cout << "\n[UI] Diagnostico inmediato: " << scan.u8string() << '\n';
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
    std::cout.flush();
}

int WINAPI PromptemonDiagnosticMessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    if (text && wcsstr(text, L"recursos de interfaz") != nullptr) dumpUiScanEarly();
    return ::MessageBoxW(hwnd, text, caption, type);
}

} // namespace

#define MessageBoxW PromptemonDiagnosticMessageBoxW
#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain
#undef MessageBoxW

#include "demo/DebugConsoleOverlay.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

constexpr int kDebugHotkeyId = 0x50C0;
promptemon::demo::DebugConsoleOverlay gDevConsole;
std::filesystem::path gBundlePath;

void openDiagnosticConsole() {
    if (!AllocConsole()) return;
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Promptemon Steidium - Monitor de carga");
    std::cout << "========================================\n"
              << " Promptemon Steidium - Monitor de carga\n"
              << "========================================\n";
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

std::string inventoryText(const std::vector<std::string>& resources) {
    std::ostringstream out;
    out << "resources=" << resources.size()
        << " font=" << countContaining(resources, "font")
        << " cfnt=" << countContaining(resources, "cfnt")
        << " bclim=" << countContaining(resources, "bclim")
        << " ui=" << countContaining(resources, "ui")
        << " arc=" << countContaining(resources, ".arc")
        << " stage=" << countContaining(resources, "stage")
        << " town=" << countContaining(resources, "town")
        << " pii=" << countContaining(resources, "pii/");
    return out.str();
}

std::string readUiScan() {
    std::ifstream input(gBundlePath / "ui_scan.tsv", std::ios::binary);
    if (!input) return "[UI] ui_scan.tsv no existe";
    std::ostringstream out;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out << "[UI] " << line << "\r\n";
    }
    return out.str();
}

std::vector<std::pair<int, std::string>> stageCandidates() {
    std::vector<std::pair<int, std::string>> result;
    for (const auto& path : gModelResources) {
        const int score = stageScore(path);
        if (score > 0) result.emplace_back(score, path);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    return result;
}

std::string debugHelp() {
    return
        "help                 lista comandos\r\n"
        "status               estado del runtime\r\n"
        "ui                   diagnostico de fuente/interfaz\r\n"
        "find <texto>         busca recursos\r\n"
        "models               modelos Pokemon resueltos\r\n"
        "model <dex>          modelo de un Pokemon\r\n"
        "stages               candidatos de escenario\r\n"
        "resource <ruta>      tamano/cabecera del recurso\r\n"
        "action <100-116>     ejecuta una accion del juego\r\n"
        "clear                limpia la consola\r\n"
        "quit                 cierra Promptemon";
}

std::string executeDebugCommand(const std::string& raw) {
    std::istringstream input(raw);
    std::string command;
    input >> command;
    command = lowerAscii(command);
    std::ostringstream out;

    if (command.empty()) return {};
    if (command == "help") return debugHelp();

    if (command == "status") {
        const auto resources = gAssets.resources();
        out << "[STATUS] screen=" << static_cast<int>(gScreen)
            << " models=" << gModelResources.size() << "\r\n";
        out << "[STATUS] " << inventoryText(resources) << "\r\n";
        out << "[STATUS] stage=" << (gStageModel.empty() ? "<NONE>" : gStageModel) << "\r\n";
        out << "[STATUS] effect=" << (gEffectModel.empty() ? "<NONE>" : gEffectModel);
        return out.str();
    }

    if (command == "ui") return readUiScan();

    if (command == "find") {
        std::string term;
        std::getline(input >> std::ws, term);
        term = lowerAscii(term);
        if (term.empty()) return "uso: find <texto>";
        std::size_t shown = 0;
        for (const auto& resource : gAssets.resources()) {
            if (lowerAscii(resource).find(term) == std::string::npos) continue;
            out << resource << "\r\n";
            if (++shown >= 50) {
                out << "... limite de 50 resultados\r\n";
                break;
            }
        }
        out << "[FIND] mostrados=" << shown;
        return out.str();
    }

    if (command == "models") {
        for (const auto& species : kSpecies) {
            const auto& model = modelFor(species);
            out << "dex=" << species.dex << " -> " << (model.empty() ? "<NO ENCONTRADO>" : model) << "\r\n";
        }
        return out.str();
    }

    if (command == "model") {
        int dex = 0;
        input >> dex;
        for (const auto& species : kSpecies) {
            if (species.dex != dex) continue;
            const auto& model = modelFor(species);
            out << "[MODEL] dex=" << dex << " -> " << (model.empty() ? "<NO ENCONTRADO>" : model);
            return out.str();
        }
        return "[MODEL] dex no registrado";
    }

    if (command == "stages") {
        const auto candidates = stageCandidates();
        const std::size_t limit = std::min<std::size_t>(12, candidates.size());
        for (std::size_t i = 0; i < limit; ++i) {
            out << '[' << i + 1 << "] score=" << candidates[i].first << ' ' << candidates[i].second << "\r\n";
        }
        if (limit == 0) out << "<NINGUNO>";
        return out.str();
    }

    if (command == "resource") {
        std::string path;
        std::getline(input >> std::ws, path);
        if (path.empty()) return "uso: resource <ruta virtual>";
        if (!gAssets.hasResource(path)) return "[RESOURCE] No existe: " + path;
        try {
            const auto bytes = gAssets.extractResource(path);
            out << "[RESOURCE] size=" << bytes.size() << " header=";
            const std::size_t n = std::min<std::size_t>(16, bytes.size());
            for (std::size_t i = 0; i < n; ++i) {
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << ' ';
            }
            return out.str();
        } catch (...) {
            return "[RESOURCE][ERROR] no se pudo extraer";
        }
    }

    if (command == "action") {
        int id = 0;
        input >> id;
        if (id < 100 || id > 116 || !gWindow) return "[ACTION] id invalido (100-116)";
        PostMessageW(gWindow, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        out << "[ACTION] enviado " << id;
        return out.str();
    }

    if (command == "clear") {
        return "[CONSOLA] clear se aplica al reiniciar el panel por ahora";
    }

    if (command == "quit" || command == "exit") {
        if (gWindow) PostMessageW(gWindow, WM_CLOSE, 0, 0);
        return "[CONSOLA] cerrando...";
    }

    return "[CONSOLA] comando desconocido: " + command + " (usa help)";
}

void dumpUiScanToStdout() {
    std::cout << readUiScan() << '\n';
}

[[noreturn]] void fatalExit(int code) {
    std::cout << "[PROCESO] Terminando proceso. Codigo " << code << '\n';
    std::cout.flush();
    FreeConsole();
    ExitProcess(static_cast<UINT>(code));
}

LRESULT CALLBACK InstalledWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && static_cast<int>(wp) == kDebugHotkeyId) {
        gDevConsole.toggle();
        return 0;
    }
    if (msg == WM_SIZE) gDevConsole.resize();
    if (msg == WM_DESTROY) {
        UnregisterHotKey(hwnd, kDebugHotkeyId);
        gDevConsole.destroy();
    }
    return WndProc(hwnd, msg, wp, lp);
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
    std::cout << "[OLE] " << (uninitializeOle ? "OK" : "continuando") << '\n';

    promptemon::storage::LocalDatabase database;
    std::cout << "[DB] Abriendo player.db...\n";
    if (!database.open()) {
        MessageBoxW(nullptr, L"No se pudo abrir player.db junto al ejecutable.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }
    std::cout << "[DB] OK\n";

    gBundlePath = database.path().parent_path() / L"assets" / L"resources-tx";
    std::cout << "[RUTA] bundle: " << gBundlePath.u8string() << '\n';
    std::cout << "[BUNDLE] Abriendo cache instalada...\n";
    if (!gAssets.openInstalledDirectory(gBundlePath)) {
        MessageBoxW(nullptr, L"Faltan los recursos del juego. Ejecuta PromptemonResources.exe.", L"Promptemon Steidium", MB_OK | MB_ICONWARNING);
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }

    const auto allResources = gAssets.resources();
    std::cout << "[BUNDLE] OK - " << inventoryText(allResources) << '\n';

    gModelResources = gAssets.modelResources();
    gResolvedModels.clear();
    std::cout << "[MODELOS] " << gModelResources.size() << " BCH utilizables.\n";
    if (gModelResources.empty()) {
        MessageBoxW(nullptr, L"El bundle no contiene modelos BCH utilizables.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }

    for (const auto& species : kSpecies) {
        std::cout << "[POKEMON] dex=" << species.dex << " model=" << modelFor(species) << '\n';
    }
    const auto candidates = stageCandidates();
    for (std::size_t i = 0; i < std::min<std::size_t>(10, candidates.size()); ++i) {
        std::cout << "[STAGE] score=" << candidates[i].first << ' ' << candidates[i].second << '\n';
    }

    if (!registerInstalledGameWindowClass(instance)) {
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }

    std::cout << "[RENDER] Creando ventana/OpenGL...\n";
    HWND window = createInstalledGameWindow(instance, showCommand);
    if (!window) {
        dumpUiScanToStdout();
        if (uninitializeOle) OleUninitialize();
        fatalExit(1);
    }

    RegisterHotKey(window, kDebugHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'C');
    if (gDevConsole.create(window, executeDebugCommand)) {
        gDevConsole.append("[STARTUP] " + inventoryText(allResources) + "\r\n");
        gDevConsole.append("[STARTUP] models=" + std::to_string(gModelResources.size()) + "\r\n");
        gDevConsole.append("[STARTUP] stage=" + (gStageModel.empty() ? std::string("<NONE>") : gStageModel) + "\r\n");
        gDevConsole.append("[STARTUP] effect=" + (gEffectModel.empty() ? std::string("<NONE>") : gEffectModel) + "\r\n");
        gDevConsole.append(readUiScan());
    }

    std::cout << "[RENDER] stage=" << (gStageModel.empty() ? "<NONE>" : gStageModel) << '\n';
    std::cout << "[RENDER] effect=" << (gEffectModel.empty() ? "<NONE>" : gEffectModel) << '\n';
    dumpUiScanToStdout();
    std::cout << "[JUEGO] Ejecutando. Ctrl+Shift+C abre la consola interna estilo Half-Life.\n";

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (uninitializeOle) OleUninitialize();
    FreeConsole();
    return static_cast<int>(message.wParam);
}
