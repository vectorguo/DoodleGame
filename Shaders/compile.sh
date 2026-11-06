#!/usr/bin/env bash
#
# 把着色器源文件变成运行时文件，并生成一份 C 数组形式的内嵌副本头文件。
#
# 这个脚本是构建的一步，不是手动步骤 —— 改了着色器直接重新构建即可，
# CMakeLists.txt 会调它。之所以要这么做，是因为手动跑的那种安排有个很坏的失败模式：
# 忘了跑 → 悄悄用着上一次的旧字节码 → 画面不对但没有任何报错，很难查到根上。
#
# 用法：
#   compile.sh <输出目录> <文件名>...
#
#   文件名相对本目录（Shaders/），由 CMakeLists.txt 按通配符收集后传进来 ——
#   本脚本不认识任何具体的着色器，加删着色器都不用改这里。
#   用 GLSLC 环境变量可以指定用哪个 glslc —— Android 侧用 NDK 自带的
#   （shader-tools 里有，不必装 Vulkan SDK），桌面侧用 PATH 上的。
#
# 产物：
#   <输出目录>/Shaders/                 运行时文件，目录结构与运行时路径一致
#       lit.vert.spv、lit.frag.spv      .vert / .frag 编出的 SPIR-V，名字 = 源文件名 + .spv
#       lit.shader.json                 描述文件，原样拷贝
#   <输出目录>/DoodleEmbeddedShaders.h  上面那些文件的 C 数组形式
#
# 桌面构建把 <输出目录>/Shaders/ 整个拷到运行时目录（见 CMakeLists.txt 的
# doodle_stage_shaders），运行时按路径读到的就是它。
#
# 为什么还需要内嵌副本：Android 上 APK 里的文件不是文件系统里的文件，
# 进程工作目录也不是工程目录，DoodleShaderModule 那两条按路径找的路子
# 一条都走不通。把字节编进二进制，各个平台就都读得到。

set -euo pipefail

# 脚本自己所在的目录 = 着色器源文件所在处，与从哪里调用无关。
# 取物理路径（pwd -P）：下面要拿它和输出目录比较，符号链接会让同一个目录比不出来
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"

if [ "$#" -lt 2 ]; then
    echo "用法：$0 <输出目录> <文件名>..." >&2
    echo "文件名相对 $SCRIPT_DIR，通常由 CMakeLists.txt 收集后传进来。" >&2
    exit 2
fi

# 输出目录。注意不要 cd 过去 —— 源文件在 SCRIPT_DIR，两边都要用到
OUTPUT_DIR="$1"
shift
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd -P)"

# 运行时文件放这里。下面会先把它整个清空：输出目录给成工程根目录时，
# 它恰好就是源文件目录本身，清空等于删掉全部着色器源码 —— 这种情况直接拒绝
RUNTIME_DIR="$OUTPUT_DIR/Shaders"
if [ "$RUNTIME_DIR" = "$SCRIPT_DIR" ]; then
    echo "输出目录不能是工程根目录：$RUNTIME_DIR 就是着色器源文件所在的目录" >&2
    exit 2
fi

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

# ---- 1. 产出运行时文件 ----
#
# 先整个清空再写：删掉的着色器不能留在这里。桌面构建会把这个目录原样拷到
# 运行时目录，残留的旧文件会跟着过去，运行时照样读得到 —— 「删了却还在生效」

rm -rf "$RUNTIME_DIR"
mkdir -p "$RUNTIME_DIR"

# 产物名，与传进来的文件名一一对应、顺序相同
OUTPUTS=()

for name in "$@"; do
    input="$SCRIPT_DIR/$name"

    case "$name" in
        *.vert|*.frag)
            output="$name.spv"
            "$GLSLC" "$input" -o "$RUNTIME_DIR/$output"
            ;;
        *.shader.json)
            output="$name"
            cp "$input" "$RUNTIME_DIR/$output"
            ;;
        *)
            # 扩展名集合与 CMakeLists.txt 里的通配符是同一份清单的两半，
            # 对不上时在这里当场报错，而不是悄悄漏掉一个文件
            echo "不认识的着色器文件类型：$name" >&2
            echo "要支持新的类型，CMakeLists.txt 的通配符与本脚本的分支都要加。" >&2
            exit 1
            ;;
    esac

    # 空文件会生成零长数组，那是编译错误，而且报在 DoodleShaderModule.cpp 里，
    # 离根因很远 —— 在这里就拦下来
    if [ ! -s "$RUNTIME_DIR/$output" ]; then
        echo "着色器文件是空的：$input" >&2
        exit 1
    fi

    OUTPUTS+=("$output")
done

# ---- 2. 生成内嵌副本 ----
#
# 先写临时文件、写完再改名。直接重定向到目标会先把它截断：中途失败时
# 留下半截头文件，时间戳却比所有输入都新，下一次构建会把它当成最新的

EMBEDDED_HEADER="$OUTPUT_DIR/DoodleEmbeddedShaders.h"
EMBEDDED_HEADER_TEMP="$EMBEDDED_HEADER.tmp"

{
    cat <<'HEADER'
//
//  自动生成，请勿手改。
//  改着色器请编辑 Shaders/ 下的源文件，然后重新构建 —— 构建会重新生成本文件。
//  （细节见 Shaders/compile.sh）
//
//  这里存的是运行时文件（.spv、描述文件）的字节副本。桌面端通常用不上
//  （构建把同一批文件拷到了运行时目录，按路径就能读到），Android 端则只能靠它 ——
//  APK 里的文件不是文件系统里的文件，按路径找根本够不着。
//
//  路径字符串与 DoodleShaderModule 收到的入参一致，两边靠它对上
//
#pragma once

#include <cstddef>

namespace Doodle::EmbeddedShaders
{
    struct File
    {
        const char* pPath;
        const unsigned char* pData;
        std::size_t size;
    };

HEADER

    # 数组名用序号，不从文件名转：a-b.vert 与 a_b.vert 转成标识符会撞名。
    # 可读的名字在每个数组上面那行注释、以及下面那张表的路径字符串里
    for index in "${!OUTPUTS[@]}"; do
        printf '    // Shaders/%s\n' "${OUTPUTS[$index]}"

        # xxd -i 产出两段：数组本体，以及一个 _len 变量。
        # 数组加上 inline constexpr（C++17 起允许在头文件里定义变量），
        # 长度那行丢掉 —— 表里用 sizeof 取，少一个要对齐的数字
        xxd -i -n "kFile$index" "$RUNTIME_DIR/${OUTPUTS[$index]}" \
            | sed -e 's/^unsigned char /inline constexpr unsigned char /' \
                  -e '/^unsigned int .*_len = /d'
        printf '\n'
    done

    cat <<'HEADER'
    inline constexpr File kFiles[] =
    {
HEADER

    for index in "${!OUTPUTS[@]}"; do
        printf '        { "Shaders/%s", kFile%s, sizeof(kFile%s) },\n' "${OUTPUTS[$index]}" "$index" "$index"
    done

    cat <<'HEADER'
    };
}
HEADER
} > "$EMBEDDED_HEADER_TEMP"

mv -f "$EMBEDDED_HEADER_TEMP" "$EMBEDDED_HEADER"
