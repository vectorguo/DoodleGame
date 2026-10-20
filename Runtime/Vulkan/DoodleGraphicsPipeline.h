//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleDepthImages.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 图形管线层：着色器与固定功能状态编译成的不可变绘制配置
     *
     * 只读引用设备层、交换链层与深度图组，生命周期须短于 DoodleSwapChain。
     *
     * 交换链在这里只出一件东西：颜色附件格式；深度图组也只出一件东西：深度格式。
     * 两者都是 Initialize 时读一次、编译进管线，之后不再读。所以交换链重建不必
     * 重建本层：重建不换格式，管线里编译进去的那份照样是对的
     *
     * 引用交换链不是为尺寸：视口与剪裁走动态状态，创建管线时不消费它
     */
    class DoodleGraphicsPipeline
    {
    public:
        /**
         * 初始化图形管线
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链，取颜色附件格式用
         * @param depthImages 已初始化的深度图组，取深度格式用
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const DoodleSwapChain& swapChain,
                        const DoodleDepthImages& depthImages);

        /**
         * 销毁图形管线
         */
        void Destroy();

        /**
         * 获取管线布局Handle
         */
        [[nodiscard]] VkPipelineLayout GetPipelineLayout() const
        {
            return m_pipelineLayout;
        }

        /**
         * 获取描述符集布局
         *
         * 门面创建描述符集时需要它。布局的所有权仍在本层：调用方只读，
         * 不能销毁，也不应该把它缓存到生命周期更长的地方
         */
        [[nodiscard]] VkDescriptorSetLayout GetDescriptorSetLayout() const
        {
            return m_descriptorSetLayout;
        }

        /**
         * 获取图形管线Handle
         */
        [[nodiscard]] VkPipeline GetGraphicsPipeline() const
        {
            return m_graphicsPipeline;
        }

    private:
        /**
         * 创建描述符集布局
         *
         * 它声明 binding 0 是一个只在顶点阶段可见的 uniform buffer。
         * 必须在 CreatePipelineLayout 之前完成 —— 管线布局会引用它
         */
        void CreateDescriptorSetLayout();

        /**
         * 销毁描述符集布局，并把句柄置空
         *
         * 调用前必须确认描述符池已经销毁：集合从池里分配，池先走，
         * 布局才能安全销毁。顺序由 DoodleVulkanManager::Destroy 保证
         */
        void DestroyDescriptorSetLayout();

        /**
         * 创建管线布局
         *
         * 管线布局引用描述符集布局，把 UBO 的契约接进管线。
         * 仍在创建图形管线之前完成。
         */
        void CreatePipelineLayout();

        /**
         * 销毁管线布局，并把句柄置空
         */
        void DestroyPipelineLayout();

        /**
         * 创建图形管线
         *
         * 前置条件：m_pipelineLayout 已创建、m_graphicsPipeline 必须为空。
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
         * 深度图组，在 Initialize 时绑定，取深度格式用
         */
        const DoodleDepthImages* m_pDepthImages = nullptr;

        /**
         * 描述符集布局
         *
         * 它是管线接口的一部分：管线布局引用它，门面用它分配描述符集。
         * 寿命与图形管线相同，销毁顺序为：管线 → 管线布局 → 本布局
         */
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

        /**
         * 管线布局
         */
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

        /**
         * 图形管线
         */
        VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    };
}
