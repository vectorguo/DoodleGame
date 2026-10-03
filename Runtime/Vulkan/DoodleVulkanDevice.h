//
// Created by 郭智 on 2026/10/1.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "QueueFamilySelection.h"
#include "SwapChainSupportDetails.h"

//前向声明。必须放在全局作用域
struct GLFWwindow;

namespace Doodle
{
    /**
     * Vulkan设备层：Instance、Surface、物理设备、逻辑设备与队列
     * 生命周期与程序一致，窗口尺寸变化不影响本层
     */
    class DoodleVulkanDevice
    {
    public:
        /**
         * 初始化Vulkan设备
         */
        void Initialize(GLFWwindow* pWindow);

        /**
         * 销毁Vulkan设备
         */
        void Destroy();

        /**
         * 获取Vulkan实例
         */
        [[nodiscard]] VkInstance GetInstance() const
        {
            return m_pInstance;
        }

        /**
         * 获取Window Surface
         */
        [[nodiscard]] VkSurfaceKHR GetSurface() const
        {
            return m_pSurface;
        }

        /**
         * 获取物理设备Handle
         */
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const
        {
            return m_pPhysicalDevice;
        }

        /**
         * 获取逻辑设备Handle
         */
        [[nodiscard]] VkDevice GetLogicalDevice() const
        {
            return m_pLogicalDevice;
        }

        /**
         * 获取Graphics Queue
         */
        [[nodiscard]] VkQueue GetGraphicsQueue() const
        {
            return m_pGraphicsQueue;
        }

        /**
         * 获取Present Queue
         */
        [[nodiscard]] VkQueue GetPresentQueue() const
        {
            return m_pPresentQueue;
        }

        /**
         * 获取选中物理设备的队列族选择结果
         */
        [[nodiscard]] const QueueFamilySelection& GetQueueFamilySelection() const
        {
            return m_queueFamilySelection;
        }

        /**
         * 查询当前物理设备的交换链支持信息
         * @return 交换链支持信息
         */
        [[nodiscard]] SwapChainSupportDetails QuerySwapChainSupport() const;

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
        [[nodiscard]] static bool IsValidationLayerSupported();
#endif

        /**
         * 获取所需的扩展
         * @return 所需扩展的名称列表
         */
        [[nodiscard]] static std::vector<const char*> GetRequiredExtensions();

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
        void SelectPhysicalDevice();

        /**
         * 判断指定的物理设备是否合适
         * @param pDevice 指定物理设备Handle
         * @return 是否合适
         */
        [[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice pDevice) const;

        /**
         * 检查物理设备是否支持所需扩展
         * @param pDevice 指定物理设备Handle
         * @param pExtensionName 扩展名称
         * @return 是否支持
         */
        [[nodiscard]] static bool IsDeviceExtensionSupported(VkPhysicalDevice pDevice, const char* pExtensionName);

        /**
         * 为各队列角色选择所需的QueueFamily索引
         * @param pDevice 指定物理设备Handle
         * @return QueueFamilySelection
         */
        [[nodiscard]] QueueFamilySelection SelectQueueFamilies(VkPhysicalDevice pDevice) const;

        /**
         * 查询指定物理设备的交换链支持信息
         * 选择物理设备阶段 m_pPhysicalDevice 尚未确定，只能按传入的Handle查询
         * @param pDevice 指定物理设备Handle
         * @return 交换链支持信息
         */
        [[nodiscard]] SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice pDevice) const;

        /**
         * 创建逻辑设备
         */
        void CreateLogicalDevice();

        /**
         * 销毁逻辑设备
         */
        void DestroyLogicalDevice();

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
         * Vulkan逻辑设备Handle
         */
        VkDevice m_pLogicalDevice = VK_NULL_HANDLE;

        /**
         * Graphics Queue
         */
        VkQueue m_pGraphicsQueue = VK_NULL_HANDLE;

        /**
         * Present Queue
         */
        VkQueue m_pPresentQueue = VK_NULL_HANDLE;

        /**
         * 选中物理设备的队列族选择结果，在选择物理设备时确定
         */
        QueueFamilySelection m_queueFamilySelection;
    };
}
