//
// Created by 郭智 on 2026/10/2.
//

#include "DoodleSwapChain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Doodle
{
    void DoodleSwapChain::Initialize(const DoodleVulkanDevice& device, const DoodleWindow& window)
    {
        m_pDevice = &device;

        //创建交换链
        CreateSwapChain(window);

        //创建图像视图
        CreateImageViews();
    }

    void DoodleSwapChain::Destroy()
    {
        //图像视图依附于交换链图像，必须先于交换链销毁
        DestroyImageViews();

        //销毁交换链
        DestroySwapChain();
    }

    void DoodleSwapChain::CreateSwapChain(const DoodleWindow& window)
    {
        const auto supportDetails = m_pDevice->QuerySwapChainSupport();
        const auto surfaceFormat = SelectSurfaceFormat(supportDetails.surfaceFormats);
        const auto presentMode = SelectSurfacePresentMode(supportDetails.surfacePresentModes);
        const auto compositeAlpha = SelectCompositeAlpha(supportDetails.surfaceCapabilities);
        const auto extent = SelectSurfaceExtent(supportDetails.surfaceCapabilities, window);

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

        //取系统当前给这个表面的变换。Android 上经常是 90/270 度（横竖屏），
        //这里照填即可 —— 让系统在合成时做旋转，比自己处理省事且不会出错
        createInfo.preTransform = supportDetails.surfaceCapabilities.currentTransform;
        createInfo.compositeAlpha = compositeAlpha;
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

        //退到驱动给的第一项。Android 上常见的是 R8G8B8A8_UNORM 或 B8G8R8A8_UNORM，
        //都不是 sRGB —— 渲染通道和图形管线都按这个格式建，所以自洽，只是颜色
        //不在 sRGB 空间里。要精确控制得另开一张 sRGB 中间图再自己转，
        //当前这个三角形不值得
        return availableSurfaceFormats[0];
    }

    VkPresentModeKHR DoodleSwapChain::SelectSurfacePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        //Android 上一般只有 FIFO，这个兜底本来就命中
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkCompositeAlphaFlagBitsKHR DoodleSwapChain::SelectCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCapabilities)
    {
        //本工程画的是不透明内容，优先 OPAQUE。
        //不能把它写死：桌面驱动基本都支持，而大量 Android 驱动只给 INHERIT
        constexpr VkCompositeAlphaFlagBitsKHR candidates[] =
        {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        };

        for (const auto candidate : candidates)
        {
            if ((surfaceCapabilities.supportedCompositeAlpha & candidate) != 0)
            {
                return candidate;
            }
        }

        //规范要求 supportedCompositeAlpha 至少有一位，走到这里说明驱动不合规。
        //给个合法值让后续流程继续，比起当场抛出更容易定位到真正的问题
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    VkExtent2D DoodleSwapChain::SelectSurfaceExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, const DoodleWindow& window)
    {
        //currentExtent 不是 uint32_t 的最大值，说明驱动已经把尺寸定死了，
        //直接用它 —— 这种情况在 Android 上是常态，桌面也常见
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return surfaceCapabilities.currentExtent;
        }

        VkExtent2D surfaceExtent = window.GetSurfaceSize();
        surfaceExtent.width = std::clamp(surfaceExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        surfaceExtent.height = std::clamp(surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        return surfaceExtent;
    }

    void DoodleSwapChain::CreateImageViews()
    {
        m_swapChainImageViews.resize(m_swapChainImages.size());

        for (size_t i = 0; i < m_swapChainImages.size(); ++i)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_swapChainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_swapChainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            const auto result = vkCreateImageView(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_swapChainImageViews[i]);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create image views!");
            }
        }
    }

    void DoodleSwapChain::DestroyImageViews()
    {
        for (const auto imageView : m_swapChainImageViews)
        {
            vkDestroyImageView(m_pDevice->GetLogicalDevice(), imageView, nullptr);
        }
        m_swapChainImageViews.clear();
    }
}
