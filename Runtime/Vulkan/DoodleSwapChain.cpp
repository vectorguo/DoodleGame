//
// Created by 郭智 on 2026/10/2.
//

#include "DoodleSwapChain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <GLFW/glfw3.h>

namespace Doodle
{
    void DoodleSwapChain::Initialize(const DoodleVulkanDevice& device, GLFWwindow* pWindow)
    {
        m_pDevice = &device;

        //创建交换链
        CreateSwapChain(pWindow);
    }

    void DoodleSwapChain::Destroy()
    {
        //销毁交换链
        DestroySwapChain();
    }

    void DoodleSwapChain::CreateSwapChain(GLFWwindow* pWindow)
    {
        const auto supportDetails = m_pDevice->QuerySwapChainSupport();
        const auto surfaceFormat = SelectSurfaceFormat(supportDetails.surfaceFormats);
        const auto presentMode = SelectPresentMode(supportDetails.surfacePresentModes);
        const auto extent = SelectExtent(supportDetails.surfaceCapabilities, pWindow);

        //决定交换链里想放多少张图像。实现会规定它能正常工作所需的最小数量
        //但仅仅贴着这个最小值，意味着我们有时不得不等待驱动完成内部操作， 才能获取下一张图像来渲染。因此建议至少比最小值多申请一张
        auto imageCount = supportDetails.surfaceCapabilities.minImageCount + 1;
        if (supportDetails.surfaceCapabilities.maxImageCount > 0 && imageCount > supportDetails.surfaceCapabilities.maxImageCount)
        {
            imageCount = supportDetails.surfaceCapabilities.maxImageCount;
        }

        //填充创建结构体
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_pDevice->GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        //队列族共享模式
        const auto& queueFamilySelection = m_pDevice->GetQueueFamilySelection();
        const uint32_t queueFamilyIndices[] = {queueFamilySelection.graphicsFamilyIndex.value(), queueFamilySelection.presentFamilyIndex.value()};
        if (queueFamilySelection.graphicsFamilyIndex == queueFamilySelection.presentFamilyIndex)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        createInfo.preTransform = supportDetails.surfaceCapabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        //创建交换链
        const auto result = vkCreateSwapchainKHR(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pSwapChain);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain!");
        }

        //记录交换链图像数据
        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainImageExtent = extent;
        vkGetSwapchainImagesKHR(m_pDevice->GetLogicalDevice(), m_pSwapChain, &imageCount, nullptr);
        m_swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_pDevice->GetLogicalDevice(), m_pSwapChain, &imageCount, m_swapChainImages.data());
    }

    void DoodleSwapChain::DestroySwapChain()
    {
        if (m_pSwapChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_pDevice->GetLogicalDevice(), m_pSwapChain, nullptr);
            m_pSwapChain = VK_NULL_HANDLE;
        }

        //句柄之外的状态一并复位，重建时从干净状态开始
        m_swapChainImages.clear();
        m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
        m_swapChainImageExtent = {};
    }

    VkSurfaceFormatKHR DoodleSwapChain::SelectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableSurfaceFormats)
    {
        for (const auto& surfaceFormat : availableSurfaceFormats)
        {
            if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return surfaceFormat;
            }
        }

        if (availableSurfaceFormats.empty())
        {
            throw std::runtime_error("No available surface formats found!");
        }

        return availableSurfaceFormats[0];
    }

    VkPresentModeKHR DoodleSwapChain::SelectPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D DoodleSwapChain::SelectExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* pWindow)
    {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return surfaceCapabilities.currentExtent;
        }

        int width, height;
        glfwGetFramebufferSize(pWindow, &width, &height);
        VkExtent2D framebufferExtent =
        {
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height)
        };

        framebufferExtent.width = std::clamp(framebufferExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        framebufferExtent.height = std::clamp(framebufferExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        return framebufferExtent;
    }
}
