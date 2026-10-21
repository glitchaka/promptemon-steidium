#include "demo/DebugConsoleOverlay.h"

#include <commctrl.h>

#include <algorithm>
#include <vector>

namespace promptemon::demo {
namespace {

constexpr wchar_t kConsoleClass[] = L"PromptemonDebugConsoleOverlay";
constexpr UINT_PTR kInputSubclassId = 0x504D434F;

COLORREF backgroundColor() { return RGB(72, 76, 72); }
COLORREF textColor() { return RGB(225, 225, 225); }
COLORREF inputColor() { return RGB(58, 61, 58); }

} // namespace

DebugConsoleOverlay::~DebugConsoleOverlay() {
    destroy();
}

std::wstring DebugConsoleOverlay::utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring(text.begin(), text.end());
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

bool DebugConsoleOverlay::create(HWND parent, Executor executor) {
    destroy();
    parent_ = parent;
    executor_ = std::move(executor);
    if (!parent_) return false;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = WindowProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kConsoleClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }

    background_ = CreateSolidBrush(backgroundColor());
    font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    window_ = CreateWindowExW(WS_EX_LAYERED,
                              kConsoleClass,
                              L"",
                              WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                              0, 0, 1, 1,
                              parent_, nullptr, instance, this);
    if (!window_) return false;

    SetLayeredWindowAttributes(window_, 0, 238, LWA_ALPHA);

    output_ = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                              ES_AUTOVSCROLL | WS_VSCROLL,
                              0, 0, 1, 1,
                              window_, nullptr, instance, nullptr);
    input_ = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 1, 1,
                             window_, nullptr, instance, nullptr);
    if (!output_ || !input_) return false;

    SendMessageW(output_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(input_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SetWindowSubclass(input_, InputProc, kInputSubclassId, reinterpret_cast<DWORD_PTR>(this));

    append("Promptemon Steidium developer console\r\n");
    append("Ctrl+Shift+C abre/cierra | Esc cierra | help lista comandos\r\n");
    resize();
    ShowWindow(window_, SW_HIDE);
    return true;
}

void DebugConsoleOverlay::destroy() {
    if (input_) RemoveWindowSubclass(input_, InputProc, kInputSubclassId);
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    output_ = nullptr;
    input_ = nullptr;
    parent_ = nullptr;
    visible_ = false;
    if (font_) DeleteObject(font_);
    if (background_) DeleteObject(background_);
    font_ = nullptr;
    background_ = nullptr;
    executor_ = {};
}

void DebugConsoleOverlay::toggle() {
    show(!visible_);
}

void DebugConsoleOverlay::show(bool visible) {
    visible_ = visible;
    if (!window_) return;
    resize();
    ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        BringWindowToTop(window_);
        SetFocus(input_);
        SendMessageW(input_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    } else if (parent_) {
        SetFocus(parent_);
    }
}

void DebugConsoleOverlay::resize() {
    if (!window_ || !parent_) return;
    RECT rc{};
    GetClientRect(parent_, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(240, static_cast<int>((rc.bottom - rc.top) * 0.66));
    SetWindowPos(window_, HWND_TOP, 0, 0, width, height, SWP_NOACTIVATE);
    layout();
}

void DebugConsoleOverlay::layout() {
    if (!window_ || !output_ || !input_) return;
    RECT rc{};
    GetClientRect(window_, &rc);
    constexpr int pad = 12;
    constexpr int inputHeight = 30;
    MoveWindow(output_, pad, pad,
               std::max(1, rc.right - pad * 2),
               std::max(1, rc.bottom - inputHeight - pad * 3), TRUE);
    MoveWindow(input_, pad, rc.bottom - inputHeight - pad,
               std::max(1, rc.right - pad * 2), inputHeight, TRUE);
}

void DebugConsoleOverlay::appendWide(const std::wstring& text) {
    if (!output_) return;
    SendMessageW(output_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(output_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(output_, EM_SCROLLCARET, 0, 0);
}

void DebugConsoleOverlay::append(const std::string& text) {
    appendWide(utf8ToWide(text));
}

void DebugConsoleOverlay::submit() {
    if (!input_ || !executor_) return;
    const int length = GetWindowTextLengthW(input_);
    if (length <= 0) return;
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(input_, wide.data(), length + 1);
    SetWindowTextW(input_, L"");

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
    std::string command(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, command.data(), bytes, nullptr, nullptr);

    append("> " + command + "\r\n");
    std::string result = executor_(command);
    if (!result.empty()) {
        append(result);
        if (result.size() < 2 || result.substr(result.size() - 2) != "\r\n") append("\r\n");
    }
}

LRESULT CALLBACK DebugConsoleOverlay::InputProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<DebugConsoleOverlay*>(refData);
    if (self) {
        if (msg == WM_KEYDOWN && wp == VK_RETURN) {
            self->submit();
            return 0;
        }
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            self->show(false);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DebugConsoleOverlay::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<DebugConsoleOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<DebugConsoleOverlay*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_SIZE:
            if (self) self->layout();
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc,
                     self && self->background_ ? self->background_ : GetSysColorBrush(COLOR_3DFACE));
            return 1;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, textColor());
            SetBkColor(dc, inputColor());
            static HBRUSH editBrush = CreateSolidBrush(inputColor());
            return reinterpret_cast<LRESULT>(editBrush);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace promptemon::demo
