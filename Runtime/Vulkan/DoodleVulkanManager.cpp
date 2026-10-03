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
    }

    void DoodleVulkanManager::Destroy()
    {
        m_swapChain.Destroy();
        m_device.Destroy();
    }
}
