#include "gl_api.hpp"

#if defined(FXPET_GL_DESKTOP)
namespace fx {

bool loadGL(std::string* error) {
    glewExperimental = GL_TRUE;
    const GLenum result = glewInit();
    if (result != GLEW_OK) {
        if (error) *error = std::string("GLEW 初始化失败: ") + reinterpret_cast<const char*>(glewGetErrorString(result));
        return false;
    }
    // glewExperimental 在部分驱动上会产生一个 GL_INVALID_ENUM，清掉即可
    while (glGetError() != GL_NO_ERROR) {
    }
    return true;
}

}  // namespace fx
#else
namespace fx {

bool loadGL(std::string*) { return true; }

}  // namespace fx
#endif
