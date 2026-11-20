//
//  DoodleShadowPass.cpp
//  Doodle
//

#include "DoodleShadowPass.h"

namespace Doodle
{
    void DoodleShadowPass::Initialize(const DoodleShadowPassInitInfo& info)
    {
        //set 0 与 push 范围由基类按 shadow 的契约自己取；这里只给预留的 pass 配置。
        //shadow map 还没建，本 pass 自己没有别的资源
        DoodleRenderPass::Initialize(info.renderPassInfo, MakePassConfig());
    }

    void DoodleShadowPass::Render(const DoodleFrameContext&, const DoodleRenderPassContext&)
    {
        //阴影渲染尚未实现：没有 shadow map 深度目标，也没有 shadow 管线。
        //实现顺序见类注释；在此之前本 pass 只占编排位置，不往命令缓冲里录任何东西
    }

    void DoodleShadowPass::WarmupShaders(const std::vector<const DoodleShader*>&)
    {
        //shadow map 还没建，管线要声明的深度格式还没定，不能建 shadow 管线。
        //真正实现时去掉这个覆盖，交给基类按 shadow 描述解析
    }

    DoodleVulkanPipelinePassConfig DoodleShadowPass::MakePassConfig()
    {
        //没有颜色附件；深度格式等 shadow map 深度目标落地时再填真实格式。
        //现在没有任何管线按这份配置创建，填 VK_FORMAT_UNDEFINED 而不是编一个假格式
        return
        {
            .colorFormats = {},
            .depthFormat = VK_FORMAT_UNDEFINED,
            .sampleCount = VK_SAMPLE_COUNT_1_BIT
        };
    }
}
