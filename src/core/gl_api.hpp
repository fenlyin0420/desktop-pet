// OpenGL 头文件切换：Linux 用 GLES2，Windows 用桌面 GL（GLEW）。
#pragma once

#include <string>

#if defined(FXPET_GL_DESKTOP)
#  ifndef GLEW_STATIC
#    define GLEW_STATIC
#  endif
#  include <GL/glew.h>
#else
#  include <GLES2/gl2.h>
#endif

namespace fx {

// 在当前上下文上初始化函数表。GLES2 下为空操作。
bool loadGL(std::string* error);

}  // namespace fx
