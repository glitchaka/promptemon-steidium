#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "storage/LocalDatabase.h"

#include <string>
#include <vector>

namespace promptemon::storage {
namespace {

constexpr int SQLITE_OK_VALUE = 0;

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

std::string sqlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    return out;
}

int captureFirstColumn(void* context, int argc, char** argv, char**) {
    if (!context || argc < 1 || !argv || !argv[0]) return 0;
    *static_cast<std::string*>(context) = argv[0];
    return 0;
}

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::filesystem::current_path();
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

LocalDatabase::~LocalDatabase() {
    close();
}

bool LocalDatabase::loadSqlite() {
    if (library_) return true;
    HMODULE module = LoadLibraryW(L"winsqlite3.dll");
    if (!module) return false;

    open16_ = reinterpret_cast<Open16Fn>(GetProcAddress(module, "sqlite3_open16"));
    close_ = reinterpret_cast<CloseFn>(GetProcAddress(module, "sqlite3_close"));
    exec_ = reinterpret_cast<ExecFn>(GetProcAddress(module, "sqlite3_exec"));
    free_ = reinterpret_cast<FreeFn>(GetProcAddress(module, "sqlite3_free"));
    if (!open16_ || !close_ || !exec_ || !free_) {
        FreeLibrary(module);
        open16_ = nullptr;
        close_ = nullptr;
        exec_ = nullptr;
        free_ = nullptr;
        return false;
    }

    library_ = module;
    return true;
}

std::filesystem::path LocalDatabase::defaultDatabasePath() {
    return executableDirectory() / L"player.db";
}

bool LocalDatabase::open() {
    close();
    if (!loadSqlite()) return false;

    path_ = defaultDatabasePath();
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) return false;

    sqlite3* opened = nullptr;
    if (open16_(path_.c_str(), &opened) != SQLITE_OK_VALUE || !opened) {
        if (opened && close_) close_(opened);
        return false;
    }
    db_ = opened;

    return exec(
        "CREATE TABLE IF NOT EXISTS settings ("
        "key TEXT PRIMARY KEY NOT NULL, "
        "value TEXT NOT NULL"
        ");");
}

void LocalDatabase::close() {
    if (db_ && close_) close_(db_);
    db_ = nullptr;
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
    open16_ = nullptr;
    close_ = nullptr;
    exec_ = nullptr;
    free_ = nullptr;
}

bool LocalDatabase::exec(const std::string& sql, ExecCallback callback, void* context) const {
    if (!db_ || !exec_) return false;
    char* error = nullptr;
    const int result = exec_(db_, sql.c_str(), callback, context, &error);
    if (error && free_) free_(error);
    return result == SQLITE_OK_VALUE;
}

std::optional<std::wstring> LocalDatabase::setting(const std::string& key) const {
    std::string value;
    const std::string sql = "SELECT value FROM settings WHERE key='" + sqlEscape(key) + "' LIMIT 1;";
    if (!exec(sql, captureFirstColumn, &value) || value.empty()) return std::nullopt;
    return fromUtf8(value);
}

bool LocalDatabase::setSetting(const std::string& key, const std::wstring& value) {
    const std::string utf8 = toUtf8(value);
    const std::string sql =
        "INSERT OR REPLACE INTO settings(key,value) VALUES('" +
        sqlEscape(key) + "','" + sqlEscape(utf8) + "');";
    return exec(sql);
}

} // namespace promptemon::storage
