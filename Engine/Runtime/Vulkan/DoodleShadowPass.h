//
//  DoodleShadowPass.h
//  Doodle
//

#pragma once

#include <vector>

#include "DoodleRenderPass.h"

namespace Doodle
{
    //只在接口签名里出现，完整定义留给源文件
    class DoodleShader;

    /**
     * 初始化阴影 pass 的全部输入
     *
     * 现在只转交基类要的设备与编译缓存；真正实现时再加 shadow map 尺寸 /
     * 格式这类本 pass 自己的配置。
     */
    struct DoodleShadowPassInitInfo
    {
        /**
         * 基类要的设备与编译缓存
         */
        DoodleRenderPassInitInfo renderPassInfo;
    };

    /**
     * 阴影 pass 骨架：接口已就位，渲染尚未实现
     *
     * 本次只保留类、pass 配置与编排位置：
     * - 不建 shadow map 深度图；
     * - 不建 per-pass UBO（passUboBufferSize = 0），因此也不会建 shadow 管线；
     * - Render / WarmupShaders 是显式空实现。
     *
     * 真正实现时按顺序接：shadow 深度目标（固定尺寸、可采样）→ set 0 UBO
     * （DoodleShadowPassUbo）→ 真实深度格式 → 逐投射物绘制（alpha 镂空用材质
     * set 1）→ forward 采样 shadow map。
     */
    class DoodleShadowPass final : public DoodleRenderPass
    {
    public:
        DoodleShadowPass() = default;
        ~DoodleShadowPass() override = default;

        /**
         * 绑定阴影 pass 的共享输入并记下预留的 pass 配置
         *
         * 本次不建任何 GPU 资源：passUboBufferSize 留 0，基类只保存 device、
         * push 范围与 passConfig。
         *
         * @param info 基类要的设备与编译缓存；都必须活得比本 pass 久
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleShadowPassInitInfo& info);

        /**
         * 录一个阴影 pass
         *
         * 本次是空操作：没有 shadow map、没有投射物绘制，也不消费任何描述符集。
         */
        void Render(const DoodleFrameContext& frameContext, const DoodleRenderPassContext& passContext) override;

        /**
         * 为这批着色器预建阴影管线
         *
         * 本次是空操作：没有 shadow 描述、也没有 set 0 布局，不能建 shadow 管线。
         * 真正实现时在这里按 shader.GetPassDescription(EDoodlePassType::Shadow) 解析。
         */
        void WarmupShaders(const std::vector<const DoodleShader*>& shaders) override;

    private:
        /**
         * 阴影 pass 的附件配置：没有颜色附件，深度格式等 shadow map 落地后再定
         */
        [[nodiscard]] static DoodleVulkanPipelinePassConfig MakePassConfig();
    };
}
