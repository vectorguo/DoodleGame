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
     * 初始化前向 pass 的全部输入
     *
     * renderPassInfo 是基类要的共享服务，本类原样转交；swapChain / depthImages
     * 是本 pass 算附件配置的来源 —— 配置由本类从这两样算出来，不由调用方给。
     */
    struct DoodleForwardPassInitInfo
    {
        /**
         * 基类要的设备与共享句柄，原样转交
         */
        DoodleRenderPassInitInfo renderPassInfo;

        /**
         * 颜色附件的来源
         */
        const DoodleSwapChain& swapChain;

        /**
         * 深度附件的来源
         */
        const DoodleDepthImages& depthImages;

        /**
         * lit 着色器的 set 1 布局（材质纹理）
         *
         * 着色器资产落地前由描述符管理器提供；之后由材质自己的着色器描述取代
         */
        VkDescriptorSetLayout materialDescriptorSetLayout = VK_NULL_HANDLE;
    };

    /**
     * 前向主 pass：颜色附件来自交换链，深度附件来自深度图组
     *
     * 它负责前向 pass 特有的事：按交换链 / 深度图算出 pass 配置、交换链重建后
     * 判断配置变没变，以及把这一 pass 录进命令缓冲（附件屏障、动态渲染、
     * 视口 / 剪裁、逐绘制项绘制）。管线表、查找、批量重建都在 DoodleRenderPass 基类里。
     *
     * 现在只有这一个 pass。以后阴影、延迟、后处理各派生一个，构造与销毁
     * 由 DoodleRenderPipeline 负责。
     */
    class DoodleForwardPass final : public DoodleRenderPass
    {
    public:
        DoodleForwardPass() = default;
        ~DoodleForwardPass() override = default;

        /**
         * 绑定前向 pass 的公共输入并算出初始 pass 配置
         *
         * @param info 基类要的共享服务 + 本 pass 的颜色 / 深度附件来源；
         *        句柄与附件对象都必须活得比本 pass 久
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleForwardPassInitInfo& info);

        /**
         * 录一个前向 pass：颜色 / 深度附件、动态渲染、逐绘制项绘制
         *
         * 现在整帧所有绘制项共用同一条 lit 管线；逐项换管线是着色器资产落地后的事。
         */
        void Render(const DoodleFrameContext& frameContext, const DoodleRenderPassContext& passContext) override;

        /**
         * 交换链 / 深度图重建后调用：配置没变就什么也不做
         *
         * 尺寸变化不影响管线（视口与剪裁是动态状态）；只有颜色 / 深度格式或
         * 采样数真的变了，才让基类原地重建这个 pass 的管线。
         */
        void RebuildIfPassConfigChanged(const DoodleSwapChain& swapChain, const DoodleDepthImages& depthImages);

    private:
        /**
         * 前向 pass 的附件配置：颜色格式来自交换链、深度格式来自深度图组
         */
        [[nodiscard]] static DoodleVulkanPipelinePassConfig MakePassConfig(
            const DoodleSwapChain& swapChain,
            const DoodleDepthImages& depthImages);

        /**
         * lit 的着色器侧描述：两份字节码 + set 1 布局 + 默认渲染状态
         *
         * 着色器资产落地前，本 pass 自己读两份 spv 拼描述；之后由材质带着自己的
         * 着色器向本 pass 要管线，这个函数随之删除
         */
        [[nodiscard]] static DoodleVulkanPipelineShaderDescription MakeLitShaderDescription(
            VkDescriptorSetLayout materialDescriptorSetLayout);

        /**
         * 录一次布局转换：取图之后、开画之前，把交换链图像转成颜色附件布局
         */
        void RecordImageTransitionToColorAttachment(const DoodleFrameContext& frameContext,
                                                    const DoodleRenderPassContext& passContext) const;

        /**
         * 录一次布局转换：把本帧的深度图转成深度附件布局
         */
        void RecordImageTransitionToDepthAttachment(const DoodleFrameContext& frameContext,
                                                    const DoodleRenderPassContext& passContext) const;

        /**
         * 录一次布局转换：画完之后、交回呈现引擎之前，转回呈现布局
         */
        void RecordImageTransitionToPresent(const DoodleFrameContext& frameContext,
                                            const DoodleRenderPassContext& passContext) const;

    private:
        /**
         * 前向 pass 当前使用的 lit 管线，指向上面的管线表；原地重建时指针不变
         */
        const DoodleVulkanPipelineObject* m_pPipeline = nullptr;
    };
}
