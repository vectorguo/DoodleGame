//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleRenderPass.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 帧缓冲层：把交换链图像视图绑定到渲染通道的附件槽位
     *
     * 生命周期与交换链绑定，重建时整层换掉，设备层不受影响。
     * 只读引用设备层、交换链层与渲染通道层，生命周期须短于 DoodleRenderPass
     *
     * 不能并入 DoodleSwapChain：帧缓冲同时引用渲染通道，
     * 而渲染通道反过来依赖交换链，并进去会形成环
     */
    class DoodleFrameBuffer
    {
    public:
        /**
         * 初始化帧缓冲
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         * @param renderPass 已初始化的渲染通道
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain, const DoodleRenderPass& renderPass);

        /**
         * 销毁帧缓冲
         */
        void Destroy();

        /**
         * 获取帧缓冲，与交换链图像视图一一对应
         *
         * 绘制时用 vkAcquireNextImageKHR 返回的索引取用，不要写死下标
         */
        [[nodiscard]] const std::vector<VkFramebuffer>& GetFramebuffers() const
        {
            return m_swapChainFramebuffers;
        }

    private:
        /**
         * 创建帧缓冲，每个交换链图像视图一个
         *
         * 前置条件：m_swapChainFramebuffers 必须为空。resize 不会重置已有槽位，
         * 未先销毁就再次调用会静默覆盖旧句柄并泄漏。重建时请走 Destroy 后重新 Initialize
         */
        void CreateFramebuffers();

        /**
         * 销毁帧缓冲，并把帧缓冲数组清空
         */
        void DestroyFramebuffers();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 渲染通道层，在 Initialize 时绑定
         */
        const DoodleRenderPass* m_pRenderPass = nullptr;

        /**
         * 交换链帧缓冲，与交换链图像视图一一对应
         */
        std::vector<VkFramebuffer> m_swapChainFramebuffers;
    };
}
