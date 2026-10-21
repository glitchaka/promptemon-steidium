#include "demo/RomUiSkin.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <string>

namespace promptemon::demo {
namespace {

struct FallbackFont {
    GLuint base = 0;
    int pixelHeight = 0;
};

std::array<FallbackFont, 3> gFonts{};

int fontHeight(int kind) {
    return kind == 2 ? 34 : (kind == 1 ? 20 : 16);
}

int fontIndex(int kind) {
    return kind == 2 ? 2 : (kind == 1 ? 1 : 0);
}

GLuint ensureFont(int kind) {
    auto& slot = gFonts[static_cast<std::size_t>(fontIndex(kind))];
    if (slot.base) return slot.base;

    HDC dc = wglGetCurrentDC();
    if (!dc) return 0;

    const int px = fontHeight(kind);
    const int weight = kind == 2 ? FW_SEMIBOLD : (kind == 1 ? FW_MEDIUM : FW_NORMAL);
    HFONT font = CreateFontW(-px, 0, 0, 0, weight,
                             FALSE, FALSE, FALSE,
                             ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_SWISS,
                             L"Segoe UI");
    if (!font) return 0;

    HGDIOBJ previous = SelectObject(dc, font);
    GLuint base = glGenLists(256);
    const BOOL ok = base ? wglUseFontBitmapsW(dc, 0, 256, base) : FALSE;
    SelectObject(dc, previous);
    DeleteObject(font);

    if (!ok) {
        if (base) glDeleteLists(base, 256);
        return 0;
    }

    slot.base = base;
    slot.pixelHeight = px;
    return base;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(1252, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0, "?", nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(1252, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), n, "?", nullptr);
    return out;
}

void roundedRect(float x, float y, float w, float h, float radius,
                 float r, float g, float b, float a) {
    const float rr = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);

    glBegin(GL_POLYGON);
    constexpr int segments = 6;
    const float pi = 3.1415926535f;
    const struct Corner { float cx, cy, start; } corners[4] = {
        {x + w - rr, y + rr, -0.5f * pi},
        {x + w - rr, y + h - rr, 0.0f},
        {x + rr, y + h - rr, 0.5f * pi},
        {x + rr, y + rr, pi}
    };
    for (const auto& c : corners) {
        for (int i = 0; i <= segments; ++i) {
            const float angle = c.start + (0.5f * pi * static_cast<float>(i) / static_cast<float>(segments));
            glVertex2f(c.cx + std::cos(angle) * rr, c.cy + std::sin(angle) * rr);
        }
    }
    glEnd();
}

void roundedOutline(float x, float y, float w, float h, float radius,
                    float r, float g, float b, float a) {
    const float rr = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    glLineWidth(1.0f);

    glBegin(GL_LINE_LOOP);
    constexpr int segments = 6;
    const float pi = 3.1415926535f;
    const struct Corner { float cx, cy, start; } corners[4] = {
        {x + w - rr, y + rr, -0.5f * pi},
        {x + w - rr, y + h - rr, 0.0f},
        {x + rr, y + h - rr, 0.5f * pi},
        {x + rr, y + rr, pi}
    };
    for (const auto& c : corners) {
        for (int i = 0; i <= segments; ++i) {
            const float angle = c.start + (0.5f * pi * static_cast<float>(i) / static_cast<float>(segments));
            glVertex2f(c.cx + std::cos(angle) * rr, c.cy + std::sin(angle) * rr);
        }
    }
    glEnd();
}

void releaseFonts() {
    for (auto& font : gFonts) {
        if (font.base) glDeleteLists(font.base, 256);
        font = {};
    }
}

} // namespace

bool RomUiSkin::load(storage::RomAssetCache& assets) {
    release();
    std::ofstream diagnostic(assets.root() / "ui_scan.tsv", std::ios::binary | std::ios::trunc);
    if (diagnostic) {
        diagnostic << "kind\tstatus\twidth\theight\tresource\n";
        diagnostic << "SUMMARY\tresources=" << assets.resources().size()
                   << ";recognized_fonts=0;decoded_fonts=0;recognized_bclim=0;decoded_bclim=0"
                   << "\t0\t0\tfont=;button=;panel=\n";
    }
    return true;
}

void RomUiSkin::release() {
    releaseFonts();
    font_ = {};
    fontTextures_.clear();
    buttonTexture_ = 0;
    panelTexture_ = 0;
    fontReady_ = false;
    fontResource_.clear();
    buttonResource_.clear();
    panelResource_.clear();
}

void RomUiSkin::drawPanel(float x, float y, float w, float h, float alpha) const {
    roundedRect(x, y, w, h, 10.0f, 0.035f, 0.055f, 0.078f, alpha * 0.90f);
    roundedOutline(x, y, w, h, 10.0f, 0.42f, 0.52f, 0.62f, alpha * 0.34f);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.45f, 0.72f, 0.92f, alpha * 0.75f);
    glBegin(GL_QUADS);
    glVertex2f(x + 12.0f, y + 1.0f);
    glVertex2f(x + std::min(w - 12.0f, 86.0f), y + 1.0f);
    glVertex2f(x + std::min(w - 12.0f, 86.0f), y + 3.0f);
    glVertex2f(x + 12.0f, y + 3.0f);
    glEnd();
}

void RomUiSkin::drawButton(float x, float y, float w, float h, bool hovered, bool enabled) const {
    if (!enabled) {
        roundedRect(x, y, w, h, 8.0f, 0.055f, 0.065f, 0.075f, 0.48f);
        roundedOutline(x, y, w, h, 8.0f, 0.30f, 0.33f, 0.36f, 0.20f);
        return;
    }

    if (hovered) {
        roundedRect(x, y, w, h, 8.0f, 0.11f, 0.22f, 0.31f, 0.97f);
        roundedOutline(x, y, w, h, 8.0f, 0.58f, 0.82f, 1.0f, 0.78f);
    } else {
        roundedRect(x, y, w, h, 8.0f, 0.060f, 0.090f, 0.12f, 0.90f);
        roundedOutline(x, y, w, h, 8.0f, 0.36f, 0.47f, 0.58f, 0.36f);
    }

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.42f, 0.74f, 0.96f, hovered ? 1.0f : 0.72f);
    glBegin(GL_QUADS);
    glVertex2f(x + 1.0f, y + 9.0f);
    glVertex2f(x + 4.0f, y + 9.0f);
    glVertex2f(x + 4.0f, y + h - 9.0f);
    glVertex2f(x + 1.0f, y + h - 9.0f);
    glEnd();
}

float RomUiSkin::fontScale(int) const {
    return 1.0f;
}

float RomUiSkin::textWidth(const std::wstring& text, int fontKind) const {
    if (text.empty()) return 0.0f;
    HDC dc = wglGetCurrentDC();
    if (!dc) return static_cast<float>(text.size() * fontHeight(fontKind)) * 0.50f;

    const int px = fontHeight(fontKind);
    const int weight = fontKind == 2 ? FW_SEMIBOLD : (fontKind == 1 ? FW_MEDIUM : FW_NORMAL);
    HFONT font = CreateFontW(-px, 0, 0, 0, weight,
                             FALSE, FALSE, FALSE, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!font) return static_cast<float>(text.size() * px) * 0.50f;

    HGDIOBJ old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    DeleteObject(font);
    return static_cast<float>(size.cx);
}

void RomUiSkin::drawText(float x, float baselineY, const std::wstring& text,
                         int fontKind, bool centered) const {
    if (text.empty()) return;
    const GLuint base = ensureFont(fontKind);
    if (!base) return;

    const std::string bytes = narrow(text);
    if (bytes.empty()) return;
    if (centered) x -= textWidth(text, fontKind) * 0.5f;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glRasterPos2f(x + 1.0f, baselineY + 1.0f);
    glListBase(base);
    glCallLists(static_cast<GLsizei>(bytes.size()), GL_UNSIGNED_BYTE, bytes.data());

    glColor4f(0.94f, 0.96f, 0.98f, 1.0f);
    glRasterPos2f(x, baselineY);
    glListBase(base);
    glCallLists(static_cast<GLsizei>(bytes.size()), GL_UNSIGNED_BYTE, bytes.data());
}

} // namespace promptemon::demo
