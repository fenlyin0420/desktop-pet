#!/usr/bin/env bash
# 从 Live2D 官方 SDK 包中取出 Linux 版 Cubism Core 静态库，放到 lib/ 下。
# 用法:
#   tools/fetch_live2d_core.sh                # 自动下载官方 SDK（默认 5-r.4）
#   tools/fetch_live2d_core.sh <sdk.zip>      # 用本地已下载的 SDK 压缩包
#   CUBISM_SDK_VERSION=5-r.5 tools/fetch_live2d_core.sh
#
# 说明：Core 是 Live2D 的专有二进制，不随本项目分发；下载即表示你接受 Live2D 的许可条款。
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${CUBISM_SDK_VERSION:-5-r.4}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "$#" -ge 1 ]; then
  cp "$1" "$TMP/sdk.zip"
else
  URL="https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-${VERSION}.zip"
  echo "下载 ${URL}"
  curl -fL --retry 3 -o "$TMP/sdk.zip" "$URL"
fi

ENTRY="$(unzip -Z1 "$TMP/sdk.zip" | grep -E 'Core/lib/linux/x86_64/libLive2DCubismCore\.(a|so)$' | head -1)"
if [ -z "$ENTRY" ]; then
  echo "错误：该 SDK 包里没有 Linux 版 Core 库" >&2
  exit 1
fi

unzip -o -q "$TMP/sdk.zip" "$ENTRY" -d "$TMP/out"
mkdir -p "$HERE/lib"
cp "$TMP/out/$ENTRY" "$HERE/lib/"
echo "已放置: lib/$(basename "$ENTRY")"
echo "重新运行构建即可静态链入，例如: cmake -S . -B build && cmake --build build -j"
