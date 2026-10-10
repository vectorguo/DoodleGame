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
     * 图形管线层：着色器与固定功能状态编译成的不可变绘制配置
     *
     * 只读引用设备层与交换链层，生命周期须短于 DoodleSwapChain。
     *
     * 交换链在这里只出一件东西：颜色附件格式 —— Initialize 时读一次、编译进管线，
     * 之后不再读。所以交换链重建不必重建本层：重建不换格式，
     * 管线里编译进去的那份照样是对的
     *
     * 引用交换链不是为尺寸：视口与剪裁走动态状态，创建管线时不消费它
     */
    class DoodleGraphicsPipeline
    {
    public:
        /**
         * 初始化图形管线
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain);

        /**
         * 销毁图形管线
         */
        void Destroy();

        /**
         * 获取管线布局Handle
         */
        [[nodiscard]] VkPipelineLayout GetPipelineLayout() const
        {
            return m_pPipelineLayout;
        }

        /**
         * 获取图形管线Handle
         */
        [[nodiscard]] VkPipeline GetGraphicsPipeline() const
        {
            return m_pGraphicsPipeline;
        }

    private:
        /**
         * 创建管线布局
         *
         * 本阶段还没有 uniform 与 push constant，布局是空的。
         * 但管线必须有一个布局，所以仍要建一个空壳
         */
        void CreatePipelineLayout();

        /**
         * 销毁管线布局，并把句柄置空
         */
        void DestroyPipelineLayout();

        /**
         * 创建图形管线
         *
         * 前置条件：m_pPipelineLayout 已创建、m_pGraphicsPipeline 必须为空。
         * 重建时请走 Destroy 后重新 Initialize
         */
        void CreateGraphicsPipeline();

        /**
         * 销毁图形管线，并把句柄置空
         */
        void DestroyGraphicsPipeline();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定，取颜色附件格式用
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 管线布局
         */
        VkPipelineLayout m_pPipelineLayout = VK_NULL_HANDLE;

        /**
         * 图形管线
         */
        VkPipeline m_pGraphicsPipeline = VK_NULL_HANDLE;
    };
}
