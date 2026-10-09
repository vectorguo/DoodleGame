//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleVulkanConfig.h"
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
     * 见 DoodleVulkanManager::DrawFrame
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
         * 获取第 frameIndex 个「图像已获取、可以开始渲染」信号量
         *
         * 由 vkAcquireNextImageKHR 在呈现引擎释放该图像后发出信号，
         * 由图形队列的提交等待
         *
         * @param frameIndex 当前在途帧索引，见 DoodleVulkanManager::m_currentFrame
         *
         * @note 按帧分配。它由本帧的提交等待，而本槽位下一轮复用它之前，
         * 必定先等过本槽位的栅栏，也就保证了本帧的提交已经执行完、
         * 这次等待已经被消费掉，信号量必定回到 unsignaled
         */
        [[nodiscard]] VkSemaphore GetImageAvailableSemaphore(const uint32_t frameIndex) const
        {
            return m_imageAvailableSemaphores[frameIndex];
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
         * 获取第 frameIndex 个帧栅栏
         *
         * 由图形队列的提交在绘制完成后发出信号，由 host 在下一轮的同一槽位上等待。
         * 它的作用是保证本槽位的资源不会被覆写 —— GPU 还在用的时候，host 不能往里重录。
         * 同时它也是「本槽位可以再用了」的唯一凭据，本层的另外两个句柄靠它决定何时可复用
         *
         * @param frameIndex 当前在途帧索引，见 DoodleVulkanManager::m_currentFrame
         *
         * @note 按帧分配，与命令缓冲一一对应
         */
        [[nodiscard]] VkFence GetInFlightFence(const uint32_t frameIndex) const
        {
            return m_inFlightFences[frameIndex];
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
         * 「图像已获取」信号量，每个在途帧一个
         */
        std::vector<VkSemaphore> m_imageAvailableSemaphores;

        /**
         * 「渲染已完成」信号量，每个交换链图像一个，按图像索引取用
         */
        std::vector<VkSemaphore> m_renderFinishedSemaphores;

        /**
         * 帧栅栏，每个在途帧一个，创建时即为已发出信号状态
         */
        std::vector<VkFence> m_inFlightFences;
    };
}
