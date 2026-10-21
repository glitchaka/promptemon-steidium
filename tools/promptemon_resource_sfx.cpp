#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr char kPackageMagic[8] = {'P','M','P','K','G','0','0','1'};
constexpr char kSfxMagic[8] = {'P','M','S','F','X','0','0','1'};

std::uint32_t readU32(std::istream& in) {
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) throw std::runtime_error("Fin inesperado del paquete");
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint64_t readU64(std::istream& in) {
    unsigned char b[8]{};
    in.read(reinterpret_cast<char*>(b), 8);
    if (!in) throw std::runtime_error("Fin inesperado del paquete");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(b[i]) << (8u * i);
    return value;
}

bool equalMagic(const char* a, const char* b) {
    return std::equal(a, a + 8, b);
}

std::uint64_t packageOffset(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < static_cast<std::streamoff>(16)) throw std::runtime_error("El ejecutable no contiene payload");

    in.seekg(end - static_cast<std::streamoff>(16), std::ios::beg);
    char magic[8]{};
    in.read(magic, 8);
    if (!in || !equalMagic(magic, kSfxMagic)) throw std::runtime_error("El ejecutable no contiene un bundle Promptemon valido");
    return readU64(in);
}

bool safeRelative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const auto& part : path) {
        if (part == "." || part == "..") return false;
    }
    return true;
}

void copyBytes(std::istream& in, std::ostream& out, std::uint64_t bytes) {
    std::vector<char> buffer(1024 * 1024);
    while (bytes > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(bytes, buffer.size()));
        in.read(buffer.data(), chunk);
        if (in.gcount() != chunk) throw std::runtime_error("Payload truncado");
        out.write(buffer.data(), chunk);
        if (!out) throw std::runtime_error("No se pudo escribir el recurso extraido");
        bytes -= static_cast<std::uint64_t>(chunk);
    }
}

std::filesystem::path defaultOutput(const std::filesystem::path& executable) {
    return executable.parent_path() / "assets" / "resources-tx";
}

std::size_t extract(const std::filesystem::path& executable,
                    const std::filesystem::path& output) {
    if (output.empty() || output == output.root_path()) {
        throw std::runtime_error("Ruta de salida insegura");
    }

    std::ifstream in(executable, std::ios::binary);
    if (!in) throw std::runtime_error("No se pudo abrir el autoextraible");
    const std::uint64_t offset = packageOffset(in);

    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    char magic[8]{};
    in.read(magic, 8);
    if (!in || !equalMagic(magic, kPackageMagic)) throw std::runtime_error("Offset de bundle invalido");
    const std::uint32_t version = readU32(in);
    if (version != 1) throw std::runtime_error("Version de bundle no soportada");
    const std::uint32_t count = readU32(in);

    std::error_code ec;
    std::filesystem::remove_all(output, ec);
    ec.clear();
    std::filesystem::create_directories(output, ec);
    if (ec) throw std::runtime_error("No se pudo crear la carpeta de recursos: " + ec.message());

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t pathLength = readU32(in);
        const std::uint64_t size = readU64(in);
        if (!pathLength || pathLength > 1024 * 1024) throw std::runtime_error("Ruta invalida en bundle");

        std::string pathText(pathLength, '\0');
        in.read(pathText.data(), static_cast<std::streamsize>(pathText.size()));
        if (!in) throw std::runtime_error("Payload truncado al leer una ruta");

        const std::filesystem::path relative = std::filesystem::u8path(pathText).lexically_normal();
        if (!safeRelative(relative)) throw std::runtime_error("Ruta insegura en bundle: " + pathText);
        const auto destination = output / relative;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) throw std::runtime_error("No se pudo crear: " + destination.parent_path().string());

        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("No se pudo crear: " + destination.string());
        copyBytes(in, out, size);
        std::cout << '[' << (i + 1) << '/' << count << "] " << relative.generic_u8string() << '\n';
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path executable = std::filesystem::absolute(
            argc > 0 ? std::filesystem::u8path(argv[0]) : std::filesystem::path{});
        const std::filesystem::path output = argc > 1
            ? std::filesystem::absolute(std::filesystem::u8path(argv[1]))
            : defaultOutput(executable);

        const auto count = extract(executable, output);
        std::cout << "Extraidos " << count << " archivos en " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PromptemonResources: " << e.what() << '\n';
        return 1;
    }
}
