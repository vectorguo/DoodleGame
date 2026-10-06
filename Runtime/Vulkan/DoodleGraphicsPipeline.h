//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleRenderPass.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 图形管线层：着色器、固定功能状态与渲染通道编译成的不可变绘制配置
     *
     * 生命周期与渲染通道绑定，重建时整层换掉，设备层不受影响。
     * 只读引用设备层与渲染通道层，生命周期须短于 DoodleRenderPass
     *
     * 不引用交换链：视口与剪裁走动态状态，创建管线时不消费交换链尺寸。
     * 交换链重建时本层照样会重建，但走的是传递依赖 ——
     * 交换链 → 渲染通道（句柄换新）→ 本层引用的句柄失效，由门面逆序串联接管
     */
    class DoodleGraphicsPipeline
    {
    public:
        /**
         * 初始化图形管线
         * @param device 已初始化的Vulkan设备
         * @param renderPass 已初始化的渲染通道
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleRenderPass& renderPass);

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
         * 渲染通道层，在 Initialize 时绑定
         */
        const DoodleRenderPass* m_pRenderPass = nullptr;

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
