//
//  DoodleShaderData.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace Doodle
{
    /**
     * CPU 侧的两块着色器数据：帧级 UBO 与逐绘制项 push constant
     *
     * 两者都是 C++ 与 GLSL 之间的字节布局契约，只含 glm 类型、不持有任何
     * Vulkan 句柄；区别只在更新频率：
     * - DoodleUniformBufferObject：每帧一份，整帧所有绘制项共用；
     * - DoodlePushConstantObject：每条绘制项一份，随 vkCmdPushConstants 推送。
     *
     * 放在 Rendering 而不是 Vulkan：数据形状归渲染域；把 UBO 写进哪块缓冲、
     * 把 push constant 推到哪条命令，归 DoodleVulkanManager / DoodleCommandBuffer。
     * 两块数据放在同一个头里，是因为它们总被同一条管线与同一次录制一起消费，
     * 且都不依赖 Vulkan。
     *
     * 纯数据块，保持 struct + 聚合初始化：它们没有行为，也不需要维护不变量。
     */

    /**
     * 每帧送进着色器的帧级数据：相机变换 + 主光源
     *
     * 布局必须与 Shaders/shader.vert / shader.frag 里的 uniform block 逐成员对应：
     *
     *     layout(set = 0, binding = 0, std140) uniform UniformBufferObject
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
     * push constant（见下面的 DoodlePushConstantObject 与
     * DoodleGraphicsPipeline 的范围声明），UBO 只装整帧共享的数据 ——
     * 一次写入，本帧所有绘制项共用
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
     * 为什么显式写 alignas：
     * - uniform block 按 std140 排布，mat4 / vec4 必须落在 16 字节边界上；
     * - 本项目不能全局定义 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES ——
     *   DoodleVertex.h 依赖 glm::vec3 是 12 字节、Vertex 是 32 字节，
     *   那类宏会把顶点布局一起改掉；
     * - 所以这里不用宏，直接在结构体上把对齐写死，并用 offsetof 检查。
     *
     * 结构体本身也加 alignas(16)：如果把它放进数组，元素之间也要保持 16 字节步进。
     * 当前每帧一块独立缓冲，不依赖这一点，但它让「这个类型的对齐要求」保持完整。
     */
    struct alignas(16) DoodleUniformBufferObject
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

    static_assert(offsetof(DoodleUniformBufferObject, view) == 0,
                  "DoodleUniformBufferObject 布局变了：view 必须从偏移 0 开始");
    static_assert(offsetof(DoodleUniformBufferObject, proj) == 64,
                  "DoodleUniformBufferObject 布局变了：proj 必须从偏移 64 开始");
    static_assert(offsetof(DoodleUniformBufferObject, lightDirection) == 128,
                  "DoodleUniformBufferObject 布局变了：lightDirection 必须从偏移 128 开始");
    static_assert(offsetof(DoodleUniformBufferObject, lightColor) == 144,
                  "DoodleUniformBufferObject 布局变了：lightColor 必须从偏移 144 开始");
    static_assert(sizeof(DoodleUniformBufferObject) == 160,
                  "DoodleUniformBufferObject 必须保持 160 字节，与着色器的 std140 布局一致");

    /**
     * 每个绘制项送进着色器的 push constant 数据块
     *
     * 它是 C++ 与 GLSL 之间对这段字节布局的共同契约：localToWorld 占 0..63，
     * baseColor 从 64 起，总长 80。着色器里的 push_constant block 必须
     * 按 std430 排出同样的偏移；下面的 static_assert 把这条钉在编译期。
     *
     * localToWorld 只有顶点阶段读，baseColor 只有片元阶段读 —— 一个 block
     * 覆盖两个阶段，范围仍是连续的一段。总长 80 字节，低于规范保证的
     * maxPushConstantsSize 下限 128。
     *
     * 名字与着色器里的 block `PushConstantObject` 对齐，格式和
     * DoodleUniformBufferObject 一致（Doodle + 名词 + Object）。
     */
    struct DoodlePushConstantObject
    {
        glm::mat4 localToWorld;
        glm::vec4 baseColor;
    };

    // 与着色器里 push_constant block 的 std430 布局对齐：mat4 占 0..63，
    // vec4 从 64 起，总长 80。这些数字一旦变化，录进去的字节会被着色器按
    // 另一套偏移解读，画面错乱而校验层不会报 —— 把它钉在编译期
    static_assert(offsetof(DoodlePushConstantObject, localToWorld) == 0,
                  "DoodlePushConstantObject 布局变了：localToWorld 必须从偏移 0 开始");
    static_assert(offsetof(DoodlePushConstantObject, baseColor) == 64,
                  "DoodlePushConstantObject 布局变了：baseColor 必须从偏移 64 开始");
    static_assert(sizeof(DoodlePushConstantObject) == 80,
                  "DoodlePushConstantObject 必须保持 80 字节，与着色器的 std430 布局一致");
}
