#!/usr/bin/env bash
#
# 把本目录下的 GLSL 编译成 SPIR-V 字节码。
#
# 产物与源文件同目录。程序读取时按 "Shaders/xxx.spv" 给出相对路径，
# ReadFile 会先按「进程当前工作目录」找，找不到再退到 .app 包内
# <可执行文件目录>/../Resources/Shaders/xxx.spv
# （CLion 开发时建议把 Working directory 设为工程根目录，走第一条）
#
# 用法：./Shaders/compile.sh

set -euo pipefail

# 切到脚本所在目录，这样从任何位置调用都等价
cd "$(dirname "$0")"

# glslc 由 Vulkan SDK 提供。装在非标准位置时用 GLSLC=/path/to/glslc 覆盖
GLSLC="${GLSLC:-glslc}"

if ! command -v "$GLSLC" >/dev/null 2>&1; then
    echo "找不到 glslc：$GLSLC" >&2
    echo "它随 Vulkan SDK 安装。若已安装但不在 PATH，用 GLSLC=/path/to/glslc $0 运行。" >&2
    exit 1
fi

# 新增着色器时在下面加一行。产物名与源码一对一，便于对照
"$GLSLC" shader.vert -o vert.spv
"$GLSLC" shader.frag -o frag.spv

echo "编译完成："
ls -l ./*.spv
