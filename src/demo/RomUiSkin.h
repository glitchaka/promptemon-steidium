#pragma once

#include "ctr/Bcfnt.h"
#include "ctr/CtrTexture.h"
#include "rom/RomModelArchive.h"

#include <string>
#include <vector>

namespace promptemon::demo {

class RomUiSkin {
public:
    bool load(rom::RomModelArchive& archive);
    void release();

    bool ready() const noexcept { return fontReady_ && buttonTexture_ != 0 && panelTexture_ != 0; }
    const std::string& fontResource() const noexcept { return fontResource_; }
    const std::string& buttonResource() const noexcept { return buttonResource_; }
    const std::string& panelResource() const noexcept { return panelResource_; }

    void drawPanel(float x, float y, float w, float h, float alpha = 1.0f) const;
    void drawButton(float x, float y, float w, float h, bool hovered, bool enabled) const;
    void drawText(float x, float baselineY, const std::wstring& text,
                  int fontKind = 0, bool centered = false) const;
    float textWidth(const std::wstring& text, int fontKind = 0) const;

private:
    struct TextureCandidate {
        std::string path;
        ctr::Image image;
        int buttonScore = -100000;
        int panelScore = -100000;
    };

    unsigned int upload(const ctr::Image& image) const;
    void drawTexture(unsigned int texture, float x, float y, float w, float h,
                     float r, float g, float b, float a) const;
    float fontScale(int fontKind) const;
    static std::string lower(std::string value);
    static bool hasToken(const std::string& value, const char* token);
    static bool likelyBclim(const std::string& path);
    static bool likelyFont(const std::string& path);
    static int fontPathScore(const std::string& path);
    static int buttonPathScore(const std::string& path);
    static int panelPathScore(const std::string& path);

    ctr::BitmapFont font_;
    std::vector<unsigned int> fontTextures_;
    unsigned int buttonTexture_ = 0;
    unsigned int panelTexture_ = 0;
    bool fontReady_ = false;

    std::string fontResource_;
    std::string buttonResource_;
    std::string panelResource_;
};

} // namespace promptemon::demo
