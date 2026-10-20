#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class Screen {
    MainMenu,
    Options,
    Intro,
    StarterSelect,
    Hub,
    Hunt,
    Battle
};

enum class Stat {
    HP,
    Attack,
    Defense,
    SpAttack,
    SpDefense,
    Speed
};

struct Species {
    const wchar_t* name;
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
};

struct OwnedPokemon {
    const Species* species = nullptr;
    int level = 5;
    int xp = 0;
    std::array<int, 6> iv{0, 0, 0, 0, 0, 0};
    int wins = 0;
    int losses = 0;
};

struct WildPokemon {
    const Species* species = nullptr;
    int level = 4;
    std::array<int, 6> iv{0, 0, 0, 0, 0, 0};
    bool shiny = false;
};

constexpr int ID_NEW_GAME = 100;
constexpr int ID_OPTIONS = 101;
constexpr int ID_BACK = 102;
constexpr int ID_INTRO_CONTINUE = 103;
constexpr int ID_STARTER_RATTATA = 104;
constexpr int ID_STARTER_PIDGEY = 105;
constexpr int ID_STARTER_CATERPIE = 106;
constexpr int ID_HUNT = 107;
constexpr int ID_START_BATTLE = 108;
constexpr int ID_MOVE_1 = 109;
constexpr int ID_MOVE_2 = 110;
constexpr int ID_RETURN_HUB = 111;
constexpr int ID_NEXT_BATTLE = 112;
constexpr int ID_VOL_DOWN = 113;
constexpr int ID_VOL_UP = 114;
constexpr int ID_FULLSCREEN = 115;
constexpr int ID_WINDOWED = 116;

constexpr int ID_DISABLED_LOAD = 200;
constexpr int ID_DISABLED_DELETE = 201;
constexpr int ID_DISABLED_EXPORT = 202;

const Species kSpecies[] = {
    {L"Rattata", 30, 56, 35, 72, Stat::Speed,     300, L"Placaje", 40, L"Ataque r\u00e1pido", 40},
    {L"Pidgey",  40, 45, 40, 56, Stat::Speed,     250, L"Placaje", 40, L"Tornado", 45},
    {L"Caterpie",45, 30, 35, 45, Stat::HP,        180, L"Placaje", 40, L"Picadura", 50},
    {L"Weedle",  40, 35, 30, 50, Stat::Speed,     140, L"Picotazo veneno", 40, L"Picadura", 50},
    {L"Zubat",   40, 45, 35, 55, Stat::Speed,      80, L"Impresionar", 30, L"Mordisco", 60},
    {L"Geodude", 40, 80,100, 20, Stat::Defense,    35, L"Placaje", 40, L"Lanzarrocas", 50},
    {L"Gastly",  30, 35, 30, 80, Stat::SpAttack,   10, L"Leng\u00fcetazo", 30, L"Tinieblas", 50},
    {L"Machop",  70, 80, 50, 35, Stat::Attack,      5, L"Golpe k\u00e1rate", 50, L"Patada baja", 50}
};

HWND gWindow = nullptr;
HFONT gFont = nullptr;
HFONT gTitleFont = nullptr;
std::vector<HWND> gControls;
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

int statIndex(Stat stat) {
    return static_cast<int>(stat);
}

int randomInt(int minValue, int maxValue) {
    std::uniform_int_distribution<int> distribution(minValue, maxValue);
    return distribution(gRng);
}

std::array<int, 6> randomIvs() {
    std::array<int, 6> result{};
    for (auto& value : result) value = randomInt(0, 31);
    return result;
}

int calculatedHp(const Species& species, int level, int iv) {
    return (((2 * species.baseHp + iv) * level) / 100) + level + 10;
}

int calculatedStat(int base, int level, int iv) {
    return (((2 * base + iv) * level) / 100) + 5;
}

int currentAttack(const Species& species, int level, const std::array<int, 6>& iv) {
    return calculatedStat(species.baseAttack, level, iv[statIndex(Stat::Attack)]);
}

int currentDefense(const Species& species, int level, const std::array<int, 6>& iv) {
    return calculatedStat(species.baseDefense, level, iv[statIndex(Stat::Defense)]);
}

int currentSpeed(const Species& species, int level, const std::array<int, 6>& iv) {
    return calculatedStat(species.baseSpeed, level, iv[statIndex(Stat::Speed)]);
}

int damageFor(const Species& attackerSpecies,
              int attackerLevel,
              const std::array<int, 6>& attackerIv,
              const Species& defenderSpecies,
              int defenderLevel,
              const std::array<int, 6>& defenderIv,
              int power) {
    const int attack = std::max(1, currentAttack(attackerSpecies, attackerLevel, attackerIv));
    const int defense = std::max(1, currentDefense(defenderSpecies, defenderLevel, defenderIv));
    const int base = (((2 * attackerLevel / 5 + 2) * power * attack / defense) / 50) + 2;
    const int roll = randomInt(85, 100);
    return std::max(1, base * roll / 100);
}

void clearControls() {
    for (HWND control : gControls) {
        if (IsWindow(control)) DestroyWindow(control);
    }
    gControls.clear();
}

void applyFont(HWND control, bool title = false) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(title ? gTitleFont : gFont), TRUE);
}

HWND addStatic(const wchar_t* text, int x, int y, int w, int h, bool title = false, DWORD extraStyle = 0) {
    HWND control = CreateWindowExW(0, L"STATIC", text,
                                   WS_CHILD | WS_VISIBLE | extraStyle,
                                   x, y, w, h,
                                   gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    applyFont(control, title);
    gControls.push_back(control);
    return control;
}

HWND addButton(const wchar_t* text, int id, int x, int y, int w, int h, bool enabled = true) {
    HWND control = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   x, y, w, h,
                                   gWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    applyFont(control, false);
    EnableWindow(control, enabled ? TRUE : FALSE);
    gControls.push_back(control);
    return control;
}

void setScreenTitle(const wchar_t* text) {
    std::wstring title = L"Promptemon Steidium - ";
    title += text;
    SetWindowTextW(gWindow, title.c_str());
}

void requestRepaint() {
    InvalidateRect(gWindow, nullptr, TRUE);
}

void showMainMenu();
void showHub();
void showHunt();
void showBattle();

void showMainMenu() {
    gScreen = Screen::MainMenu;
    clearControls();
    setScreenTitle(L"Men\u00fa inicial");

    addStatic(L"PROMPTEMON STEIDIUM", 70, 55, 820, 54, true, SS_CENTER);
    addStatic(L"Prototipo jugable", 70, 112, 820, 28, false, SS_CENTER);

    addButton(L"Nueva partida", ID_NEW_GAME, 325, 190, 350, 52, true);
    addButton(L"Cargar partida", ID_DISABLED_LOAD, 325, 252, 350, 52, false);
    addButton(L"Borrar partida", ID_DISABLED_DELETE, 325, 314, 350, 52, false);
    addButton(L"Exportar partida", ID_DISABLED_EXPORT, 325, 376, 350, 52, false);
    addButton(L"Opciones", ID_OPTIONS, 325, 438, 350, 52, true);

    addStatic(L"Solo Nueva partida est\u00e1 habilitada como flujo de juego en este prototipo.",
              180, 530, 640, 28, false, SS_CENTER);
    requestRepaint();
}

void showOptions() {
    gScreen = Screen::Options;
    clearControls();
    setScreenTitle(L"Opciones");

    addStatic(L"Opciones", 70, 60, 820, 48, true, SS_CENTER);

    std::wstringstream volume;
    volume << L"Volumen de audio: " << gVolume << L"%";
    addStatic(volume.str().c_str(), 300, 175, 400, 36, false, SS_CENTER);
    addButton(L"-", ID_VOL_DOWN, 330, 225, 120, 48, gVolume > 0);
    addButton(L"+", ID_VOL_UP, 550, 225, 120, 48, gVolume < 100);

    addStatic(L"Modo de pantalla", 300, 330, 400, 36, false, SS_CENTER);
    addButton(L"Pantalla completa", ID_FULLSCREEN, 250, 380, 240, 48, !gFullscreen);
    addButton(L"Ventana", ID_WINDOWED, 510, 380, 240, 48, gFullscreen);

    addStatic(L"El volumen ya queda guardado como ajuste del prototipo; todav\u00eda no hay audio cargado.",
              170, 475, 660, 50, false, SS_CENTER);
    addButton(L"Volver", ID_BACK, 375, 560, 250, 48, true);
    requestRepaint();
}

void showIntro() {
    gScreen = Screen::Intro;
    clearControls();
    setScreenTitle(L"Introducci\u00f3n");

    addStatic(L"Bienvenido a Promptemon Steidium", 70, 70, 820, 48, true, SS_CENTER);
    addStatic(L"En este prototipo, tu partida comienza en un peque\u00f1o hub.\r\n"
              L"Desde all\u00ed podr\u00e1s entrar a una zona de Caza, encontrar Pok\u00e9mon salvajes y combatir.\r\n\r\n"
              L"Los rivales tienen una probabilidad de ser shiny y cada especie muestra qu\u00e9 estad\u00edstica aporta al entrenamiento de IV.",
              180, 185, 640, 190, false, SS_CENTER);
    addButton(L"Continuar", ID_INTRO_CONTINUE, 375, 470, 250, 52, true);
    requestRepaint();
}

void showStarterSelect() {
    gScreen = Screen::StarterSelect;
    clearControls();
    setScreenTitle(L"Elige tu primer compa\u00f1ero");

    addStatic(L"Elige tu primer compa\u00f1ero", 70, 65, 820, 48, true, SS_CENTER);
    addStatic(L"Los tres son Pok\u00e9mon comunes. Tu elecci\u00f3n quedar\u00e1 activa para el combate de este prototipo.",
              120, 125, 760, 50, false, SS_CENTER);

    addButton(L"Rattata\r\nCom\u00fan - aporta Velocidad", ID_STARTER_RATTATA, 130, 240, 230, 95, true);
    addButton(L"Pidgey\r\nCom\u00fan - aporta Velocidad", ID_STARTER_PIDGEY, 385, 240, 230, 95, true);
    addButton(L"Caterpie\r\nCom\u00fan - aporta PS", ID_STARTER_CATERPIE, 640, 240, 230, 95, true);

    addStatic(L"Los IV iniciales se generan al elegir al compa\u00f1ero. Ganar combates puede mejorar el IV indicado por la especie rival.",
              170, 405, 660, 70, false, SS_CENTER);
    requestRepaint();
}

void chooseStarter(const Species& species) {
    gPlayer = {};
    gPlayer.species = &species;
    gPlayer.level = 5;
    gPlayer.iv = randomIvs();
    gHasStarter = true;
    showHub();
}

std::wstring ivSummary() {
    if (!gHasStarter) return L"";
    std::wstringstream text;
    text << L"IV: PS " << gPlayer.iv[0]
         << L" | ATQ " << gPlayer.iv[1]
         << L" | DEF " << gPlayer.iv[2]
         << L" | ATQ.ESP " << gPlayer.iv[3]
         << L" | DEF.ESP " << gPlayer.iv[4]
         << L" | VEL " << gPlayer.iv[5];
    return text.str();
}

void showHub() {
    gScreen = Screen::Hub;
    clearControls();
    setScreenTitle(L"Proto Hub");

    addStatic(L"PROTO HUB", 70, 55, 820, 48, true, SS_CENTER);
    addStatic(L"Tu compa\u00f1ero", 120, 155, 760, 34, false, SS_CENTER);

    std::wstringstream info;
    info << gPlayer.species->name
         << L"  Nv. " << gPlayer.level
         << L"  | XP " << gPlayer.xp << L"/100"
         << L"  | Victorias " << gPlayer.wins
         << L"  | Derrotas " << gPlayer.losses;
    addStatic(info.str().c_str(), 100, 205, 800, 40, false, SS_CENTER);
    addStatic(ivSummary().c_str(), 70, 260, 860, 45, false, SS_CENTER);

    addStatic(L"La Caza usa una tabla ponderada: los Pok\u00e9mon comunes aparecen mucho m\u00e1s que los menos frecuentes.",
              150, 345, 700, 55, false, SS_CENTER);
    addButton(L"Ir a la zona de Caza", ID_HUNT, 325, 435, 350, 58, true);
    addButton(L"Volver al men\u00fa", ID_BACK, 375, 535, 250, 45, true);
    requestRepaint();
}

void generateWild() {
    int totalWeight = 0;
    for (const Species& species : kSpecies) totalWeight += species.encounterWeight;
    int roll = randomInt(1, totalWeight);
    gWild.species = &kSpecies[0];
    for (const Species& species : kSpecies) {
        roll -= species.encounterWeight;
        if (roll <= 0) {
            gWild.species = &species;
            break;
        }
    }
    gWild.level = randomInt(3, 6);
    gWild.iv = randomIvs();
    gWild.shiny = randomInt(1, 4096) == 1;
}

void enterHunt() {
    generateWild();
    showHunt();
}

void showHunt() {
    gScreen = Screen::Hunt;
    clearControls();
    setScreenTitle(L"Zona de Caza");

    addStatic(L"ZONA DE CAZA", 70, 55, 820, 48, true, SS_CENTER);
    addStatic(L"Ha aparecido un Pok\u00e9mon salvaje", 120, 140, 760, 38, false, SS_CENTER);

    std::wstringstream encounter;
    if (gWild.shiny) encounter << L"\u2605 SHINY \u2605  ";
    encounter << gWild.species->name << L"  Nv. " << gWild.level;
    addStatic(encounter.str().c_str(), 120, 205, 760, 50, true, SS_CENTER);

    std::wstringstream reward;
    reward << L"Aporta IV: " << statName(gWild.species->ivReward);
    addStatic(reward.str().c_str(), 220, 290, 560, 38, false, SS_CENTER);

    addStatic(L"En el juego final, este encuentro tambi\u00e9n podr\u00e1 capturarse. En este corte cerramos primero el combate.",
              170, 350, 660, 65, false, SS_CENTER);

    addButton(L"Combatir", ID_START_BATTLE, 325, 455, 350, 58, true);
    addButton(L"Volver al Hub", ID_RETURN_HUB, 375, 545, 250, 45, true);
    requestRepaint();
}

void beginBattle() {
    gBattleOver = false;
    gBattleWon = false;
    gPlayerMaxHp = calculatedHp(*gPlayer.species, gPlayer.level, gPlayer.iv[0]);
    gPlayerHp = gPlayerMaxHp;
    gWildMaxHp = calculatedHp(*gWild.species, gWild.level, gWild.iv[0]);
    gWildHp = gWildMaxHp;
    gBattleLog = L"El combate ha comenzado.";
    showBattle();
}

void awardVictory() {
    gBattleOver = true;
    gBattleWon = true;
    ++gPlayer.wins;

    const int rewardIndex = statIndex(gWild.species->ivReward);
    const int before = gPlayer.iv[rewardIndex];
    if (gPlayer.iv[rewardIndex] < 31) ++gPlayer.iv[rewardIndex];

    gPlayer.xp += 25;
    bool levelUp = false;
    if (gPlayer.xp >= 100) {
        gPlayer.xp -= 100;
        ++gPlayer.level;
        levelUp = true;
    }

    std::wstringstream log;
    log << L"\u00a1Victoria! " << gWild.species->name << L" ha ca\u00eddo. ";
    if (before < 31) {
        log << L"IV de " << statName(gWild.species->ivReward) << L": " << before << L" -> " << gPlayer.iv[rewardIndex] << L". ";
    } else {
        log << L"El IV de " << statName(gWild.species->ivReward) << L" ya estaba al m\u00e1ximo. ";
    }
    log << L"+25 XP.";
    if (levelUp) log << L" \u00a1Subiste de nivel!";
    gBattleLog = log.str();
}

void awardDefeat() {
    gBattleOver = true;
    gBattleWon = false;
    ++gPlayer.losses;
    gBattleLog = L"Tu Pok\u00e9mon se ha debilitado. La derrota queda registrada en esta sesi\u00f3n.";
}

void resolveTurn(int playerPower, const wchar_t* playerMove) {
    if (gBattleOver) return;

    const int playerSpeed = currentSpeed(*gPlayer.species, gPlayer.level, gPlayer.iv);
    const int wildSpeed = currentSpeed(*gWild.species, gWild.level, gWild.iv);
    std::wstringstream log;

    auto playerAttack = [&]() {
        const int damage = damageFor(*gPlayer.species, gPlayer.level, gPlayer.iv,
                                     *gWild.species, gWild.level, gWild.iv,
                                     playerPower);
        gWildHp = std::max(0, gWildHp - damage);
        log << gPlayer.species->name << L" usa " << playerMove << L" y causa " << damage << L" de da\u00f1o. ";
    };

    auto wildAttack = [&]() {
        const bool useSecond = randomInt(0, 1) == 1;
        const int power = useSecond ? gWild.species->move2Power : gWild.species->move1Power;
        const wchar_t* move = useSecond ? gWild.species->move2 : gWild.species->move1;
        const int damage = damageFor(*gWild.species, gWild.level, gWild.iv,
                                     *gPlayer.species, gPlayer.level, gPlayer.iv,
                                     power);
        gPlayerHp = std::max(0, gPlayerHp - damage);
        log << gWild.species->name << L" usa " << move << L" y causa " << damage << L" de da\u00f1o. ";
    };

    if (wildSpeed > playerSpeed) {
        wildAttack();
        if (gPlayerHp > 0) playerAttack();
    } else {
        playerAttack();
        if (gWildHp > 0) wildAttack();
    }

    gBattleLog = log.str();
    if (gWildHp <= 0) awardVictory();
    else if (gPlayerHp <= 0) awardDefeat();
    showBattle();
}

std::wstring hpLine(const wchar_t* label, int hp, int maxHp) {
    std::wstringstream text;
    text << label << L"  PS " << hp << L" / " << maxHp << L"  [";
    const int segments = 20;
    const int filled = maxHp > 0 ? (hp * segments / maxHp) : 0;
    for (int i = 0; i < segments; ++i) text << (i < filled ? L'#' : L'-');
    text << L"]";
    return text.str();
}

void showBattle() {
    gScreen = Screen::Battle;
    clearControls();
    setScreenTitle(L"Combate");

    addStatic(L"COMBATE LOCAL", 70, 35, 820, 44, true, SS_CENTER);

    std::wstringstream wildName;
    if (gWild.shiny) wildName << L"\u2605 SHINY \u2605  ";
    wildName << gWild.species->name << L"  Nv. " << gWild.level
             << L"  | Aporta IV: " << statName(gWild.species->ivReward);
    addStatic(wildName.str().c_str(), 100, 105, 800, 42, false, SS_CENTER);
    addStatic(hpLine(L"Rival", gWildHp, gWildMaxHp).c_str(), 120, 155, 760, 34, false, SS_CENTER);

    std::wstringstream playerName;
    playerName << gPlayer.species->name << L"  Nv. " << gPlayer.level;
    addStatic(playerName.str().c_str(), 100, 235, 800, 42, false, SS_CENTER);
    addStatic(hpLine(L"T\u00fa", gPlayerHp, gPlayerMaxHp).c_str(), 120, 285, 760, 34, false, SS_CENTER);

    addStatic(gBattleLog.c_str(), 140, 360, 720, 75, false, SS_CENTER);

    if (!gBattleOver) {
        addButton(gPlayer.species->move1, ID_MOVE_1, 235, 485, 250, 58, true);
        addButton(gPlayer.species->move2, ID_MOVE_2, 515, 485, 250, 58, true);
    } else {
        addButton(L"Otro encuentro", ID_NEXT_BATTLE, 235, 500, 250, 55, true);
        addButton(L"Volver al Hub", ID_RETURN_HUB, 515, 500, 250, 55, true);
    }
    requestRepaint();
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
                         monitor.rcMonitor.left,
                         monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            gFullscreen = true;
        }
    } else {
        SetWindowLongPtrW(gWindow, GWL_STYLE, gWindowedStyle);
        SetWindowPlacement(gWindow, &gWindowPlacement);
        SetWindowPos(gWindow, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        gFullscreen = false;
    }
}

void handleCommand(int id) {
    switch (id) {
        case ID_NEW_GAME:
            gHasStarter = false;
            showIntro();
            break;
        case ID_OPTIONS:
            showOptions();
            break;
        case ID_BACK:
            if (gScreen == Screen::Hub) showMainMenu();
            else showMainMenu();
            break;
        case ID_INTRO_CONTINUE:
            showStarterSelect();
            break;
        case ID_STARTER_RATTATA:
            chooseStarter(kSpecies[0]);
            break;
        case ID_STARTER_PIDGEY:
            chooseStarter(kSpecies[1]);
            break;
        case ID_STARTER_CATERPIE:
            chooseStarter(kSpecies[2]);
            break;
        case ID_HUNT:
            enterHunt();
            break;
        case ID_START_BATTLE:
            beginBattle();
            break;
        case ID_MOVE_1:
            resolveTurn(gPlayer.species->move1Power, gPlayer.species->move1);
            break;
        case ID_MOVE_2:
            resolveTurn(gPlayer.species->move2Power, gPlayer.species->move2);
            break;
        case ID_RETURN_HUB:
            showHub();
            break;
        case ID_NEXT_BATTLE:
            enterHunt();
            break;
        case ID_VOL_DOWN:
            gVolume = std::max(0, gVolume - 10);
            showOptions();
            break;
        case ID_VOL_UP:
            gVolume = std::min(100, gVolume + 10);
            showOptions();
            break;
        case ID_FULLSCREEN:
            setFullscreen(true);
            showOptions();
            break;
        case ID_WINDOWED:
            setFullscreen(false);
            showOptions();
            break;
    }
}

COLORREF backgroundColor() {
    switch (gScreen) {
        case Screen::MainMenu: return RGB(228, 241, 250);
        case Screen::Options: return RGB(235, 239, 245);
        case Screen::Intro: return RGB(244, 238, 220);
        case Screen::StarterSelect: return RGB(226, 244, 226);
        case Screen::Hub: return RGB(225, 241, 247);
        case Screen::Hunt: return RGB(226, 242, 215);
        case Screen::Battle: return RGB(241, 232, 220);
    }
    return RGB(240, 240, 240);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            gWindow = hwnd;
            gFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            gTitleFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            showMainMenu();
            return 0;
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED) {
                handleCommand(LOWORD(wParam));
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(28, 42, 55));
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH brush = CreateSolidBrush(backgroundColor());
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {900, 650};
            return 0;
        }
        case WM_DESTROY:
            clearControls();
            if (gFont) DeleteObject(gFont);
            if (gTitleFont) DeleteObject(gTitleFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = L"PromptemonPlayablePrototype";

    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    HWND window = CreateWindowExW(0,
                                  windowClass.lpszClassName,
                                  L"Promptemon Steidium",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  1000,
                                  700,
                                  nullptr,
                                  nullptr,
                                  instance,
                                  nullptr);
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
