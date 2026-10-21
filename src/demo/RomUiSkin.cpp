#include "demo/RomUiSkin.h"

#include "ctr/Bclim.h"

#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace promptemon::demo {

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
           hasToken(p, "/ui") || hasToken(p, "common");
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
    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") || hasToken(p, "face")) score -= 250;
    if (hasToken(p, "bg") || hasToken(p, "background")) score -= 160;
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
    if (hasToken(p, "button") || hasToken(p, "btn")) score += 30;
    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") || hasToken(p, "face")) score -= 250;
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

bool RomUiSkin::load(rom::RomModelArchive& archive) {
    release();
    const auto resources = archive.resources();
    if (resources.empty()) return false;

    std::vector<std::pair<int, std::string>> fonts;
    fonts.reserve(resources.size());
    for (const auto& path : resources) {
        if (likelyFont(path)) fonts.push_back({fontPathScore(path), path});
    }
    std::sort(fonts.begin(), fonts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const std::size_t fontAttempts = std::min<std::size_t>(fonts.size(), 64);
    for (std::size_t i = 0; i < fontAttempts && !fontReady_; ++i) {
        try {
            const auto bytes = archive.extractResource(fonts[i].second);
            if (!ctr::isBcfnt(bytes)) continue;
            auto decoded = ctr::decodeBcfnt(bytes);
            if (!decoded.valid()) continue;

            std::vector<unsigned int> textures;
            textures.reserve(decoded.sheets.size());
            bool ok = true;
            for (const auto& sheet : decoded.sheets) {
                const unsigned int texture = upload(sheet);
                if (!texture) {
                    ok = false;
                    break;
                }
                textures.push_back(texture);
            }
            if (!ok) {
                for (unsigned int texture : textures) {
                    GLuint id = static_cast<GLuint>(texture);
                    glDeleteTextures(1, &id);
                }
                continue;
            }

            font_ = std::move(decoded);
            fontTextures_ = std::move(textures);
            fontResource_ = fonts[i].second;
            fontReady_ = true;
        } catch (...) {
        }
    }

    TextureCandidate bestButton;
    TextureCandidate bestPanel;
    std::vector<std::pair<int, std::string>> images;
    images.reserve(resources.size());
    for (const auto& path : resources) {
        if (!likelyBclim(path)) continue;
        const int broadScore = std::max(buttonPathScore(path), panelPathScore(path));
        images.push_back({broadScore, path});
    }
    std::sort(images.begin(), images.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const std::size_t imageAttempts = std::min<std::size_t>(images.size(), 160);
    for (std::size_t i = 0; i < imageAttempts; ++i) {
        try {
            const auto bytes = archive.extractResource(images[i].second);
            if (!ctr::isBclim(bytes)) continue;
            ctr::Image image = ctr::decodeBclim(bytes);
            if (!image.valid()) continue;

            const float aspect = static_cast<float>(image.width) / static_cast<float>(std::max(1, image.height));
            int buttonScore = buttonPathScore(images[i].second);
            int panelScore = panelPathScore(images[i].second);

            if (aspect >= 1.8f && aspect <= 12.0f) buttonScore += 180;
            else buttonScore -= 120;
            if (image.width >= 48 && image.width <= 1024) buttonScore += 45;
            if (image.height >= 12 && image.height <= 160) buttonScore += 70;

            if (aspect >= 0.7f && aspect <= 8.0f) panelScore += 80;
            if (image.width >= 64 && image.height >= 32) panelScore += 70;
            if (image.width > 1024 || image.height > 1024) panelScore -= 100;

            if (buttonScore > bestButton.buttonScore) {
                bestButton = {images[i].second, image, buttonScore, panelScore};
            }
            if (panelScore > bestPanel.panelScore) {
                bestPanel = {images[i].second, std::move(image), buttonScore, panelScore};
            }
        } catch (...) {
        }
    }

    if (bestButton.image.valid()) {
        buttonTexture_ = upload(bestButton.image);
        buttonResource_ = bestButton.path;
    }
    if (bestPanel.image.valid()) {
        panelTexture_ = upload(bestPanel.image);
        panelResource_ = bestPanel.path;
    }

    if (!panelTexture_ && buttonTexture_) {
        panelTexture_ = buttonTexture_;
        panelResource_ = buttonResource_;
    }
    if (!buttonTexture_ && panelTexture_) {
        buttonTexture_ = panelTexture_;
        buttonResource_ = panelResource_;
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
    drawTexture(panelTexture_, x, y, w, h, 1.0f, 1.0f, 1.0f, alpha);
}

void RomUiSkin::drawButton(float x, float y, float w, float h, bool hovered, bool enabled) const {
    const float brightness = !enabled ? 0.48f : (hovered ? 1.0f : 0.86f);
    const float alpha = enabled ? 1.0f : 0.72f;
    drawTexture(buttonTexture_, x, y, w, h, brightness, brightness, brightness, alpha);
}

float RomUiSkin::fontScale(int fontKind) const {
    if (!fontReady_ || font_.cellHeight <= 0) return 1.0f;
    const float target = fontKind == 2 ? 38.0f : (fontKind == 1 ? 23.0f : 17.0f);
    return target / static_cast<float>(font_.cellHeight);
}

float RomUiSkin::textWidth(const std::wstring& text, int fontKind) const {
    if (!fontReady_) return 0.0f;
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
    if (!fontReady_ || text.empty()) return;
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
