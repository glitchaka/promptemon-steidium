#define main promptemon_resource_packager_v2_legacy_main
#include "promptemon_resource_packager_v2.cpp"
#undef main

namespace {
std::string parentVirtualPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "Uso: promptemon_resource_packager <resources-tx> <promptemon_resource_sfx.exe> <salida.exe>\n";
            return 2;
        }

        const std::filesystem::path resourcesRepo = std::filesystem::u8path(argv[1]);
        const std::filesystem::path stub = std::filesystem::u8path(argv[2]);
        const std::filesystem::path output = std::filesystem::u8path(argv[3]);
        const auto root = resolveSourceRoot(resourcesRepo);
        const auto files = scanFiles(root);
        if (files.empty()) throw std::runtime_error("resources-tx no contiene archivos utilizables");

        std::set<std::size_t> selected;
        auto select = [&](std::size_t index) {
            if (index < files.size()) selected.insert(index);
        };

        const std::array<std::vector<std::string>, 8> speciesKeys = {{
            {"koratta", "rattata"},
            {"poppo", "pidgey"},
            {"kyatapi", "caterpie"},
            {"biidoru", "weedle"},
            {"zubatto", "zubat"},
            {"ishitsubute", "isitubute", "geodude"},
            {"goosu", "gosu", "gastly"},
            {"wanrikii", "machop"}
        }};

        // A Pokemon's model and animation files are commonly siblings with generic
        // names such as model.bch / anim.bch. Selecting only paths that contain the
        // species name drops the animation. Find the species-bearing resource first,
        // then package the complete containing directory.
        for (const auto& keys : speciesKeys) {
            std::set<std::string> speciesDirectories;
            for (std::size_t i = 0; i < files.size(); ++i) {
                const auto& p = files[i].lowerPath;
                if (!startsWith(p, "pii/")) continue;
                for (const auto& key : keys) {
                    if (p.find(key) != std::string::npos) {
                        speciesDirectories.insert(parentVirtualPath(p));
                        break;
                    }
                }
            }
            if (speciesDirectories.empty()) {
                throw std::runtime_error("Falta un recurso Pokemon requerido: " + keys.front());
            }

            for (std::size_t i = 0; i < files.size(); ++i) {
                const auto& p = files[i].lowerPath;
                for (const auto& directory : speciesDirectories) {
                    if (!directory.empty() && startsWith(p, directory.c_str())) {
                        select(i);
                        break;
                    }
                }
            }
        }

        // Live title/menu scene: use the actual Rumble World Ho-Oh and Lugia models,
        // plus every companion file in their directories.
        bool foundHoOh = false;
        bool foundLugia = false;
        std::set<std::string> menuPokemonDirectories;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (!startsWith(p, "pii/")) continue;
            if (contains(p, "houou")) {
                menuPokemonDirectories.insert(parentVirtualPath(p));
                foundHoOh = true;
            }
            if (contains(p, "rugia") || contains(p, "lugia")) {
                menuPokemonDirectories.insert(parentVirtualPath(p));
                foundLugia = true;
            }
        }
        for (std::size_t i = 0; i < files.size(); ++i) {
            for (const auto& directory : menuPokemonDirectories) {
                if (!directory.empty() && startsWith(files[i].lowerPath, directory.c_str())) {
                    select(i);
                    break;
                }
            }
        }
        if (!foundHoOh) throw std::runtime_error("Falta pii/HOUOU para la escena inicial");
        if (!foundLugia) throw std::runtime_error("Falta pii/RUGIA para la escena inicial");

        // The starter/capture presentation uses the game's own Mii models. Package
        // the complete Mii directory so costumes/parts are not replaced by placeholders.
        std::size_t miiCount = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (startsWith(p, "mii/")) {
                select(i);
                ++miiCount;
            }
        }
        if (miiCount == 0) throw std::runtime_error("No se encontraron recursos Mii bajo mii/");

        std::size_t stageCount = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (startsWith(p, "stage/")) {
                select(i);
                ++stageCount;
            }
        }
        if (stageCount == 0) throw std::runtime_error("No se encontraron escenarios bajo stage/");

        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (p == "waza_ctr.ptcl" || contains(p, "effect/") || contains(p, "/eff/") || contains(p, "waza")) {
                select(i);
            }
        }

        bool foundFontArchive = false;
        bool foundSpanishUi = false;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (p == "font.arc" || p == "gaiji.arc") {
                select(i);
                if (p == "font.arc") foundFontArchive = true;
            }
            if (p == "game2d.eu_spanish.arc") {
                select(i);
                foundSpanishUi = true;
            }
            if (p == "boot2d.arc" || p == "flags.arc") select(i);
            if (contains(p, "ui/") || contains(p, "menu") || contains(p, "button") ||
                contains(p, "btn") || contains(p, "window") || contains(p, "waku") ||
                contains(p, "frame") || contains(p, "panel")) {
                select(i);
            }
        }

        if (!foundFontArchive) throw std::runtime_error("No se encontro font.arc en resources-tx");
        if (!foundSpanishUi) throw std::runtime_error("No se encontro game2d.EU_Spanish.arc en resources-tx");

        std::cout << "resources-tx: " << resourcesRepo << '\n';
        std::cout << "Origen: " << root << '\n';
        std::cout << "Escenarios stage/: " << stageCount << " archivos detectados\n";
        std::cout << "Mii/: " << miiCount << " archivos detectados\n";
        std::cout << "Pokemon: directorios completos con modelo + animaciones\n";
        std::cout << "Menu 3D: HOUOU + RUGIA\n";
        std::cout << "Fuente: font.arc\n";
        std::cout << "UI: game2d.EU_Spanish.arc + boot2d.arc + recursos UI detectados\n";
        std::cout << "Recursos seleccionados: " << selected.size() << " de " << files.size() << '\n';

        buildSelfExtractor(stub, output, files, selected);
        std::cout << "Autoextraible creado: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "promptemon_resource_packager: " << e.what() << '\n';
        return 1;
    }
}
