//
//  DoodleForwardPass.h
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#pragma once

#include "DoodleRenderPass.h"

namespace Doodle
{
    //只在接口签名里出现，完整定义留给源文件
    class DoodleSwapChain;
    class DoodleDepthImages;

    /**
     * 前向主 pass：颜色附件来自交换链，深度附件来自深度图组
     *
     * 它只负责两件前向 pass 特有的事：按交换链 / 深度图算出 pass 配置，
     * 以及交换链重建后判断配置有没有变。管线表、查找、批量重建都在
     * DoodleRenderPass 基类里。
     *
     * 现在只有这一个 pass。以后阴影、延迟、后处理各派生一个，构造与销毁
     * 由渲染图或门面负责。
     */
    class DoodleForwardPass final : public DoodleRenderPass
    {
    public:
        DoodleForwardPass() = default;
        ~DoodleForwardPass() override = default;

        /**
         * 绑定前向 pass 的公共输入并算出初始 pass 配置
         *
         * @param device 已初始化的 Vulkan 设备
         * @param pipelineCompileCache 驱动编译缓存，必须活得比本 pass 久
         * @param frameSetLayout set 0 布局，必须活得比本 pass 久
         * @param swapChain 颜色附件的来源
         * @param depthImages 深度附件的来源
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleVulkanDevice& device,
                        VkPipelineCache pipelineCompileCache,
                        VkDescriptorSetLayout frameSetLayout,
                        const DoodleSwapChain& swapChain,
                        const DoodleDepthImages& depthImages);

        /**
         * 交换链 / 深度图重建后调用：配置没变就什么也不做
         *
         * 尺寸变化不影响管线（视口与剪裁是动态状态）；只有颜色 / 深度格式或
         * 采样数真的变了，才让基类原地重建这个 pass 的管线。
         */
        void RebuildIfPassConfigChanged(const DoodleSwapChain& swapChain,
                                        const DoodleDepthImages& depthImages);

    private:
        /**
         * 前向 pass 的附件配置：颜色格式来自交换链、深度格式来自深度图组
         */
        [[nodiscard]] static DoodlePipelinePassConfig MakePassConfig(
            const DoodleSwapChain& swapChain,
            const DoodleDepthImages& depthImages);
    };
}
