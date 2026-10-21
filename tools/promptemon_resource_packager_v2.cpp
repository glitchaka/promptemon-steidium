#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr char kPackageMagic[8] = {'P','M','P','K','G','0','0','1'};
constexpr char kSfxMagic[8] = {'P','M','S','F','X','0','0','1'};

struct SourceFile {
    std::filesystem::path absolute;
    std::string virtualPath;
    std::string lowerPath;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

bool startsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::filesystem::path resolveSourceRoot(const std::filesystem::path& input) {
    std::error_code ec;

    const auto decoded = input / "files" / "decoded" / "romfs";
    if (std::filesystem::is_directory(decoded, ec) && !ec) {
        std::cout << "Usando recursos decodificados: " << decoded << '\n';
        return decoded;
    }

    ec.clear();
    const auto romfs = input / "files" / "romfs";
    if (std::filesystem::is_directory(romfs, ec) && !ec) {
        std::cout << "Aviso: files/decoded/romfs no existe; usando files/romfs\n";
        return romfs;
    }

    throw std::runtime_error("No se encontro files/decoded/romfs ni files/romfs dentro de resources-tx");
}

std::vector<SourceFile> scanFiles(const std::filesystem::path& root) {
    std::vector<SourceFile> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const auto relative = std::filesystem::relative(it->path(), root, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const std::string virtualPath = relative.generic_u8string();
        if (!virtualPath.empty()) {
            out.push_back({it->path(), virtualPath, lower(virtualPath)});
        }
    }

    if (ec) throw std::runtime_error("No se pudo recorrer el arbol de recursos: " + ec.message());

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.virtualPath < b.virtualPath;
    });
    return out;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("No se pudo abrir: " + path.string());
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) throw std::runtime_error("No se pudo medir: " + path.string());
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!in && !data.empty()) throw std::runtime_error("No se pudo leer: " + path.string());
    return data;
}

void writeU32(std::ostream& out, std::uint32_t value) {
    const char b[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8) & 0xffu),
        static_cast<char>((value >> 16) & 0xffu),
        static_cast<char>((value >> 24) & 0xffu)
    };
    out.write(b, 4);
}

void writeU64(std::ostream& out, std::uint64_t value) {
    char b[8]{};
    for (unsigned i = 0; i < 8; ++i) {
        b[i] = static_cast<char>((value >> (8u * i)) & 0xffu);
    }
    out.write(b, 8);
}

void copyStream(std::istream& in, std::ostream& out) {
    std::vector<char> buffer(1024 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got > 0) out.write(buffer.data(), got);
    }
}

void writeEntry(std::ostream& out,
                const std::string& path,
                const std::vector<std::uint8_t>& bytes) {
    if (path.empty() || path.size() > 0xffffffffu) {
        throw std::runtime_error("Ruta invalida en el paquete");
    }

    writeU32(out, static_cast<std::uint32_t>(path.size()));
    writeU64(out, static_cast<std::uint64_t>(bytes.size()));
    out.write(path.data(), static_cast<std::streamsize>(path.size()));
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out) throw std::runtime_error("No se pudo escribir el paquete");
}

void buildSelfExtractor(const std::filesystem::path& stub,
                        const std::filesystem::path& output,
                        const std::vector<SourceFile>& files,
                        const std::set<std::size_t>& selected) {
    std::ifstream stubIn(stub, std::ios::binary);
    if (!stubIn) throw std::runtime_error("No se pudo abrir el stub autoextraible: " + stub.string());

    std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("No se pudo crear: " + output.string());

    copyStream(stubIn, out);

    const auto packagePosition = out.tellp();
    if (packagePosition < std::streampos(0)) {
        throw std::runtime_error("No se pudo determinar el offset del paquete");
    }
    const auto packageOffset = static_cast<std::uint64_t>(static_cast<std::streamoff>(packagePosition));

    out.write(kPackageMagic, 8);
    writeU32(out, 1);
    writeU32(out, static_cast<std::uint32_t>(selected.size() + 1));

    std::ostringstream manifest;
    manifest << "# promptemon-resource-bundle-v2\n";
    manifest << "# selected\t" << selected.size() << "\n";
    manifest << "path\tsize\n";

    std::size_t done = 0;
    for (std::size_t index : selected) {
        const auto bytes = readFile(files[index].absolute);
        const std::string packagePath = "files/romfs/" + files[index].virtualPath;
        writeEntry(out, packagePath, bytes);
        manifest << packagePath << '\t' << bytes.size() << '\n';
        ++done;
        std::cout << '[' << done << '/' << selected.size() << "] " << files[index].virtualPath << '\n';
    }

    const std::string manifestText = manifest.str();
    const std::vector<std::uint8_t> manifestBytes(manifestText.begin(), manifestText.end());
    writeEntry(out, "bundle_manifest.tsv", manifestBytes);

    out.write(kSfxMagic, 8);
    writeU64(out, packageOffset);
    if (!out) throw std::runtime_error("No se pudo finalizar el autoextraible");
}

} // namespace

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
            if (!found) {
                throw std::runtime_error("Falta un recurso Pokemon requerido: " + keys.front());
            }
        }

        bool foundTown = false;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (startsWith(p, "stage/town1/")) {
                select(i);
                foundTown = true;
            }
        }
        if (!foundTown) {
            throw std::runtime_error("No se encontro stage/town1 para el Hub");
        }

        // Efectos principales usados por combate. El PTCL es un contenedor y debe
        // conservarse completo; no se exige que cada efecto interno tenga nombre propio.
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (p == "waza_ctr.ptcl" || contains(p, "effect/") || contains(p, "/eff/")) {
                select(i);
            }
        }

        // En Rumble World las fuentes y buena parte de la UI no viven como BCFNT/BCLIM
        // sueltos: estan dentro de ARC. Seleccionamos los contenedores reales en vez de
        // fallar buscando una BCFNT inexistente en el arbol de archivos.
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

            if (p == "boot2d.arc" || p == "flags.arc") {
                select(i);
            }

            // Mantener tambien cualquier recurso UI que si haya sido extraido suelto.
            if (contains(p, "ui/") || contains(p, "menu") || contains(p, "button") ||
                contains(p, "btn") || contains(p, "window") || contains(p, "waku") ||
                contains(p, "frame") || contains(p, "panel")) {
                select(i);
            }
        }

        if (!foundFontArchive) {
            throw std::runtime_error("No se encontro font.arc en resources-tx");
        }
        if (!foundSpanishUi) {
            // No abortar por idioma: la UI puede seguir funcionando con el resto del bundle.
            std::cout << "Aviso: no se encontro game2d.EU_Spanish.arc; se empaqueta la UI disponible\n";
        }

        std::cout << "resources-tx: " << resourcesRepo << '\n';
        std::cout << "Origen: " << root << '\n';
        std::cout << "Fuente: font.arc (contenedor real del juego)\n";
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
