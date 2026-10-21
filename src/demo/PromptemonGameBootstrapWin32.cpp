#include "demo/ExtractionProgressWindow.h"
#include "rom/Lzh8.h"

#define wWinMain promptemonLegacyWWinMain
#include "demo/PromptemonGameWin32Cache.cpp"
#undef wWinMain

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int ID_DEV_CONSOLE_HOTKEY = 0x5043;
constexpr UINT WM_DEV_CONSOLE_COMMAND = WM_APP + 0x43;

std::string gDefaultStageModel;
std::string gHubStageModel;
std::string gDevStageOverride;
std::size_t gDecodedBchCount = 0;
bool gDevConsoleCreated = false;
std::atomic_bool gDevConsoleStop{false};

Species gDebugPlayerSpecies = kSpecies[0];
Species gDebugWildSpecies = kSpecies[1];
std::wstring gDebugPlayerName = L"DEBUG_PLAYER";
std::wstring gDebugWildName = L"DEBUG_ENEMY";

bool devEndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::wstring devWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size);
    return out;
}

std::string devNarrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::string devNarrow(const wchar_t* value) {
    return value ? devNarrow(std::wstring(value)) : std::string{};
}

bool devParseInt(const std::string& text, int& value) {
    try {
        std::size_t used = 0;
        value = std::stoi(text, &used, 10);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

std::string devLeafName(std::string path) {
    const auto marker = path.rfind(" :: ");
    if (marker != std::string::npos) path = path.substr(marker + 4);
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) path = path.substr(slash + 1);
    std::string p = lower(path);
    if (devEndsWith(p, ".cx")) {
        path.resize(path.size() - 3);
        p.resize(p.size() - 3);
    }
    if (devEndsWith(p, ".bch")) path.resize(path.size() - 4);
    return path;
}

bool devNormalizeCachedBch(const RomAssetCache::ProgressCallback& progress) {
    std::ifstream manifest(gAssets.root() / L"manifest.tsv", std::ios::binary);
    if (!manifest) return false;

    std::unordered_map<std::string, std::filesystem::path> files;
    std::string line;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto tab = line.find('\t');
        if (tab != std::string::npos) {
            files[line.substr(0, tab)] = std::filesystem::u8path(line.substr(tab + 1));
        }
    }

    const auto resources = gAssets.resources();
    gDecodedBchCount = 0;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        const auto& virtualPath = resources[i];
        const std::string p = lower(virtualPath);
        if (!(devEndsWith(p, ".bch") || devEndsWith(p, ".bch.cx"))) continue;
        if (progress) progress(i, resources.size(), "Preparando BCH: " + virtualPath);

        try {
            auto bytes = gAssets.extractResource(virtualPath);
            if (!promptemon::rom::Lzh8::isCompressed(bytes)) continue;
            auto decoded = promptemon::rom::Lzh8::decompress(bytes);
            if (decoded.size() < 4 || decoded[0] != 'B' || decoded[1] != 'C' ||
                decoded[2] != 'H' || decoded[3] != 0) continue;
            const auto found = files.find(virtualPath);
            if (found == files.end()) continue;

            std::ofstream output(gAssets.root() / found->second,
                                 std::ios::binary | std::ios::trunc);
            if (!output) continue;
            output.write(reinterpret_cast<const char*>(decoded.data()),
                         static_cast<std::streamsize>(decoded.size()));
            if (output) ++gDecodedBchCount;
        } catch (...) {
        }
    }
    if (progress) progress(resources.size(), resources.size(), std::string{});
    return true;
}

bool prepareAssetCacheVisible(promptemon::storage::LocalDatabase& database,
                              std::wstring& error) {
    promptemon::demo::ExtractionProgressWindow progressWindow;
    bool visible = false;
    try {
        const std::filesystem::path base = database.path().parent_path() / L"assets";
        const RomAssetCache::ProgressCallback progress = [&](std::size_t current,
                                                              std::size_t total,
                                                              const std::string& path) {
            if (!visible) visible = progressWindow.create();
            if (visible) progressWindow.update(current, total, path);
        };

        if (!gAssets.prepare(gRomArchive, base, progress)) {
            progressWindow.close();
            error = L"No se pudieron exportar los recursos necesarios de la ROM.";
            return false;
        }

        devNormalizeCachedBch(progress);
        progressWindow.close();
        gModelResources = gAssets.modelResources();
        if (gModelResources.empty()) {
            error = L"La exportacion termino sin modelos BCH utilizables.";
            return false;
        }
        gRomArchive.close();
        return true;
    } catch (...) {
        progressWindow.close();
        error = L"Fallo la extraccion de recursos de la ROM.";
        return false;
    }
}

bool devBchUsable(const std::string& path) {
    try {
        const auto scene = promptemon::bch::Scene::parse(gAssets.extractBch(path));
        return !scene.meshes().empty();
    } catch (...) {
        return false;
    }
}

std::string devPreferredHubScene() {
    std::vector<std::pair<int, std::string>> candidates;
    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (contains(p, "pii/") || contains(p, "mii/") || contains(p, "/ui/")) continue;

        int score = stageScore(path);
        if (contains(p, "stage/town1/ground_96.bch")) score += 20000;
        if (contains(p, "stage/town1/rep_01.bch")) score += 16000;
        if (contains(p, "event/makuai/3d.bch")) score += 12000;
        if (contains(p, "op_toykingdom_01.bch")) score += 11000;
        if (contains(p, "kingscourt_01.bch")) score += 10000;
        if (contains(p, "makuai_idou.bch")) score += 9000;
        if (score > 0) candidates.push_back({score, path});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [score, path] : candidates) {
        (void)score;
        if (devBchUsable(path)) return path;
    }
    return {};
}

void devApplyStage() {
    const std::string desired = !gDevStageOverride.empty()
        ? gDevStageOverride
        : (gScreen == Screen::Hub && !gHubStageModel.empty()
            ? gHubStageModel
            : gDefaultStageModel);
    if (!desired.empty() && desired != gStageModel) {
        gStageModel = desired;
        gRenderer.setStageModel(desired);
    }
}

std::string devFindPokemonModel(const std::string& token) {
    const std::string q = lower(token);
    int bestScore = -1000000;
    std::string best;
    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (!contains(p, "pii/") || p.find(q) == std::string::npos) continue;
        const std::string leaf = lower(devLeafName(path));
        int score = 100;
        if (leaf == q) score += 1000;
        if (leaf.rfind(q, 0) == 0) score += 500;
        if (contains(leaf, "_normal")) score += 150;
        if (!contains(q, "mega") && contains(leaf, "mega")) score -= 300;
        if (score > bestScore && devBchUsable(path)) {
            bestScore = score;
            best = path;
        }
    }
    return best;
}

std::string devFindScene(const std::string& token) {
    const std::string q = lower(token);
    int bestScore = -1000000;
    std::string best;
    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (contains(p, "pii/") || contains(p, "mii/") || contains(p, "/ui/")) continue;
        if (p.find(q) == std::string::npos) continue;
        int score = stageScore(path);
        if (contains(p, "stage/")) score += 200;
        if (contains(p, "ground")) score += 120;
        if (score > bestScore && devBchUsable(path)) {
            bestScore = score;
            best = path;
        }
    }
    return best;
}

const Species* devKnownSpecies(const std::string& token) {
    const std::string q = lower(token);
    int dex = -1;
    const bool numeric = devParseInt(q, dex);
    for (const auto& species : kSpecies) {
        if (numeric && species.dex == dex) return &species;
        if (lower(devNarrow(species.name)) == q) return &species;
        const char* keys[] = {species.modelKey1, species.modelKey2, species.modelKey3};
        for (const char* key : keys) {
            if (key && *key && lower(key) == q) return &species;
        }
    }
    return nullptr;
}

void devEnsurePlayer() {
    if (gPlayer.species) return;
    gPlayer = {};
    gPlayer.species = &kSpecies[0];
    gPlayer.level = 5;
    gPlayer.iv = randomIvs();
    gHasStarter = true;
}

void devRefresh() {
    devApplyStage();
    if (gScreen == Screen::Hub && gPlayer.species) showHub();
    else if (gScreen == Screen::Hunt && gWild.species) showHunt();
    else if (gScreen == Screen::Battle && gPlayer.species && gWild.species) showBattleState();
}

bool devSetPokemon(bool player, const std::string& token,
                   std::optional<int> requestedLevel, bool refresh = true) {
    if (player) devEnsurePlayer();
    const Species* species = devKnownSpecies(token);

    if (!species) {
        const std::string model = devFindPokemonModel(token);
        if (model.empty()) {
            std::cout << "No encontre Pokemon/modelo: " << token << "\n";
            return false;
        }

        if (player) {
            gResolvedModels.erase(&gDebugPlayerSpecies);
            gDebugPlayerSpecies = kSpecies[0];
            gDebugPlayerName = devWide(devLeafName(model));
            gDebugPlayerSpecies.name = gDebugPlayerName.c_str();
            gResolvedModels[&gDebugPlayerSpecies] = model;
            species = &gDebugPlayerSpecies;
        } else {
            gResolvedModels.erase(&gDebugWildSpecies);
            gDebugWildSpecies = kSpecies[1];
            gDebugWildName = devWide(devLeafName(model));
            gDebugWildSpecies.name = gDebugWildName.c_str();
            gResolvedModels[&gDebugWildSpecies] = model;
            species = &gDebugWildSpecies;
        }
    }

    if (player) {
        gPlayer.species = species;
        if (requestedLevel) gPlayer.level = std::clamp(*requestedLevel, 1, 100);
        if (gPlayer.level < 1) gPlayer.level = 5;
        gHasStarter = true;
        std::cout << "player = " << devNarrow(species->name) << " Nv." << gPlayer.level << "\n";
    } else {
        gWild.species = species;
        if (requestedLevel) gWild.level = std::clamp(*requestedLevel, 1, 100);
        if (gWild.level < 1) gWild.level = 5;
        gWild.iv = randomIvs();
        std::cout << "enemy = " << devNarrow(species->name) << " Nv." << gWild.level << "\n";
    }

    if (refresh) {
        if (gScreen == Screen::Battle && gPlayer.species && gWild.species) beginBattle();
        else devRefresh();
    }
    return true;
}

int devIvIndex(const std::string& token) {
    const std::string q = lower(token);
    if (q == "hp" || q == "ps") return 0;
    if (q == "atk" || q == "atq") return 1;
    if (q == "def") return 2;
    if (q == "spa" || q == "spatk") return 3;
    if (q == "spd" || q == "spdef") return 4;
    if (q == "spe" || q == "speed" || q == "vel") return 5;
    return -1;
}

void devPrintHelp() {
    std::cout
        << "\nPROMPTEMON - CONSOLA DE PRUEBAS\n"
        << "help | status\n"
        << "player <pokemon/modelo> [nivel]\n"
        << "enemy <pokemon/modelo> [nivel]\n"
        << "level [player|enemy] <1-100>\n"
        << "battle [pokemon/modelo] [nivel]\n"
        << "hunt [pokemon/modelo] [nivel]\n"
        << "random-wild | shiny <on|off>\n"
        << "iv <hp|atk|def|spa|spd|spe> <0-31>\n"
        << "enemy-iv <stat> <0-31>\n"
        << "xp <0-99> | wins <n> | losses <n>\n"
        << "hp <n|max> | enemy-hp <n|max> | heal\n"
        << "win | lose | hub | menu\n"
        << "scene <texto> | scene hub | scene auto\n"
        << "find <texto>\n"
        << "Ctrl+Shift+C: mostrar/ocultar consola\n\n";
}

void devPrintStatus() {
    std::cout << "--- status ---\n";
    std::cout << "player: "
              << (gPlayer.species ? devNarrow(gPlayer.species->name) : "<none>")
              << " level=" << gPlayer.level << "\n";
    std::cout << "enemy: "
              << (gWild.species ? devNarrow(gWild.species->name) : "<none>")
              << " level=" << gWild.level << " shiny=" << (gWild.shiny ? "on" : "off") << "\n";
    std::cout << "xp=" << gPlayer.xp << " wins=" << gPlayer.wins
              << " losses=" << gPlayer.losses << "\n";
    std::cout << "stage: " << (gStageModel.empty() ? "<none>" : gStageModel) << "\n";
    std::cout << "hub: " << (gHubStageModel.empty() ? "<fallback>" : gHubStageModel) << "\n";
    std::cout << "decoded BCH: " << gDecodedBchCount << "\n";
}

void devFind(const std::string& token) {
    const std::string q = lower(token);
    int count = 0;
    for (const auto& path : gModelResources) {
        if (!q.empty() && lower(path).find(q) == std::string::npos) continue;
        std::cout << "  " << path << "\n";
        if (++count == 60) {
            std::cout << "  ... limite 60\n";
            break;
        }
    }
    if (!count) std::cout << "Sin coincidencias.\n";
}

void devExecuteCommand(const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    cmd = lower(cmd);
    if (cmd.empty()) return;

    if (cmd == "help" || cmd == "?") { devPrintHelp(); return; }
    if (cmd == "status") { devPrintStatus(); return; }
    if (cmd == "find") {
        std::string token; in >> token;
        if (token.empty()) std::cout << "Uso: find <texto>\n";
        else devFind(token);
        return;
    }

    if (cmd == "player" || cmd == "pokemon" || cmd == "enemy" || cmd == "opponent") {
        const bool player = cmd == "player" || cmd == "pokemon";
        std::string token, levelText; in >> token >> levelText;
        if (token.empty()) {
            std::cout << "Uso: " << cmd << " <pokemon/modelo> [nivel]\n";
            return;
        }
        std::optional<int> level;
        if (!levelText.empty()) {
            int n = 0;
            if (!devParseInt(levelText, n)) { std::cout << "Nivel invalido.\n"; return; }
            level = n;
        }
        devSetPokemon(player, token, level);
        return;
    }

    if (cmd == "level" || cmd == "enemy-level") {
        bool enemy = cmd == "enemy-level";
        std::string a, b; in >> a >> b;
        if (cmd == "level" && (lower(a) == "player" || lower(a) == "enemy")) {
            enemy = lower(a) == "enemy";
            a = b;
        }
        int n = 0;
        if (!devParseInt(a, n)) { std::cout << "Uso: level [player|enemy] <1-100>\n"; return; }
        n = std::clamp(n, 1, 100);
        if (enemy) {
            if (!gWild.species) generateWild();
            gWild.level = n;
        } else {
            devEnsurePlayer();
            gPlayer.level = n;
        }
        if (gScreen == Screen::Battle && gPlayer.species && gWild.species) beginBattle();
        else devRefresh();
        return;
    }

    if (cmd == "battle" || cmd == "hunt" || cmd == "encounter") {
        std::string token, levelText; in >> token >> levelText;
        devEnsurePlayer();
        if (!token.empty()) {
            std::optional<int> level;
            if (!levelText.empty()) {
                int n = 0;
                if (!devParseInt(levelText, n)) { std::cout << "Nivel invalido.\n"; return; }
                level = n;
            }
            if (!devSetPokemon(false, token, level, false)) return;
        } else if (!gWild.species) {
            generateWild();
        }
        if (cmd == "battle") beginBattle(); else showHunt();
        devApplyStage();
        return;
    }

    if (cmd == "random-wild") {
        generateWild();
        if (gScreen == Screen::Battle) beginBattle();
        else if (gScreen == Screen::Hunt) showHunt();
        std::cout << "enemy random = " << devNarrow(gWild.species->name)
                  << " Nv." << gWild.level << "\n";
        return;
    }

    if (cmd == "shiny") {
        std::string value; in >> value; value = lower(value);
        if (value != "on" && value != "off") { std::cout << "Uso: shiny <on|off>\n"; return; }
        if (!gWild.species) generateWild();
        gWild.shiny = value == "on";
        devRefresh();
        return;
    }

    if (cmd == "iv" || cmd == "enemy-iv") {
        const bool enemy = cmd == "enemy-iv";
        std::string statText, valueText; in >> statText >> valueText;
        const int index = devIvIndex(statText);
        int value = 0;
        if (index < 0 || !devParseInt(valueText, value)) {
            std::cout << "Uso: " << cmd << " <hp|atk|def|spa|spd|spe> <0-31>\n";
            return;
        }
        value = std::clamp(value, 0, 31);
        if (enemy) {
            if (!gWild.species) generateWild();
            gWild.iv[static_cast<std::size_t>(index)] = value;
        } else {
            devEnsurePlayer();
            gPlayer.iv[static_cast<std::size_t>(index)] = value;
        }
        if (gScreen == Screen::Battle && gPlayer.species && gWild.species) beginBattle();
        else devRefresh();
        return;
    }

    if (cmd == "xp" || cmd == "wins" || cmd == "losses") {
        std::string text; in >> text;
        int n = 0;
        if (!devParseInt(text, n)) { std::cout << "Valor invalido.\n"; return; }
        devEnsurePlayer();
        if (cmd == "xp") gPlayer.xp = std::clamp(n, 0, 99);
        else if (cmd == "wins") gPlayer.wins = std::max(0, n);
        else gPlayer.losses = std::max(0, n);
        devRefresh();
        return;
    }

    if (cmd == "hp" || cmd == "enemy-hp") {
        if (gScreen != Screen::Battle) { std::cout << "Requiere combate.\n"; return; }
        std::string text; in >> text;
        const bool enemy = cmd == "enemy-hp";
        int& hp = enemy ? gWildHp : gPlayerHp;
        const int maxHp = enemy ? gWildMaxHp : gPlayerMaxHp;
        if (lower(text) == "max") hp = maxHp;
        else {
            int n = 0;
            if (!devParseInt(text, n)) { std::cout << "Uso: " << cmd << " <n|max>\n"; return; }
            hp = std::clamp(n, 0, maxHp);
        }
        showBattleState();
        return;
    }

    if (cmd == "heal") {
        if (gScreen != Screen::Battle) { std::cout << "Requiere combate.\n"; return; }
        gPlayerHp = gPlayerMaxHp; gWildHp = gWildMaxHp; showBattleState(); return;
    }
    if (cmd == "win" || cmd == "lose") {
        if (gScreen != Screen::Battle) { std::cout << "Requiere combate.\n"; return; }
        if (cmd == "win") awardVictory(); else awardDefeat();
        showBattleState(); return;
    }
    if (cmd == "hub") {
        devEnsurePlayer(); gScreen = Screen::Hub; devApplyStage(); showHub(); return;
    }
    if (cmd == "menu") {
        gScreen = Screen::MainMenu; devApplyStage(); showMainMenu(); return;
    }

    if (cmd == "scene" || cmd == "stage") {
        std::string token; in >> token; token = lower(token);
        if (token.empty()) { std::cout << gStageModel << "\n"; return; }
        if (token == "auto") {
            gDevStageOverride.clear(); devApplyStage(); devRefresh();
            std::cout << "scene = auto\n"; return;
        }
        std::string path = token == "hub" ? gHubStageModel : devFindScene(token);
        if (path.empty()) { std::cout << "No encontre escena: " << token << "\n"; return; }
        gDevStageOverride = path;
        gStageModel = path;
        gRenderer.setStageModel(path);
        devRefresh();
        std::cout << "scene = " << path << "\n";
        return;
    }

    std::cout << "Comando desconocido. Escribe help.\n";
}

void devConsoleInputLoop() {
    std::string line;
    while (!gDevConsoleStop.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        auto command = std::make_unique<std::string>(line);
        HWND target = gWindow;
        if (!target || !IsWindow(target) ||
            !PostMessageW(target, WM_DEV_CONSOLE_COMMAND, 0,
                          reinterpret_cast<LPARAM>(command.get()))) break;
        command.release();
    }
}

void devCreateConsole() {
    if (gDevConsoleCreated) return;
    if (!AllocConsole() && GetLastError() != ERROR_ACCESS_DENIED) return;

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio(true);
    std::cin.clear();
    std::cout.clear();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Promptemon Steidium - Consola de pruebas");

    HWND console = GetConsoleWindow();
    if (console) {
        if (HMENU menu = GetSystemMenu(console, FALSE)) DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
        ShowWindow(console, SW_HIDE);
    }

    gDevConsoleCreated = true;
    gDevConsoleStop.store(false);
    std::cout << "Promptemon Steidium - consola de pruebas\n";
    devPrintHelp();
    std::thread(devConsoleInputLoop).detach();
}

void devToggleConsole() {
    if (!gDevConsoleCreated) devCreateConsole();
    HWND console = GetConsoleWindow();
    if (!console) return;
    if (IsWindowVisible(console)) ShowWindow(console, SW_HIDE);
    else {
        ShowWindow(console, SW_SHOW);
        SetForegroundWindow(console);
    }
}

LRESULT CALLBACK BootstrapWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && static_cast<int>(wp) == ID_DEV_CONSOLE_HOTKEY) {
        devToggleConsole();
        return 0;
    }
    if (msg == WM_DEV_CONSOLE_COMMAND) {
        std::unique_ptr<std::string> command(reinterpret_cast<std::string*>(lp));
        if (command) devExecuteCommand(*command);
        return 0;
    }
    if (msg == WM_DESTROY) {
        UnregisterHotKey(hwnd, ID_DEV_CONSOLE_HOTKEY);
        gDevConsoleStop.store(true);
    }

    const LRESULT result = WndProc(hwnd, msg, wp, lp);
    if (msg == WM_CREATE && result != -1) {
        gDefaultStageModel = gStageModel;
        gHubStageModel = devPreferredHubScene();
        if (gHubStageModel.empty()) gHubStageModel = gDefaultStageModel;
        RegisterHotKey(hwnd, ID_DEV_CONSOLE_HOTKEY,
                       MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'C');
    } else if (msg == WM_COMMAND) {
        devApplyStage();
    }
    return result;
}

bool registerGameWindowClass(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = BootstrapWndProc;
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
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT ole = OleInitialize(nullptr);
    const bool uninitializeOle = SUCCEEDED(ole);

    promptemon::storage::LocalDatabase database;
    if (!database.open()) {
        MessageBoxW(nullptr, L"No se pudo abrir player.db junto al ejecutable.",
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
        if (!prepareAssetCacheVisible(database, error)) {
            MessageBoxW(nullptr, error.c_str(), L"No se pudieron extraer los recursos", MB_OK | MB_ICONERROR);
            candidate.reset();
            continue;
        }

        HWND window = createGameWindow(instance, showCommand);
        if (!window) {
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
