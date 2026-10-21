#include "bch/BchScene.h"
#include "ctr/Bcfnt.h"
#include "ctr/Bclim.h"
#include "rom/Lz11.h"
#include "rom/Lzh8.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

struct RankedFile {
    int score = std::numeric_limits<int>::min();
    std::size_t index = 0;
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

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool looksLikeBchPath(const std::string& p) {
    return endsWith(p, ".bch") || endsWith(p, ".bch.cx") || endsWith(p, ".bch.xc");
}

bool looksLikeUiPath(const std::string& p) {
    return contains(p, "ui/") || contains(p, "font") || contains(p, "common") ||
           contains(p, "menu") || contains(p, "button") || contains(p, "btn") ||
           contains(p, "window") || contains(p, "waku") || contains(p, "frame") ||
           contains(p, "panel") || contains(p, "message");
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("No se pudo abrir: " + path.string());
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) throw std::runtime_error("No se pudo medir: " + path.string());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw std::runtime_error("No se pudo leer: " + path.string());
    return data;
}

std::vector<std::uint8_t> decodeKnown(std::vector<std::uint8_t> data) {
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (promptemon::rom::Lzh8::isCompressed(data)) {
            data = promptemon::rom::Lzh8::decompress(data);
            continue;
        }
        if (promptemon::rom::Lz11::isCompressed(data)) {
            data = promptemon::rom::Lz11::decompress(data);
            continue;
        }
        break;
    }
    return data;
}

bool bchMagic(const std::vector<std::uint8_t>& data) {
    return data.size() >= 4 && data[0] == 'B' && data[1] == 'C' && data[2] == 'H' && data[3] == 0;
}

std::filesystem::path resolveRomFs(const std::filesystem::path& input) {
    std::error_code ec;
    const auto candidate = input / "files" / "romfs";
    if (std::filesystem::is_directory(candidate, ec) && !ec) return candidate;
    ec.clear();
    if (std::filesystem::is_directory(input, ec) && !ec && lower(input.filename().string()) == "romfs") return input;
    throw std::runtime_error("No se encontro files/romfs dentro de resources-tx");
}

std::vector<SourceFile> scanRomFs(const std::filesystem::path& romfs) {
    std::vector<SourceFile> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(romfs, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const auto relative = std::filesystem::relative(it->path(), romfs, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::string virtualPath = relative.generic_u8string();
        if (!virtualPath.empty()) out.push_back({it->path(), virtualPath, lower(virtualPath)});
    }
    if (ec) throw std::runtime_error("No se pudo recorrer files/romfs: " + ec.message());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.virtualPath < b.virtualPath;
    });
    return out;
}

int stageScore(const std::string& p) {
    if (contains(p, "pii/") || contains(p, "mii/") || contains(p, "ui/") ||
        contains(p, "icon") || contains(p, "font") || contains(p, "menu") ||
        contains(p, "effect") || contains(p, "/eff")) return -100000;
    int score = 0;
    if (contains(p, "stage/town1/ground_96")) score += 50000;
    if (contains(p, "stage/town1/rep_01")) score += 40000;
    if (contains(p, "stage/town1/")) score += 8000;
    if (contains(p, "stage/")) score += 1200;
    if (contains(p, "town")) score += 700;
    if (contains(p, "ground")) score += 500;
    if (contains(p, "field") || contains(p, "course") || contains(p, "area")) score += 300;
    if (contains(p, "castle") || contains(p, "forest") || contains(p, "plain")) score += 180;
    return score;
}

int effectScore(const std::string& p) {
    if (contains(p, "pii/") || contains(p, "mii/") || contains(p, "ui/")) return -100000;
    int score = 0;
    if (contains(p, "effect")) score += 1000;
    if (contains(p, "/eff") || contains(p, "eff_")) score += 850;
    if (contains(p, "hit") || contains(p, "impact")) score += 600;
    if (contains(p, "spark") || contains(p, "flash")) score += 450;
    if (contains(p, "waza") || contains(p, "attack")) score += 300;
    return score;
}

int fontScore(const std::string& p) {
    int score = 0;
    if (contains(p, ".bcfnt")) score += 500;
    if (contains(p, ".cfnt")) score += 420;
    if (contains(p, "standard")) score += 300;
    if (contains(p, "font")) score += 250;
    if (contains(p, "common")) score += 80;
    if (contains(p, "message") || contains(p, "text")) score += 60;
    if (contains(p, "debug")) score -= 300;
    return score;
}

int buttonScore(const std::string& p, int width, int height) {
    int score = 0;
    if (contains(p, "button")) score += 420;
    if (contains(p, "btn")) score += 380;
    if (contains(p, "select")) score += 150;
    if (contains(p, "menu")) score += 120;
    if (contains(p, "common")) score += 80;
    if (contains(p, "waku") || contains(p, "frame")) score += 50;
    if (contains(p, "ui/")) score += 180;
    if (contains(p, "icon") || contains(p, "logo") || contains(p, "title") || contains(p, "face")) score -= 250;
    if (contains(p, "bg") || contains(p, "background")) score -= 160;
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(1, height));
    if (aspect >= 1.8f && aspect <= 12.0f) score += 180; else score -= 120;
    if (width >= 48 && width <= 1024) score += 45;
    if (height >= 12 && height <= 180) score += 70;
    if (height > width) score -= 140;
    return score;
}

int panelScore(const std::string& p, int width, int height) {
    int score = 0;
    if (contains(p, "window")) score += 420;
    if (contains(p, "waku")) score += 360;
    if (contains(p, "frame")) score += 300;
    if (contains(p, "panel")) score += 280;
    if (contains(p, "message")) score += 180;
    if (contains(p, "common")) score += 100;
    if (contains(p, "ui/")) score += 180;
    if (contains(p, "button") || contains(p, "btn")) score += 30;
    if (contains(p, "icon") || contains(p, "logo") || contains(p, "title") || contains(p, "face")) score -= 250;
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(1, height));
    if (aspect >= 0.7f && aspect <= 8.0f) score += 80;
    if (width >= 64 && height >= 32) score += 70;
    if (width > 2048 || height > 2048) score -= 160;
    return score;
}

bool usableBch(const SourceFile& file) {
    try {
        const auto bytes = decodeKnown(readFile(file.absolute));
        if (!bchMagic(bytes)) return false;
        const auto scene = promptemon::bch::Scene::parse(bytes);
        return !scene.meshes().empty();
    } catch (...) {
        return false;
    }
}

std::size_t chooseRankedBch(const std::vector<SourceFile>& files,
                            int (*scoreFn)(const std::string&),
                            bool required) {
    std::vector<RankedFile> ranked;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!looksLikeBchPath(files[i].lowerPath)) continue;
        const int score = scoreFn(files[i].lowerPath);
        if (score > 0) ranked.push_back({score, i});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    const std::size_t limit = std::min<std::size_t>(ranked.size(), 96);
    for (std::size_t i = 0; i < limit; ++i) {
        if (usableBch(files[ranked[i].index])) return ranked[i].index;
    }
    if (required) throw std::runtime_error("No se encontro una escena BCH utilizable para el Hub");
    return files.size();
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
    for (unsigned i = 0; i < 8; ++i) b[i] = static_cast<char>((value >> (8u * i)) & 0xffu);
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

void writeEntry(std::ostream& out, const std::string& path, const std::vector<std::uint8_t>& bytes) {
    if (path.empty() || path.size() > 0xffffffffu) throw std::runtime_error("Ruta invalida en el paquete");
    writeU32(out, static_cast<std::uint32_t>(path.size()));
    writeU64(out, static_cast<std::uint64_t>(bytes.size()));
    out.write(path.data(), static_cast<std::streamsize>(path.size()));
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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
    if (packagePosition < std::streampos(0)) throw std::runtime_error("No se pudo determinar el offset del paquete");
    const auto packageOffset = static_cast<std::uint64_t>(static_cast<std::streamoff>(packagePosition));

    out.write(kPackageMagic, 8);
    writeU32(out, 1);
    writeU32(out, static_cast<std::uint32_t>(selected.size() + 1));

    std::ostringstream manifest;
    manifest << "# promptemon-resource-bundle-v1\n";
    manifest << "# selected\t" << selected.size() << "\n";
    manifest << "path\tsize\n";

    std::size_t done = 0;
    for (std::size_t index : selected) {
        auto bytes = decodeKnown(readFile(files[index].absolute));
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
        const auto romfs = resolveRomFs(resourcesRepo);
        const auto files = scanRomFs(romfs);
        if (files.empty()) throw std::runtime_error("resources-tx no contiene archivos en files/romfs");

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
                if (p.rfind("pii/", 0) != 0 || !looksLikeBchPath(p)) continue;
                for (const auto& key : keys) {
                    if (p.find(key) != std::string::npos) {
                        select(i);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) throw std::runtime_error("Falta un modelo Pokemon requerido: " + keys.front());
        }

        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& p = files[i].lowerPath;
            if (p.rfind("pii/", 0) == 0 && looksLikeBchPath(p) && contains(p, "anim")) select(i);
        }

        const std::size_t stageIndex = chooseRankedBch(files, stageScore, true);
        select(stageIndex);
        const std::string stageParent = lower(std::filesystem::path(files[stageIndex].virtualPath).parent_path().generic_u8string());
        if (!stageParent.empty()) {
            const std::string prefix = stageParent + "/";
            for (std::size_t i = 0; i < files.size(); ++i) {
                if (files[i].lowerPath.rfind(prefix, 0) == 0) select(i);
            }
        }

        const std::size_t effectIndex = chooseRankedBch(files, effectScore, false);
        if (effectIndex < files.size()) {
            select(effectIndex);
            const std::string parent = lower(std::filesystem::path(files[effectIndex].virtualPath).parent_path().generic_u8string());
            if (!parent.empty()) {
                for (std::size_t i = 0; i < files.size(); ++i) {
                    const auto& p = files[i].lowerPath;
                    if (p.rfind(parent + "/", 0) == 0 && looksLikeBchPath(p) && contains(p, "anim")) select(i);
                }
            }
        }

        RankedFile bestFont;
        RankedFile bestButton;
        RankedFile bestPanel;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto& file = files[i];
            const bool uiLike = looksLikeUiPath(file.lowerPath);

            try {
                const auto bytes = decodeKnown(readFile(file.absolute));

                // Fonts are detected by their actual BCFNT contents, regardless of filename/path.
                if (promptemon::ctr::isBcfnt(bytes)) {
                    const auto font = promptemon::ctr::decodeBcfnt(bytes);
                    if (font.valid()) {
                        const int score = fontScore(file.lowerPath);
                        if (score > bestFont.score) bestFont = {score, i};
                    }
                }

                // Button/panel candidates still need a UI-like path to avoid choosing arbitrary textures.
                if (!uiLike) continue;

                if (promptemon::ctr::isBclim(bytes)) {
                    const auto image = promptemon::ctr::decodeBclim(bytes);
                    if (image.valid()) {
                        const int bs = buttonScore(file.lowerPath, image.width, image.height);
                        const int ps = panelScore(file.lowerPath, image.width, image.height);
                        if (bs > bestButton.score) bestButton = {bs, i};
                        if (ps > bestPanel.score) bestPanel = {ps, i};
                    }
                } else if (bchMagic(bytes) && looksLikeBchPath(file.lowerPath)) {
                    const auto scene = promptemon::bch::Scene::parse(bytes);
                    for (const auto& texture : scene.textures()) {
                        if (texture.width <= 0 || texture.height <= 0 || texture.rgba.empty()) continue;
                        const std::string scorePath = file.lowerPath + "/" + lower(texture.name);
                        const int bs = buttonScore(scorePath, texture.width, texture.height);
                        const int ps = panelScore(scorePath, texture.width, texture.height);
                        if (bs > bestButton.score) bestButton = {bs, i};
                        if (ps > bestPanel.score) bestPanel = {ps, i};
                    }
                }
            } catch (...) {
            }
        }

        if (bestFont.score == std::numeric_limits<int>::min())
            throw std::runtime_error("No se encontro una fuente BCFNT utilizable");
        if (bestButton.score == std::numeric_limits<int>::min())
            throw std::runtime_error("No se encontro un recurso de boton utilizable");
        if (bestPanel.score == std::numeric_limits<int>::min())
            throw std::runtime_error("No se encontro un recurso de panel utilizable");

        select(bestFont.index);
        select(bestButton.index);
        select(bestPanel.index);

        std::cout << "resources-tx: " << resourcesRepo << '\n';
        std::cout << "Hub: " << files[stageIndex].virtualPath << '\n';
        std::cout << "Fuente: " << files[bestFont.index].virtualPath << '\n';
        std::cout << "Boton: " << files[bestButton.index].virtualPath << '\n';
        std::cout << "Panel: " << files[bestPanel.index].virtualPath << '\n';
        if (effectIndex < files.size()) std::cout << "Efecto: " << files[effectIndex].virtualPath << '\n';
        std::cout << "Recursos seleccionados: " << selected.size() << " de " << files.size() << '\n';

        buildSelfExtractor(stub, output, files, selected);
        std::cout << "Autoextraible creado: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "promptemon_resource_packager: " << e.what() << '\n';
        return 1;
    }
}
