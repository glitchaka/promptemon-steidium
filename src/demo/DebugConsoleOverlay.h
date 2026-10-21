#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace promptemon::demo {

class DebugConsoleOverlay {
public:
    using Executor = std::function<std::string(const std::string&)>;

    DebugConsoleOverlay() = default;
    ~DebugConsoleOverlay();

    bool create(HWND parent, Executor executor);
    void destroy();
    void toggle();
    void show(bool visible);
    void resize();
    void append(const std::string& text);
    bool visible() const noexcept { return visible_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR subclassId, DWORD_PTR refData);

    void layout();
    void submit();
    void appendWide(const std::wstring& text);
    static std::wstring utf8ToWide(const std::string& text);

    HWND parent_ = nullptr;
    HWND window_ = nullptr;
    HWND output_ = nullptr;
    HWND input_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    Executor executor_;
    bool visible_ = false;
};

} // namespace promptemon::demo
