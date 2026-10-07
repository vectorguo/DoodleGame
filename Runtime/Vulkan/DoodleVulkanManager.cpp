//
// Created by 郭智 on 2026/9/5.
//

#include "DoodleVulkanManager.h"

namespace Doodle
{
    void DoodleVulkanManager::Initialize(GLFWwindow* pWindow)
    {
        m_device.Initialize(pWindow);
        m_swapChain.Initialize(m_device, pWindow);
        m_renderPass.Initialize(m_device, m_swapChain);
        m_graphicsPipeline.Initialize(m_device, m_renderPass);
        m_frameBuffer.Initialize(m_device, m_swapChain, m_renderPass);
        m_commandBuffer.Initialize(m_device, m_swapChain, m_renderPass, m_graphicsPipeline, m_frameBuffer);
    }

    void DoodleVulkanManager::Destroy()
    {
        //逆序销毁
        m_commandBuffer.Destroy();
        m_frameBuffer.Destroy();
        m_graphicsPipeline.Destroy();
        m_renderPass.Destroy();
        m_swapChain.Destroy();
        m_device.Destroy();
    }
}
