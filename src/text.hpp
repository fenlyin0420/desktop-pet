// 文字渲染：fontconfig 找 CJK 字体，freetype 光栅化，字形纹理缓存。
#pragma once

#include <map>
#include <string>

#include "gl.hpp"

namespace fx {

class TextRenderer {
public:
    bool init(std::string* error);
    void shutdown(GLContext& gl);

    float lineHeight(float size) const;
    float measure(const std::string& utf8, float size);
    // 在 (x, y) 处绘制，y 为文字行顶部
    void draw(GLContext& gl, const std::string& utf8, float x, float y, float size, const Color& color);
    const std::string& fontPath() const { return fontPath_; }

private:
    struct Glyph {
        Texture tex;
        int w = 0;
        int h = 0;
        int bearingX = 0;
        int bearingY = 0;
        float advance = 0;
    };
    Glyph* getGlyph(GLContext& gl, unsigned int codepoint, float size);

    void* lib_ = nullptr;
    void* face_ = nullptr;
    std::string fontPath_;
    std::map<long long, Glyph> cache_;
};

std::wstring utf8Decode(const std::string& in);

}  // namespace fx
