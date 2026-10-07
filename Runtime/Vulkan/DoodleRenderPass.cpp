//
// Created by 郭智 on 2026/10/6.
//

#include "DoodleRenderPass.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleRenderPass::Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;

        //创建渲染通道
        CreateRenderPass();
    }

    void DoodleRenderPass::Destroy()
    {
        DestroyRenderPass();
    }

    void DoodleRenderPass::CreateRenderPass()
    {
        //附件描述：一个颜色附件，由交换链图像充当
        //格式必须与交换链图像视图一致，否则创建帧缓冲时校验层会报错
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_pSwapChain->GetImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        //附件引用：子通道通过索引 0 指向上面那个附件，并指定使用期间的布局
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        //子通道：一个图形子通道，使用该颜色附件
        //pColorAttachments 的下标直接对应片段着色器里的 layout(location = N) 输出
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        //子通道依赖：把渲染通道开头那次布局转换推迟到颜色附件输出阶段。
        //
        //渲染通道会自动把图从 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 转到
        //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL，但内建的隐式依赖假设这次转换
        //发生在管线最开头(TOP_OF_PIPE)。而那个时刻 vkAcquireNextImageKHR 的等待点
        //还没到（它在 COLOR_ATTACHMENT_OUTPUT 阶段解除），图像可能仍被呈现引擎读取，
        //这时去动它的布局就是数据竞争。
        //
        //srcSubpass 用 VK_SUBPASS_EXTERNAL 表示「渲染通道之前的隐式伪子通道」，
        //dstSubpass 用 0 表示本子通道。显式依赖与隐式依赖的 src/dst 组合相同时，
        //显式依赖会取代隐式依赖，于是转换时机被我们改写。
        //
        //四个 mask 的含义：srcStageMask 说明「等谁」——等颜色附件输出阶段，
        //与 drawFrame 里 pWaitDstStageMask 的取值必须一致，两边对不上就会出现
        //依赖链断裂；srcAccessMask 为 0 表示不等待任何内存可见性，只等执行阶段，
        //因为我们关心的是「呈现引擎别再读这张图」，而不是「谁写过它」；
        //dstStageMask/dstAccessMask 说明「谁在等」以及被保护的具体操作——写颜色附件
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        //渲染通道
        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        const auto result = vkCreateRenderPass(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pRenderPass);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    void DoodleRenderPass::DestroyRenderPass()
    {
        if (m_pRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_pDevice->GetLogicalDevice(), m_pRenderPass, nullptr);
            m_pRenderPass = VK_NULL_HANDLE;
        }
    }
}
