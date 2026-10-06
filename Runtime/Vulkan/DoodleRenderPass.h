//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 渲染通道层：本帧的渲染目标结构
     *
     * 生命周期与交换链绑定，重建时整层换掉，设备层不受影响。
     * 只读引用设备层与交换链层，生命周期须短于 DoodleSwapChain
     */
    class DoodleRenderPass
    {
    public:
        /**
         * 初始化渲染通道
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain);

        /**
         * 销毁渲染通道
         */
        void Destroy();

        /**
         * 获取渲染通道Handle
         */
        [[nodiscard]] VkRenderPass GetRenderPass() const
        {
            return m_pRenderPass;
        }

    private:
        /**
         * 创建渲染通道
         *
         * 前置条件：m_pRenderPass 必须为空。重建时请走 Destroy 后重新 Initialize
         */
        void CreateRenderPass();

        /**
         * 销毁渲染通道，并把句柄置空
         */
        void DestroyRenderPass();

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
         * 渲染通道
         */
        VkRenderPass m_pRenderPass = VK_NULL_HANDLE;
    };
}
