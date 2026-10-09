#!/usr/bin/env bash
#
# 把本目录下的 GLSL 编译成 SPIR-V，并生成一份 C 数组形式的内嵌副本头文件。
#
# 这个脚本是构建的一步，不是手动步骤 —— 改了 .vert / .frag 直接重新构建即可，
# CMakeLists.txt 会调它。之所以要这么做，是因为手动跑的那种安排有个很坏的失败模式：
# 忘了跑 → 悄悄用着上一次的旧字节码 → 画面不对但没有任何报错，很难查到根上。
#
# 用法：
#   compile.sh [输出目录]
#
#   不给参数时输出到脚本自己所在的目录。CMake 会传构建目录进来。
#   用 GLSLC 环境变量可以指定用哪个 glslc —— Android 侧用 NDK 自带的
#   （shader-tools 里有，不必装 Vulkan SDK），桌面侧用 PATH 上的。
#
# 产物两个：
#   <输出目录>/vert.spv  等 SPIR-V 字节码
#   <输出目录>/DoodleEmbeddedShaders.h  上面那些字节码的 C 数组形式
#
# 为什么需要内嵌副本：Android 上 APK 里的文件不是文件系统里的文件，
# 进程工作目录也不是工程目录，DoodleShaderModule 那两条按路径找的路子
# 一条都走不通。把字节码编进二进制，两个平台就都读得到。
# 桌面端仍然优先按路径找（那样改完着色器不重新构建也能看到效果），
# 找不到才退到内嵌副本。

set -euo pipefail

# 脚本自己所在的目录 = GLSL 源码所在处，与从哪里调用无关
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 输出目录。注意不要 cd 过去 —— 源码在 SCRIPT_DIR，两边都要用到
OUTPUT_DIR="${1:-$SCRIPT_DIR}"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

# glslc 由 Vulkan SDK 提供，或由调用方经环境变量指定
GLSLC="${GLSLC:-glslc}"

if ! command -v "$GLSLC" >/dev/null 2>&1 && [ ! -x "$GLSLC" ]; then
    echo "找不到 glslc：$GLSLC" >&2
    echo "它随 Vulkan SDK 安装，NDK 也在 shader-tools/ 下带了一份。" >&2
    echo "装在非标准位置时用 GLSLC=/path/to/glslc $0 运行。" >&2
    exit 1
fi

if ! command -v xxd >/dev/null 2>&1; then
    echo "找不到 xxd，无法生成内嵌副本。" >&2
    echo "它随 vim 一起装，Homebrew 上可以 brew install vim。" >&2
    exit 1
fi

# 着色器清单，三列用 | 分隔：
#   GLSL 源码 | 产出的 .spv 名 | 内嵌时用的数组名
# 新增着色器时在下面加一行，并同步 DoodleGraphicsPipeline 里的加载路径
SHADERS=(
    "shader.vert|vert.spv|kVertSpv"
    "shader.frag|frag.spv|kFragSpv"
)

# ---- 1. 编译 ----

for entry in "${SHADERS[@]}"; do
    IFS='|' read -r source output symbol <<< "$entry"
    "$GLSLC" "$SCRIPT_DIR/$source" -o "$OUTPUT_DIR/$output"
done

# ---- 2. 生成内嵌副本 ----

EMBEDDED_HEADER="$OUTPUT_DIR/DoodleEmbeddedShaders.h"

{
    cat <<'HEADER'
//
//  自动生成，请勿手改。
//  改着色器请编辑 Shaders/*.vert / *.frag，然后重新构建 —— 构建会重新生成本文件。
//  （细节见 Shaders/compile.sh）
//
//  这里存的是 .spv 的字节副本。桌面端通常用不上（它优先从文件读，
//  开发时能直接命中最新产物），Android 端则只能靠它 ——
//  APK 里的文件不是文件系统里的文件，按路径找根本够不着。
//
//  路径字符串与 DoodleShaderModule 收到的入参一致，两边靠它对上
//
#pragma once

#include <cstddef>

namespace Doodle::EmbeddedShaders
{
    struct Shader
    {
        const char* pPath;
        const unsigned char* pData;
        std::size_t size;
    };

HEADER

    for entry in "${SHADERS[@]}"; do
        IFS='|' read -r source output symbol <<< "$entry"
        # xxd -i 产出两段：数组本体，以及一个 _len 变量。
        # 数组加上 inline constexpr（C++17 起允许在头文件里定义变量），
        # 长度那行丢掉 —— 表里用 sizeof 取，少一个要对齐的数字
        xxd -i -n "$symbol" "$OUTPUT_DIR/$output" \
            | sed -e 's/^unsigned char /inline constexpr unsigned char /' \
                  -e '/^unsigned int .*_len = /d'
        printf '\n'
    done

    cat <<'HEADER'
    inline constexpr Shader kShaders[] =
    {
HEADER

    for entry in "${SHADERS[@]}"; do
        IFS='|' read -r source output symbol <<< "$entry"
        printf '        { "Shaders/%s", %s, sizeof(%s) },\n' "$output" "$symbol" "$symbol"
    done

    cat <<'HEADER'
    };
}
HEADER
} > "$EMBEDDED_HEADER"
