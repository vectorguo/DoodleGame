//
//  DoodleForwardPass.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#include "DoodleForwardPass.h"

#include "DoodleDepthImages.h"
#include "DoodleSwapChain.h"

namespace Doodle
{
    void DoodleForwardPass::Initialize(const DoodleVulkanDevice& device,
                                       VkPipelineCache pipelineCompileCache,
                                       VkDescriptorSetLayout frameSetLayout,
                                       const DoodleSwapChain& swapChain,
                                       const DoodleDepthImages& depthImages)
    {
        DoodleRenderPass::Initialize(device,
                                     pipelineCompileCache,
                                     frameSetLayout,
                                     MakePassConfig(swapChain, depthImages));
    }

    void DoodleForwardPass::RebuildIfPassConfigChanged(const DoodleSwapChain& swapChain,
                                                       const DoodleDepthImages& depthImages)
    {
        const DoodlePipelinePassConfig passConfig = MakePassConfig(swapChain, depthImages);
        if (passConfig != GetPassConfig())
        {
            RebuildPipelines(passConfig);
        }
    }

    DoodlePipelinePassConfig DoodleForwardPass::MakePassConfig(const DoodleSwapChain& swapChain,
                                                               const DoodleDepthImages& depthImages)
    {
        return
        {
            .colorFormats = {swapChain.GetImageFormat()},
            .depthFormat = depthImages.GetFormat(),
            .sampleCount = VK_SAMPLE_COUNT_1_BIT
        };
    }
}
