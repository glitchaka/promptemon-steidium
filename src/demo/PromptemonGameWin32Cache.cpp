#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <ole2.h>

#include "demo/PrototypeRomRenderer.h"
#include "rom/RomModelArchive.h"
#include "storage/LocalDatabase.h"
#include "storage/RomAssetCache.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
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
using promptemon::storage::LocalDatabase;
using promptemon::storage::RomAssetCache;

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
