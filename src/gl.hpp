// OpenGL 2.1 绘制：纹理（预乘 alpha）、任意三角形、剪裁、模板。
#pragma once

#include <cstdint>
#include <string>

#include <GLES2/gl2.h>

namespace fx {

enum class Blend {
    Normal,        // 预乘 alpha：src + dst*(1-src.a)
    Additive,      // 加算
    Multiplicative // 乘算
};

struct Texture {
    GLuint id = 0;
    int w = 0;
    int h = 0;
    bool valid() const { return id != 0; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

class GLContext {
public:
    bool init(std::string* error);
    void shutdown();

    // 渲染到离屏 FBO：读取像素（裁剪形状/截图）才有确定的语义
    void beginFrame(int width, int height, const Color& clear);
    void present();  // 直接渲染模式下为空操作
    void setViewport(int width, int height);

    Texture loadTexture(const std::string& pngPath, std::string* error);
    Texture createTextureRGBA(const uint8_t* rgba, int w, int h);
    void destroyTexture(Texture& t);

    void setBlend(Blend mode);
    void setColorMask(bool on);
    void setTexture(const Texture& t);
    void setTint(const Color& c);  // base color（相当于 SDK 的 u_baseColor）
    // Cubism 的着色参数：multiply 乘算色 + screen 加色
    void setColorParams(const Color& base, const Color& multiply, const Color& screen);
    void resetColorParams();
    void setScissor(bool enable, int x, int y, int w, int h);

    // 像素坐标（左上原点，y 向下）绘制一个纹理矩形片
    void quad(const Texture& t, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
              bool flipX = false);
    // 纯色矩形（菜单/提示用）
    void fillRect(float x, float y, float w, float h, const Color& c);
    // 任意三角形网格（Live2D 用）；pos 为窗口像素坐标，uv 归一化
    void mesh(const Texture& t, const float* posXY, const float* uvXY, const uint16_t* indices, int indexCount,
              bool doubleSided);
    // 仅写模板（模板遮罩用），不写颜色
    void meshStencilOnly(const float* posXY, const uint16_t* indices, int indexCount, bool doubleSided);

    void readPixels(int x, int y, int w, int h, uint8_t* rgbaOut);

    // ---- 遮罩（Cubism 5 的做法：遮罩画进离屏纹理，着色器里乘 alpha）
    bool beginMask(int w, int h);   // 绑定遮罩 FBO 并清空
    void endMask();                 // 回到默认帧缓冲
    void beginWhiteOut();           // 画遮罩时只输出 alpha（白色）
    void endWhiteOut();
    // 启用遮罩采样：rect 为遮罩在窗口中的矩形（左上原点，y 向下）
    void enableMask(int x, int y, int w, int h, bool inverted);
    void disableMask();

private:
    void flushMesh();
    void flushMeshWithBase();

    GLuint prog_ = 0;
    GLuint vbo_ = 0;
    GLint attrPos_ = -1, attrUV_ = -1, attrCol_ = -1;
    GLint uniView_ = -1, uniTex_ = -1, uniBase_ = -1, uniMultiply_ = -1, uniScreen_ = -1;
    int viewW_ = 1, viewH_ = 1;
    GLuint maskFbo_ = 0, maskTex_ = 0;
    int maskW_ = 0, maskH_ = 0;
    int savedViewW_ = 0, savedViewH_ = 0;
    bool whiteOut_ = false;
    bool maskEnabled_ = false;
    bool maskInverted_ = false;
    float maskOrigin_[2] = {0, 0};
    float maskSize_[2] = {1, 1};
    GLint uniMask_ = -1, uniMaskEnable_ = -1, uniMaskInvert_ = -1, uniMaskOrigin_ = -1, uniMaskSize_ = -1, uniWhiteOut_ = -1;
    Color tint_;
    Color multiply_{1, 1, 1, 1};
    Color screen_{0, 0, 0, 0};
    Blend blend_ = Blend::Normal;
    GLuint currentTex_ = 0;
};

}  // namespace fx
