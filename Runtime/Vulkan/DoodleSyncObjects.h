//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 同步对象层：帧与帧之间的信号量、以及 host 侧的栅栏
     *
     * renderFinished 按交换链图像数量分配，所以本层的生命周期与交换链绑定，
     * 重建时整层换掉。设备层不受影响。
     *
     * 只读引用设备层与交换链层，生命周期须短于 DoodleSwapChain
     *
     * 本层只持有句柄，不做等待、复位、提交这些动作：那些属于帧循环的流程，
     * 见 DoodleApplication::DrawFrame
     */
    class DoodleSyncObjects
    {
    public:
        /**
         * 初始化同步对象
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链，取图像数量用
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain);

        /**
         * 销毁同步对象
         */
        void Destroy();

        /**
         * 获取「图像已获取、可以开始渲染」信号量
         *
         * 由 vkAcquireNextImageKHR 在呈现引擎释放该图像后发出信号，
         * 由图形队列的提交等待
         *
         * @note 只此一个。它由本帧的提交等待，而提交完成由帧栅栏保证，
         * 所以下一帧复用它时必定已被消费
         */
        [[nodiscard]] VkSemaphore GetImageAvailableSemaphore() const
        {
            return m_pImageAvailableSemaphore;
        }

        /**
         * 获取第 imageIndex 张交换链图像对应的「渲染已完成、可以呈现」信号量
         *
         * 由图形队列的提交在绘制完成后发出信号，由呈现等待
         *
         * @param imageIndex vkAcquireNextImageKHR 返回的图像索引
         *
         * @note 按图像分配的原因见 CreateSyncObjects
         */
        [[nodiscard]] VkSemaphore GetRenderFinishedSemaphore(const uint32_t imageIndex) const
        {
            return m_renderFinishedSemaphores[imageIndex];
        }

        /**
         * 获取帧栅栏
         *
         * 由图形队列的提交在绘制完成后发出信号，由 host 在下一帧开头等待。
         * 它的作用是保证命令缓冲不会被覆写 —— GPU 还在用的时候，host 不能往里重录。
         *
         * @note 只此一个，因为同一时刻只允许一帧在途
         */
        [[nodiscard]] VkFence GetInFlightFence() const
        {
            return m_pInFlightFence;
        }

    private:
        /**
         * 创建信号量与栅栏
         *
         * 前置条件：m_pDevice 与 m_pSwapChain 已绑定，且句柄均为空。
         * 重建时请走 Destroy 后重新 Initialize
         */
        void CreateSyncObjects();

        /**
         * 销毁信号量与栅栏，并把句柄清空
         *
         * 销毁前须确认 GPU 已空闲（由 DoodleVulkanManager::Destroy 保证）
         */
        void DestroySyncObjects();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定，取图像数量用
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 「图像已获取」信号量
         */
        VkSemaphore m_pImageAvailableSemaphore = VK_NULL_HANDLE;

        /**
         * 「渲染已完成」信号量，每个交换链图像一个，按图像索引取用
         */
        std::vector<VkSemaphore> m_renderFinishedSemaphores;

        /**
         * 帧栅栏，创建时即为已发出信号状态
         */
        VkFence m_pInFlightFence = VK_NULL_HANDLE;
    };
}
