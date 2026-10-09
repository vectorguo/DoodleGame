//
// Created by 郭智 on 2026/10/1.
//

#include "DoodleVulkanDevice.h"

#include <stdexcept>
#include <vector>

#include "../Platform/DoodleLog.h"

namespace Doodle
{
#ifndef NDEBUG
    //可用的检查工具层
    const std::vector<const char*> validationLayers =
    {
        "VK_LAYER_KHRONOS_validation"
    };
#endif

    void DoodleVulkanDevice::Initialize(const DoodleWindow& window)
    {
        CreateVkInstance(window);
        CreateSurface(window);
        SelectPhysicalDevice();
        CreateLogicalDevice();
    }

    void DoodleVulkanDevice::Destroy()
    {
        DestroyLogicalDevice();
        DestroySurface();
        DestroyVkInstance();
    }

    void DoodleVulkanDevice::CreateVkInstance(const DoodleWindow& window)
    {
        //AppInfo
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "DoodleGame";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "DoodleEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        //请求 1.3，为后面的动态渲染铺路：vkCmdBeginRendering 是 1.3 的核心功能，
        //而规范要求「新核心版本的设备级功能必须同时被设备版本和这里声明的版本支持」
        //（fundamentals.adoc，Valid Usage for Newer Core Versions）——
        //光设备支持不够，不在这里声明的话 loader 连函数指针都不给
        //
        //顺带解掉了压在 1.0 时的一个校验层报错：VK_KHR_portability_subset 依赖实例扩展
        //VK_KHR_get_physical_device_properties2，这个依赖要 1.1 以上才被核心版本吃掉，
        //1.0 的实例上会撞 VUID-vkCreateDevice-ppEnabledExtensionNames-01387（macOS 实测）
        //
        //代价是这个声明成了硬门槛：AOSP loader 的 SanitizeApiVersion() 只替 1.0 驱动兜底，
        //1.1/1.2 的驱动拿到更高的请求不在保护范围内，vkCreateInstance 会直接返回
        //VK_ERROR_INCOMPATIBLE_DRIVER。minSdk 仍留在 24，与这里不匹配是明知而为 ——
        //眼下只跑测试机（Android 16 / Adreno）。发布时怎么收口见 android/app/build.gradle.kts
        appInfo.apiVersion = VK_API_VERSION_1_3;
        appInfo.pNext = nullptr;

        //Extension
        const auto requiredExtensions = GetRequiredExtensions(window);

        //Instance CreateInfo
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
#ifdef __APPLE__
        //MoltenVK 把 Vulkan 翻译到 Metal 上，不是原生 Vulkan 实现。
        //这个标志让 loader 把这类「可移植」设备也枚举出来，
        //不加的话有 Vulkan 能力的 Mac 会在枚举阶段就消失
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();

        //校验层：有就用，没有就跳过。
        //
        //原先的写法是 NDEBUG 下没有校验层就抛异常。桌面这么写没问题，
        //Android 上会直接要命：那边的校验层要手动 push 到设备上才存在，
        //默认没有 —— 于是 Debug 包启动即崩，而 std::cerr 在 Android 上
        //不进 logcat，表现为纯黑屏加零线索
        uint32_t enabledLayerCount = 0;
        const char* const* ppEnabledLayerNames = nullptr;
#ifndef NDEBUG
        if (IsValidationLayerSupported())
        {
            enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            ppEnabledLayerNames = validationLayers.data();
        }
        else
        {
            Log::Info("validation layers not available, running without them");
        }
#endif
        createInfo.enabledLayerCount = enabledLayerCount;
        createInfo.ppEnabledLayerNames = ppEnabledLayerNames;

        //Create Instance
        const auto result = vkCreateInstance(&createInfo, nullptr, &m_pInstance);
        if (result != VK_SUCCESS)
        {
            Log::Error("vkCreateInstance failed");
            throw std::runtime_error("failed to create vulkan instance");
        }
    }

    void DoodleVulkanDevice::DestroyVkInstance()
    {
        if (m_pInstance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_pInstance, nullptr);
            m_pInstance = VK_NULL_HANDLE;
        }
    }

#ifndef NDEBUG
    bool DoodleVulkanDevice::IsValidationLayerSupported()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers)
        {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }
        return true;
    }
#endif

    std::vector<const char*> DoodleVulkanDevice::GetRequiredExtensions(const DoodleWindow& window)
    {
        //与窗口系统相关的那几个（VK_KHR_surface 加平台自己的 surface 扩展）
        //由窗口层给出 —— 那是唯一知道窗口是什么类型的地方
        std::vector<const char*> requiredExtensions = window.GetSurfaceExtensions();

#ifdef __APPLE__
        //与上面 ENUMERATE_PORTABILITY 标志配套，两个必须同时给
        requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        return requiredExtensions;
    }

    void DoodleVulkanDevice::CreateSurface(const DoodleWindow& window)
    {
        //先清掉旧的：Android 上这个函数会在一轮生命周期里被调第二次，
        //留着旧表面就是泄漏，而且可能和新的撞上
        DestroySurface();

        const auto result = window.CreateSurface(m_pInstance, &m_pSurface);
        if (result != VK_SUCCESS)
        {
            Log::Error("surface creation failed");
            throw std::runtime_error("failed to create surface");
        }
    }

    void DoodleVulkanDevice::DestroySurface()
    {
        if (m_pSurface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_pInstance, m_pSurface, nullptr);
            m_pSurface = VK_NULL_HANDLE;
        }
    }

    void DoodleVulkanDevice::VerifyQueueFamilySelection() const
    {
        const auto currentSelection = SelectQueueFamilies(m_pPhysicalDevice);
        if (currentSelection != m_queueFamilySelection)
        {
            //真到这里就没救了：队列是创建逻辑设备那一刻绑定的，换不了。
            //报出来总好过带着错的假设继续跑 —— 那样会在提交或呈现时
            //以完全不相干的症状炸掉
            Log::Error("queue family selection changed after surface recreation");
            throw std::runtime_error("queue family selection changed after surface recreation");
        }
    }

    void DoodleVulkanDevice::SelectPhysicalDevice()
    {
        //获取所有的物理设备数量
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_pInstance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support");
        }

        //获取所有的物理设备
        std::vector<VkPhysicalDevice> pDevices(deviceCount);
        vkEnumeratePhysicalDevices(m_pInstance, &deviceCount, pDevices.data());

        //检查是否有可用的物理设备
        for (const auto& pDevice : pDevices)
        {
            if (IsDeviceSuitable(pDevice))
            {
                m_pPhysicalDevice = pDevice;
                m_queueFamilySelection = SelectQueueFamilies(m_pPhysicalDevice);
                break;
            }
        }

        //检查是否获取到可用的物理设备
        if (m_pPhysicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to find suitable GPU");
        }
    }

    bool DoodleVulkanDevice::IsDeviceSuitable(VkPhysicalDevice pDevice) const
    {
        const QueueFamilySelection queueFamilySelection = SelectQueueFamilies(pDevice);
        if (queueFamilySelection.HasAllRequiredFamilies())
        {
            // device 级扩展：SwapChain 是必需能力，不是可选的
            if (IsDeviceExtensionSupported(pDevice, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                const auto details = QuerySwapChainSupport(pDevice);
                if (!details.surfaceFormats.empty() && !details.surfacePresentModes.empty())
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool DoodleVulkanDevice::IsDeviceExtensionSupported(VkPhysicalDevice pDevice, const char* pExtensionName)
    {
        //获取所有设备扩展数量
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &count, nullptr);

        //获取所有设备扩展属性
        std::vector<VkExtensionProperties> availableExtensions(count);
        vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &count, availableExtensions.data());

        //检查设备支持的扩展属性里是否有指定的扩展名称
        for (const auto& extension : availableExtensions)
        {
            if (strcmp(extension.extensionName, pExtensionName) == 0)
            {
                return true;
            }
        }
        return false;
    }

    QueueFamilySelection DoodleVulkanDevice::SelectQueueFamilies(VkPhysicalDevice pDevice) const
    {
        //获取所有QueueFamily数量
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);

        //获取所有QueueFamily
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

        QueueFamilySelection selection;
        for (auto i = 0; i < queueFamilyCount; ++i)
        {
            const auto queueFamily = queueFamilies[i];
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                selection.graphicsFamilyIndex = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, m_pSurface, &presentSupport);
            if (presentSupport)
            {
                selection.presentFamilyIndex = i;
            }

            //检查是否全部查找完成
            if (selection.HasAllRequiredFamilies())
            {
                break;
            }
        }
        return selection;
    }

    void DoodleVulkanDevice::CreateLogicalDevice()
    {
        constexpr auto queuePriority = 1.0f;

        //设备Queue创建信息。同一个族只允许创建一次，去重逻辑收在 QueueFamilySelection 自己身上
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        for (const uint32_t family : m_queueFamilySelection.GetUniqueFamilyIndices())
        {
            VkDeviceQueueCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            info.queueFamilyIndex = family;
            info.queueCount = 1;
            info.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(info);
        }

        //物理设备特性
        VkPhysicalDeviceFeatures deviceFeatures{};

        //设备创建信息
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pEnabledFeatures = &deviceFeatures;

        //device layer 已废弃，规范要求必须为 0；layer 只在 instance 级启用
        createInfo.enabledLayerCount = 0;

        //处理扩展
        std::vector<const char*> deviceExtensions;
        deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (IsDeviceExtensionSupported(m_pPhysicalDevice, "VK_KHR_portability_subset"))
        {
            deviceExtensions.emplace_back("VK_KHR_portability_subset");
        }
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        //创建逻辑设备
        if (vkCreateDevice(m_pPhysicalDevice, &createInfo, nullptr, &m_pLogicalDevice) == VK_SUCCESS)
        {
            vkGetDeviceQueue(m_pLogicalDevice, m_queueFamilySelection.graphicsFamilyIndex.value(), 0, &m_pGraphicsQueue);
            vkGetDeviceQueue(m_pLogicalDevice, m_queueFamilySelection.presentFamilyIndex.value(),  0, &m_pPresentQueue);
        }
        else
        {
            throw std::runtime_error("failed to create logical device!");
        }
    }

    void DoodleVulkanDevice::DestroyLogicalDevice()
    {
        if (m_pLogicalDevice != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_pLogicalDevice, nullptr);
            m_pLogicalDevice = VK_NULL_HANDLE;
        }
    }

    SwapChainSupportDetails DoodleVulkanDevice::QuerySwapChainSupport() const
    {
        return QuerySwapChainSupport(m_pPhysicalDevice);
    }

    SwapChainSupportDetails DoodleVulkanDevice::QuerySwapChainSupport(VkPhysicalDevice pDevice) const
    {
        SwapChainSupportDetails details;

        //查询基本表面能力
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pDevice, m_pSurface, &details.surfaceCapabilities);

        //查询表面格式
        uint32_t surfaceFormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, m_pSurface, &surfaceFormatCount, nullptr);
        if (surfaceFormatCount > 0)
        {
            details.surfaceFormats.resize(surfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, m_pSurface, &surfaceFormatCount, details.surfaceFormats.data());
        }

        //查询呈现模式
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, m_pSurface, &presentModeCount, nullptr);
        if (presentModeCount > 0)
        {
            details.surfacePresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, m_pSurface, &presentModeCount, details.surfacePresentModes.data());
        }

        return details;
    }
}
