#include "demo/ExtractionProgressWindow.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

namespace promptemon::demo {

std::wstring ExtractionProgressWindow::utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

bool ExtractionProgressWindow::create() {
    close();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    const int width = 680;
    const int height = 180;
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);

    window_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"STATIC",
        L"Promptemon Steidium - Extrayendo recursos",
        WS_OVERLAPPED | WS_CAPTION,
        x, y, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) return false;

    CreateWindowExW(0, L"STATIC", L"Extrayendo recursos de Pokemon Rumble World...",
                    WS_CHILD | WS_VISIBLE,
                    24, 20, width - 48, 24,
                    window_, nullptr, GetModuleHandleW(nullptr), nullptr);

    status_ = CreateWindowExW(0, L"STATIC", L"Preparando...",
                              WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                              24, 55, width - 48, 24,
                              window_, nullptr, GetModuleHandleW(nullptr), nullptr);

    progress_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                24, 95, width - 48, 24,
                                window_, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!status_ || !progress_) {
        close();
        return false;
    }

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    pumpMessages();
    return true;
}

void ExtractionProgressWindow::close() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    status_ = nullptr;
    progress_ = nullptr;
}

void ExtractionProgressWindow::update(std::size_t current,
                                      std::size_t total,
                                      const std::string& resourcePath) {
    if (!window_ || !progress_ || !status_) return;

    const int maximum = static_cast<int>(std::min<std::size_t>(total, 0x7fffffff));
    const int position = static_cast<int>(std::min<std::size_t>(current, static_cast<std::size_t>(std::max(0, maximum))));
    SendMessageW(progress_, PBM_SETRANGE32, 0, maximum);
    SendMessageW(progress_, PBM_SETPOS, position, 0);

    std::wstring text;
    if (total != 0) {
        text = L"[" + std::to_wstring(std::min(current + 1, total)) + L" / " + std::to_wstring(total) + L"] ";
    }
    if (!resourcePath.empty()) text += utf8ToWide(resourcePath);
    else text += L"Finalizando cache...";

    if (text.size() > 110) text = L"..." + text.substr(text.size() - 107);
    SetWindowTextW(status_, text.c_str());
    UpdateWindow(window_);
    pumpMessages();
}

void ExtractionProgressWindow::pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace promptemon::demo
