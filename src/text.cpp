#include "text.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstring>
#include <vector>

namespace fx {

std::wstring utf8Decode(const std::string& in) {
    std::wstring out;
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        unsigned int cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }
        if (i + size_t(extra) >= in.size()) break;
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(in[i + size_t(k)]) & 0x3F);
        i += size_t(extra) + 1;
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

static std::string findFont() {
    const char* families[] = {"Noto Sans CJK SC", "Noto Sans SC", "Source Han Sans SC", "WenQuanYi Micro Hei",
                              "Noto Sans CJK TC", "Droid Sans Fallback", "DejaVu Sans", nullptr};
    FcInit();
    for (int i = 0; families[i]; ++i) {
        FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(families[i]));
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res = FcResultNoMatch;
        FcPattern* match = FcFontMatch(nullptr, pat, &res);
        FcPatternDestroy(pat);
        if (!match) continue;
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            std::string path(reinterpret_cast<char*>(file));
            FcPatternDestroy(match);
            return path;
        }
        FcPatternDestroy(match);
    }
    return std::string();
}

bool TextRenderer::init(std::string* error) {
    fontPath_ = findFont();
    if (fontPath_.empty()) {
        if (error) *error = "找不到可用字体（fontconfig）";
        return false;
    }
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0) {
        if (error) *error = "FreeType 初始化失败";
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(lib, fontPath_.c_str(), 0, &face) != 0) {
        if (error) *error = "无法打开字体: " + fontPath_;
        FT_Done_FreeType(lib);
        return false;
    }
    lib_ = lib;
    face_ = face;
    return true;
}

void TextRenderer::shutdown(GLContext& gl) {
    for (auto& kv : cache_) gl.destroyTexture(kv.second.tex);
    cache_.clear();
    if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
    if (lib_) FT_Done_FreeType(static_cast<FT_Library>(lib_));
    face_ = nullptr;
    lib_ = nullptr;
}

float TextRenderer::lineHeight(float size) const {
    FT_Face face = static_cast<FT_Face>(face_);
    if (!face) return size * 1.4f;
    return float(face->height) / float(face->units_per_EM) * size;
}

TextRenderer::Glyph* TextRenderer::getGlyph(GLContext& gl, unsigned int codepoint, float size) {
    const long long key = (static_cast<long long>(codepoint) << 20) | static_cast<long long>(size * 4.0f);
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;

    FT_Face face = static_cast<FT_Face>(face_);
    if (!face) return nullptr;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size)) != 0) return nullptr;
    if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) return nullptr;

    FT_GlyphSlot slot = face->glyph;
    Glyph g;
    g.w = int(slot->bitmap.width);
    g.h = int(slot->bitmap.rows);
    g.bearingX = slot->bitmap_left;
    g.bearingY = slot->bitmap_top;
    g.advance = float(slot->advance.x) / 64.0f;
    if (g.w > 0 && g.h > 0) {
        std::vector<uint8_t> rgba(size_t(g.w) * size_t(g.h) * 4);
        for (int y = 0; y < g.h; ++y) {
            for (int x = 0; x < g.w; ++x) {
                const uint8_t a = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
                uint8_t* px = &rgba[(size_t(y) * size_t(g.w) + size_t(x)) * 4];
                px[0] = px[1] = px[2] = 255;  // 直通 alpha：着色器里预乘
                px[3] = a;
            }
        }
        g.tex = gl.createTextureRGBA(rgba.data(), g.w, g.h);
    }
    auto res = cache_.emplace(key, std::move(g));
    return &res.first->second;
}

float TextRenderer::measure(const std::string& utf8, float size) {
    const std::wstring w = utf8Decode(utf8);
    float total = 0.0f;
    FT_Face face = static_cast<FT_Face>(face_);
    if (!face) return 0.0f;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size)) != 0) return 0.0f;
    for (wchar_t ch : w) {
        if (FT_Load_Char(face, static_cast<FT_ULong>(ch), FT_LOAD_DEFAULT) == 0)
            total += float(face->glyph->advance.x) / 64.0f;
    }
    return total;
}

void TextRenderer::draw(GLContext& gl, const std::string& utf8, float x, float y, float size, const Color& color) {
    const std::wstring w = utf8Decode(utf8);
    float penX = x;
    // 行内基线：用字体 ascent 估算，保证中文行高一致
    FT_Face face = static_cast<FT_Face>(face_);
    if (!face) return;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size)) != 0) return;
    const float ascent = float(face->size->metrics.ascender) / 64.0f;

    gl.setColorParams(color, Color{1, 1, 1, 1}, Color{0, 0, 0, 0});
    gl.setBlend(Blend::Normal);
    for (wchar_t ch : w) {
        if (ch < 32) continue;
        Glyph* g = getGlyph(gl, static_cast<unsigned int>(ch), size);
        if (!g) continue;
        if (g->tex.valid()) {
            const float gx = penX + float(g->bearingX);
            const float gy = y + ascent - float(g->bearingY);
            gl.quad(g->tex, gx, gy, float(g->w), float(g->h));
        }
        penX += g->advance;
    }
    gl.resetColorParams();
}

}  // namespace fx
