//
// Created by 郭智 on 2026/10/2.
//

#include "DoodleSwapChain.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "DoodleVulkanDevice.h"
#include "DoodleVulkanUtils.h"
#include "SwapChainSupportDetails.h"
#include "../Platform/DoodleWindow.h"

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

        //预旋转：本工程自己把画面转到图像朝向上（见 DoodleVulkanManager::UpdateUniformBuffer），
        //所以这里如实填 currentTransform，声明"已经转好了"。
        //这意味着后面所有跟着图像尺寸走的东西（extent、视口、深度图）拿到的都是
        //面板自然方向的尺寸，不是用户看到的那个 —— 必须成对处理
        const auto preTransform = supportDetails.surfaceCapabilities.currentTransform;
        const auto extent = SelectImageExtent(supportDetails.surfaceCapabilities, window, preTransform);

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

        //填 currentTransform 是**承诺**，不是请求：它的意思是"图像内容已经按这个变换转好了"，
        //合成器因此不会再转一次。真正要转的那一步在投影里补（见 UpdateUniformBuffer）。
        //
        //桌面（macOS / Windows）上 currentTransform 恒为 IDENTITY，这里什么都不做；
        //Android 手机的自然方向是竖屏，横屏 Activity 拿到的是 90/270 —— 这条路径只在那时生效
        createInfo.preTransform = preTransform;
        createInfo.compositeAlpha = compositeAlpha;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        //创建交换链
        const auto result = vkCreateSwapchainKHR(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_swapChain);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain!");
        }

        //记录交换链图像数据
        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainImageExtent = extent;
        m_preTransform = preTransform;
        vkGetSwapchainImagesKHR(m_pDevice->GetLogicalDevice(), m_swapChain, &imageCount, nullptr);
        m_swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_pDevice->GetLogicalDevice(), m_swapChain, &imageCount, m_swapChainImages.data());
    }

    void DoodleSwapChain::DestroySwapChain()
    {
        if (m_swapChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_pDevice->GetLogicalDevice(), m_swapChain, nullptr);
            m_swapChain = VK_NULL_HANDLE;
        }

        //句柄之外的状态一并复位，重建时从干净状态开始
        m_swapChainImages.clear();
        m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
        m_swapChainImageExtent = {};
        m_preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
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

    VkExtent2D DoodleSwapChain::SelectImageExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities,
                                                  const DoodleWindow& window,
                                                  VkSurfaceTransformFlagBitsKHR preTransform)
    {
        //currentExtent 不是 uint32_t 的最大值，说明驱动已经把尺寸定死了，
        //直接用它 —— 这种情况在 Android 上是常态，桌面也常见。
        //注意它描述的是**显示方向**的尺寸，也就是用户看到的那一组
        VkExtent2D imageExtent;
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            imageExtent = surfaceCapabilities.currentExtent;
        }
        else
        {
            imageExtent = window.GetSurfaceSize();
        }

        //声明了 preTransform 的交换链要在面板自然方向上分配图像：90/270 时宽高正好是对调的。
        //不对调就等于把横图塞进竖框 —— 合成器按对调后的尺寸去读，画面被转置并拉伸。
        //窗口兜底那条路也一样：GetSurfaceSize 给的同样是显示方向的尺寸
        if (DoodleVulkanUtils::SwapsExtent(preTransform))
        {
            std::swap(imageExtent.width, imageExtent.height);
        }

        //夹在驱动给的范围里。对调之后才是图像的真实尺寸，所以这一步必须排在交换之后
        imageExtent.width = std::clamp(imageExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        imageExtent.height = std::clamp(imageExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        return imageExtent;
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
