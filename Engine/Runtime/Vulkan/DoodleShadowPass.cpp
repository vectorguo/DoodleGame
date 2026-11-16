//
//  DoodleShadowPass.cpp
//  Doodle
//

#include "DoodleShadowPass.h"

#include "Rendering/DoodlePassShaderInterface.h"

namespace Doodle
{
    void DoodleShadowPass::Initialize(const DoodleShadowPassInitInfo& info)
    {
        //本次不建 GPU 资源：只把设备、预留的 push 范围与 pass 配置交给基类。
        //passUboBufferSize 保持 0，基类不会建 set 0 UBO / 布局 / 池 / 集合
        DoodleRenderPassInitInfo renderPassInfo = info.renderPassInfo;
        renderPassInfo.pushConstantRange = GetPassShaderInterface(EDoodlePassType::Shadow).pushConstantRange;

        DoodleRenderPass::Initialize(renderPassInfo, MakePassConfig());
    }

    void DoodleShadowPass::Render(const DoodleFrameContext&, const DoodleRenderPassContext&)
    {
        //阴影渲染尚未实现：没有 shadow map 深度目标，也没有 shadow 管线。
        //实现顺序见类注释；在此之前本 pass 只占编排位置，不往命令缓冲里录任何东西
    }

    void DoodleShadowPass::WarmupShaders(const std::vector<const DoodleShader*>&)
    {
        //没有 shadow 描述、也没有 set 0 布局，不能建 shadow 管线。
        //真正实现时在这里遍历 shaders，按 GetPassDescription(EDoodlePassType::Shadow)
        //跳过缺失的着色器，再向本 pass 的管线表解析
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
