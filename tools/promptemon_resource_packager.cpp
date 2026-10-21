#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr char kPackageMagic[8] = {'P','M','P','K','G','0','0','1'};
constexpr char kSfxMagic[8] = {'P','M','S','F','X','0','0','1'};

struct Rule {
    enum class Kind { Prefix, Contains, Suffix, Basename };
    Kind kind = Kind::Contains;
    std::string value;
};

struct FileEntry {
    std::string packagePath;
    std::filesystem::path source;
    std::uint64_t size = 0;
};

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string normalize(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) value.erase(0, 2);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<Rule> loadRules(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("No se pudo abrir la lista de recursos: " + file.string());

    std::vector<Rule> rules;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;

        const auto split = line.find('=');
        if (split == std::string::npos) {
            throw std::runtime_error("Regla invalida en linea " + std::to_string(lineNumber));
        }

        const std::string key = normalize(trim(line.substr(0, split)));
        const std::string value = normalize(trim(line.substr(split + 1)));
        if (value.empty()) throw std::runtime_error("Regla vacia en linea " + std::to_string(lineNumber));

        Rule rule;
        if (key == "prefix") rule.kind = Rule::Kind::Prefix;
        else if (key == "contains") rule.kind = Rule::Kind::Contains;
        else if (key == "suffix") rule.kind = Rule::Kind::Suffix;
        else if (key == "basename") rule.kind = Rule::Kind::Basename;
        else throw std::runtime_error("Tipo de regla desconocido en linea " + std::to_string(lineNumber) + ": " + key);
        rule.value = value;
        rules.push_back(std::move(rule));
    }

    if (rules.empty()) throw std::runtime_error("La lista de recursos no contiene reglas");
    return rules;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool matches(const std::string& relativePath, const std::vector<Rule>& rules) {
    const std::string path = normalize(relativePath);
    const auto slash = path.find_last_of('/');
    const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);

    for (const auto& rule : rules) {
        switch (rule.kind) {
            case Rule::Kind::Prefix:
                if (path.rfind(rule.value, 0) == 0) return true;
                break;
            case Rule::Kind::Contains:
                if (path.find(rule.value) != std::string::npos) return true;
                break;
            case Rule::Kind::Suffix:
                if (endsWith(path, rule.value)) return true;
                break;
            case Rule::Kind::Basename:
                if (basename == rule.value) return true;
                break;
        }
    }
    return false;
}

std::optional<std::string> decodedEquivalent(const std::string& relativePath) {
    std::string path = normalize(relativePath);
    constexpr const char* filesPrefix = "files/";
    constexpr const char* decodedPrefix = "files/decoded/";
    if (path.rfind(decodedPrefix, 0) == 0 || path.rfind(filesPrefix, 0) != 0) return std::nullopt;

    std::string tail = path.substr(std::char_traits<char>::length(filesPrefix));
    if (endsWith(tail, ".cx") || endsWith(tail, ".xc")) {
        tail.resize(tail.size() - 3);
        return std::string(decodedPrefix) + tail;
    }
    return std::nullopt;
}

void writeU32(std::ostream& out, std::uint32_t value) {
    const char b[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu)
    };
    out.write(b, 4);
}

void writeU64(std::ostream& out, std::uint64_t value) {
    char b[8]{};
    for (unsigned i = 0; i < 8; ++i) b[i] = static_cast<char>((value >> (8u * i)) & 0xFFu);
    out.write(b, 8);
}

void copyStream(std::istream& in, std::ostream& out, std::uint64_t bytes) {
    std::vector<char> buffer(1024 * 1024);
    while (bytes > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(bytes, buffer.size()));
        in.read(buffer.data(), chunk);
        if (in.gcount() != chunk) throw std::runtime_error("Lectura incompleta al construir el paquete");
        out.write(buffer.data(), chunk);
        if (!out) throw std::runtime_error("Escritura incompleta al construir el paquete");
        bytes -= static_cast<std::uint64_t>(chunk);
    }
}

void writeEntryHeader(std::ostream& out, const std::string& path, std::uint64_t size) {
    if (path.empty() || path.size() > 0xFFFFFFFFull) throw std::runtime_error("Ruta invalida en el paquete");
    writeU32(out, static_cast<std::uint32_t>(path.size()));
    writeU64(out, size);
    out.write(path.data(), static_cast<std::streamsize>(path.size()));
}

std::string buildManifest(const std::vector<FileEntry>& files) {
    std::ostringstream out;
    out << "# promptemon-resource-bundle-v1\n";
    out << "# files\t" << files.size() << "\n";
    out << "path\tsize\n";
    for (const auto& file : files) out << file.packagePath << '\t' << file.size << '\n';
    return out.str();
}

void buildSelfExtractor(const std::filesystem::path& stub,
                        const std::filesystem::path& output,
                        const std::vector<FileEntry>& files) {
    std::ifstream stubIn(stub, std::ios::binary);
    if (!stubIn) throw std::runtime_error("No se pudo abrir el stub autoextraible: " + stub.string());

    std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("No se pudo crear: " + output.string());

    stubIn.seekg(0, std::ios::end);
    const auto stubLength = stubIn.tellg();
    if (stubLength < 0) throw std::runtime_error("No se pudo medir el stub autoextraible");
    stubIn.seekg(0, std::ios::beg);
    copyStream(stubIn, out, static_cast<std::uint64_t>(stubLength));

    const std::streampos packagePosition = out.tellp();
    if (packagePosition < std::streampos(0)) throw std::runtime_error("No se pudo determinar el offset del paquete");
    const auto packageOffset = static_cast<std::uint64_t>(static_cast<std::streamoff>(packagePosition));

    const std::string manifest = buildManifest(files);
    out.write(kPackageMagic, sizeof(kPackageMagic));
    writeU32(out, 1);
    writeU32(out, static_cast<std::uint32_t>(files.size() + 1));

    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        std::cout << '[' << (i + 1) << '/' << files.size() << "] " << file.packagePath << '\n';
        writeEntryHeader(out, file.packagePath, file.size);
        std::ifstream in(file.source, std::ios::binary);
        if (!in) throw std::runtime_error("No se pudo abrir recurso: " + file.source.string());
        copyStream(in, out, file.size);
    }

    writeEntryHeader(out, "bundle_manifest.tsv", static_cast<std::uint64_t>(manifest.size()));
    out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
    out.write(kSfxMagic, sizeof(kSfxMagic));
    writeU64(out, packageOffset);
    if (!out) throw std::runtime_error("Fallo al finalizar el autoextraible");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            std::cerr
                << "Uso: promptemon_resource_packager <resources-tx> <required_resources.txt> "
                   "<promptemon_resource_sfx.exe> <salida.exe>\n";
            return 2;
        }

        const std::filesystem::path resourcesRoot = std::filesystem::u8path(argv[1]);
        const std::filesystem::path rulesFile = std::filesystem::u8path(argv[2]);
        const std::filesystem::path stub = std::filesystem::u8path(argv[3]);
        const std::filesystem::path output = std::filesystem::u8path(argv[4]);

        std::error_code ec;
        if (!std::filesystem::is_directory(resourcesRoot / "files", ec) || ec) {
            throw std::runtime_error("La ruta no parece ser resources-tx: falta la carpeta files");
        }
        if (!std::filesystem::is_regular_file(stub, ec) || ec) {
            throw std::runtime_error("No existe el stub autoextraible: " + stub.string());
        }

        const auto rules = loadRules(rulesFile);
        std::vector<FileEntry> selected;
        std::unordered_set<std::string> selectedPaths;

        for (std::filesystem::recursive_directory_iterator it(resourcesRoot, ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            const auto relative = std::filesystem::relative(it->path(), resourcesRoot, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            const std::string relativeText = relative.generic_u8string();
            if (!matches(relativeText, rules)) continue;

            const std::uint64_t size = static_cast<std::uint64_t>(it->file_size(ec));
            if (ec) {
                ec.clear();
                continue;
            }
            selected.push_back({relativeText, it->path(), size});
            selectedPaths.insert(normalize(relativeText));
        }

        if (selected.empty()) throw std::runtime_error("Ningun recurso de resources-tx coincide con las reglas");

        std::vector<FileEntry> filtered;
        filtered.reserve(selected.size());
        for (auto& file : selected) {
            if (const auto decoded = decodedEquivalent(file.packagePath);
                decoded && selectedPaths.find(*decoded) != selectedPaths.end()) {
                continue;
            }
            filtered.push_back(std::move(file));
        }

        std::sort(filtered.begin(), filtered.end(), [](const FileEntry& a, const FileEntry& b) {
            return normalize(a.packagePath) < normalize(b.packagePath);
        });

        std::uint64_t totalBytes = 0;
        for (const auto& file : filtered) totalBytes += file.size;

        std::cout << "Seleccionados: " << filtered.size() << " archivos de resources-tx\n";
        std::cout << "Payload: " << (totalBytes / (1024.0 * 1024.0)) << " MiB\n";
        buildSelfExtractor(stub, output, filtered);
        std::cout << "Autoextraible creado: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "promptemon_resource_packager: " << e.what() << '\n';
        return 1;
    }
}
