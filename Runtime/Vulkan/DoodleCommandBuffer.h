//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "DoodleFrameBuffer.h"
#include "DoodleGraphicsPipeline.h"
#include "DoodleRenderPass.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 命令缓冲层：录制本帧要提交给图形队列的全部命令
     *
     * 生命周期与程序一致。命令池与命令缓冲都只建一次，反复重录不重建；
     * 交换链重建时本层也不重建 —— 本层不缓存任何上游层的句柄，
     * 录制时现取，重建后重录拿到的自然是新句柄
     *
     * 只读引用设备层、交换链层、渲染通道层、图形管线层与帧缓冲层，
     * 生命周期须短于 DoodleFrameBuffer
     *
     * 命令池与命令缓冲不拆成两个类：命令缓冲从池中分配、随池释放，
     * 两者的生命周期完全重合，没有各自独立的锚点
     */
    class DoodleCommandBuffer
    {
    public:
        /**
         * 初始化命令缓冲层
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         * @param renderPass 已初始化的渲染通道
         * @param graphicsPipeline 已初始化的图形管线
         * @param frameBuffer 已初始化的帧缓冲
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const DoodleSwapChain& swapChain,
                        const DoodleRenderPass& renderPass,
                        const DoodleGraphicsPipeline& graphicsPipeline,
                        const DoodleFrameBuffer& frameBuffer);

        /**
         * 销毁命令缓冲层
         */
        void Destroy();

        /**
         * 获取命令缓冲Handle
         */
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const
        {
            return m_pCommandBuffer;
        }

        /**
         * 录制一帧的命令
         * @param pCommandBuffer 目标命令缓冲
         * @param imageIndex 当前交换链图像的索引，来自vkAcquireNextImageKHR
         */
        void RecordCommandBuffer(VkCommandBuffer pCommandBuffer, uint32_t imageIndex);

    private:
        /**
         * 创建命令池
         */
        void CreateCommandPool();

        /**
         * 销毁命令池，并把命令池与命令缓冲的句柄一并置空
         *
         * 不需要逐个销毁命令缓冲：缓冲从池中分配，随池一起释放
         */
        void DestroyCommandPool();

        /**
         * 从命令池分配命令缓冲
         *
         * 前置条件：m_pCommandPool 已创建
         */
        void CreateCommandBuffer();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定，取渲染区域尺寸用
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 渲染通道层，在 Initialize 时绑定
         */
        const DoodleRenderPass* m_pRenderPass = nullptr;

        /**
         * 图形管线层，在 Initialize 时绑定
         */
        const DoodleGraphicsPipeline* m_pGraphicsPipeline = nullptr;

        /**
         * 帧缓冲层，在 Initialize 时绑定，按 imageIndex 取对应的那个帧缓冲
         */
        const DoodleFrameBuffer* m_pFrameBuffer = nullptr;

        /**
         * 命令池
         */
        VkCommandPool m_pCommandPool = VK_NULL_HANDLE;

        /**
         * 命令缓冲，随命令池释放，不需要单独销毁
         */
        VkCommandBuffer m_pCommandBuffer = VK_NULL_HANDLE;
    };
}
