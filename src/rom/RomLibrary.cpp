#include "rom/RomLibrary.h"

#include "rom/IRomModelSource.h"

#include <algorithm>
#include <exception>
#include <system_error>
#include <unordered_set>

namespace promptemon::rom {

RomLibrary RomLibrary::scan(const std::filesystem::path& input) {
    return scan(std::vector<std::filesystem::path>{input});
}

RomLibrary RomLibrary::scan(const std::vector<std::filesystem::path>& inputs) {
    RomLibrary library;
    std::unordered_set<std::wstring> seen;

    auto addRom = [&](const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) return;
        if (!RomInspector::looksLikeRomFile(path)) return;

        const auto normalized = std::filesystem::weakly_canonical(path, ec);
        const std::filesystem::path keyPath = ec ? path.lexically_normal() : normalized;
        if (!seen.insert(keyPath.wstring()).second) return;

        RomLibraryEntry entry;
        try {
            entry.info = RomInspector::inspect(path);
        } catch (const std::exception& e) {
            entry.info.path = path;
            entry.modelError = e.what();
            library.entries_.push_back(std::move(entry));
            return;
        }

        try {
            auto source = openModelSource(entry.info);
            if (source) {
                entry.hasModelProvider = true;
                entry.modelProvider = source->providerName();
            } else {
                switch (entry.info.format) {
                    case RomFormat::GameBoy:
                    case RomFormat::GameBoyColor:
                    case RomFormat::Gba:
                    case RomFormat::NintendoDs:
                        entry.modelError = "ROM de datos: no contiene modelos 3D nativos compatibles";
                        break;
                    default:
                        entry.modelError = "Formato sin proveedor de modelos";
                        break;
                }
            }
        } catch (const std::exception& e) {
            entry.modelError = e.what();
        }
        library.entries_.push_back(std::move(entry));
    };

    for (const auto& input : inputs) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(input, ec) && !ec) {
            addRom(input);
            continue;
        }

        ec.clear();
        if (!std::filesystem::is_directory(input, ec) || ec) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 input,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            addRom(it->path());
        }
    }

    std::sort(library.entries_.begin(), library.entries_.end(), [](const auto& a, const auto& b) {
        if (a.hasModelProvider != b.hasModelProvider) return a.hasModelProvider > b.hasModelProvider;
        return a.info.path.filename().string() < b.info.path.filename().string();
    });
    return library;
}

} // namespace promptemon::rom
