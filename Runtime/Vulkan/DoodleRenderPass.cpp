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

        //渲染通道
        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;

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
