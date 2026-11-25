//
//  DoodleForwardPassData.h
//  Doodle
//

#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace Doodle
{
    /**
     * 前向 pass 的 CPU 侧着色器数据：每帧 UBO 与逐绘制项数据块
     *
     * 两者都是 C++ 与 GLSL 之间的字节布局契约，只含 glm 类型、不持有任何
     * Vulkan 句柄；区别只在更新频率：
     * - DoodleForwardPassUbo：每帧一块，本 pass 的所有绘制项共用；
     * - DoodleForwardPassDrawItemData：每条绘制项一份，经 push constant 推给着色器。
     *
     * 名字按「数据是什么」起，不按传输方式起：哪天逐绘制项数据换成每项一块 UBO
     * 或存储缓冲，改的是 DoodleForwardPass 的录制，类型名与着色器里的 block 不用动。
     * 「绘制项」就是 DoodleDrawItem：DrawItemData 是它送进本 pass 着色器的那一份
     * （localToWorld 原样、baseColor 取自它的材质），不是绘制命令本身。
     * 本文件只定数据形状；把 UBO 写进哪块缓冲、把逐绘制项数据推到哪条命令，
     * 归 DoodleForwardPass / DoodleRenderPass。
     *
     * 它们只属于前向 pass。别人的每帧数据（阴影的光空间矩阵、后处理的参数）
     * 各有自己的 per-pass 数据块，不在这份里加字段。
     */

    /**
     * 前向 pass 每帧送进着色器的数据：相机变换 + 主光源
     *
     * 布局必须与 Shaders/Include/DoodleForwardPassInterface.glsl 里的
     * uniform block 逐成员对应：
     *
     *     layout(set = 0, binding = 0, std140) uniform ForwardPassUbo
     *     {
     *         mat4 view;
     *         mat4 proj;
     *         vec4 lightDirection;
     *         vec4 lightColor;
     *     } ubo;
     *
     * 两个 mat4 都是 64 字节、按 16 字节对齐，所以偏移是 0 / 64；
     * 两个 vec4 各 16 字节，偏移 128 / 144，总长 160。
     * 下面的 static_assert 把这条契约钉在编译期：改结构体顺序、加字段，
     * 或者哪天有人引入会改变 glm 类型布局的宏，构建都会当场失败。
     *
     * localToWorld 与材质基色不在这里：它们每个绘制项都不一样，装在 UBO 里
     * 就得每个绘制项一块缓冲、或者每帧按绘制项重写同一块缓冲。它们走
     * push constant（见下面的 DoodleForwardPassDrawItemData）。
     *
     * 光源用 vec4 而不是 vec3：std140 里 vec3 的对齐是 16、占 16 字节，
     * 而 glm::vec3 是 12 字节，直接排会让后面每个成员整体错位 4 字节。
     * 两个 vec4 的第四个分量当前不用，但保持「一个成员 16 字节」让布局
     * 一眼可数 —— 将来要加环境光强度或光源类型，正好用掉那个空位
     *
     * lightDirection 的 xyz 是**光线传播方向**（从光源指向场景）的世界空间
     * 单位向量，由 DoodleLight 从宿主 Transform 的 +X 前向取得；着色器算
     * Lambert 时取它的反方向。lightColor 的 rgb 已经乘过强度。
     *
     * 为什么显式写 alignas：uniform block 按 std140 排布，mat4 / vec4 必须落在
     * 16 字节边界上；本项目不能全局定义 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES ——
     * DoodleVertex.h 依赖 glm::vec3 是 12 字节。所以这里不用宏，直接在结构体上
     * 把对齐写死，并用 offsetof 检查。
     */
    struct alignas(16) DoodleForwardPassUbo
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;

        /**
         * xyz：世界空间单位向量，光线传播方向；w 暂未使用
         */
        alignas(16) glm::vec4 lightDirection;

        /**
         * rgb：光源颜色 × 强度；w 暂未使用
         */
        alignas(16) glm::vec4 lightColor;
    };

    static_assert(offsetof(DoodleForwardPassUbo, view) == 0,             "DoodleForwardPassUbo 布局变了：view 必须从偏移 0 开始");
    static_assert(offsetof(DoodleForwardPassUbo, proj) == 64,            "DoodleForwardPassUbo 布局变了：proj 必须从偏移 64 开始");
    static_assert(offsetof(DoodleForwardPassUbo, lightDirection) == 128, "DoodleForwardPassUbo 布局变了：lightDirection 必须从偏移 128 开始");
    static_assert(offsetof(DoodleForwardPassUbo, lightColor) == 144,     "DoodleForwardPassUbo 布局变了：lightColor 必须从偏移 144 开始");
    static_assert(sizeof(DoodleForwardPassUbo) == 160,                   "DoodleForwardPassUbo 必须保持 160 字节，与着色器的 std140 布局一致");

    /**
     * 前向 pass 每条绘制项的数据块：局部到世界矩阵 + 材质基色
     *
     * 它是 C++ 与 GLSL 之间这段字节布局的共同契约：localToWorld 占 0..63，
     * baseColor 从 64 起，总长 80。这份数据经 push constant 传送，着色器里的
     * push_constant block 必须按 std430 排出同样的偏移；下面的 static_assert
     * 把这条钉在编译期。
     *
     * localToWorld 只有顶点阶段读，baseColor 只有片元阶段读 —— 一个 block
     * 覆盖两个阶段，范围仍是连续的一段。baseColor 是线性空间的材质基色：
     * CPU 侧已从配置里的 sRGB 值转换，纹理采样结果同样是线性，两者在片元里
     * 直接相乘。总长 80 字节，低于规范保证的 maxPushConstantsSize 下限 128。
     *
     * 名字与着色器里的 block `ForwardPassDrawItemData` 对齐。
     */
    struct DoodleForwardPassDrawItemData
    {
        glm::mat4 localToWorld;
        glm::vec4 baseColor;
    };

    static_assert(offsetof(DoodleForwardPassDrawItemData, localToWorld) == 0,  "DoodleForwardPassDrawItemData 布局变了：localToWorld 必须从偏移 0 开始");
    static_assert(offsetof(DoodleForwardPassDrawItemData, baseColor) == 64,    "DoodleForwardPassDrawItemData 布局变了：baseColor 必须从偏移 64 开始");
    static_assert(sizeof(DoodleForwardPassDrawItemData) == 80,                 "DoodleForwardPassDrawItemData 必须保持 80 字节，与着色器的 std430 布局一致");
}
