//
// Created by 郭智 on 2026/10/1.
//

#include "DoodleVulkanDevice.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <GLFW/glfw3.h>

namespace Doodle
{
#ifndef NDEBUG
    //可用的检查工具层
    const std::vector<const char*> validationLayers =
    {
        "VK_LAYER_KHRONOS_validation"
    };
#endif

    void DoodleVulkanDevice::Initialize(GLFWwindow* pWindow)
    {
        CreateVkInstance();
        CreateSurface(pWindow);
        SelectPhysicalDevice();
        CreateLogicalDevice();
    }

    void DoodleVulkanDevice::Destroy()
    {
        DestroyLogicalDevice();
        DestroySurface();
        DestroyVkInstance();
    }

    void DoodleVulkanDevice::CreateVkInstance()
    {
#ifndef NDEBUG
        if (!IsValidationLayerSupported())
        {
            throw std::runtime_error("validation layers not available");
        }
#endif

        //AppInfo
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "DoodleGame";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "DoodleEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;
        appInfo.pNext = nullptr;

        //Extension
        const auto requiredExtensions = GetRequiredExtensions();

        //Instance CreateInfo
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.pNext = nullptr;
        createInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();
#ifdef NDEBUG
        createInfo.enabledLayerCount = 0;
#else
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
#endif

        //Create Instance
        const auto result = vkCreateInstance(&createInfo, nullptr, &m_pInstance);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create vulkan instance");
        }

        //Check Extension
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        // std::cout << "available extensions:\n";
        // for (const auto& extension : extensions)
        // {
        //     std::cout << '\t' << extension.extensionName << '\n';
        // }
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

    std::vector<const char*> DoodleVulkanDevice::GetRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        const auto** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> requiredExtensions;
        requiredExtensions.reserve(glfwExtensionCount + 1);
        for(auto i = 0; i < glfwExtensionCount; ++i)
        {
            requiredExtensions.emplace_back(glfwExtensions[i]);
        }
        requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        return requiredExtensions;
    }

    void DoodleVulkanDevice::CreateSurface(GLFWwindow* pWindow)
    {
        const auto result = glfwCreateWindowSurface(m_pInstance, pWindow, nullptr, &m_pSurface);
        if (result != VK_SUCCESS)
        {
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
