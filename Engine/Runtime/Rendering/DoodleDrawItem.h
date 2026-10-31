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
    class DoodleMaterial;
    class DoodleMesh;

    /**
     * 一个绘制项：画哪份网格的哪一段、用哪个材质、以什么局部到世界矩阵画
     *
     * 这是「收集绘制对象」与「录制绘制命令」之间唯一的数据形状。
     * 收集侧不必知道 Vulkan 缓冲句柄，录制侧也不必知道 GameObject / Transform；
     * 两边只对这个结构负责。
     *
     * 一条绘制项对应一次 vkCmdDrawIndexed：收集侧把每个网格按子网格展开，
     * 同一份网格的多个子网格会是相邻的多条项。子网格用下标引用，不拷贝
     * firstIndex / indexCount —— 网格的 submesh 表在 Initialize 之后不可变，
     * 下标和 pMesh 一起构成唯一真相；录制侧只做一次 GetSubmeshes()[i]。
     * 将来逐子网格做可见性、排序，也是在收集侧往这个数组里增删。
     *
     * 存 const DoodleMesh* / const DoodleMaterial* 而不是顶点 / 索引句柄或颜色：
     * 网格与材质都是共享数据的视图，缓冲句柄和材质参数是它们的内部实现。
     * 将来加实例化时，这里长的是「实例参数」，而不是把更底层的 Vulkan 句柄
     * 摊进来。
     *
     * 指针非拥有：网格寿命由资产管理器保证，材质寿命由 DoodleMeshRenderer
     * 保证（场景先于资产管理器销毁）；绘制项是每帧的临时数据，不会活过它们。
     * localToWorld 是收集那一刻的快照；pMaterial 与 submeshIndex 指向的也是
     * 收集时已经确定下来的数据，本帧不会变
     */
    struct DoodleDrawItem
    {
        /**
         * 局部到世界的变换，作为 push constant 送进顶点着色器
         *
         * 它与材质基色共用 DoodlePushConstantObject（布局与 static_assert 在
         * 那个头里；管线布局创建时按同一布局声明范围）；
         * 本项只提供 localToWorld，基色从 pMaterial 现取
         */
        glm::mat4 localToWorld{1.0f};

        /**
         * 本项使用的材质；不可为空
         *
         * 收集时按 submesh.materialIndex 从 DoodleMeshRenderer 的材质表解析，
         * 录制侧只负责把它的基色推进 push constant。指针指向 renderer 持有的
         * 材质，寿命覆盖本帧
         */
        const DoodleMaterial* pMaterial = nullptr;

        /**
         * 要画的网格；不可为空
         */
        const DoodleMesh* pMesh = nullptr;

        /**
         * 本项画网格 submesh 表里的第几段
         *
         * 区间与材质槽都在 DoodleMesh::GetSubmeshes()[submeshIndex] 里；
         * 表在 Initialize 之后不可变，所以下标不会失效
         */
        uint32_t submeshIndex = 0;
    };
}
