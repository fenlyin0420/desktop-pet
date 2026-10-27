#include "gl.hpp"

#include <png.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace fx {

static const char* kVert = "#version 100\n"
                           "precision mediump float;\n"
                           "attribute vec2 a_pos;\n"
                           "attribute vec2 a_uv;\n"
                           "attribute vec4 a_col;\n"
                           "uniform vec2 u_view;\n"
                           "varying vec2 v_uv;\n"
                           "varying vec4 v_col;\n"
                           "void main() {\n"
                           "  vec2 ndc = vec2(a_pos.x / u_view.x * 2.0 - 1.0, 1.0 - a_pos.y / u_view.y * 2.0);\n"
                           "  gl_Position = vec4(ndc, 0.0, 1.0);\n"
                           "  v_uv = a_uv;\n"
                           "  v_col = a_col;\n"
                           "}\n";

static const char* kFrag = "#version 100\n"
                           "precision highp float;\n"
                           "uniform sampler2D u_tex;\n"
                           "uniform vec4 u_base;\n"
                           "uniform vec4 u_multiply;\n"
                           "uniform vec4 u_screen;\n"
                           "uniform sampler2D u_mask;\n"
                           "uniform float u_maskEnable;\n"
                           "uniform float u_maskInvert;\n"
                           "uniform vec2 u_maskOrigin;\n"
                           "uniform vec2 u_maskSize;\n"
                           "uniform float u_whiteOut;\n"
                           "varying vec2 v_uv;\n"
                           "varying vec4 v_col;\n"
                           "void main() {\n"
                           "  vec4 tex = texture2D(u_tex, v_uv);\n"
                           "  tex.rgb = tex.rgb * u_multiply.rgb;\n"
                           "  tex.rgb = tex.rgb + u_screen.rgb - (tex.rgb * u_screen.rgb);\n"
                           "  vec4 color = tex * u_base * v_col;\n"
                           "  if (u_whiteOut > 0.5) { color.rgb = vec3(1.0); }\n"
                           "  if (u_maskEnable > 0.5) {\n"
                           "    vec2 suv = (gl_FragCoord.xy - u_maskOrigin) / u_maskSize;\n"
                           "    float m = texture2D(u_mask, suv).a;\n"
                           "    if (u_maskInvert > 0.5) m = 1.0 - m;\n"
                           "    color.a *= m;\n"
                           "  }\n"
                           "  gl_FragColor = vec4(color.rgb * color.a, color.a);\n"
                           "}\n";

static GLuint compile(GLenum type, const char* src, std::string* error) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        if (error) *error = std::string("shader 编译失败: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool GLContext::init(std::string* error) {
    GLuint vs = compile(GL_VERTEX_SHADER, kVert, error);
    if (!vs) return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, error);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glBindAttribLocation(prog_, 0, "a_pos");
    glBindAttribLocation(prog_, 1, "a_uv");
    glBindAttribLocation(prog_, 2, "a_col");
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(prog_, sizeof(log) - 1, nullptr, log);
        if (error) *error = std::string("shader 链接失败: ") + log;
        return false;
    }
    attrPos_ = glGetAttribLocation(prog_, "a_pos");
    attrUV_ = glGetAttribLocation(prog_, "a_uv");
    attrCol_ = glGetAttribLocation(prog_, "a_col");
    uniView_ = glGetUniformLocation(prog_, "u_view");
    uniTex_ = glGetUniformLocation(prog_, "u_tex");
    uniBase_ = glGetUniformLocation(prog_, "u_base");
    uniMultiply_ = glGetUniformLocation(prog_, "u_multiply");
    uniScreen_ = glGetUniformLocation(prog_, "u_screen");
    uniMask_ = glGetUniformLocation(prog_, "u_mask");
    uniMaskEnable_ = glGetUniformLocation(prog_, "u_maskEnable");
    uniMaskInvert_ = glGetUniformLocation(prog_, "u_maskInvert");
    uniMaskOrigin_ = glGetUniformLocation(prog_, "u_maskOrigin");
    uniMaskSize_ = glGetUniformLocation(prog_, "u_maskSize");
    uniWhiteOut_ = glGetUniformLocation(prog_, "u_whiteOut");

    glGenBuffers(1, &vbo_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    return true;
}



void GLContext::present() {}  // 直接渲染，无需中转


void GLContext::shutdown() {
    if (maskTex_) glDeleteTextures(1, &maskTex_);
    if (maskFbo_) glDeleteFramebuffers(1, &maskFbo_);
    maskTex_ = maskFbo_ = 0;
    maskW_ = maskH_ = 0;
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (prog_) glDeleteProgram(prog_);
    vbo_ = 0;
    prog_ = 0;
}

void GLContext::beginFrame(int width, int height, const Color& clear) {
    // 直接画到窗口的后缓冲：FBO 中转在部分驱动上会把最后绘制的图层丢掉
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    setViewport(width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClearStencil(0);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    tint_ = Color{};
    resetColorParams();
    blend_ = Blend::Normal;
    currentTex_ = 0;
}

void GLContext::setViewport(int width, int height) {
    viewW_ = width > 0 ? width : 1;
    viewH_ = height > 0 ? height : 1;
    glViewport(0, 0, viewW_, viewH_);
}

void GLContext::setBlend(Blend mode) {
    blend_ = mode;
    switch (mode) {
        case Blend::Normal:
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case Blend::Additive:
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
            break;
        case Blend::Multiplicative:
            // Cubism 官方：乗算は color=(DST_COLOR, ONE_MINUS_SRC_ALPHA), alpha=(ZERO, ONE)
            glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            break;
    }
}

void GLContext::setColorMask(bool on) { glColorMask(on, on, on, on); }

void GLContext::setTexture(const Texture& t) {
    currentTex_ = t.id;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.id);
}

void GLContext::setTint(const Color& c) { tint_ = c; }

void GLContext::setColorParams(const Color& base, const Color& multiply, const Color& screen) {
    tint_ = base;
    multiply_ = multiply;
    screen_ = screen;
}

void GLContext::resetColorParams() {
    tint_ = Color{};
    multiply_ = Color{1, 1, 1, 1};
    screen_ = Color{0, 0, 0, 0};
}

void GLContext::setScissor(bool enable, int x, int y, int w, int h) {
    if (!enable) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    int cx = x < 0 ? 0 : x;
    int cy = y < 0 ? 0 : y;
    glScissor(cx, viewH_ - (cy + h), w < 0 ? 0 : w, h < 0 ? 0 : h);
}

void GLContext::flushMesh() {
    glUseProgram(prog_);
    glUniform2f(uniView_, float(viewW_), float(viewH_));
    glUniform1i(uniTex_, 0);
    glUniform4f(uniBase_, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform4f(uniMultiply_, multiply_.r, multiply_.g, multiply_.b, multiply_.a);
    glUniform4f(uniScreen_, screen_.r, screen_.g, screen_.b, screen_.a);
    glUniform1i(uniMask_, 1);
    glUniform1f(uniMaskEnable_, maskEnabled_ ? 1.0f : 0.0f);
    glUniform1f(uniMaskInvert_, maskInverted_ ? 1.0f : 0.0f);
    glUniform2f(uniMaskOrigin_, maskOrigin_[0], maskOrigin_[1]);
    glUniform2f(uniMaskSize_, maskSize_[0] > 0 ? maskSize_[0] : 1.0f, maskSize_[1] > 0 ? maskSize_[1] : 1.0f);
    glUniform1f(uniWhiteOut_, whiteOut_ ? 1.0f : 0.0f);
}

void GLContext::flushMeshWithBase() {
    flushMesh();
    glUniform4f(uniBase_, tint_.r, tint_.g, tint_.b, tint_.a);
}

void GLContext::quad(const Texture& t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, bool flipX) {
    if (!t.valid()) return;
    if (flipX) {
        std::swap(u0, u1);
    }
    const float pos[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    const float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
    static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    mesh(t, pos, uv, idx, 6, false);
}

void GLContext::fillRect(float x, float y, float w, float h, const Color& c) {
    const float pos[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    const float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    GLuint savedTex = currentTex_;
    Color savedTint = tint_;
    Color savedMul = multiply_;
    Color savedScr = screen_;
    // 用 1x1 白色纹理最省事；这里借用固定绑定
    static GLuint white = 0;
    if (!white) {
        const uint8_t px[4] = {255, 255, 255, 255};
        glGenTextures(1, &white);
        glBindTexture(GL_TEXTURE_2D, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    Texture wt;
    wt.id = white;
    wt.w = wt.h = 1;
    tint_ = c;
    multiply_ = Color{1, 1, 1, 1};
    screen_ = Color{0, 0, 0, 0};
    mesh(wt, pos, uv, idx, 6, false);
    tint_ = savedTint;
    multiply_ = savedMul;
    screen_ = savedScr;
    if (savedTex) glBindTexture(GL_TEXTURE_2D, savedTex);
    currentTex_ = savedTex;
}

void GLContext::mesh(const Texture& t, const float* posXY, const float* uvXY, const uint16_t* indices, int indexCount,
                     bool doubleSided) {
    if (!t.valid() || indexCount <= 0) return;
    uint16_t maxIndex = 0;
    for (int i = 0; i < indexCount; ++i)
        if (indices[i] > maxIndex) maxIndex = indices[i];
    const int vcount = int(maxIndex) + 1;

    std::vector<float> buf(size_t(vcount) * 8);
    for (int i = 0; i < vcount; ++i) {
        buf[size_t(i) * 8 + 0] = posXY[i * 2 + 0];
        buf[size_t(i) * 8 + 1] = posXY[i * 2 + 1];
        buf[size_t(i) * 8 + 2] = uvXY[i * 2 + 0];
        buf[size_t(i) * 8 + 3] = uvXY[i * 2 + 1];
        buf[size_t(i) * 8 + 4] = 1.0f;
        buf[size_t(i) * 8 + 5] = 1.0f;
        buf[size_t(i) * 8 + 6] = 1.0f;
        buf[size_t(i) * 8 + 7] = 1.0f;
    }

    flushMeshWithBase();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(buf.size() * sizeof(float)), buf.data(), GL_STREAM_DRAW);
    const GLsizei stride = GLsizei(8 * sizeof(float));
    glEnableVertexAttribArray(GLuint(attrPos_));
    glVertexAttribPointer(GLuint(attrPos_), 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(GLuint(attrUV_));
    glVertexAttribPointer(GLuint(attrUV_), 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(GLuint(attrCol_));
    glVertexAttribPointer(GLuint(attrCol_), 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.id);
    if (doubleSided) glDisable(GL_CULL_FACE);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indices);
}

void GLContext::meshStencilOnly(const float* posXY, const uint16_t* indices, int indexCount, bool doubleSided) {
    if (indexCount <= 0) return;
    uint16_t maxIndex = 0;
    for (int i = 0; i < indexCount; ++i)
        if (indices[i] > maxIndex) maxIndex = indices[i];
    const int vcount = int(maxIndex) + 1;
    std::vector<float> buf(size_t(vcount) * 8);
    for (int i = 0; i < vcount; ++i) {
        buf[size_t(i) * 8 + 0] = posXY[i * 2 + 0];
        buf[size_t(i) * 8 + 1] = posXY[i * 2 + 1];
        buf[size_t(i) * 8 + 2] = 0;
        buf[size_t(i) * 8 + 3] = 0;
        buf[size_t(i) * 8 + 4] = 1;
        buf[size_t(i) * 8 + 5] = 1;
        buf[size_t(i) * 8 + 6] = 1;
        buf[size_t(i) * 8 + 7] = 1;
    }
    flushMesh();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(buf.size() * sizeof(float)), buf.data(), GL_STREAM_DRAW);
    const GLsizei stride = GLsizei(8 * sizeof(float));
    glEnableVertexAttribArray(GLuint(attrPos_));
    glVertexAttribPointer(GLuint(attrPos_), 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(GLuint(attrUV_));
    glVertexAttribPointer(GLuint(attrUV_), 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(GLuint(attrCol_));
    glVertexAttribPointer(GLuint(attrCol_), 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    if (doubleSided) glDisable(GL_CULL_FACE);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indices);
}

Texture GLContext::createTextureRGBA(const uint8_t* rgba, int w, int h) {
    Texture t;
    if (!rgba || w <= 0 || h <= 0) return t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    t.w = w;
    t.h = h;
    return t;
}

Texture GLContext::loadTexture(const std::string& pngPath, std::string* error) {
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, pngPath.c_str())) {
        if (error) *error = "读取 PNG 失败: " + pngPath;
        return Texture{};
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> buf(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, buf.data(), 0, nullptr)) {
        if (error) *error = "解码 PNG 失败: " + pngPath;
        png_image_free(&image);
        return Texture{};
    }
    const int w = int(image.width);
    const int h = int(image.height);
    png_image_free(&image);

    // 纹理保持直通 alpha，预乘在片元着色器里做（与 Cubism 官方 shader 一致）
    return createTextureRGBA(buf.data(), w, h);
}

void GLContext::destroyTexture(Texture& t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t.id = 0;
    t.w = t.h = 0;
}

bool GLContext::beginMask(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (maskW_ != w || maskH_ != h || !maskFbo_) {
        if (maskTex_) glDeleteTextures(1, &maskTex_);
        if (maskFbo_) glDeleteFramebuffers(1, &maskFbo_);
        glGenTextures(1, &maskTex_);
        glBindTexture(GL_TEXTURE_2D, maskTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &maskFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTex_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        maskW_ = w;
        maskH_ = h;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    }
    savedViewW_ = viewW_;
    savedViewH_ = viewH_;
    glViewport(0, 0, w, h);
    viewW_ = w;
    viewH_ = h;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    whiteOut_ = true;
    return true;
}

void GLContext::endMask() {
    whiteOut_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    viewW_ = savedViewW_ > 0 ? savedViewW_ : viewW_;
    viewH_ = savedViewH_ > 0 ? savedViewH_ : viewH_;
    glViewport(0, 0, viewW_, viewH_);
    // 遮罩纹理绑到 unit1，注意别和 albedo 的 unit0 冲突
    glActiveTexture(GL_TEXTURE0);
}

void GLContext::beginWhiteOut() { whiteOut_ = true; }
void GLContext::endWhiteOut() { whiteOut_ = false; }

void GLContext::enableMask(int x, int y, int w, int h, bool inverted) {
    maskEnabled_ = true;
    maskInverted_ = inverted;
    // 转成 GL 的"左下原点"坐标
    maskOrigin_[0] = float(x);
    maskOrigin_[1] = float(viewH_ - (y + h));
    maskSize_[0] = float(w);
    maskSize_[1] = float(h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glActiveTexture(GL_TEXTURE0);
}

void GLContext::disableMask() { maskEnabled_ = false; }

void GLContext::readPixels(int x, int y, int w, int h, uint8_t* rgbaOut) {
    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgbaOut);
}

}  // namespace fx
