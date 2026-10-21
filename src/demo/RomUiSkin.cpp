#include "demo/RomUiSkin.h"

#include "bch/BchScene.h"
#include "ctr/Bclim.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace promptemon::demo {
namespace {

bool isBch(const std::vector<std::uint8_t>& data) {
    return data.size() >= 4 && data[0] == 'B' && data[1] == 'C' && data[2] == 'H' && data[3] == 0;
}

ctr::Image imageFromBchTexture(const bch::Texture& texture) {
    ctr::Image image;
    image.width = texture.width;
    image.height = texture.height;
    image.rgba = texture.rgba;
    return image;
}

struct FallbackFont {
    GLuint base = 0;
    int pixelHeight = 0;
};

std::array<FallbackFont, 3> gFallbackFonts{};

int fallbackPixelHeight(int fontKind) {
    return fontKind == 2 ? 38 : (fontKind == 1 ? 23 : 17);
}

int fallbackIndex(int fontKind) {
    return fontKind == 2 ? 2 : (fontKind == 1 ? 1 : 0);
}

GLuint ensureFallbackFont(int fontKind) {
    const int index = fallbackIndex(fontKind);
    auto& cached = gFallbackFonts[static_cast<std::size_t>(index)];
    if (cached.base) return cached.base;

    HDC dc = wglGetCurrentDC();
    if (!dc) return 0;

    const int pixelHeight = fallbackPixelHeight(fontKind);
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0,
                             fontKind == 2 ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE,
                             ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    if (!font) return 0;

    HGDIOBJ old = SelectObject(dc, font);
    const GLuint base = glGenLists(256);
    const BOOL ok = base != 0 ? wglUseFontBitmapsW(dc, 0, 256, base) : FALSE;
    SelectObject(dc, old);
    DeleteObject(font);

    if (!ok) {
        if (base) glDeleteLists(base, 256);
        return 0;
    }

    cached.base = base;
    cached.pixelHeight = pixelHeight;
    return base;
}

std::string narrowFallbackText(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(1252, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, "?", nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(1252, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, "?", nullptr);
    return out;
}

void drawFallbackQuad(float x, float y, float w, float h,
                      float r, float g, float b, float a) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

} // namespace

std::string RomUiSkin::lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool RomUiSkin::hasToken(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

bool RomUiSkin::likelyBclim(const std::string& path) {
    const std::string p = lower(path);
    if (hasToken(p, ".bclim") || hasToken(p, ".clim")) return true;
    if (hasToken(p, ".bch")) return false;
    return hasToken(p, "button") || hasToken(p, "btn") || hasToken(p, "window") ||
           hasToken(p, "waku") || hasToken(p, "frame") || hasToken(p, "menu") ||
           hasToken(p, "/ui") || hasToken(p, "::ui") || hasToken(p, "common");
}

bool RomUiSkin::likelyFont(const std::string& path) {
    const std::string p = lower(path);
    return hasToken(p, ".bcfnt") || hasToken(p, ".cfnt") || hasToken(p, "font") || hasToken(p, "fnt");
}

int RomUiSkin::fontPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, ".bcfnt")) score += 500;
    if (hasToken(p, ".cfnt")) score += 420;
    if (hasToken(p, "font")) score += 250;
    if (hasToken(p, "common")) score += 80;
    if (hasToken(p, "message") || hasToken(p, "text")) score += 60;
    if (hasToken(p, "debug")) score -= 300;
    return score;
}

int RomUiSkin::buttonPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, "button")) score += 420;
    if (hasToken(p, "btn")) score += 380;
    if (hasToken(p, "select")) score += 150;
    if (hasToken(p, "menu")) score += 120;
    if (hasToken(p, "common")) score += 80;
    if (hasToken(p, "waku") || hasToken(p, "frame")) score += 50;
    if (hasToken(p, "/ui") || hasToken(p, "::ui")) score += 180;
    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") || hasToken(p, "face")) score -= 250;
    if (hasToken(p, "bg") || hasToken(p, "background")) score -= 160;
    if (hasToken(p, "pii/") || hasToken(p, "mii/") || hasToken(p, "stage") || hasToken(p, "field")) score -= 500;
    return score;
}

int RomUiSkin::panelPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, "window")) score += 420;
    if (hasToken(p, "waku")) score += 360;
    if (hasToken(p, "frame")) score += 300;
    if (hasToken(p, "panel")) score += 280;
    if (hasToken(p, "message")) score += 180;
    if (hasToken(p, "common")) score += 100;
    if (hasToken(p, "/ui") || hasToken(p, "::ui")) score += 180;
    if (hasToken(p, "button") || hasToken(p, "btn")) score += 30;
    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") || hasToken(p, "face")) score -= 250;
    if (hasToken(p, "pii/") || hasToken(p, "mii/") || hasToken(p, "stage") || hasToken(p, "field")) score -= 500;
    return score;
}

unsigned int RomUiSkin::upload(const ctr::Image& image) const {
    if (!image.valid()) return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

bool RomUiSkin::load(storage::RomAssetCache& assets) {
    release();
    const auto resources = assets.resources();
    if (resources.empty()) return false;

    std::ofstream diagnostic(assets.root() / "ui_scan.tsv", std::ios::binary | std::ios::trunc);
    if (diagnostic) diagnostic << "kind\tstatus\twidth\theight\tresource\n";

    ctr::BitmapFont bestFont;
    int bestFontScore = std::numeric_limits<int>::min();
    TextureCandidate bestButton;
    TextureCandidate bestPanel;
    int recognizedFonts = 0;
    int decodedFonts = 0;
    int recognizedImages = 0;
    int decodedImages = 0;

    auto considerImage = [&](const std::string& resourcePath, const std::string& textureName, const ctr::Image& image) {
        if (!image.valid()) return;
        const std::string scorePath = resourcePath + "/" + textureName;
        const float aspect = static_cast<float>(image.width) / static_cast<float>(std::max(1, image.height));
        int buttonScore = buttonPathScore(scorePath);
        int panelScore = panelPathScore(scorePath);

        if (aspect >= 1.8f && aspect <= 12.0f) buttonScore += 180;
        else buttonScore -= 120;
        if (image.width >= 48 && image.width <= 1024) buttonScore += 45;
        if (image.height >= 12 && image.height <= 180) buttonScore += 70;
        if (image.height > image.width) buttonScore -= 140;

        if (aspect >= 0.7f && aspect <= 8.0f) panelScore += 80;
        if (image.width >= 64 && image.height >= 32) panelScore += 70;
        if (image.width > 2048 || image.height > 2048) panelScore -= 160;

        if (buttonScore > bestButton.buttonScore) {
            bestButton.path = resourcePath + (textureName.empty() ? "" : "#" + textureName);
            bestButton.image = image;
            bestButton.buttonScore = buttonScore;
            bestButton.panelScore = panelScore;
        }
        if (panelScore > bestPanel.panelScore) {
            bestPanel.path = resourcePath + (textureName.empty() ? "" : "#" + textureName);
            bestPanel.image = image;
            bestPanel.buttonScore = buttonScore;
            bestPanel.panelScore = panelScore;
        }
    };

    for (const auto& path : resources) {
        try {
            const auto bytes = assets.extractResource(path);

            if (ctr::isBcfnt(bytes)) {
                ++recognizedFonts;
                auto decoded = ctr::decodeBcfnt(bytes);
                if (decoded.valid()) {
                    ++decodedFonts;
                    const int score = fontPathScore(path);
                    if (diagnostic) diagnostic << "BCFNT\tok\t" << decoded.sheetWidth << '\t' << decoded.sheetHeight << '\t' << path << '\n';
                    if (score > bestFontScore) {
                        bestFontScore = score;
                        bestFont = std::move(decoded);
                        fontResource_ = path;
                    }
                } else if (diagnostic) {
                    diagnostic << "BCFNT\tdecode-failed\t0\t0\t" << path << '\n';
                }
            }

            if (ctr::isBclim(bytes)) {
                ++recognizedImages;
                ctr::Image image = ctr::decodeBclim(bytes);
                if (image.valid()) {
                    ++decodedImages;
                    if (diagnostic) diagnostic << "BCLIM\tok\t" << image.width << '\t' << image.height << '\t' << path << '\n';
                    considerImage(path, "", image);
                } else if (diagnostic) {
                    diagnostic << "BCLIM\tdecode-failed\t0\t0\t" << path << '\n';
                }
            }
        } catch (...) {
            if (diagnostic && (likelyFont(path) || likelyBclim(path))) {
                diagnostic << "RESOURCE\tread-failed\t0\t0\t" << path << '\n';
            }
        }
    }

    if (!bestButton.image.valid() || !bestPanel.image.valid()) {
        for (const auto& path : resources) {
            const std::string p = lower(path);
            const bool uiNamed = hasToken(p, "/ui") || hasToken(p, "::ui") ||
                                 hasToken(p, "menu") || hasToken(p, "common") ||
                                 hasToken(p, "button") || hasToken(p, "window") ||
                                 hasToken(p, "waku") || hasToken(p, "frame");
            if (!uiNamed) continue;
            try {
                const auto bytes = assets.extractResource(path);
                if (!isBch(bytes)) continue;
                const auto scene = bch::Scene::parse(bytes);
                for (const auto& texture : scene.textures()) {
                    const ctr::Image image = imageFromBchTexture(texture);
                    if (!image.valid()) continue;
                    if (diagnostic) diagnostic << "BCH_TEXTURE\tok\t" << image.width << '\t' << image.height << '\t' << path << '#' << texture.name << '\n';
                    considerImage(path, texture.name, image);
                }
            } catch (...) {
            }
        }
    }

    if (bestFont.valid()) {
        std::vector<unsigned int> textures;
        textures.reserve(bestFont.sheets.size());
        bool ok = true;
        for (const auto& sheet : bestFont.sheets) {
            const unsigned int texture = upload(sheet);
            if (!texture) {
                ok = false;
                break;
            }
            textures.push_back(texture);
        }
        if (ok) {
            font_ = std::move(bestFont);
            fontTextures_ = std::move(textures);
            fontReady_ = true;
        } else {
            for (unsigned int texture : textures) {
                GLuint id = static_cast<GLuint>(texture);
                if (id) glDeleteTextures(1, &id);
            }
            fontResource_.clear();
        }
    }

    if (bestButton.image.valid()) {
        buttonTexture_ = upload(bestButton.image);
        if (buttonTexture_) buttonResource_ = bestButton.path;
    }
    if (bestPanel.image.valid()) {
        panelTexture_ = upload(bestPanel.image);
        if (panelTexture_) panelResource_ = bestPanel.path;
    }

    if (!panelTexture_ && buttonTexture_) {
        panelTexture_ = buttonTexture_;
        panelResource_ = buttonResource_;
    }
    if (!buttonTexture_ && panelTexture_) {
        buttonTexture_ = panelTexture_;
        buttonResource_ = panelResource_;
    }

    if (diagnostic) {
        diagnostic << "SUMMARY\tresources=" << resources.size()
                   << ";recognized_fonts=" << recognizedFonts
                   << ";decoded_fonts=" << decodedFonts
                   << ";recognized_bclim=" << recognizedImages
                   << ";decoded_bclim=" << decodedImages
                   << "\t0\t0\t";
        diagnostic << "font=" << fontResource_ << ";button=" << buttonResource_ << ";panel=" << panelResource_ << '\n';
    }

    return ready();
}

void RomUiSkin::release() {
    for (unsigned int texture : fontTextures_) {
        GLuint id = static_cast<GLuint>(texture);
        if (id) glDeleteTextures(1, &id);
    }
    fontTextures_.clear();

    if (buttonTexture_ && buttonTexture_ != panelTexture_) {
        GLuint id = static_cast<GLuint>(buttonTexture_);
        glDeleteTextures(1, &id);
    }
    if (panelTexture_) {
        GLuint id = static_cast<GLuint>(panelTexture_);
        glDeleteTextures(1, &id);
    }

    buttonTexture_ = 0;
    panelTexture_ = 0;
    font_ = {};
    fontReady_ = false;
    fontResource_.clear();
    buttonResource_.clear();
    panelResource_.clear();
}

void RomUiSkin::drawTexture(unsigned int texture,
                            float x, float y, float w, float h,
                            float r, float g, float b, float a) const {
    if (!texture) return;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + h);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void RomUiSkin::drawPanel(float x, float y, float w, float h, float alpha) const {
    if (panelTexture_) {
        drawTexture(panelTexture_, x, y, w, h, 1.0f, 1.0f, 1.0f, alpha);
        return;
    }
    drawFallbackQuad(x, y, w, h, 0.055f, 0.095f, 0.15f, alpha * 0.94f);
    glColor4f(0.50f, 0.76f, 0.96f, alpha);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void RomUiSkin::drawButton(float x, float y, float w, float h, bool hovered, bool enabled) const {
    const float brightness = !enabled ? 0.48f : (hovered ? 1.0f : 0.86f);
    const float alpha = enabled ? 1.0f : 0.72f;
    if (buttonTexture_) {
        drawTexture(buttonTexture_, x, y, w, h, brightness, brightness, brightness, alpha);
        return;
    }

    if (!enabled) drawFallbackQuad(x, y, w, h, 0.13f, 0.15f, 0.18f, 0.72f);
    else if (hovered) drawFallbackQuad(x, y, w, h, 0.08f, 0.43f, 0.78f, 0.98f);
    else drawFallbackQuad(x, y, w, h, 0.07f, 0.28f, 0.55f, 0.94f);

    glColor4f(enabled ? 0.68f : 0.34f, enabled ? 0.86f : 0.38f, enabled ? 1.0f : 0.44f, 1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

float RomUiSkin::fontScale(int fontKind) const {
    if (!fontReady_ || font_.cellHeight <= 0) return 1.0f;
    const float target = fontKind == 2 ? 38.0f : (fontKind == 1 ? 23.0f : 17.0f);
    return target / static_cast<float>(font_.cellHeight);
}

float RomUiSkin::textWidth(const std::wstring& text, int fontKind) const {
    if (!fontReady_) {
        return static_cast<float>(text.size()) * static_cast<float>(fallbackPixelHeight(fontKind)) * 0.56f;
    }
    const float scale = fontScale(fontKind);
    float width = 0.0f;
    for (wchar_t ch : text) {
        const auto* glyph = font_.findGlyph(static_cast<std::uint32_t>(ch));
        if (!glyph) continue;
        const int advance = glyph->metrics.charWidth > 0 ? glyph->metrics.charWidth : font_.cellWidth;
        width += static_cast<float>(advance) * scale;
    }
    return width;
}

void RomUiSkin::drawText(float x,
                         float baselineY,
                         const std::wstring& text,
                         int fontKind,
                         bool centered) const {
    if (text.empty()) return;

    if (!fontReady_) {
        const GLuint base = ensureFallbackFont(fontKind);
        if (!base) return;
        if (centered) x -= textWidth(text, fontKind) * 0.5f;
        const std::string bytes = narrowFallbackText(text);
        if (bytes.empty()) return;

        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glRasterPos2f(x, baselineY);
        glListBase(base);
        glCallLists(static_cast<GLsizei>(bytes.size()), GL_UNSIGNED_BYTE, bytes.data());
        return;
    }

    const float scale = fontScale(fontKind);
    if (centered) x -= textWidth(text, fontKind) * 0.5f;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    float cursor = x;
    for (wchar_t ch : text) {
        const auto* glyph = font_.findGlyph(static_cast<std::uint32_t>(ch));
        if (!glyph || glyph->sheet < 0 || glyph->sheet >= static_cast<int>(fontTextures_.size())) continue;

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(fontTextures_[static_cast<std::size_t>(glyph->sheet)]));
        const float u0 = static_cast<float>(glyph->x) / static_cast<float>(font_.sheetWidth);
        const float v0 = static_cast<float>(glyph->y) / static_cast<float>(font_.sheetHeight);
        const float u1 = static_cast<float>(glyph->x + font_.cellWidth) / static_cast<float>(font_.sheetWidth);
        const float v1 = static_cast<float>(glyph->y + font_.cellHeight) / static_cast<float>(font_.sheetHeight);

        const float left = cursor + static_cast<float>(glyph->metrics.left) * scale;
        const float top = baselineY - static_cast<float>(font_.baseline) * scale;
        const float right = left + static_cast<float>(font_.cellWidth) * scale;
        const float bottom = top + static_cast<float>(font_.cellHeight) * scale;

        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(left, top);
        glTexCoord2f(u1, v0); glVertex2f(right, top);
        glTexCoord2f(u1, v1); glVertex2f(right, bottom);
        glTexCoord2f(u0, v1); glVertex2f(left, bottom);
        glEnd();

        const int advance = glyph->metrics.charWidth > 0 ? glyph->metrics.charWidth : font_.cellWidth;
        cursor += static_cast<float>(advance) * scale;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

} // namespace promptemon::demo
