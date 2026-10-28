cmake_minimum_required(VERSION 3.16)
project(fxpet LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLES REQUIRED egl glesv2)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
find_package(PNG REQUIRED)
find_package(X11 REQUIRED)
find_library(XTST_LIB Xtst REQUIRED)
find_library(XEXT_LIB Xext REQUIRED)

# ---------------------------------------------------------------- Live2D Cubism Core
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

# ---------------------------------------------------------------- 目标
add_executable(fxpet
  src/main.cpp
  src/config.cpp
  src/gl.cpp
  src/text.cpp
  src/window.cpp
  src/l2d_core.cpp
  src/l2d_model.cpp
  src/l2d_physics.cpp
  src/global_input.cpp
  src/evdev_input.cpp
  src/pointer.cpp
  src/menu.cpp
  src/pet.cpp
)

target_include_directories(fxpet PRIVATE
  src
  ${X11_INCLUDE_DIR}
  ${GLES_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${FONTCONFIG_INCLUDE_DIRS}
)
target_link_directories(fxpet PRIVATE ${GLES_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS} ${FONTCONFIG_LIBRARY_DIRS})
target_link_libraries(fxpet PRIVATE
  PNG::PNG
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

install(TARGETS fxpet RUNTIME DESTINATION bin)
