//
// Created by 郭智 on 2026/10/2.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleVulkanDevice.h"
#include "../Platform/DoodleWindow.h"
#include "SwapChainSupportDetails.h"

namespace Doodle
{
    /**
     * 交换链层：窗口的呈现目标
     *
     * 生命周期与窗口尺寸绑定，重建时整层换掉，设备层不受影响。
     * 只读引用设备层，生命周期须短于 DoodleVulkanDevice
     */
    class DoodleSwapChain
    {
    public:
        /**
         * 初始化交换链
         * @param device 已初始化的Vulkan设备
         * @param window 窗口，用它取呈现尺寸
         */
        void Initialize(const DoodleVulkanDevice& device, const DoodleWindow& window);

        /**
         * 销毁交换链
         */
        void Destroy();

        /**
         * 获取交换链Handle
         */
        [[nodiscard]] VkSwapchainKHR GetSwapChain() const
        {
            return m_pSwapChain;
        }

        /**
         * 获取交换链图像格式
         */
        [[nodiscard]] VkFormat GetImageFormat() const
        {
            return m_swapChainImageFormat;
        }

        /**
         * 获取交换链图像尺寸
         */
        [[nodiscard]] VkExtent2D GetImageExtent() const
        {
            return m_swapChainImageExtent;
        }

        /**
         * 获取交换链图像
         */
        [[nodiscard]] const std::vector<VkImage>& GetImages() const
        {
            return m_swapChainImages;
        }

        /**
         * 获取交换链图像视图
         */
        [[nodiscard]] const std::vector<VkImageView>& GetImageViews() const
        {
            return m_swapChainImageViews;
        }

    private:
        /**
         * 创建交换链
         * @param window 窗口，用它取呈现尺寸
         */
        void CreateSwapChain(const DoodleWindow& window);

        /**
         * 销毁交换链，并把图像格式、尺寸等状态一并复位
         */
        void DestroySwapChain();

        /**
         * 选择SurfaceFormat
         * @param availableSurfaceFormats 可用的SurfaceFormat
         * @return 选中的SurfaceFormat
         */
        [[nodiscard]] static VkSurfaceFormatKHR SelectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableSurfaceFormats);

        /**
         * 选择PresentMode
         * @param availablePresentModes 可用的PresentMode
         * @return 选中的PresentMode
         */
        [[nodiscard]] static VkPresentModeKHR SelectSurfacePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

        /**
         * 选择CompositeAlpha
         *
         * 不能把 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 写死。桌面驱动基本都支持它，
         * 而大量 Android 驱动只提供 INHERIT —— 写死的话交换链创建会直接失败
         *
         * @param surfaceCapabilities 可用的SurfaceCapability
         * @return 选中的CompositeAlpha，都不可用时返回 INHERIT
         */
        [[nodiscard]] static VkCompositeAlphaFlagBitsKHR SelectCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);

        /**
         * 选择交换链图像的尺寸
         * @param surfaceCapabilities 可用的SurfaceCapability
         * @param window 窗口，currentExtent 无效时用它兜底
         * @return 交换链图像的尺寸
         */
        [[nodiscard]] static VkExtent2D SelectSurfaceExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, const DoodleWindow& window);

        /**
         * 创建图像视图，每个交换链图像一个
         *
         * 前置条件：m_swapChainImageViews 必须为空。resize 不会重置已有槽位，
         * 未先销毁就再次调用会静默覆盖旧句柄并泄漏。重建时请走 Destroy 后重新 Initialize
         */
        void CreateImageViews();

        /**
         * 销毁图像视图，并把图像视图数组清空
         */
        void DestroyImageViews();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链
         */
        VkSwapchainKHR m_pSwapChain = VK_NULL_HANDLE;

        /**
         * 交换链图像格式
         */
        VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;

        /**
         * 交换链图像尺寸
         */
        VkExtent2D m_swapChainImageExtent{};

        /**
         * 交换链图像
         */
        std::vector<VkImage> m_swapChainImages;

        /**
         * 交换链图像视图
         */
        std::vector<VkImageView> m_swapChainImageViews;
    };
}
