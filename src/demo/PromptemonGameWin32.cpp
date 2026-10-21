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
#include <vector>

namespace {

using promptemon::demo::PrototypeRomRenderer;
using promptemon::rom::RomModelArchive;

enum class Screen { MainMenu, Options, Intro, StarterSelect, Hub, Hunt, Battle };
enum class Stat { HP, Attack, Defense, SpAttack, SpDefense, Speed };

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
    {L"Rattata", 19, 30,56,35,72, Stat::Speed,   300, L"Placaje",40, L"Ataque rápido",40, "koratta", "rattata", nullptr},
    {L"Pidgey",  16, 40,45,40,56, Stat::Speed,   250, L"Placaje",40, L"Tornado",45, "poppo", "pidgey", nullptr},
    {L"Caterpie",10, 45,30,35,45, Stat::HP,      180, L"Placaje",40, L"Picadura",50, "kyatapi", "caterpie", nullptr},
    {L"Weedle",  13, 40,35,30,50, Stat::Speed,   140, L"Picotazo veneno",40, L"Picadura",50, "biidoru", "weedle", nullptr},
    {L"Zubat",   41, 40,45,35,55, Stat::Speed,    80, L"Impresionar",30, L"Mordisco",60, "zubatto", "zubat", nullptr},
    {L"Geodude", 74, 40,80,100,20,Stat::Defense,  35, L"Placaje",40, L"Lanzarrocas",50, "ishitsubute", "isitubute", "geodude"},
    {L"Gastly",  92, 30,35,30,80, Stat::SpAttack, 10, L"Lengüetazo",30, L"Tinieblas",50, "goosu", "ghos", "gastly"},
    {L"Machop",  66, 70,80,50,35, Stat::Attack,    5, L"Golpe kárate",50, L"Patada baja",50, "wanrikii", "machop", nullptr}
};

constexpr int ID_NEW_GAME=100, ID_OPTIONS=101, ID_BACK=102, ID_INTRO_CONTINUE=103;
constexpr int ID_STARTER_RATTATA=104, ID_STARTER_PIDGEY=105, ID_STARTER_CATERPIE=106;
constexpr int ID_HUNT=107, ID_START_BATTLE=108, ID_MOVE_1=109, ID_MOVE_2=110;
constexpr int ID_RETURN_HUB=111, ID_NEXT_BATTLE=112, ID_VOL_DOWN=113, ID_VOL_UP=114;
constexpr int ID_FULLSCREEN=115, ID_WINDOWED=116;
constexpr int ID_DISABLED_LOAD=200, ID_DISABLED_DELETE=201, ID_DISABLED_EXPORT=202;
constexpr UINT_PTR ID_RENDER_TIMER=1;

HWND gWindow=nullptr;
HFONT gFont=nullptr, gTitleFont=nullptr;
std::vector<HWND> gControls;
Screen gScreen=Screen::MainMenu;
OwnedPokemon gPlayer;
WildPokemon gWild;
bool gHasStarter=false, gBattleOver=false, gBattleWon=false;
int gPlayerHp=0, gPlayerMaxHp=0, gWildHp=0, gWildMaxHp=0;
std::wstring gBattleLog;
std::mt19937 gRng{std::random_device{}()};
int gVolume=70;
bool gFullscreen=false;
WINDOWPLACEMENT gWindowPlacement{sizeof(WINDOWPLACEMENT)};
DWORD gWindowedStyle=0;

RomModelArchive gArchive;
std::vector<std::string> gModelResources;
std::unordered_map<const Species*, std::string> gResolvedModels;
PrototypeRomRenderer gRenderer;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
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
int randomInt(int lo,int hi){ std::uniform_int_distribution<int> d(lo,hi); return d(gRng); }
std::array<int,6> randomIvs(){ std::array<int,6> v{}; for(auto& x:v)x=randomInt(0,31); return v; }
int calculatedHp(const Species&s,int level,int iv){ return (((2*s.baseHp+iv)*level)/100)+level+10; }
int calculatedStat(int base,int level,int iv){ return (((2*base+iv)*level)/100)+5; }
int currentAttack(const Species&s,int l,const std::array<int,6>&iv){ return calculatedStat(s.baseAttack,l,iv[1]); }
int currentDefense(const Species&s,int l,const std::array<int,6>&iv){ return calculatedStat(s.baseDefense,l,iv[2]); }
int currentSpeed(const Species&s,int l,const std::array<int,6>&iv){ return calculatedStat(s.baseSpeed,l,iv[5]); }

int damageFor(const Species&a,int al,const std::array<int,6>&aiv,const Species&d,int dl,const std::array<int,6>&div,int power){
    const int attack=std::max(1,currentAttack(a,al,aiv));
    const int defense=std::max(1,currentDefense(d,dl,div));
    const int base=(((2*al/5+2)*power*attack/defense)/50)+2;
    return std::max(1,base*randomInt(85,100)/100);
}

std::filesystem::path executableDirectory(){
    std::wstring buffer(32768,L'\0');
    DWORD n=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!n||n>=buffer.size()) return {};
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path findRomsFrom(std::filesystem::path start){
    std::error_code ec;
    start=std::filesystem::absolute(start,ec);
    if(ec) return {};
    for(;;){
        auto candidate=start/L"roms";
        if(std::filesystem::is_directory(candidate,ec)) return candidate;
        ec.clear();
        auto parent=start.parent_path();
        if(parent.empty()||parent==start) break;
        start=parent;
    }
    return {};
}

std::filesystem::path romDirectory(){
    if(auto p=findRomsFrom(std::filesystem::current_path());!p.empty()) return p;
    if(auto p=findRomsFrom(executableDirectory());!p.empty()) return p;
    return {};
}

const std::string& modelFor(const Species& species){
    auto existing=gResolvedModels.find(&species);
    if(existing!=gResolvedModels.end()) return existing->second;

    const char* keys[]={species.modelKey1,species.modelKey2,species.modelKey3};
    for(const auto& path:gModelResources){
        const std::string p=lower(path);
        for(const char* key:keys){
            if(key&&*key&&p.find(key)!=std::string::npos){
                return gResolvedModels.emplace(&species,path).first->second;
            }
        }
    }
    static const std::string empty;
    return empty;
}

void clearControls(){ for(HWND c:gControls) if(IsWindow(c)) DestroyWindow(c); gControls.clear(); }
void applyFont(HWND c,bool title=false){ SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(title?gTitleFont:gFont),TRUE); }
HWND addStatic(const std::wstring& text,int x,int y,int w,int h,bool title=false,DWORD style=0){
    HWND c=CreateWindowExW(0,L"STATIC",text.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,w,h,gWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
    applyFont(c,title); gControls.push_back(c); return c;
}
HWND addButton(const wchar_t* text,int id,int x,int y,int w,int h,bool enabled=true){
    HWND c=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,x,y,w,h,gWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    applyFont(c); EnableWindow(c,enabled?TRUE:FALSE); gControls.push_back(c); return c;
}
void setScreenTitle(const wchar_t* text){ std::wstring t=L"Promptemon Steidium - "; t+=text; SetWindowTextW(gWindow,t.c_str()); }
void repaint(){ InvalidateRect(gWindow,nullptr,TRUE); }
void hideRenderer(){ gRenderer.show(false); }

void showMainMenu(); void showHub(); void showHunt(); void showBattle();

void showMainMenu(){
    gScreen=Screen::MainMenu; clearControls(); hideRenderer(); setScreenTitle(L"Menú inicial");
    addStatic(L"PROMPTEMON STEIDIUM",70,55,820,54,true,SS_CENTER);
    addStatic(L"ROM de Pokémon Rumble World cargada: modelos y animaciones se usan dentro del prototipo.",80,116,840,34,false,SS_CENTER);
    addButton(L"Nueva partida",ID_NEW_GAME,325,190,350,52);
    addButton(L"Cargar partida",ID_DISABLED_LOAD,325,252,350,52,false);
    addButton(L"Borrar partida",ID_DISABLED_DELETE,325,314,350,52,false);
    addButton(L"Exportar partida",ID_DISABLED_EXPORT,325,376,350,52,false);
    addButton(L"Opciones",ID_OPTIONS,325,438,350,52);
    repaint();
}

void showOptions(){
    gScreen=Screen::Options; clearControls(); hideRenderer(); setScreenTitle(L"Opciones");
    addStatic(L"Opciones",70,60,820,48,true,SS_CENTER);
    std::wstringstream v; v<<L"Volumen de audio: "<<gVolume<<L"%";
    addStatic(v.str(),300,175,400,36,false,SS_CENTER);
    addButton(L"-",ID_VOL_DOWN,330,225,120,48,gVolume>0); addButton(L"+",ID_VOL_UP,550,225,120,48,gVolume<100);
    addStatic(L"Modo de pantalla",300,330,400,36,false,SS_CENTER);
    addButton(L"Pantalla completa",ID_FULLSCREEN,250,380,240,48,!gFullscreen); addButton(L"Ventana",ID_WINDOWED,510,380,240,48,gFullscreen);
    addButton(L"Volver",ID_BACK,375,560,250,48); repaint();
}

void showIntro(){
    gScreen=Screen::Intro; clearControls(); hideRenderer(); setScreenTitle(L"Introducción");
    addStatic(L"Bienvenido a Promptemon Steidium",70,70,820,48,true,SS_CENTER);
    addStatic(L"Tu partida empieza en un pequeño Hub. Desde allí entrarás a Caza, encontrarás Pokémon salvajes y combatirás.\r\n\r\nLos Pokémon que ves se cargan desde tu ROM. Cada rival indica qué estadística aporta al entrenamiento de IV y puede aparecer shiny.",160,170,680,220,false,SS_CENTER);
    addButton(L"Continuar",ID_INTRO_CONTINUE,375,470,250,52); repaint();
}

void showStarterSelect(){
    gScreen=Screen::StarterSelect; clearControls(); hideRenderer(); setScreenTitle(L"Elige tu primer compañero");
    addStatic(L"Elige tu primer compañero",70,65,820,48,true,SS_CENTER);
    addStatic(L"Tres Pokémon comunes para comenzar",120,125,760,36,false,SS_CENTER);
    addButton(L"Rattata\r\nAporta Velocidad",ID_STARTER_RATTATA,130,240,230,95);
    addButton(L"Pidgey\r\nAporta Velocidad",ID_STARTER_PIDGEY,385,240,230,95);
    addButton(L"Caterpie\r\nAporta PS",ID_STARTER_CATERPIE,640,240,230,95);
    addStatic(L"Al elegirlo, su modelo real de Rumble World aparecerá en el Hub y en combate.",150,405,700,55,false,SS_CENTER); repaint();
}

std::wstring ivSummary(){
    std::wstringstream s; s<<L"IV: PS "<<gPlayer.iv[0]<<L" | ATQ "<<gPlayer.iv[1]<<L" | DEF "<<gPlayer.iv[2]<<L" | ATQ.ESP "<<gPlayer.iv[3]<<L" | DEF.ESP "<<gPlayer.iv[4]<<L" | VEL "<<gPlayer.iv[5]; return s.str();
}

void chooseStarter(const Species& species){ gPlayer={}; gPlayer.species=&species; gPlayer.level=5; gPlayer.iv=randomIvs(); gHasStarter=true; showHub(); }

void configureSingleRenderer(const Species& species){
    const auto& model=modelFor(species);
    gRenderer.move(180,120,640,260); gRenderer.show(true);
    if(model.empty()) gRenderer.clearModels(); else gRenderer.setSingleModel(model);
}

void showHub(){
    gScreen=Screen::Hub; clearControls(); setScreenTitle(L"Proto Hub"); configureSingleRenderer(*gPlayer.species);
    addStatic(L"PROTO HUB",70,35,820,45,true,SS_CENTER);
    std::wstringstream info; info<<gPlayer.species->name<<L"  Nv. "<<gPlayer.level<<L" | XP "<<gPlayer.xp<<L"/100 | Victorias "<<gPlayer.wins<<L" | Derrotas "<<gPlayer.losses;
    addStatic(info.str(),80,395,840,35,false,SS_CENTER); addStatic(ivSummary(),55,438,890,38,false,SS_CENTER);
    if(modelFor(*gPlayer.species).empty()) addStatic(L"No se pudo resolver el modelo de esta especie dentro de la ROM.",120,478,760,30,false,SS_CENTER);
    addButton(L"Ir a la zona de Caza",ID_HUNT,325,525,350,55); addButton(L"Volver al menú",ID_BACK,375,590,250,42); repaint();
}

void generateWild(){
    int total=0; for(const auto&s:kSpecies) total+=s.encounterWeight; int roll=randomInt(1,total); gWild.species=&kSpecies[0];
    for(const auto&s:kSpecies){ roll-=s.encounterWeight; if(roll<=0){gWild.species=&s;break;} }
    gWild.level=randomInt(3,6); gWild.iv=randomIvs(); gWild.shiny=randomInt(1,4096)==1;
}
void enterHunt(){ generateWild(); showHunt(); }

void showHunt(){
    gScreen=Screen::Hunt; clearControls(); setScreenTitle(L"Zona de Caza"); configureSingleRenderer(*gWild.species);
    addStatic(L"ZONA DE CAZA",70,28,820,44,true,SS_CENTER);
    std::wstringstream e; if(gWild.shiny)e<<L"★ SHINY ★  "; e<<gWild.species->name<<L"  Nv. "<<gWild.level;
    addStatic(e.str(),100,395,800,40,true,SS_CENTER);
    std::wstringstream r; r<<L"Aporta IV: "<<statName(gWild.species->ivReward); addStatic(r.str(),220,442,560,34,false,SS_CENTER);
    if(modelFor(*gWild.species).empty()) addStatic(L"Modelo no resuelto en la ROM.",200,477,600,28,false,SS_CENTER);
    addButton(L"Combatir",ID_START_BATTLE,325,520,350,55); addButton(L"Volver al Hub",ID_RETURN_HUB,375,585,250,42); repaint();
}

void beginBattle(){
    gBattleOver=false; gBattleWon=false; gPlayerMaxHp=calculatedHp(*gPlayer.species,gPlayer.level,gPlayer.iv[0]); gPlayerHp=gPlayerMaxHp;
    gWildMaxHp=calculatedHp(*gWild.species,gWild.level,gWild.iv[0]); gWildHp=gWildMaxHp; gBattleLog=L"El combate ha comenzado."; showBattle();
}

void awardVictory(){
    gBattleOver=true; gBattleWon=true; ++gPlayer.wins; int idx=statIndex(gWild.species->ivReward); int before=gPlayer.iv[idx]; if(gPlayer.iv[idx]<31)++gPlayer.iv[idx];
    gPlayer.xp+=25; bool levelUp=false; if(gPlayer.xp>=100){gPlayer.xp-=100;++gPlayer.level;levelUp=true;}
    std::wstringstream s; s<<L"¡Victoria! "; if(before<31)s<<L"IV de "<<statName(gWild.species->ivReward)<<L": "<<before<<L" → "<<gPlayer.iv[idx]<<L". "; s<<L"+25 XP."; if(levelUp)s<<L" ¡Subiste de nivel!"; gBattleLog=s.str();
}
void awardDefeat(){ gBattleOver=true; gBattleWon=false; ++gPlayer.losses; gBattleLog=L"Tu Pokémon se ha debilitado."; }

void resolveTurn(int playerPower,const wchar_t* playerMove){
    if(gBattleOver)return; int ps=currentSpeed(*gPlayer.species,gPlayer.level,gPlayer.iv), ws=currentSpeed(*gWild.species,gWild.level,gWild.iv); std::wstringstream log;
    auto pa=[&](){int d=damageFor(*gPlayer.species,gPlayer.level,gPlayer.iv,*gWild.species,gWild.level,gWild.iv,playerPower);gWildHp=std::max(0,gWildHp-d);log<<gPlayer.species->name<<L" usa "<<playerMove<<L" y causa "<<d<<L" de daño. ";};
    auto wa=[&](){bool second=randomInt(0,1)==1;int p=second?gWild.species->move2Power:gWild.species->move1Power;const wchar_t*m=second?gWild.species->move2:gWild.species->move1;int d=damageFor(*gWild.species,gWild.level,gWild.iv,*gPlayer.species,gPlayer.level,gPlayer.iv,p);gPlayerHp=std::max(0,gPlayerHp-d);log<<gWild.species->name<<L" usa "<<m<<L" y causa "<<d<<L" de daño. ";};
    if(ws>ps){wa();if(gPlayerHp>0)pa();}else{pa();if(gWildHp>0)wa();} gBattleLog=log.str(); if(gWildHp<=0)awardVictory();else if(gPlayerHp<=0)awardDefeat(); showBattle();
}

std::wstring hpLine(const wchar_t* who,int hp,int maxHp){ std::wstringstream s;s<<who<<L"  PS "<<hp<<L" / "<<maxHp;return s.str(); }

void showBattle(){
    gScreen=Screen::Battle; clearControls(); setScreenTitle(L"Combate");
    const auto& playerModel=modelFor(*gPlayer.species); const auto& wildModel=modelFor(*gWild.species);
    gRenderer.move(120,95,760,300); gRenderer.show(true); if(playerModel.empty()||wildModel.empty())gRenderer.clearModels();else gRenderer.setBattleModels(playerModel,wildModel);
    addStatic(L"COMBATE LOCAL",70,25,820,42,true,SS_CENTER);
    std::wstringstream wild; if(gWild.shiny)wild<<L"★ SHINY ★  ";wild<<gWild.species->name<<L" Nv. "<<gWild.level<<L" | Aporta IV: "<<statName(gWild.species->ivReward); addStatic(wild.str(),80,405,840,30,false,SS_CENTER);
    addStatic(hpLine(L"Rival",gWildHp,gWildMaxHp),100,440,380,30,false,SS_CENTER); addStatic(hpLine(gPlayer.species->name,gPlayerHp,gPlayerMaxHp),520,440,380,30,false,SS_CENTER);
    addStatic(gBattleLog,120,480,760,55,false,SS_CENTER);
    if(!gBattleOver){ addButton(gPlayer.species->move1,ID_MOVE_1,235,550,250,50); addButton(gPlayer.species->move2,ID_MOVE_2,515,550,250,50); }
    else { addButton(L"Otro encuentro",ID_NEXT_BATTLE,235,550,250,50); addButton(L"Volver al Hub",ID_RETURN_HUB,515,550,250,50); }
    repaint();
}

void setFullscreen(bool fs){
    if(fs==gFullscreen)return;
    if(fs){gWindowedStyle=static_cast<DWORD>(GetWindowLongPtrW(gWindow,GWL_STYLE));GetWindowPlacement(gWindow,&gWindowPlacement);MONITORINFO m{sizeof(m)};if(GetMonitorInfoW(MonitorFromWindow(gWindow,MONITOR_DEFAULTTONEAREST),&m)){SetWindowLongPtrW(gWindow,GWL_STYLE,gWindowedStyle&~WS_OVERLAPPEDWINDOW);SetWindowPos(gWindow,HWND_TOP,m.rcMonitor.left,m.rcMonitor.top,m.rcMonitor.right-m.rcMonitor.left,m.rcMonitor.bottom-m.rcMonitor.top,SWP_NOOWNERZORDER|SWP_FRAMECHANGED);gFullscreen=true;}}
    else{SetWindowLongPtrW(gWindow,GWL_STYLE,gWindowedStyle);SetWindowPlacement(gWindow,&gWindowPlacement);SetWindowPos(gWindow,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);gFullscreen=false;}
}

void handleCommand(int id){
    switch(id){
        case ID_NEW_GAME:gHasStarter=false;showIntro();break; case ID_OPTIONS:showOptions();break; case ID_BACK:showMainMenu();break;
        case ID_INTRO_CONTINUE:showStarterSelect();break; case ID_STARTER_RATTATA:chooseStarter(kSpecies[0]);break; case ID_STARTER_PIDGEY:chooseStarter(kSpecies[1]);break; case ID_STARTER_CATERPIE:chooseStarter(kSpecies[2]);break;
        case ID_HUNT:enterHunt();break; case ID_START_BATTLE:beginBattle();break; case ID_MOVE_1:resolveTurn(gPlayer.species->move1Power,gPlayer.species->move1);break; case ID_MOVE_2:resolveTurn(gPlayer.species->move2Power,gPlayer.species->move2);break;
        case ID_RETURN_HUB:showHub();break; case ID_NEXT_BATTLE:enterHunt();break; case ID_VOL_DOWN:gVolume=std::max(0,gVolume-10);showOptions();break; case ID_VOL_UP:gVolume=std::min(100,gVolume+10);showOptions();break;
        case ID_FULLSCREEN:setFullscreen(true);showOptions();break; case ID_WINDOWED:setFullscreen(false);showOptions();break;
    }
}

COLORREF backgroundColor(){
    switch(gScreen){case Screen::MainMenu:return RGB(228,241,250);case Screen::Options:return RGB(235,239,245);case Screen::Intro:return RGB(244,238,220);case Screen::StarterSelect:return RGB(226,244,226);case Screen::Hub:return RGB(225,241,247);case Screen::Hunt:return RGB(226,242,215);case Screen::Battle:return RGB(241,232,220);}return RGB(240,240,240);
}

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_CREATE:
            gWindow=hwnd; gFont=CreateFontW(19,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
            gTitleFont=CreateFontW(31,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
            if(!gRenderer.create(hwnd,&gArchive)) return -1; SetTimer(hwnd,ID_RENDER_TIMER,16,nullptr); showMainMenu(); return 0;
        case WM_TIMER: if(wp==ID_RENDER_TIMER){gRenderer.tick();return 0;} break;
        case WM_COMMAND: if(HIWORD(wp)==BN_CLICKED){handleCommand(LOWORD(wp));return 0;} break;
        case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(28,42,55));return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));}
        case WM_ERASEBKGND:{HDC dc=reinterpret_cast<HDC>(wp);RECT r{};GetClientRect(hwnd,&r);HBRUSH b=CreateSolidBrush(backgroundColor());FillRect(dc,&r,b);DeleteObject(b);return 1;}
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={900,650};return 0;
        case WM_DESTROY:KillTimer(hwnd,ID_RENDER_TIMER);gRenderer.destroy();clearControls();if(gFont)DeleteObject(gFont);if(gTitleFont)DeleteObject(gTitleFont);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    const auto roms=romDirectory();
    if(roms.empty()){MessageBoxW(nullptr,L"No se encontró la carpeta roms. Coloca tu ROM de Pokémon Rumble World dentro de una carpeta 'roms' junto al ejecutable o al proyecto.",L"Promptemon Steidium",MB_OK|MB_ICONERROR);return 1;}
    if(!gArchive.open(roms)||gArchive.library().empty()){MessageBoxW(nullptr,L"La carpeta roms no contiene una ROM reconocible.",L"Promptemon Steidium",MB_OK|MB_ICONERROR);return 1;}
    gModelResources=gArchive.modelResources();
    if(gModelResources.empty()){MessageBoxW(nullptr,L"La ROM fue detectada, pero no se encontraron modelos 3D compatibles. El prototipo jugable necesita los recursos de Rumble World.",L"Promptemon Steidium",MB_OK|MB_ICONERROR);return 1;}

    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=nullptr;wc.lpszClassName=L"PromptemonPlayableGame";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Promptemon Steidium",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1000,700,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;ShowWindow(window,showCommand);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}
