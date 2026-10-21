#define main promptemon_resource_packager_v2_legacy_main
#include "promptemon_resource_packager_v2.cpp"
#undef main

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
            {"goosu", "gosu", "ghos", "gastly"},
            {"wanrikii", "machop"}
        }};

        for (const auto& keys : speciesKeys) {
            bool found = false;
            for (std::size_t i = 0; i < files.size(); ++i) {
                const auto& p = files[i].lowerPath;
                if (!startsWith(p, "pii/")) continue;
                for (const auto& key : keys) {
                    if (p.find(key) != std::string::npos) {
                        select(i);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) throw std::runtime_error("Falta un recurso Pokemon requerido: " + keys.front());
        }

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
        if (!foundSpanishUi) std::cout << "Aviso: no se encontro game2d.EU_Spanish.arc; se empaqueta la UI disponible\n";

        std::cout << "resources-tx: " << resourcesRepo << '\n';
        std::cout << "Origen: " << root << '\n';
        std::cout << "Escenarios stage/: " << stageCount << " archivos detectados\n";
        std::cout << "Fuente: font.arc\n";
        std::cout << "UI: game2d.EU_Spanish.arc + recursos UI detectados\n";
        std::cout << "Recursos seleccionados: " << selected.size() << " de " << files.size() << '\n';

        buildSelfExtractor(stub, output, files, selected);
        std::cout << "Autoextraible creado: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "promptemon_resource_packager: " << e.what() << '\n';
        return 1;
    }
}
