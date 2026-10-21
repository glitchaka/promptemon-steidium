#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "demo/PrototypeRomRenderer.h"
#include "rom/RomModelArchive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using promptemon::demo::GameAction;
using promptemon::demo::PrototypeRomRenderer;
using promptemon::rom::RomModelArchive;

enum class Screen { MainMenu, Options, Intro, StarterSelect, Hub, Hunt, Battle };
enum class Stat { HP, Attack, Defense, SpAttack, SpDefense, Speed };
enum class PendingAction { None, PlayerCounter, WildCounter };

struct Species {
    const wchar_t* name;
    int dex;
    int baseHp;
    int baseAttack;
    int baseDefense;
    int baseSpeed;
    Stat ivReward;
    int encounterWeight;
    const wchar_t* move1;
    int move1Power;
    const wchar_t* move2;
    int move2Power;
    const char* modelKey1;
    const char* modelKey2;
    const char* modelKey3;
};

struct OwnedPokemon {
    const Species* species = nullptr;
    int level = 5;
    int xp = 0;
    std::array<int, 6> iv{};
    int wins = 0;
    int losses = 0;
};

struct WildPokemon {
    const Species* species = nullptr;
    int level = 4;
    std::array<int, 6> iv{};
    bool shiny = false;
};

const Species kSpecies[] = {
    {L"Rattata", 19, 30,56,35,72, Stat::Speed,   300, L"Placaje",40, L"Ataque rapido",40, "koratta", "rattata", nullptr},
    {L"Pidgey",  16, 40,45,40,56, Stat::Speed,   250, L"Placaje",40, L"Tornado",45, "poppo", "pidgey", nullptr},
    {L"Caterpie",10, 45,30,35,45, Stat::HP,      180, L"Placaje",40, L"Picadura",50, "kyatapi", "caterpie", nullptr},
    {L"Weedle",  13, 40,35,30,50, Stat::Speed,   140, L"Picotazo veneno",40, L"Picadura",50, "biidoru", "weedle", nullptr},
    {L"Zubat",   41, 40,45,35,55, Stat::Speed,    80, L"Impresionar",30, L"Mordisco",60, "zubatto", "zubat", nullptr},
    {L"Geodude", 74, 40,80,100,20,Stat::Defense,  35, L"Placaje",40, L"Lanzarrocas",50, "ishitsubute", "isitubute", "geodude"},
    {L"Gastly",  92, 30,35,30,80, Stat::SpAttack, 10, L"Lenguetazo",30, L"Tinieblas",50, "goosu", "ghos", "gastly"},
    {L"Machop",  66, 70,80,50,35, Stat::Attack,    5, L"Golpe karate",50, L"Patada baja",50, "wanrikii", "machop", nullptr}
};

constexpr UINT_PTR ID_RENDER_TIMER = 1;

HWND gWindow = nullptr;
Screen gScreen = Screen::MainMenu;
OwnedPokemon gPlayer;
WildPokemon gWild;
bool gHasStarter = false;
bool gBattleOver = false;
bool gBattleWon = false;
int gPlayerHp = 0;
int gPlayerMaxHp = 0;
int gWildHp = 0;
int gWildMaxHp = 0;
std::wstring gBattleLog;
std::mt19937 gRng{std::random_device{}()};
int gVolume = 70;
bool gFullscreen = false;
WINDOWPLACEMENT gWindowPlacement{sizeof(WINDOWPLACEMENT)};
DWORD gWindowedStyle = 0;

PendingAction gPendingAction = PendingAction::None;
ULONGLONG gPendingAt = 0;
int gPendingPlayerPower = 0;
std::wstring gPendingPlayerMove;

RomModelArchive gArchive;
std::vector<std::string> gModelResources;
std::unordered_map<const Species*, std::string> gResolvedModels;
std::string gStageModel;
std::string gEffectModel;
PrototypeRomRenderer gRenderer;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

const wchar_t* statName(Stat stat) {
    switch (stat) {
        case Stat::HP: return L"PS";
        case Stat::Attack: return L"Ataque";
        case Stat::Defense: return L"Defensa";
        case Stat::SpAttack: return L"Ataque especial";
        case Stat::SpDefense: return L"Defensa especial";
        case Stat::Speed: return L"Velocidad";
    }
    return L"?";
}

int statIndex(Stat stat) { return static_cast<int>(stat); }
int randomInt(int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(gRng); }
std::array<int,6> randomIvs() { std::array<int,6> v{}; for (auto& x : v) x = randomInt(0,31); return v; }
int calculatedHp(const Species& s, int level, int iv) { return (((2*s.baseHp+iv)*level)/100)+level+10; }
int calculatedStat(int base, int level, int iv) { return (((2*base+iv)*level)/100)+5; }
int currentAttack(const Species& s, int l, const std::array<int,6>& iv) { return calculatedStat(s.baseAttack,l,iv[1]); }
int currentDefense(const Species& s, int l, const std::array<int,6>& iv) { return calculatedStat(s.baseDefense,l,iv[2]); }
int currentSpeed(const Species& s, int l, const std::array<int,6>& iv) { return calculatedStat(s.baseSpeed,l,iv[5]); }

int damageFor(const Species& a, int al, const std::array<int,6>& aiv,
              const Species& d, int dl, const std::array<int,6>& div, int power) {
    const int attack = std::max(1, currentAttack(a, al, aiv));
    const int defense = std::max(1, currentDefense(d, dl, div));
    const int base = (((2*al/5+2)*power*attack/defense)/50)+2;
    return std::max(1, base * randomInt(85,100) / 100);
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) return {};
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path findRomsFrom(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) return {};
    for (;;) {
        const auto candidate = start / L"roms";
        if (std::filesystem::is_directory(candidate, ec)) return candidate;
        ec.clear();
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

std::filesystem::path romDirectory() {
    if (auto p = findRomsFrom(std::filesystem::current_path()); !p.empty()) return p;
    if (auto p = findRomsFrom(executableDirectory()); !p.empty()) return p;
    return {};
}

const std::string& modelFor(const Species& species) {
    auto existing = gResolvedModels.find(&species);
    if (existing != gResolvedModels.end()) return existing->second;

    const char* keys[] = {species.modelKey1, species.modelKey2, species.modelKey3};
    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        if (p.find("pii/") == std::string::npos) continue;
        for (const char* key : keys) {
            if (key && *key && p.find(key) != std::string::npos) {
                return gResolvedModels.emplace(&species, path).first->second;
            }
        }
    }
    for (const auto& path : gModelResources) {
        const std::string p = lower(path);
        for (const char* key : keys) {
            if (key && *key && p.find(key) != std::string::npos) {
                return gResolvedModels.emplace(&species, path).first->second;
            }
        }
    }
    static const std::string empty;
    return empty;
}

int stageScore(const std::string& path) {
    const std::string p = lower(path);
    if (contains(p,"pii/") || contains(p,"mii/") || contains(p,"/ui") ||
        contains(p,"icon") || contains(p,"font") || contains(p,"menu") ||
        contains(p,"effect") || contains(p,"/eff")) return -10000;

    int score = 0;
    if (contains(p,".arc.xc::") || contains(p,".arc.cx::") || contains(p,".darc::")) score += 80;
    if (contains(p,"n001")) score += 220;
    if (contains(p,"stage")) score += 160;
    if (contains(p,"stg")) score += 130;
    if (contains(p,"field")) score += 120;
    if (contains(p,"fld")) score += 110;
    if (contains(p,"course")) score += 100;
    if (contains(p,"map")) score += 90;
    if (contains(p,"area")) score += 80;
    if (contains(p,"grass") || contains(p,"forest") || contains(p,"plain") || contains(p,"meadow")) score += 70;
    if (contains(p,"ground") || contains(p,"land") || contains(p,"floor")) score += 55;
    if (contains(p,"town") || contains(p,"castle")) score += 45;
    if (contains(p,"bg")) score += 25;
    return score;
}

int effectScore(const std::string& path) {
    const std::string p = lower(path);
    if (contains(p,"pii/") || contains(p,"mii/")) return -10000;
    int score = 0;
    if (contains(p,".arc.xc::") || contains(p,".arc.cx::") || contains(p,".darc::")) score += 40;
    if (contains(p,"effect")) score += 180;
    if (contains(p,"/eff") || contains(p,"eff_")) score += 150;
    if (contains(p,"hit") || contains(p,"impact")) score += 120;
    if (contains(p,"spark") || contains(p,"flash")) score += 100;
    if (contains(p,"smoke") || contains(p,"dust")) score += 70;
    if (contains(p,"waza") || contains(p,"attack")) score += 60;
    return score;
}

std::string bestResource(int (*scoreFn)(const std::string&)) {
    int best = 0;
    std::string result;
    for (const auto& path : gModelResources) {
        const int score = scoreFn(path);
        if (score > best) {
            best = score;
            result = path;
        }
    }
    return result;
}

std::wstring ivSummary() {
    std::wstringstream s;
    s << L"IV  PS " << gPlayer.iv[0]
      << L"  ATQ " << gPlayer.iv[1]
      << L"  DEF " << gPlayer.iv[2]
      << L"  ATQ.ESP " << gPlayer.iv[3]
      << L"  DEF.ESP " << gPlayer.iv[4]
      << L"  VEL " << gPlayer.iv[5];
    return s.str();
}

void showMainMenu() {
    gScreen = Screen::MainMenu;
    gRenderer.showMainMenu();
}

void showOptions() {
    gScreen = Screen::Options;
    gRenderer.showOptions(gVolume, gFullscreen);
}

void showIntro() {
    gScreen = Screen::Intro;
    gRenderer.showIntro();
}

void showStarterSelect() {
    gScreen = Screen::StarterSelect;
    gRenderer.showStarterSelect(modelFor(kSpecies[0]), modelFor(kSpecies[1]), modelFor(kSpecies[2]));
}

void showHub() {
    gScreen = Screen::Hub;
    gPendingAction = PendingAction::None;
    gRenderer.showHub(modelFor(*gPlayer.species), gPlayer.species->name, gPlayer.level,
                      gPlayer.xp, gPlayer.wins, gPlayer.losses, ivSummary());
}

void chooseStarter(const Species& species) {
    gPlayer = {};
    gPlayer.species = &species;
    gPlayer.level = 5;
    gPlayer.iv = randomIvs();
    gHasStarter = true;
    showHub();
}

void generateWild() {
    int total = 0;
    for (const auto& s : kSpecies) total += s.encounterWeight;
    int roll = randomInt(1, total);
    gWild.species = &kSpecies[0];
    for (const auto& s : kSpecies) {
        roll -= s.encounterWeight;
        if (roll <= 0) { gWild.species = &s; break; }
    }
    gWild.level = randomInt(3,6);
    gWild.iv = randomIvs();
    gWild.shiny = randomInt(1,4096) == 1;
}

void showHunt() {
    gScreen = Screen::Hunt;
    gRenderer.showHunt(modelFor(*gWild.species), gWild.species->name, gWild.level,
                       statName(gWild.species->ivReward), gWild.shiny);
}

void enterHunt() {
    generateWild();
    showHunt();
}

void showBattleState() {
    gScreen = Screen::Battle;
    gRenderer.showBattle(modelFor(*gPlayer.species), modelFor(*gWild.species),
                         gPlayer.species->name, gPlayer.level, gPlayerHp, gPlayerMaxHp,
                         gWild.species->name, gWild.level, gWildHp, gWildMaxHp,
                         statName(gWild.species->ivReward), gWild.shiny,
                         gPlayer.species->move1, gPlayer.species->move2,
                         gBattleLog, gBattleOver, gBattleWon);
}

void beginBattle() {
    gBattleOver = false;
    gBattleWon = false;
    gPendingAction = PendingAction::None;
    gPlayerMaxHp = calculatedHp(*gPlayer.species, gPlayer.level, gPlayer.iv[0]);
    gPlayerHp = gPlayerMaxHp;
    gWildMaxHp = calculatedHp(*gWild.species, gWild.level, gWild.iv[0]);
    gWildHp = gWildMaxHp;
    gBattleLog = L"El combate ha comenzado.";
    showBattleState();
}

void awardVictory() {
    gBattleOver = true;
    gBattleWon = true;
    ++gPlayer.wins;
    const int idx = statIndex(gWild.species->ivReward);
    const int before = gPlayer.iv[idx];
    if (gPlayer.iv[idx] < 31) ++gPlayer.iv[idx];
    gPlayer.xp += 25;
    bool levelUp = false;
    if (gPlayer.xp >= 100) {
        gPlayer.xp -= 100;
        ++gPlayer.level;
        levelUp = true;
    }

    std::wstringstream s;
    s << L"Victoria. ";
    if (before < 31) s << L"IV de " << statName(gWild.species->ivReward) << L" " << before << L" -> " << gPlayer.iv[idx] << L". ";
    s << L"+25 XP.";
    if (levelUp) s << L" Subiste de nivel.";
    gBattleLog = s.str();
}

void awardDefeat() {
    gBattleOver = true;
    gBattleWon = false;
    ++gPlayer.losses;
    gBattleLog = L"Tu Pokemon se ha debilitado.";
}

std::pair<int,const wchar_t*> chooseWildMove() {
    const bool second = randomInt(0,1) == 1;
    return second
        ? std::make_pair(gWild.species->move2Power, gWild.species->move2)
        : std::make_pair(gWild.species->move1Power, gWild.species->move1);
}

void playerAttack(int power, const wchar_t* move) {
    const int damage = damageFor(*gPlayer.species, gPlayer.level, gPlayer.iv,
                                 *gWild.species, gWild.level, gWild.iv, power);
    gWildHp = std::max(0, gWildHp - damage);
    std::wstringstream s;
    s << gPlayer.species->name << L" usa " << move << L". " << damage << L" de dano.";
    gBattleLog = s.str();
    if (gWildHp <= 0) awardVictory();
    showBattleState();
    gRenderer.playPlayerAction();
    if (gWildHp <= 0) gRenderer.playWildFaint(); else gRenderer.playWildHit();
}

void wildAttack() {
    const auto move = chooseWildMove();
    const int damage = damageFor(*gWild.species, gWild.level, gWild.iv,
                                 *gPlayer.species, gPlayer.level, gPlayer.iv, move.first);
    gPlayerHp = std::max(0, gPlayerHp - damage);
    std::wstringstream s;
    s << gWild.species->name << L" usa " << move.second << L". " << damage << L" de dano.";
    gBattleLog = s.str();
    if (gPlayerHp <= 0) awardDefeat();
    showBattleState();
    gRenderer.playWildAction();
    if (gPlayerHp <= 0) gRenderer.playPlayerFaint(); else gRenderer.playPlayerHit();
}

void resolvePlayerChoice(int playerPower, const wchar_t* playerMove) {
    if (gBattleOver || gPendingAction != PendingAction::None) return;
    const int playerSpeed = currentSpeed(*gPlayer.species, gPlayer.level, gPlayer.iv);
    const int wildSpeed = currentSpeed(*gWild.species, gWild.level, gWild.iv);

    if (playerSpeed >= wildSpeed) {
        playerAttack(playerPower, playerMove);
        if (!gBattleOver) {
            gPendingAction = PendingAction::WildCounter;
            gPendingAt = GetTickCount64() + 650;
        }
    } else {
        gPendingPlayerPower = playerPower;
        gPendingPlayerMove = playerMove;
        wildAttack();
        if (!gBattleOver) {
            gPendingAction = PendingAction::PlayerCounter;
            gPendingAt = GetTickCount64() + 650;
        }
    }
}

void processPendingAction() {
    if (gPendingAction == PendingAction::None || GetTickCount64() < gPendingAt) return;
    const PendingAction pending = gPendingAction;
    gPendingAction = PendingAction::None;
    if (gBattleOver) return;
    if (pending == PendingAction::WildCounter) wildAttack();
    else if (pending == PendingAction::PlayerCounter) playerAttack(gPendingPlayerPower, gPendingPlayerMove.c_str());
}

void setFullscreen(bool fullscreen) {
    if (fullscreen == gFullscreen) return;
    if (fullscreen) {
        gWindowedStyle = static_cast<DWORD>(GetWindowLongPtrW(gWindow, GWL_STYLE));
        GetWindowPlacement(gWindow, &gWindowPlacement);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST), &monitor)) {
            SetWindowLongPtrW(gWindow, GWL_STYLE, gWindowedStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(gWindow, HWND_TOP,
                         monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            gFullscreen = true;
        }
    } else {
        SetWindowLongPtrW(gWindow, GWL_STYLE, gWindowedStyle);
        SetWindowPlacement(gWindow, &gWindowPlacement);
        SetWindowPos(gWindow, nullptr, 0,0,0,0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        gFullscreen = false;
    }
}

void handleAction(GameAction action) {
    switch (action) {
        case GameAction::NewGame: gHasStarter = false; showIntro(); break;
        case GameAction::Options: showOptions(); break;
        case GameAction::Back:
            if (gScreen == Screen::Options || gScreen == Screen::Hub) showMainMenu();
            else showMainMenu();
            break;
        case GameAction::IntroContinue: showStarterSelect(); break;
        case GameAction::StarterRattata: chooseStarter(kSpecies[0]); break;
        case GameAction::StarterPidgey: chooseStarter(kSpecies[1]); break;
        case GameAction::StarterCaterpie: chooseStarter(kSpecies[2]); break;
        case GameAction::Hunt: enterHunt(); break;
        case GameAction::StartBattle: beginBattle(); break;
        case GameAction::Move1: resolvePlayerChoice(gPlayer.species->move1Power, gPlayer.species->move1); break;
        case GameAction::Move2: resolvePlayerChoice(gPlayer.species->move2Power, gPlayer.species->move2); break;
        case GameAction::ReturnHub: showHub(); break;
        case GameAction::NextBattle: enterHunt(); break;
        case GameAction::VolumeDown: gVolume = std::max(0, gVolume - 10); showOptions(); break;
        case GameAction::VolumeUp: gVolume = std::min(100, gVolume + 10); showOptions(); break;
        case GameAction::Fullscreen: setFullscreen(true); showOptions(); break;
        case GameAction::Windowed: setFullscreen(false); showOptions(); break;
        default: break;
    }
}

void resizeSurface(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    gRenderer.move(0, 0, rc.right - rc.left, rc.bottom - rc.top);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            gWindow = hwnd;
            if (!gRenderer.create(hwnd, &gArchive)) return -1;
            resizeSurface(hwnd);
            gStageModel = bestResource(stageScore);
            gEffectModel = bestResource(effectScore);
            gRenderer.setStageModel(gStageModel);
            gRenderer.setEffectModel(gEffectModel);
            SetTimer(hwnd, ID_RENDER_TIMER, 16, nullptr);
            showMainMenu();
            return 0;
        }
        case WM_SIZE:
            resizeSurface(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == ID_RENDER_TIMER) {
                processPendingAction();
                gRenderer.tick();
                return 0;
            }
            break;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) {
                handleAction(static_cast<GameAction>(LOWORD(wp)));
                return 0;
            }
            break;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {960, 620};
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, ID_RENDER_TIMER);
            gRenderer.destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const auto roms = romDirectory();
    if (roms.empty()) {
        MessageBoxW(nullptr,
                    L"No se encontro la carpeta roms. Coloca tu ROM de Pokemon Rumble World dentro de la carpeta roms del proyecto.",
                    L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!gArchive.open(roms) || gArchive.library().empty()) {
        MessageBoxW(nullptr, L"La carpeta roms no contiene una ROM reconocible.", L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        return 1;
    }

    gModelResources = gArchive.modelResources();
    if (gModelResources.empty()) {
        MessageBoxW(nullptr,
                    L"La ROM fue detectada, pero no se encontraron recursos BCH utilizables.",
                    L"Promptemon Steidium", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"PromptemonPlayableGame";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Promptemon Steidium",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
