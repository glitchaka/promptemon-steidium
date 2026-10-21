#include "demo/RomUiSkin.h"

#include "bch/BchScene.h"
#include "ctr/Bclim.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>
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

bool inRumbleUiArchive(const std::string& lowerPath) {
    return lowerPath.find("boot2d.arc") != std::string::npos ||
           lowerPath.find("game2d.") != std::string::npos ||
           lowerPath.find("game2d_") != std::string::npos ||
           lowerPath.find("game2d.arc") != std::string::npos ||
           lowerPath.find("font.arc") != std::string::npos;
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
    return hasToken(p, ".bclim") || hasToken(p, ".clim") || hasToken(p, ".bflim") || hasToken(p, ".flim") ||
           hasToken(p, "button") || hasToken(p, "btn") || hasToken(p, "window") || hasToken(p, "waku") ||
           hasToken(p, "frame") || hasToken(p, "menu") || hasToken(p, "panel") || inRumbleUiArchive(p);
}

bool RomUiSkin::likelyFont(const std::string& path) {
    const std::string p = lower(path);
    return hasToken(p, ".bffnt") || hasToken(p, ".bcfnt") || hasToken(p, ".cfnt") ||
           hasToken(p, "font") || hasToken(p, "fnt");
}

int RomUiSkin::fontPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, "cbf_caviar.bffnt")) score += 10000;
    if (hasToken(p, ".bffnt")) score += 1200;
    if (hasToken(p, ".bcfnt")) score += 900;
    if (hasToken(p, ".cfnt")) score += 700;
    if (hasToken(p, "boot2d.arc")) score += 900;
    if (hasToken(p, "font.arc")) score += 700;
    if (hasToken(p, "caviar")) score += 600;
    if (hasToken(p, "font")) score += 250;
    if (hasToken(p, "debug")) score -= 2000;
    return score;
}

int RomUiSkin::buttonPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, "boot2d.arc") || hasToken(p, "game2d.")) score += 900;
    if (hasToken(p, "button")) score += 650;
    if (hasToken(p, "btn")) score += 620;
    if (hasToken(p, "select")) score += 260;
    if (hasToken(p, "choice")) score += 240;
    if (hasToken(p, "menu")) score += 220;
    if (hasToken(p, "waku")) score += 170;
    if (hasToken(p, "frame")) score += 120;
    if (hasToken(p, "common")) score += 90;
    if (hasToken(p, "plate")) score += 80;

    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") ||
        hasToken(p, "face") || hasToken(p, "mark") || hasToken(p, "cursor")) score -= 500;
    if (hasToken(p, "gauge") || hasToken(p, "meter") || hasToken(p, "hp")) score -= 420;
    if (hasToken(p, "background") || hasToken(p, "bg_")) score -= 300;
    if (hasToken(p, "pii/") || hasToken(p, "mii/") || hasToken(p, "stage")) score -= 1200;
    return score;
}

int RomUiSkin::panelPathScore(const std::string& path) {
    const std::string p = lower(path);
    int score = 0;
    if (hasToken(p, "boot2d.arc") || hasToken(p, "game2d.")) score += 900;
    if (hasToken(p, "window")) score += 680;
    if (hasToken(p, "waku")) score += 620;
    if (hasToken(p, "panel")) score += 580;
    if (hasToken(p, "frame")) score += 460;
    if (hasToken(p, "message")) score += 380;
    if (hasToken(p, "dialog")) score += 350;
    if (hasToken(p, "menu")) score += 220;
    if (hasToken(p, "common")) score += 100;

    if (hasToken(p, "icon") || hasToken(p, "logo") || hasToken(p, "title") ||
        hasToken(p, "face") || hasToken(p, "mark") || hasToken(p, "cursor")) score -= 500;
    if (hasToken(p, "gauge") || hasToken(p, "meter") || hasToken(p, "hp")) score -= 380;
    if (hasToken(p, "background") || hasToken(p, "bg_")) score -= 220;
    if (hasToken(p, "pii/") || hasToken(p, "mii/") || hasToken(p, "stage")) score -= 1200;
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
    if (diagnostic) diagnostic << "kind\tstatus\twidth\theight\tscore\tresource\n";

    ctr::BitmapFont bestFont;
    int bestFontScore = std::numeric_limits<int>::min();
    TextureCandidate bestButton;
    TextureCandidate bestPanel;

    auto considerImage = [&](const std::string& resourcePath,
                             const std::string& textureName,
                             const ctr::Image& image) {
        if (!image.valid()) return;
        const std::string scorePath = resourcePath + (textureName.empty() ? "" : "#" + textureName);
        const float aspect = static_cast<float>(image.width) / static_cast<float>(std::max(1, image.height));
        int buttonScore = buttonPathScore(scorePath);
        int panelScore = panelPathScore(scorePath);

        if (aspect >= 2.0f && aspect <= 10.0f) buttonScore += 320;
        else if (aspect >= 1.5f && aspect <= 14.0f) buttonScore += 120;
        else buttonScore -= 220;
        if (image.width >= 48 && image.width <= 1024) buttonScore += 80;
        if (image.height >= 12 && image.height <= 160) buttonScore += 120;
        if (image.height > image.width) buttonScore -= 260;

        if (aspect >= .85f && aspect <= 6.5f) panelScore += 180;
        if (image.width >= 64 && image.height >= 32) panelScore += 120;
        if (image.width >= 128 && image.height >= 64) panelScore += 80;
        if (image.width > 2048 || image.height > 2048) panelScore -= 300;

        if (buttonScore > bestButton.buttonScore) {
            bestButton.path = scorePath;
            bestButton.image = image;
            bestButton.buttonScore = buttonScore;
            bestButton.panelScore = panelScore;
        }
        if (panelScore > bestPanel.panelScore) {
            bestPanel.path = scorePath;
            bestPanel.image = image;
            bestPanel.buttonScore = buttonScore;
            bestPanel.panelScore = panelScore;
        }

        if (diagnostic && inRumbleUiArchive(lower(scorePath))) {
            diagnostic << "IMAGE\tok\t" << image.width << '\t' << image.height << '\t'
                       << buttonScore << '/' << panelScore << '\t' << scorePath << '\n';
        }
    };

    for (const auto& path : resources) {
        try {
            const auto bytes = assets.extractResource(path);

            if (ctr::isBcfnt(bytes)) {
                auto decoded = ctr::decodeBcfnt(bytes);
                if (decoded.valid()) {
                    const int score = fontPathScore(path);
                    if (diagnostic) diagnostic << "FONT\tok\t" << decoded.sheetWidth << '\t' << decoded.sheetHeight
                                               << '\t' << score << '\t' << path << '\n';
                    if (score > bestFontScore) {
                        bestFontScore = score;
                        bestFont = std::move(decoded);
                        fontResource_ = path;
                    }
                }
            }

            if (ctr::isBclim(bytes)) {
                ctr::Image image = ctr::decodeBclim(bytes);
                if (image.valid()) considerImage(path, "", image);
            }
        } catch (...) {
            if (diagnostic && (likelyFont(path) || likelyBclim(path)))
                diagnostic << "RESOURCE\tread-failed\t0\t0\t0\t" << path << '\n';
        }
    }

    if (!bestButton.image.valid() || !bestPanel.image.valid()) {
        for (const auto& path : resources) {
            const std::string p = lower(path);
            if (!inRumbleUiArchive(p) && !hasToken(p, "/ui") && !hasToken(p, "::ui")) continue;
            try {
                const auto bytes = assets.extractResource(path);
                if (!isBch(bytes)) continue;
                const auto scene = bch::Scene::parse(bytes);
                for (const auto& texture : scene.textures()) {
                    const ctr::Image image = imageFromBchTexture(texture);
                    if (image.valid()) considerImage(path, texture.name, image);
                }
            } catch (...) {
            }
        }
    }

    if (bestFont.valid()) {
        std::vector<unsigned int> textures;
        bool ok = true;
        textures.reserve(bestFont.sheets.size());
        for (const auto& sheet : bestFont.sheets) {
            const unsigned int texture = upload(sheet);
            if (!texture) { ok = false; break; }
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

    if (diagnostic) {
        diagnostic << "SELECTED\tfont\t0\t0\t" << bestFontScore << '\t' << fontResource_ << '\n';
        diagnostic << "SELECTED\tbutton\t0\t0\t" << bestButton.buttonScore << '\t' << buttonResource_ << '\n';
        diagnostic << "SELECTED\tpanel\t0\t0\t" << bestPanel.panelScore << '\t' << panelResource_ << '\n';
    }

    return nativeSkinReady();
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
    glTexCoord2f(0,0); glVertex2f(x,y);
    glTexCoord2f(1,0); glVertex2f(x+w,y);
    glTexCoord2f(1,1); glVertex2f(x+w,y+h);
    glTexCoord2f(0,1); glVertex2f(x,y+h);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void RomUiSkin::drawPanel(float x, float y, float w, float h, float alpha) const {
    if (!panelTexture_) return;
    drawTexture(panelTexture_, x, y, w, h, 1, 1, 1, alpha);
}

void RomUiSkin::drawButton(float x, float y, float w, float h, bool hovered, bool enabled) const {
    if (!buttonTexture_) return;
    const float brightness = !enabled ? .55f : (hovered ? 1.0f : .90f);
    const float alpha = enabled ? 1.0f : .70f;
    drawTexture(buttonTexture_, x, y, w, h, brightness, brightness, brightness, alpha);
}

float RomUiSkin::fontScale(int fontKind) const {
    if (!fontReady_ || font_.cellHeight <= 0) return 1.0f;
    const float target = fontKind == 2 ? 34.0f : (fontKind == 1 ? 22.0f : 16.0f);
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

void RomUiSkin::drawText(float x, float baselineY, const std::wstring& text,
                         int fontKind, bool centered) const {
    if (!fontReady_ || text.empty()) return;
    const float scale = fontScale(fontKind);
    if (centered) x -= textWidth(text, fontKind) * .5f;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);

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
        glTexCoord2f(u0,v0); glVertex2f(left,top);
        glTexCoord2f(u1,v0); glVertex2f(right,top);
        glTexCoord2f(u1,v1); glVertex2f(right,bottom);
        glTexCoord2f(u0,v1); glVertex2f(left,bottom);
        glEnd();

        const int advance = glyph->metrics.charWidth > 0 ? glyph->metrics.charWidth : font_.cellWidth;
        cursor += static_cast<float>(advance) * scale;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

} // namespace promptemon::demo
