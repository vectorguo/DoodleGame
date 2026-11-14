//
//  DoodleForwardPass.h
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#pragma once

#include <unordered_map>
#include <vector>

#include "DoodleRenderPass.h"

namespace Doodle
{
    //只在接口签名里出现，完整定义留给源文件
    class DoodleSwapChain;
    class DoodleDepthImages;
    class DoodleShader;

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
         * 每条绘制项按材质的着色器向本 pass 的管线表解析管线；管线变了才重新绑定，
         * set 1 跟着失效。set 0 与 push constant 范围在全工程内相同，换管线不必重绑 set 0
         */
        void Render(const DoodleFrameContext& frameContext, const DoodleRenderPassContext& passContext) override;

        /**
         * 为这批着色器预建本 pass 的管线：按内容去重，只解析不录制
         *
         * 与 Render 共用同一个 ResolvePipeline，所以预热过的着色器在录制时不会再建管线；
         * memo 也在这里被填好，Render 里只剩一次指针查表
         */
        void WarmupShaders(const std::vector<const DoodleShader*>& shaders) override;

        /**
         * 交换链 / 深度图重建后调用：配置没变就什么也不做
         *
         * 尺寸变化不影响管线（视口与剪裁是动态状态）；只有颜色 / 深度格式或
         * 采样数真的变了，才让基类原地重建这个 pass 的管线。
         */
        void RebuildIfPassConfigChanged(const DoodleSwapChain& swapChain, const DoodleDepthImages& depthImages);

    protected:
        /**
         * 清掉着色器 → 管线的 memo；管线本身由基类先拆完
         */
        void OnDestroy() override;

    private:

        /**
         * 取某个着色器在本 pass 里的管线；没解析过就向基类的管线表要一条
         *
         * memo 只是内容键查表的缓存：着色器资产常驻（见 DoodleRenderPass 类注释），
         * 地址稳定，表的大小只跟着着色器数量走；RebuildPipelines 原地重建不改变
         * 管线对象地址，所以 memo 不需要失效
         */
        [[nodiscard]] const DoodleVulkanPipelineObject& ResolvePipeline(const DoodleShader& shader);

        /**
         * 前向 pass 的附件配置：颜色格式来自交换链、深度格式来自深度图组
         */
        [[nodiscard]] static DoodleVulkanPipelinePassConfig MakePassConfig(
            const DoodleSwapChain& swapChain,
            const DoodleDepthImages& depthImages);

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
         * 着色器 → 本 pass 的管线
         *
         * 只是「着色器描述 → 管线」这张内容键查表的 memo：着色器资产常驻，
         * 按资产地址做键是安全的，规模只跟着着色器数量走。管线原地重建不改变
         * 对象地址，所以不需要失效；Destroy 时跟着清空
         */
        std::unordered_map<const DoodleShader*, const DoodleVulkanPipelineObject*> m_pipelinesByShader;
    };
}
