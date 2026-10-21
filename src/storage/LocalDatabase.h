#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct sqlite3;

namespace promptemon::storage {

class LocalDatabase {
public:
    LocalDatabase() = default;
    ~LocalDatabase();

    LocalDatabase(const LocalDatabase&) = delete;
    LocalDatabase& operator=(const LocalDatabase&) = delete;

    bool open();
    void close();

    const std::filesystem::path& path() const noexcept { return path_; }

    std::optional<std::wstring> setting(const std::string& key) const;
    bool setSetting(const std::string& key, const std::wstring& value);

private:
    using Open16Fn = int (*)(const void*, sqlite3**);
    using CloseFn = int (*)(sqlite3*);
    using ExecCallback = int (*)(void*, int, char**, char**);
    using ExecFn = int (*)(sqlite3*, const char*, ExecCallback, void*, char**);
    using FreeFn = void (*)(void*);

    bool loadSqlite();
    bool exec(const std::string& sql, ExecCallback callback = nullptr, void* context = nullptr) const;
    static std::filesystem::path defaultDatabasePath();

    void* library_ = nullptr;
    sqlite3* db_ = nullptr;
    Open16Fn open16_ = nullptr;
    CloseFn close_ = nullptr;
    ExecFn exec_ = nullptr;
    FreeFn free_ = nullptr;
    std::filesystem::path path_;
};

} // namespace promptemon::storage
