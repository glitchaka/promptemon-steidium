#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

namespace promptemon::demo {

class ExtractionProgressWindow {
public:
    bool create();
    void close();
    void update(std::size_t current, std::size_t total, const std::string& resourcePath);

private:
    static std::wstring utf8ToWide(const std::string& value);
    void pumpMessages();

    HWND window_ = nullptr;
    HWND status_ = nullptr;
    HWND progress_ = nullptr;
};

} // namespace promptemon::demo
