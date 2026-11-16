//
//  DoodleShadowPassData.h
//  Doodle
//

#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace Doodle
{
    /**
     * 阴影 pass 的 CPU 侧着色器数据：光空间矩阵与逐绘制项 push constant
     *
     * 这是编译期契约，当前没有生产者：DoodleLight 还没有光空间矩阵 API，
     * shadow map 也还没建。真正实现阴影时，由 DoodleShadowPass 每帧把
     * 光空间矩阵写进自己的 per-pass UBO，并用 DoodleShadowPassPushConstants
     * 推每个投射物的 localToWorld。
     *
     * 布局必须与 Shaders/Include/DoodleShadowPassInterface.glsl 逐成员对应：
     *
     *     layout(set = 0, binding = 0, std140) uniform ShadowPassUbo
     *     {
     *         mat4 lightView;
     *         mat4 lightProj;
     *     } shadowUbo;
     *
     * 两个 mat4 偏移 0 / 64，总长 128。保持 view / proj 分开而不是乘成一个
     * lightViewProj：顶点着色器的写法与 forward 一致，将来做级联阴影时也
     * 更容易按级联替换。
     */
    struct alignas(16) DoodleShadowPassUbo
    {
        alignas(16) glm::mat4 lightView;
        alignas(16) glm::mat4 lightProj;
    };

    static_assert(offsetof(DoodleShadowPassUbo, lightView) == 0,
                  "DoodleShadowPassUbo 布局变了：lightView 必须从偏移 0 开始");
    static_assert(offsetof(DoodleShadowPassUbo, lightProj) == 64,
                  "DoodleShadowPassUbo 布局变了：lightProj 必须从偏移 64 开始");
    static_assert(sizeof(DoodleShadowPassUbo) == 128,
                  "DoodleShadowPassUbo 必须保持 128 字节，与着色器的 std140 布局一致");

    /**
     * 阴影 pass 每条投射物的 push constant 数据块
     *
     * 阴影只关心顶点位置，不需要材质基色；localToWorld 与 forward 的
     * push constant 同形（0..63，总长 64）。alpha 镂空由片元着色器采样
     * 材质纹理完成，不走 push constant。
     */
    struct DoodleShadowPassPushConstants
    {
        glm::mat4 localToWorld;
    };

    static_assert(offsetof(DoodleShadowPassPushConstants, localToWorld) == 0,
                  "DoodleShadowPassPushConstants 布局变了：localToWorld 必须从偏移 0 开始");
    static_assert(sizeof(DoodleShadowPassPushConstants) == 64,
                  "DoodleShadowPassPushConstants 必须保持 64 字节，与着色器的 std430 布局一致");
}
