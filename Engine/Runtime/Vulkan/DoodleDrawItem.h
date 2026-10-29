//
//  DoodleDrawItem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <glm/glm.hpp>

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleMesh;

    /**
     * 一个绘制项：画哪份网格、以什么模型矩阵画
     *
     * 这是「收集绘制对象」与「录制绘制命令」之间唯一的数据形状。
     * 收集侧不必知道 Vulkan 缓冲句柄，录制侧也不必知道 GameObject / Transform；
     * 两边只对这个结构负责。
     *
     * 存 const DoodleMesh* 而不是顶点 / 索引句柄：网格是共享资产，缓冲句柄是
     * 它的内部实现。将来加材质、实例化时，这里长的是「材质」「实例参数」，
     * 而不是把更底层的 Vulkan 句柄摊进来。
     *
     * 指针非拥有：寿命由资产管理器保证；绘制项是每帧的临时数据，
     * 不会活过资产管理器。
     * model 是收集那一刻的快照，不指回 Transform —— 收集之后场景再改位姿，
     * 本帧画出来的仍是收集时的样子，不会录到一半变掉
     */
    struct DoodleDrawItem
    {
        /**
         * 要画的网格；不可为空
         */
        const DoodleMesh* pMesh = nullptr;

        /**
         * 局部到世界的变换，作为 push constant 送进顶点着色器
         *
         * 布局由 DoodleGraphicsPipeline 里那条 VkPushConstantRange 定义，
         * 大小取自 sizeof(glm::mat4)；这里只提供数据，不参与接口约定
         */
        glm::mat4 model{1.0f};
    };
}
