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
     * 本次只保留类、附件格式与编排位置：
     * - 每帧常量的 UBO / 描述符集布局 / 集合已由基类按 shadow 的契约建好
     *   （DoodleShadowPassUbo 大小），但还没有人往里写；
     * - 不建 shadow map 深度图，深度格式未定，所以也不建 shadow 管线；
     * - Render / WarmupShaders 是显式空实现。
     *
     * 真正实现时按顺序接：shadow 深度目标（固定尺寸、可采样）→ 真实深度格式 →
     * 写入每帧常量（光空间矩阵）→ 逐投射物绘制（alpha 镂空用材质的 set 1，
     * 与 forward 绑的是同一个集合）→ forward 采样 shadow map。
     */
    class DoodleShadowPass final : public DoodleRenderPass
    {
    public:
        DoodleShadowPass()
            : DoodleRenderPass(EDoodleRenderPassType::Shadow)
        {
        }

        ~DoodleShadowPass() override = default;

        /**
         * 绑定阴影 pass 的共享输入
         *
         * 基类按 shadow 的契约建出每帧常量资源；本 pass 自己还不建任何资源。
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
         * 本次是空操作：shadow map 还没建，管线要声明的深度格式还没定，不能建 shadow 管线。
         * 真正实现时去掉这个覆盖，用基类的默认实现（按 shadow 描述解析，跳过没有的）。
         */
        void WarmupShaders(const std::vector<const DoodleShader*>& shaders) override;

    protected:
        /**
         * 阴影 pass 的附件格式：没有颜色附件，深度格式等 shadow map 落地后再定
         *
         * 现在是占位：基类会在 Initialize 时记下它，但本 pass 不建管线，没有管线按它建
         */
        [[nodiscard]] DoodleRenderPassAttachmentFormats GetAttachmentFormats() const override;
    };
}
