//
//  DoodleMesh.h
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsBuffer.h"
#include "DoodleVertex.h"

namespace Doodle
{
    /**
     * 一份网格资产：顶点缓冲 + 索引缓冲 + 索引数
     *
     * 几何数据住在设备本地显存里，本类只负责这组资源的生死。它不知道
     * GameObject、Transform 或绘制命令 —— 谁摆在哪里、画不画，是场景与
     * 渲染收集的事。
     *
     * 名字不在这里：名字是 DoodleMeshLibrary 的索引键。同一份网格可以被
     * 多个对象共享，名字与资产的对应关系归库管。
     *
     * 不可复制、不可移动：两个 DoodleMesh 对象会各自销毁同一对 VkBuffer。
     * 使用方拿到的是 const 指针（非拥有），寿命由 DoodleMeshLibrary 保证；
     * 库内则用 unique_ptr 持有，不存在需要搬运对象本身的场景
     *
     * 索引类型固定为 uint16_t：与 DoodleCommandBuffer::Record 里写死的
     * VK_INDEX_TYPE_UINT16 是同一个决定。顶点数越过 65535 时，两边要一起改
     */
    class DoodleMesh
    {
    public:
        DoodleMesh() = default;

        DoodleMesh(const DoodleMesh&) = delete;
        DoodleMesh& operator=(const DoodleMesh&) = delete;
        DoodleMesh(DoodleMesh&&) = delete;
        DoodleMesh& operator=(DoodleMesh&&) = delete;

        /**
         * 把两份数据送上设备本地缓冲
         *
         * @param device 已初始化的 Vulkan 设备，且命令缓冲层的传输池已经建好
         * @param vertices 顶点数据，按 Vertex 的布局排列
         * @param indices 索引数据，每个值都必须小于 vertices.size()
         *
         * @note 内部会提交一次拷贝并等待完成，只能在初始化期调用，
         *       不得与在途的帧并发 —— 见 DoodleGraphicsBuffer::CopyTo
         */
        void Initialize(const DoodleVulkanDevice& device,
                        std::span<const Vertex> vertices,
                        std::span<const uint16_t> indices);

        /**
         * 销毁两份缓冲
         *
         * @note 前置条件：GPU 已空闲。由 DoodleMeshLibrary::Destroy 在
         *       DoodleVulkanManager 的销毁路径上统一保证
         */
        void Destroy();

        /**
         * 顶点缓冲句柄
         */
        [[nodiscard]] VkBuffer GetVertexBuffer() const
        {
            return m_vertexBuffer.GetBuffer();
        }

        /**
         * 索引缓冲句柄
         */
        [[nodiscard]] VkBuffer GetIndexBuffer() const
        {
            return m_indexBuffer.GetBuffer();
        }

        /**
         * 索引条数，即 vkCmdDrawIndexed 要的 indexCount
         *
         * 不是顶点数：本项目这颗正方体是 36 个索引、24 个顶点，两个数字不一样
         */
        [[nodiscard]] uint32_t GetIndexCount() const
        {
            return m_indexCount;
        }

    private:
        /**
         * 顶点缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_vertexBuffer;

        /**
         * 索引缓冲，走设备本地显存
         */
        DoodleGraphicsBuffer m_indexBuffer;

        /**
         * 索引条数
         */
        uint32_t m_indexCount = 0;
    };
}
