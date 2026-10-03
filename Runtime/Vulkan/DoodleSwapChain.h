//
// Created by 郭智 on 2026/10/2.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleVulkanDevice.h"
#include "SwapChainSupportDetails.h"

//前向声明。必须放在全局作用域
struct GLFWwindow;

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
         * @param pWindow 窗口句柄
         */
        void Initialize(const DoodleVulkanDevice& device, GLFWwindow* pWindow);

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

    private:
        /**
         * 创建交换链
         * @param pWindow 窗口句柄
         */
        void CreateSwapChain(GLFWwindow* pWindow);

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
        [[nodiscard]] static VkPresentModeKHR SelectPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

        /**
         * 选择交换链图像的尺寸
         * @param surfaceCapabilities 可用的SurfaceCapability
         * @param pWindow 窗口句柄
         * @return 交换链图像的尺寸
         */
        [[nodiscard]] static VkExtent2D SelectExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* pWindow);

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
         * 交换链图像
         */
        std::vector<VkImage> m_swapChainImages;

        /**
         * 交换链图像格式
         */
        VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;

        /**
         * 交换链图像尺寸
         */
        VkExtent2D m_swapChainImageExtent{};
    };
}
