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
     * renderPassInfo 是基类要的设备与编译缓存，原样转交；每帧常量与逐绘制项数据范围由基类按
     * forward 的契约自己取。swapChain / depthImages 是本 pass 算附件格式的来源
     * —— 格式由本类从这两样算出来，不由调用方给。
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
     * 它负责前向 pass 特有的事：按交换链 / 深度图报出附件格式（GetAttachmentFormats，
     * 基类初始化与比对时来问），以及把这一 pass 录进命令缓冲（附件屏障、动态渲染、
     * 视口 / 剪裁、逐绘制项绘制）。管线表、着色器 → 管线的解析与预热、附件格式的
     * 比对与原地重建、per-pass 常量资源都在 DoodleRenderPass 基类里。
     *
     * 阴影 pass 目前是 DoodleShadowPass 骨架；延迟、后处理以后各派生一个，
     * 构造与销毁由 DoodleRenderPipeline 负责。
     */
    class DoodleForwardPass final : public DoodleRenderPass
    {
    public:
        DoodleForwardPass() : DoodleRenderPass(EDoodleRenderPassType::Forward)
        {
        }

        ~DoodleForwardPass() override = default;

        /**
         * 绑定前向 pass 的公共输入与颜色 / 深度附件来源
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
         * 每条绘制项按材质的着色器向基类解析管线；管线变了才重新绑定，
         * set 1 跟着失效。本 pass 内的所有管线共用每帧常量的描述符集布局与 push 范围，
         * 换管线不必重绑每帧常量。
         *
         * 本帧的相机 / 光源常量在录任何命令之前写进基类的 per-pass UBO；
         * 调用位置由 DoodleRenderPipeline 保证在帧栅栏之后。
         */
        void Render(const DoodleFrameContext& frameContext, const DoodleRenderPassContext& passContext) override;

    protected:
        /**
         * 前向 pass 的附件格式：颜色格式取自交换链、深度格式取自深度图组
         *
         * 读的是本 pass 录制时用的那两个附件来源：建管线与录制认的是同一对对象。
         * 两者都原地重建、地址不变，所以交换链重建之后问到的就是新格式
         */
        [[nodiscard]] DoodleRenderPassAttachmentFormats GetAttachmentFormats() const override;

    private:
        /**
         * 把本帧的 view / proj 与主光源写进基类的 per-pass UBO
         *
         * 调用位置与投影的推导细节见 Render 的注释；本函数只负责填数据与写入
         *
         * @param frameContext 本帧上下文，只取其中的相机与光源
         */
        void UpdatePassUbo(const DoodleFrameContext& frameContext);

        /**
         * 录一次布局转换：取图之后、开画之前，把交换链图像转成颜色附件布局
         */
        void RecordImageTransitionToColorAttachment(const DoodleFrameContext& frameContext) const;

        /**
         * 录一次布局转换：把本帧的深度图转成深度附件布局
         */
        void RecordImageTransitionToDepthAttachment(const DoodleFrameContext& frameContext) const;

        /**
         * 录一次布局转换：画完之后、交回呈现引擎之前，转回呈现布局
         */
        void RecordImageTransitionToPresent(const DoodleFrameContext& frameContext) const;

    private:
        /**
         * 颜色附件的来源，不拥有；寿命由 DoodleRenderPipeline 保证
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 深度附件的来源，不拥有；对象地址稳定，内容随交换链重建
         */
        const DoodleDepthImages* m_pDepthImages = nullptr;
    };
}
