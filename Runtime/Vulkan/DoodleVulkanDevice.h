//
// Created by 郭智 on 2026/10/1.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "QueueFamilySelection.h"
#include "SwapChainSupportDetails.h"
#include "../Platform/DoodleWindow.h"

namespace Doodle
{
    /**
     * Vulkan设备层：Instance、Surface、物理设备、逻辑设备与队列
     *
     * Instance / 物理设备 / 逻辑设备 / 队列与程序同寿。
     *
     * Surface 不在此列 —— 它由窗口而来，窗口没了它就得跟着没。
     * 桌面上窗口与程序同寿，所以这个区别看不出来；Android 上切后台、
     * 锁屏、旋转都会让 ANativeWindow 消失又回来，那时必须调
     * DestroySurface / CreateSurface 把这一层换掉，
     * 而其余部分原样保留
     */
    class DoodleVulkanDevice
    {
    public:
        /**
         * 初始化Vulkan设备
         *
         * 表面也在这里创建 —— 选择物理设备时要靠它判断队列族是否支持
         * present，所以必须排在选设备之前
         */
        void Initialize(const DoodleWindow& window);

        /**
         * 销毁Vulkan设备
         */
        void Destroy();

        /**
         * 用当前窗口（重新）创建表面
         *
         * 初始化时调一次；Android 上窗口回来后还要再调。
         * 调用方负责保证此刻没有在途的帧 —— 换表面会让既有的交换链、
         * 帧缓冲、同步对象全部失效，那些由 DoodleVulkanManager 一起处理
         */
        void CreateSurface(const DoodleWindow& window);

        /**
         * 销毁表面
         */
        void DestroySurface();

        /**
         * 复查队列族选择在当前表面下是否仍然成立
         *
         * present 支持与否是(物理设备 × 队列族 × 表面)三者的函数，换了表面理论
         * 上要重查。实际不会变 —— 同一个物理设备、同一类窗口 —— 但队列是绑在
         * 逻辑设备上的，真变了也改不了，只能当场报出来，而不是带着错的假设继续跑
         *
         * 只在表面重建后调用。初始化路径上 SelectPhysicalDevice 里已经查过了
         */
        void VerifyQueueFamilySelection() const;

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
        void CreateVkInstance(const DoodleWindow& window);

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
         * @param window 提供与窗口系统相关的那几个，其余由本函数按平台追加
         * @return 所需扩展的名称列表
         */
        [[nodiscard]] static std::vector<const char*> GetRequiredExtensions(const DoodleWindow& window);

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
         * 检查物理设备是否支持动态渲染
         *
         * 1.3 起 dynamicRendering 是强制特性，但这里不看版本号、只看特性位：
         * 设备报出来的 apiVersion 会被 loader 夹到实例版本，不是驱动的上限
         *
         * @param pDevice 指定物理设备Handle
         * @return 是否支持
         */
        [[nodiscard]] static bool IsDynamicRenderingSupported(VkPhysicalDevice pDevice);

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
         *
         * 生命周期跟窗口走，不跟本对象走。见类注释
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
