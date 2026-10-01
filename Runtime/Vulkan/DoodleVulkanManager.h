//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "QueueFamilyIndices.h"
#include "SwapChainSupportDetails.h"
#include "Utility/Singleton.h"

//前向声明。必须放在全局作用域
struct GLFWwindow;

namespace Doodle
{
    class DoodleVulkanManager : public Singleton<DoodleVulkanManager>
    {
    public:
        /**
         * 初始化Vulkan
         */
        void Initialize(GLFWwindow* pWindow);

        /**
         * 销毁Vulkan
         */
        void Destroy();

    private:
        /**
         * 创建Vulkan Instance
         */
        void CreateVkInstance();

        /**
         * 销毁Vulkan Instance
         */
        void DestroyVkInstance();

#ifndef NDEBUG
        /**
         * 检查ValidationLayer的支持情况
         */
        static bool IsValidationLayerSupported();
#endif

        /**
         * 获取所需的扩展
         * @return 所需扩展的名称列表
         */
        static std::vector<const char*> GetRequiredExtensions();

        /**
         * 创建Window Surface
         */
        void CreateSurface(GLFWwindow* pWindow);

        /**
         * 销毁Window Surface
         */
        void DestroySurface();

        /**
         * 选择物理设备
         */
        void ChoosePhysicalDevice();

        /**
         * 判断指定的物理设备是否合适
         * @param pDevice 指定物理设备Handle
         * @return 是否合适
         */
        bool IsDeviceSuitable(VkPhysicalDevice pDevice) const;

        /**
         * 检查物理设备是否支持所需扩展
         * @param pDevice 指定物理设备Handle
         * @param pExtensionName 扩展名称
         * @return 是否支持
         */
        static bool IsDeviceExtensionSupported(VkPhysicalDevice pDevice, const char* pExtensionName);

        /**
         * 查找所需的QueueFamily的索引
         * @param pDevice 指定物理设备Handle
         * @return QueueFamilyIndices
         */
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice pDevice) const;

        /**
         * 创建本地设备
         */
        void CreateLogicalDevice();

        /**
         * 销毁本地设备
         */
        void DestroyLogicalDevice();

        /**
         * 创建交换链
         */
        void CreateSwapChain(GLFWwindow* pWindow);

        /**
         * 销毁交换链
         */
        void DestroySwapChain();

        /**
         * 查询交换链支持
         * @param pDevice 指定物理设备Handle
         * @return 交换链支持信息
         */
        SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice pDevice) const;

        /**
         * 选择SurfaceFormat
         * @param availableSurfaceFormats 可用的SurfaceFormat
         * @return 选中的SurfaceFormat
         */
        static VkSurfaceFormatKHR ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableSurfaceFormats);

        /**
         * 选择PresentMode
         * @param availablePresentModes 可用的PresentMode
         * @return 选中的PresentMode
         */
        static VkPresentModeKHR ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

        /**
         * 选择交换链图像的分辨率
         * @param surfaceCapabilities 可用的SurfaceCapability
         * @param pWindow 窗口句柄
         * @return 交换链图像的分辨率
         */
        static VkExtent2D ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* pWindow);

    private:
        /**
         * Vulkan实例
         */
        VkInstance m_pInstance = VK_NULL_HANDLE;

        /**
         * Window Surface
         */
        VkSurfaceKHR m_pSurface = VK_NULL_HANDLE;

        /**
         * Vulkan物理设备Handle
         */
        VkPhysicalDevice m_pPhysicalDevice = VK_NULL_HANDLE;

        /**
         * Vulkan设备Handle
         */
        VkDevice m_pLogicalDevice = VK_NULL_HANDLE;

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
        VkExtent2D m_swapChainExtent{};

        /**
         * Graphics Queue
         */
        VkQueue m_pGraphicsQueue = VK_NULL_HANDLE;

        /**
         * Present Queue
         */
        VkQueue m_pPresentQueue = VK_NULL_HANDLE;
    };
}