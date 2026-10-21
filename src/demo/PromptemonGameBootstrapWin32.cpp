#include "demo/ExtractionProgressWindow.h"
#include "rom/Lzh8.h"

// Reuse the existing game implementation in the same translation unit, but
// replace only its startup routine. This keeps gameplay/rendering untouched
// while the ROM bootstrap becomes deterministic and self-contained.
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
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), needed);
    return out;
}

std::string devNarrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0,
                                            nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::string devNarrow(const wchar_t* value) {
    return value ? devNarrow(std::wstring(value)) : std::string{};
}

bool devParseInt(const std::string& text, int& value) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(text, &used, 10);
        if (used != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::string devLeafName(const std::string& virtualPath) {
    std::string value = virtualPath;
    const auto marker = value.rfind(" :: ");
    if (marker != std::string::npos) value = value.substr(marker + 4);
    const auto slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value = value.substr(slash + 1);
    std::string lowered = lower(value);
    if (devEndsWith(lowered, ".cx")) {
        value.resize(value.size() - 3);
        lowered.resize(lowered.size() - 3);
    }
    if (devEndsWith(lowered, ".bch")) value.resize(value.size() - 4);
    return value;
}

bool devNormalizeCachedBchResources(const RomAssetCache::ProgressCallback& progress) {
    const std::filesystem::path manifestPath = gAssets.root() / L"manifest.tsv";
    std::ifstream manifest(manifestPath, std::ios::binary);
    if (!manifest) return false;

    std::unordered_map<std::string, std::filesystem::path> localFiles;
    std::string line;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        localFiles[line.substr(0, tab)] = std::filesystem::u8path(line.substr(tab + 1));
    }

    const auto resources = gAssets.resources();
    gDecodedBchCount = 0;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        const std::string& path = resources[i];
        const std::string p = lower(path);
        if (!(devEndsWith(p, ".bch") || devEndsWith(p, ".bch.cx"))) continue;
        if (progress) progress(i, resources.size(), "Preparando BCH: " + path);

        try {
            auto bytes = gAssets.extractResource(path);
            if (!promptemon::rom::Lzh8::isCompressed(bytes)) continue;
            auto decoded = promptemon::rom::Lzh8::decompress(bytes);
            if (decoded.size() < 4 || decoded[0] != 'B' || decoded[1] != 'C' ||
                decoded[2] != 'H' || decoded[3] != 0) {
                continue;
            }

            const auto found = localFiles.find(path);
            if (found == localFiles.end()) continue;
            const std::filesystem::path target = gAssets.root() / found->second;
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            if (!output) continue;
            output.write(reinterpret_cast<const char*>(decoded.data()),
                         static_cast<std::streamsize>(decoded.size()));
            if (output) ++gDecodedBchCount;
        } catch (...) {
            // One unsupported compressed BCH should not invalidate the ROM.
        }
    }
    if (progress) progress(resources.size(), resources.size(), std::string{});
    return true;
}

bool prepareAssetCacheVisible(promptemon::storage::LocalDatabase& database,
                              std::wstring& error) {
    promptemon::demo::ExtractionProgressWindow progressWindow;
    bool progressVisible = false;

    try {
        const std::filesystem::path base = database.path().parent_path() / L"assets";
        const RomAssetCache::ProgressCallback progress = [&](std::size_t current,
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

        // The ROM stores many BCH files as LZH8/LZ11 .cx streams. Keep the
        // readable cache layout but normalize those BCH payloads in place so
        // the renderer, animations and developer console can consume them.
        devNormalizeCachedBchResources(progress);

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

bool devSceneUsable(const std::string& path) {
    if (path.empty()) return false;
    try {
        const auto bytes = gAssets.extractBch(path);
        const auto scene = promptemon::bch::Scene::parse(bytes);
        return !scene.meshes().empty();
    } catch (...) {
        return false;
    }
}

std::string devPreferredHubScene() {
    std::vector<std::pair<int, std::string>> candidates;
    candidates.reserve(gModelResources.size());

    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (contains(p, "pii/") || contains(p, "mii/") || contains(p, "/ui/")) continue;

        int score = stageScore(path);
        if (contains(p, "event/makuai/3d.bch")) score += 12000;
        if (contains(p, "op_toykingdom_01.bch")) score += 11000;
        if (contains(p, "kingscourt_01.bch")) score += 10000;
        if (contains(p, "makuai_idou.bch")) score += 9000;
        if (contains(p, "makuai_08_flowergarden_bg.bch")) score += 8500;
        if (contains(p, "makuai_10_castlefront_bg.bch")) score += 8000;
        if (score > 0) candidates.push_back({score, path});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& candidate : candidates) {
        if (devSceneUsable(candidate.second)) return candidate.second;
    }
    return {};
}

void devApplyStageForScreen() {
    std::string desired;
    if (!gDevStageOverride.empty()) desired = gDevStageOverride;
    else if (gScreen == Screen::Hub && !gHubStageModel.empty()) desired = gHubStageModel;
    else desired = gDefaultStageModel;

    if (!desired.empty() && desired != gStageModel) {
        gStageModel = desired;
        gRenderer.setStageModel(gStageModel);
    }
}

const Species* devKnownSpecies(const std::string& token) {
    const std::string q = lower(token);
    int numeric = -1;
    const bool number = devParseInt(q, numeric);

    for (const auto& species : kSpecies) {
        if (number && numeric == species.dex) return &species;
        if (lower(devNarrow(species.name)) == q) return &species;
        const char* keys[] = {species.modelKey1, species.modelKey2, species.modelKey3};
        for (const char* key : keys) {
            if (key && *key && lower(key) == q) return &species;
        }
    }
    return nullptr;
}

std::string devFindPokemonModel(const std::string& token) {
    const std::string q = lower(token);
    int bestScore = -1000000;
    std::string best;

    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (!contains(p, "pii/")) continue;
        if (p.find(q) == std::string::npos) continue;

        int score = 100;
        const std::string leaf = lower(devLeafName(path));
        if (leaf == q) score += 1000;
        if (leaf.rfind(q, 0) == 0) score += 500;
        if (contains(leaf, "_normal")) score += 150;
        if (!contains(q, "mega") && contains(leaf, "mega")) score -= 300;
        if (!contains(q, "shiny") && contains(leaf, "shiny")) score -= 200;
        if (score > bestScore && devSceneUsable(path)) {
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
        if (!q.empty() && p.find(q) == std::string::npos) continue;
        int score = stageScore(path) + 100;
        if (contains(p, "event/makuai/")) score += 120;
        if (score > bestScore && devSceneUsable(path)) {
            bestScore = score;
            best = path;
        }
    }
    return best;
}

void devEnsurePlayer() {
    if (gPlayer.species) return;
    gPlayer = {};
    gPlayer.species = &kSpecies[0];
    gPlayer.level = 5;
    gPlayer.iv = randomIvs();
    gHasStarter = true;
}

void devRefreshVisible() {
    devApplyStageForScreen();
    switch (gScreen) {
        case Screen::Hub:
            if (gPlayer.species) showHub();
            break;
        case Screen::Hunt:
            if (gWild.species) showHunt();
            break;
        case Screen::Battle:
            if (gPlayer.species && gWild.species) showBattleState();
            break;
        default:
            break;
    }
}

bool devSetPlayer(const std::string& token, std::optional<int> requestedLevel, bool refresh = true) {
    devEnsurePlayer();
    const Species* species = devKnownSpecies(token);

    if (!species) {
        const std::string model = devFindPokemonModel(token);
        if (model.empty()) {
            std::cout << "No encontre un modelo Pokemon para: " << token << "\n";
            return false;
        }
        gResolvedModels.erase(&gDebugPlayerSpecies);
        gDebugPlayerSpecies = kSpecies[0];
        gDebugPlayerName = devWide(devLeafName(model));
        gDebugPlayerSpecies.name = gDebugPlayerName.c_str();
        gResolvedModels[&gDebugPlayerSpecies] = model;
        species = &gDebugPlayerSpecies;
    }

    gPlayer.species = species;
    if (requestedLevel) gPlayer.level = std::clamp(*requestedLevel, 1, 100);
    else if (gPlayer.level < 1) gPlayer.level = 5;
    gHasStarter = true;

    std::cout << "Jugador: " << devNarrow(gPlayer.species->name)
              << " Nv." << gPlayer.level << "\n";
    if (refresh) {
        if (gScreen == Screen::Battle && gWild.species) beginBattle();
        else devRefreshVisible();
    }
    return true;
}

bool devSetEnemy(const std::string& token, std::optional<int> requestedLevel, bool refresh = true) {
    const Species* species = devKnownSpecies(token);
    if (!species) {
        const std::string model = devFindPokemonModel(token);
        if (model.empty()) {
            std::cout << "No encontre un modelo Pokemon para: " << token << "\n";
            return false;
        }
        gResolvedModels.erase(&gDebugWildSpecies);
        gDebugWildSpecies = kSpecies[1];
        gDebugWildName = devWide(devLeafName(model));
        gDebugWildSpecies.name = gDebugWildName.c_str();
        gResolvedModels[&gDebugWildSpecies] = model;
        species = &gDebugWildSpecies;
    }

    gWild.species = species;
    if (requestedLevel) gWild.level = std::clamp(*requestedLevel, 1, 100);
    else if (gWild.level < 1) gWild.level = 5;
    gWild.iv = randomIvs();

    std::cout << "Rival: " << devNarrow(gWild.species->name)
              << " Nv." << gWild.level << (gWild.shiny ? " SHINY" : "") << "\n";
    if (refresh) {
        if (gScreen == Screen::Battle && gPlayer.species) beginBattle();
        else devRefreshVisible();
    }
    return true;
}

int devIvIndex(const std::string& token) {
    const std::string q = lower(token);
    if (q == "hp" || q == "ps") return 0;
    if (q == "atk" || q == "attack" || q == "atq") return 1;
    if (q == "def" || q == "defense") return 2;
    if (q == "spa" || q == "spatk" || q == "special-attack") return 3;
    if (q == "spd" || q == "spdef" || q == "special-defense") return 4;
    if (q == "spe" || q == "speed" || q == "vel") return 5;
    return -1;
}

void devPrintStatus() {
    std::cout << "--- ESTADO ---\n";
    if (gPlayer.species) {
        std::cout << "player: " << devNarrow(gPlayer.species->name)
                  << " level=" << gPlayer.level << " xp=" << gPlayer.xp
                  << " wins=" << gPlayer.wins << " losses=" << gPlayer.losses << "\n";
    } else {
        std::cout << "player: <sin seleccionar>\n";
    }
    if (gWild.species) {
        std::cout << "enemy: " << devNarrow(gWild.species->name)
                  << " level=" << gWild.level << " shiny=" << (gWild.shiny ? "on" : "off") << "\n";
    } else {
        std::cout << "enemy: <sin seleccionar>\n";
    }
    std::cout << "stage: " << (gStageModel.empty() ? "<ninguno>" : gStageModel) << "\n";
    std::cout << "hub scene: " << (gHubStageModel.empty() ? "<fallback>" : gHubStageModel) << "\n";
    std::cout << "BCH descomprimidos en cache: " << gDecodedBchCount << "\n";
}

void devPrintHelp() {
    std::cout
        << "\nCOMANDOS DE PRUEBA\n"
        << "  help                              muestra esta ayuda\n"
        << "  status                            estado actual\n"
        << "  player <pokemon/modelo> [nivel]   cambia tu Pokemon\n"
        << "  enemy <pokemon/modelo> [nivel]    fija el rival\n"
        << "  level [player|enemy] <1-100>      cambia nivel\n"
        << "  battle [pokemon/modelo] [nivel]   entra directo a combate\n"
        << "  hunt [pokemon/modelo] [nivel]     abre encuentro de caza\n"
        << "  random-wild                       genera rival aleatorio\n"
        << "  shiny <on|off>                    fuerza shiny del rival\n"
        << "  iv <stat> <0-31>                  IV del jugador\n"
        << "  enemy-iv <stat> <0-31>            IV del rival\n"
        << "  xp <0-99>                         XP del jugador\n"
        << "  wins <n> | losses <n>             contadores del jugador\n"
        << "  hp <n|max> | enemy-hp <n|max>     PS durante combate\n"
        << "  heal                              cura ambos en combate\n"
        << "  win | lose                        fuerza resultado del combate\n"
        << "  hub | menu                        cambia de pantalla\n"
        << "  scene <texto>                     usa una escena BCH que coincida\n"
        << "  scene hub                         escena real elegida para el Hub\n"
        << "  scene auto                        vuelve a seleccion automatica\n"
        << "  find <texto>                      lista modelos BCH coincidentes\n"
        << "\nCtrl+Shift+C muestra/oculta esta consola.\n\n";
}

void devFindResources(const std::string& token) {
    const std::string q = lower(token);
    int shown = 0;
    for (const auto& path : gModelResources) {
        if (!q.empty() && lower(path).find(q) == std::string::npos) continue;
        std::cout << "  " << path << "\n";
        if (++shown >= 60) {
            std::cout << "  ... limite de 60 resultados\n";
            break;
        }
    }
    if (!shown) std::cout << "Sin coincidencias para: " << token << "\n";
}

void devExecuteCommand(const std::string& rawLine) {
    std::istringstream input(rawLine);
    std::string command;
    input >> command;
    command = lower(command);
    if (command.empty()) return;

    if (command == "help" || command == "?") {
        devPrintHelp();
        return;
    }
    if (command == "status") {
        devPrintStatus();
        return;
    }
    if (command == "find") {
        std::string token;
        input >> token;
        if (token.empty()) std::cout << "Uso: find <texto>\n";
        else devFindResources(token);
        return;
    }
    if (command == "player" || command == "pokemon") {
        std::string token, levelText;
        input >> token >> levelText;
        if (token.empty()) {
            std::cout << "Uso: player <pokemon/modelo> [nivel]\n";
            return;
        }
        std::optional<int> level;
        if (!levelText.empty()) {
            int parsed = 0;
            if (!devParseInt(levelText, parsed)) {
                std::cout << "Nivel invalido.\n";
                return;
            }
            level = parsed;
        }
        devSetPlayer(token, level);
        return;
    }
    if (command == "enemy" || command == "opponent") {
        std::string token, levelText;
        input >> token >> levelText;
        if (token.empty()) {
            std::cout << "Uso: enemy <pokemon/modelo> [nivel]\n";
            return;
        }
        std::optional<int> level;
        if (!levelText.empty()) {
            int parsed = 0;
            if (!devParseInt(levelText, parsed)) {
                std::cout << "Nivel invalido.\n";
                return;
            }
            level = parsed;
        }
        devSetEnemy(token, level);
        return;
    }
    if (command == "level" || command == "enemy-level") {
        bool enemy = command == "enemy-level";
        std::string first, second;
        input >> first >> second;
        if (command == "level" && (lower(first) == "player" || lower(first) == "enemy")) {
            enemy = lower(first) == "enemy";
            first = second;
        }
        int level = 0;
        if (!devParseInt(first, level)) {
            std::cout << "Uso: level [player|enemy] <1-100>\n";
            return;
        }
        level = std::clamp(level, 1, 100);
        if (enemy) {
            if (!gWild.species) generateWild();
            gWild.level = level;
            std::cout << "Nivel rival = " << level << "\n";
        } else {
            devEnsurePlayer();
            gPlayer.level = level;
            std::cout << "Nivel jugador = " << level << "\n";
        }
        if (gScreen == Screen::Battle && gPlayer.species && gWild.species) beginBattle();
        else devRefreshVisible();
        return;
    }
    if (command == "battle" || command == "hunt" || command == "encounter") {
        std::string token, levelText;
        input >> token >> levelText;
        devEnsurePlayer();
        if (!token.empty()) {
            std::optional<int> level;
            if (!levelText.empty()) {
                int parsed = 0;
                if (!devParseInt(levelText, parsed)) {
                    std::cout << "Nivel invalido.\n";
                    return;
                }
                level = parsed;
            }
            if (!devSetEnemy(token, level, false)) return;
        } else if (!gWild.species) {
            generateWild();
        }
        if (command == "battle") beginBattle();
        else showHunt();
        devApplyStageForScreen();
        return;
    }
    if (command == "random-wild") {
        generateWild();
        std::cout << "Rival aleatorio: " << devNarrow(gWild.species->name)
                  << " Nv." << gWild.level << "\n";
        if (gScreen == Screen::Battle) beginBattle();
        else if (gScreen == Screen::Hunt) showHunt();
        return;
    }
    if (command == "shiny") {
        std::string value;
        input >> value;
        value = lower(value);
        if (value != "on" && value != "off") {
            std::cout << "Uso: shiny <on|off>\n";
            return;
        }
        if (!gWild.species) generateWild();
        gWild.shiny = value == "on";
        std::cout << "Shiny rival = " << (gWild.shiny ? "on" : "off") << "\n";
        devRefreshVisible();
        return;
    }
    if (command == "iv" || command == "enemy-iv") {
        const bool enemy = command == "enemy-iv";
        std::string statText, valueText;
        input >> statText >> valueText;
        const int index = devIvIndex(statText);
        int value = 0;
        if (index < 0 || !devParseInt(valueText, value)) {
            std::cout << "Uso: " << command << " <hp|atk|def|spa|spd|spe> <0-31>\n";
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
        std::cout << "IV actualizado.\n";
        if (gScreen == Screen::Battle && gPlayer.species && gWild.species) beginBattle();
        else devRefreshVisible();
        return;
    }
    if (command == "xp" || command == "wins" || command == "losses") {
        std::string valueText;
        input >> valueText;
        int value = 0;
        if (!devParseInt(valueText, value)) {
            std::cout << "Valor invalido.\n";
            return;
        }
        devEnsurePlayer();
        if (command == "xp") gPlayer.xp = std::clamp(value, 0, 99);
        else if (command == "wins") gPlayer.wins = std::max(0, value);
        else gPlayer.losses = std::max(0, value);
        devRefreshVisible();
        return;
    }
    if (command == "hp" || command == "enemy-hp") {
        if (gScreen != Screen::Battle) {
            std::cout << "Este comando requiere estar en combate.\n";
            return;
        }
        std::string valueText;
        input >> valueText;
        const bool enemy = command == "enemy-hp";
        int& hp = enemy ? gWildHp : gPlayerHp;
        const int maxHp = enemy ? gWildMaxHp : gPlayerMaxHp;
        if (lower(valueText) == "max") hp = maxHp;
        else {
            int value = 0;
            if (!devParseInt(valueText, value)) {
                std::cout << "Uso: " << command << " <n|max>\n";
                return;
            }
            hp = std::clamp(value, 0, maxHp);
        }
        showBattleState();
        return;
    }
    if (command == "heal") {
        if (gScreen != Screen::Battle) {
            std::cout << "Este comando requiere estar en combate.\n";
            return;
        }
        gPlayerHp = gPlayerMaxHp;
        gWildHp = gWildMaxHp;
        showBattleState();
        return;
    }
    if (command == "win" || command == "lose") {
        if (gScreen != Screen::Battle) {
            std::cout << "Este comando requiere estar en combate.\n";
            return;
        }
        if (command == "win") awardVictory();
        else awardDefeat();
        showBattleState();
        return;
    }
    if (command == "hub") {
        devEnsurePlayer();
        gScreen = Screen::Hub;
        devApplyStageForScreen();
        showHub();
        return;
    }
    if (command == "menu") {
        gScreen = Screen::MainMenu;
        devApplyStageForScreen();
        showMainMenu();
        return;
    }
    if (command == "scene" || command == "stage") {
        std::string token;
        input >> token;
        token = lower(token);
        if (token.empty()) {
            std::cout << "scene actual: " << gStageModel << "\n";
            return;
        }
        if (token == "auto") {
            gDevStageOverride.clear();
            devApplyStageForScreen();
            std::cout << "Seleccion automatica de escena restaurada.\n";
            return;
        }
        std::string scene;
        if (token == "hub") scene = gHubStageModel;
        else scene = devFindScene(token);
        if (scene.empty()) {
            std::cout << "No encontre una escena BCH para: " << token << "\n";
            return;
        }
        gDevStageOverride = scene;
        gStageModel = scene;
        gRenderer.setStageModel(scene);
        std::cout << "Escena: " << scene << "\n";
        devRefreshVisible();
        return;
    }

    std::cout << "Comando desconocido: " << command << ". Escribe help.\n";
}

void devConsoleInputLoop() {
    std::string line;
    while (!gDevConsoleStop.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (gDevConsoleStop.load()) break;
        auto command = std::make_unique<std::string>(line);
        HWND target = gWindow;
        if (!target || !IsWindow(target) ||
            !PostMessageW(target, WM_DEV_CONSOLE_COMMAND, 0,
                          reinterpret_cast<LPARAM>(command.get()))) {
            break;
        }
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

    if (HWND console = GetConsoleWindow()) {
        if (HMENU menu = GetSystemMenu(console, FALSE)) {
            DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
        }
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
    if (IsWindowVisible(console)) {
        ShowWindow(console, SW_HIDE);
    } else {
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
        devApplyStageForScreen();
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
