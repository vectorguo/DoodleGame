//
// Created by 郭智 on 2026/10/6.
//

#include "DoodleFrameBuffer.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleFrameBuffer::Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain, const DoodleRenderPass& renderPass)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;
        m_pRenderPass = &renderPass;

        //创建帧缓冲
        CreateFramebuffers();
    }

    void DoodleFrameBuffer::Destroy()
    {
        DestroyFramebuffers();
    }

    void DoodleFrameBuffer::CreateFramebuffers()
    {
        //尺寸与视图都从交换链层现取，本层不缓存副本，
        //这样交换链重建后不会残留旧值
        const auto& imageViews = m_pSwapChain->GetImageViews();
        const auto imageExtent = m_pSwapChain->GetImageExtent();

        //每个交换链图像视图一个帧缓冲。因为取哪张图像要等 vkAcquireNextImageKHR
        //才知道，只能提前把 N 份组合都备好
        m_swapChainFramebuffers.resize(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); ++i)
        {
            //本章只有一个颜色附件，直接把这个视图放进 0 号槽位。
            //数组顺序即索引，必须与渲染通道 pAttachments 的下标一一对应
            const VkImageView attachments[] = {imageViews[i]};

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = m_pRenderPass->GetRenderPass();
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = attachments;
            createInfo.width = imageExtent.width;
            createInfo.height = imageExtent.height;
            createInfo.layers = 1;   //图像数组的层数，与 imageArrayLayers 一致，不是图像张数

            //attachments 是栈上数组，但 vkCreateFramebuffer 只在本次调用期间读取它，
            //返回后不再引用；帧缓冲长期持有的是数组里的那些 VkImageView 句柄
            const auto result = vkCreateFramebuffer(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_swapChainFramebuffers[i]);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create framebuffers!");
            }
        }
    }

    void DoodleFrameBuffer::DestroyFramebuffers()
    {
        //帧缓冲引用了图像视图与渲染通道，必须先于两者销毁
        for (const auto framebuffer : m_swapChainFramebuffers)
        {
            vkDestroyFramebuffer(m_pDevice->GetLogicalDevice(), framebuffer, nullptr);
        }
        m_swapChainFramebuffers.clear();
    }
}
