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
     * 每帧送进顶点着色器的变换数据
     *
     * 布局必须与 Shaders/shader.vert 里的 uniform block 逐成员对应：
     *
     *     layout(set = 0, binding = 0) uniform UniformBufferObject
     *     {
     *         mat4 model;
     *         mat4 view;
     *         mat4 proj;
     *     } ubo;
     *
     * 三个 mat4 都是 64 字节、按 16 字节对齐，所以偏移是 0 / 64 / 128，
     * 总长 192。末尾的 static_assert 把这条契约钉在编译期：改结构体顺序、加字段，
     * 或者哪天有人引入会改变 glm 类型布局的宏，构建都会当场失败。
     *
     * 为什么显式写 alignas：
     * - uniform block 按 std140 排布，mat4 必须落在 16 字节边界上；
     * - 本项目不能全局定义 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES ——
     *   DoodleVertex.h 依赖 glm::vec3 是 12 字节、Vertex 是 24 字节，
     *   那类宏会把顶点布局一起改掉；
     * - 所以这里不用宏，直接在结构体上把对齐写死，并用 offsetof 检查。
     *
     * 结构体本身也加 alignas(16)：如果把它放进数组，元素之间也要保持 16 字节步进。
     * 当前每帧一块独立缓冲，不依赖这一点，但它让「这个类型的对齐要求」保持完整。
     */
    struct alignas(16) UniformBufferObject
    {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
    };

    static_assert(offsetof(UniformBufferObject, model) == 0,
                  "UniformBufferObject 布局变了：model 必须从偏移 0 开始");
    static_assert(offsetof(UniformBufferObject, view) == 64,
                  "UniformBufferObject 布局变了：view 必须从偏移 64 开始");
    static_assert(offsetof(UniformBufferObject, proj) == 128,
                  "UniformBufferObject 布局变了：proj 必须从偏移 128 开始");
    static_assert(sizeof(UniformBufferObject) == 192,
                  "UniformBufferObject 必须保持 192 字节，与着色器的 std140 布局一致");
}
