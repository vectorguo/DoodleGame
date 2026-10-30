//
//  DoodleUniformBufferObject.h
//  Doodle
//
//  Created by 郭智 on 2026/10/19.
//

#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace Doodle
{
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
     * 末尾的 static_assert 把这条契约钉在编译期：改结构体顺序、加字段，
     * 或者哪天有人引入会改变 glm 类型布局的宏，构建都会当场失败。
     *
     * model 不在这里：它每个对象都不一样，装在 UBO 里就得每个对象一块缓冲、
     * 或者每帧按对象重写同一块缓冲。它走 push constant
     * （见 DoodleGraphicsPipeline 的 ModelPushConstant* 与 DoodleDrawItem），
     * UBO 只装整帧共享的数据 —— 一次写入，本帧所有对象共用
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
    struct alignas(16) UniformBufferObject
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

    static_assert(offsetof(UniformBufferObject, view) == 0,
                  "UniformBufferObject 布局变了：view 必须从偏移 0 开始");
    static_assert(offsetof(UniformBufferObject, proj) == 64,
                  "UniformBufferObject 布局变了：proj 必须从偏移 64 开始");
    static_assert(offsetof(UniformBufferObject, lightDirection) == 128,
                  "UniformBufferObject 布局变了：lightDirection 必须从偏移 128 开始");
    static_assert(offsetof(UniformBufferObject, lightColor) == 144,
                  "UniformBufferObject 布局变了：lightColor 必须从偏移 144 开始");
    static_assert(sizeof(UniformBufferObject) == 160,
                  "UniformBufferObject 必须保持 160 字节，与着色器的 std140 布局一致");
}
