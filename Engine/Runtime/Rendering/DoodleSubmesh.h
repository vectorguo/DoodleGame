//
//  DoodleSubmesh.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <cstdint>

namespace Doodle
{
    /**
     * 网格里一段共用同一材质的连续索引区间
     *
     * 它不是一份独立的顶点 / 索引缓冲：拍平后的网格仍然只有一对缓冲，
     * 子网格只是其中的切片。三个字段正好是 vkCmdDrawIndexed 需要的
     * firstIndex / indexCount 与「用哪个材质」的槽号，所以分段绘制不需要
     * 为每段另建资源，换段时只换这几个数。
     *
     * 放在 Rendering 而不是 Vulkan：它和 DoodleDrawItem 一样是绘制侧的数据形状，
     * 不碰任何 Vulkan 句柄。DoodleMesh（Vulkan）与导入器（Asset）都引用它，
     * 依赖方向保持 Vulkan → Rendering 这条现成的单向线。
     *
     * 三个字段都是 uint32_t，与索引类型和 vkCmdDrawIndexed 的参数一致：
     * 索引缓冲本身就是 uint32_t，firstIndex / indexCount 直接透传。
     *
     * 区间按构造保证有序、首尾相接、覆盖整个索引缓冲 —— 导入器逐段顺序发射三角形。
     * DoodleMesh::Initialize 会校验这条不变量；materialIndex 槽号的合法性由
     * DoodleMeshRenderer 按网格的材质槽数保证（材质表归 renderer 所有）。
     */
    struct DoodleSubmesh
    {
        /**
         * 该段第一个索引在索引缓冲里的下标
         *
         * 是索引下标，不是字节偏移：vkCmdBindIndexBuffer 的 offset 才是字节偏移，
         * vkCmdDrawIndexed 的 firstIndex 是这里这个数
         */
        uint32_t firstIndex = 0;

        /**
         * 该段的索引条数，直接作为 vkCmdDrawIndexed 的 indexCount
         */
        uint32_t indexCount = 0;

        /**
         * 材质槽：DoodleMeshRenderer 材质表里的下标
         *
         * 导入器按首次使用顺序从 0 连续分配；同一份网格的不同 renderer 可以
         * 给同一个槽配不同的 DoodleMaterial
         */
        uint32_t materialIndex = 0;
    };
}
