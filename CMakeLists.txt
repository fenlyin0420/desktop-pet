cmake_minimum_required(VERSION 3.21)
project(fxpet LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FXPET_CONSOLE "Windows: keep a console window (debug)" OFF)

if(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(FXPET_CORE_SOURCES
  src/app/main.cpp
  src/core/config.cpp
  src/core/file_io.cpp
  src/core/gl.cpp
  src/core/gl_api.cpp
  src/core/stb_impl.cpp
  src/core/text.cpp
  src/core/l2d_core.cpp
  src/core/l2d_model.cpp
  src/core/l2d_physics.cpp
  src/core/menu.cpp
  src/core/pet.cpp
  src/platform/input_common.cpp
)

if(WIN32)
  list(APPEND FXPET_CORE_SOURCES
    src/platform/windows/window_glfw.cpp
    src/platform/windows/input_raw.cpp
    src/platform/windows/paths_win.cpp
    src/platform/windows/dynlib_win.cpp
    src/platform/windows/font_win.cpp
    src/platform/windows/memory_win.cpp
  )
else()
  list(APPEND FXPET_CORE_SOURCES
    src/platform/linux/window_x11.cpp
    src/platform/linux/input_x11.cpp
    src/platform/linux/evdev_input.cpp
    src/platform/linux/pointer_wayland.cpp
    src/platform/linux/paths_posix.cpp
    src/platform/linux/dynlib_posix.cpp
    src/platform/linux/font_fontconfig.cpp
    src/platform/linux/memory_posix.cpp
  )
endif()

add_executable(fxpet ${FXPET_CORE_SOURCES})

target_include_directories(fxpet PRIVATE
  src
  src/core
  src/platform
  third_party/stb
)

# ---------------------------------------------------------------- Windows
if(WIN32)
  target_compile_definitions(fxpet PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    FXPET_GL_DESKTOP=1
    GLEW_STATIC
  )
  target_compile_options(fxpet PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wno-unused-parameter>
  )

  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
  FetchContent_MakeAvailable(glfw)

  # GLEW 只编译它自己的 glew.c，避免依赖它的 CMake 选项
  FetchContent_Declare(glew
    URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
    URL_HASH SHA256=d4fc82893cfb00109578d0a1a2337fb8ca335b3ceccf97b97e5cc7f08e4353e1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(glew)
  if(NOT glew_POPULATED)
    FetchContent_Populate(glew)
  endif()
  add_library(fxpet_glew STATIC ${glew_SOURCE_DIR}/src/glew.c)
  target_include_directories(fxpet_glew PUBLIC ${glew_SOURCE_DIR}/include)
  target_compile_definitions(fxpet_glew PUBLIC GLEW_STATIC)
  target_link_libraries(fxpet_glew PUBLIC opengl32)

  set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
  FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3)
  FetchContent_MakeAvailable(freetype)
  if(MSVC AND TARGET freetype)
    target_compile_options(freetype PRIVATE /utf-8)
  endif()

  target_link_libraries(fxpet PRIVATE
    glfw
    fxpet_glew
    freetype
    opengl32
    dwmapi
    winmm
  )

  if(NOT FXPET_CONSOLE)
    set_target_properties(fxpet PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
      # 使用 main() 作为入口，但保持 GUI 子系统（不弹控制台）
      target_link_options(fxpet PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()

  # 资源与 Core 随构建目录一起就位，方便直接运行
  add_custom_command(TARGET fxpet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:fxpet>/assets"
    COMMENT "复制 assets 到输出目录")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/Live2DCubismCore.dll")
    add_custom_command(TARGET fxpet POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:fxpet>/lib"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/lib/Live2DCubismCore.dll"
              "$<TARGET_FILE_DIR:fxpet>/lib/Live2DCubismCore.dll")
  endif()

# ---------------------------------------------------------------- Linux
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLES REQUIRED egl glesv2)
  pkg_check_modules(FREETYPE REQUIRED freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
  find_package(X11 REQUIRED)
  find_library(XTST_LIB Xtst REQUIRED)
  find_library(XEXT_LIB Xext REQUIRED)

  target_include_directories(fxpet PRIVATE
    ${X11_INCLUDE_DIR}
    ${GLES_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
  )
  target_link_directories(fxpet PRIVATE ${GLES_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS} ${FONTCONFIG_LIBRARY_DIRS})
  target_link_libraries(fxpet PRIVATE
    ${X11_LIBRARIES}
    ${XTST_LIB}
    ${XEXT_LIB}
    Xi
    ${GLES_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    dl
    m
    pthread
  )
  target_compile_options(fxpet PRIVATE -Wall -Wextra -Wno-unused-parameter)

  # -------------------------------------------------------------- Live2D Cubism Core
  # 官方 SDK 只提供静态库（Core/lib/linux/x86_64/libLive2DCubismCore.a）。
  # 存在时静态链入；否则运行时 dlopen（lib/*.so 或已安装的 live2d-py）。
  set(FXPET_CUBISM_LIB "" CACHE FILEPATH "libLive2DCubismCore.a/.so 路径（留空自动在 lib/ 下查找）")
  if(NOT FXPET_CUBISM_LIB)
    foreach(cand lib/libLive2DCubismCore.a lib/libLive2DCubismCore.so)
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${cand}")
        set(FXPET_CUBISM_LIB "${CMAKE_CURRENT_SOURCE_DIR}/${cand}")
        break()
      endif()
    endforeach()
  endif()

  if(FXPET_CUBISM_LIB MATCHES "\\.a$")
    message(STATUS "fxpet: 静态链接 Live2D Cubism Core: ${FXPET_CUBISM_LIB}")
    target_compile_definitions(fxpet PRIVATE FXPET_CUBISM_STATIC=1)
    target_link_libraries(fxpet PRIVATE ${FXPET_CUBISM_LIB})
    # Core 静态库未用 -fPIC 编译，链接非 PIE 可执行文件
    target_link_options(fxpet PRIVATE -no-pie)
    target_compile_options(fxpet PRIVATE -fno-pie)
  elseif(FXPET_CUBISM_LIB)
    message(STATUS "fxpet: 运行时加载 Live2D Cubism Core: ${FXPET_CUBISM_LIB}")
  else()
    message(STATUS "fxpet: 未找到 Live2D Cubism Core，将使用静态图回退（可运行 tools/fetch_live2d_core.sh）")
  endif()
endif()

if(WIN32)
  install(TARGETS fxpet RUNTIME DESTINATION .)
  install(DIRECTORY assets DESTINATION .)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/Live2DCubismCore.dll")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/lib/Live2DCubismCore.dll" DESTINATION lib)
  endif()
else()
  install(TARGETS fxpet RUNTIME DESTINATION bin)
endif()
