//
// Created by 郭智 on 2026/9/5.
//

#include "DoodleVulkanManager.h"

#include <iostream>
#include <stdexcept>
#include <set>
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

    void DoodleVulkanManager::Initialize(GLFWwindow* pWindow)
    {
        CreateVkInstance();
        CreateSurface(pWindow);
        ChoosePhysicalDevice();
        CreateLogicalDevice();
        CreateSwapChain(pWindow);
    }

    void DoodleVulkanManager::Destroy()
    {
        DestroySwapChain();
        DestroyLogicalDevice();

        DestroySurface();
        DestroyVkInstance();
    }

    void DoodleVulkanManager::CreateVkInstance()
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

    void DoodleVulkanManager::DestroyVkInstance()
    {
        vkDestroyInstance(m_pInstance, nullptr);
        m_pInstance = nullptr;
    }

#ifndef NDEBUG
    bool DoodleVulkanManager::IsValidationLayerSupported()
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

    std::vector<const char*> DoodleVulkanManager::GetRequiredExtensions()
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

    void DoodleVulkanManager::CreateSurface(GLFWwindow* pWindow)
    {
        const auto result = glfwCreateWindowSurface(m_pInstance, pWindow, nullptr, &m_pSurface);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create surface");
        }
    }

    void DoodleVulkanManager::DestroySurface()
    {
        if (m_pSurface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_pInstance, m_pSurface, nullptr);
            m_pSurface = VK_NULL_HANDLE;
        }
    }

    void DoodleVulkanManager::ChoosePhysicalDevice()
    {
        //获取所有的物理设备数量
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_pInstance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support");
        }

        //获取所有的物理设备
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_pInstance, &deviceCount, devices.data());

        //检查是否有可用的物理设备
        for (const auto& device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_pPhysicalDevice = device;
                break;
            }
        }

        //检查是否获取到可用的物理设备
        if (m_pPhysicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to find suitable GPU");
        }
    }

    bool DoodleVulkanManager::IsDeviceSuitable(VkPhysicalDevice pDevice) const
    {
        const QueueFamilyIndices queueFamily = FindQueueFamilies(pDevice);
        if (queueFamily.IsComplete())
        {
            // device 级扩展：SwapChain 是必需能力，不是可选的
            if (IsDeviceExtensionSupported(pDevice, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                const auto details = QuerySwapChainSupport(pDevice);
                if (!details.surfaceFormats.empty() && !details.presentModes.empty())
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool DoodleVulkanManager::IsDeviceExtensionSupported(VkPhysicalDevice pDevice, const char* pExtensionName)
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

    QueueFamilyIndices DoodleVulkanManager::FindQueueFamilies(VkPhysicalDevice pDevice) const
    {
        QueueFamilyIndices indices;

        //获取所有QueueFamily数量
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);

        //获取所有QueueFamily
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

        for (auto i = 0; i < queueFamilyCount; ++i)
        {
            const auto queueFamily = queueFamilies[i];
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamilyIndex = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, m_pSurface, &presentSupport);
            if (presentSupport)
            {
                indices.presentFamilyIndex = i;
            }

            //检查是否全部查找完成
            if (indices.IsComplete())
            {
                break;
            }
        }
        return indices;
    }

    void DoodleVulkanManager::CreateLogicalDevice()
    {
        const auto queueFamily = FindQueueFamilies(m_pPhysicalDevice);
        const auto queuePriority = 1.0f;

        //设备Queue创建信息
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        const std::set<uint32_t> uniqueFamilies =
        {
            queueFamily.graphicsFamilyIndex.value(),
            queueFamily.presentFamilyIndex.value()
        };
        for (uint32_t family : uniqueFamilies)
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

        //创建本地设备
        if (vkCreateDevice(m_pPhysicalDevice, &createInfo, nullptr, &m_pLogicalDevice) == VK_SUCCESS)
        {
            vkGetDeviceQueue(m_pLogicalDevice, queueFamily.graphicsFamilyIndex.value(), 0, &m_pGraphicsQueue);
            vkGetDeviceQueue(m_pLogicalDevice, queueFamily.presentFamilyIndex.value(),  0, &m_pPresentQueue);

        }
        else
        {
            throw std::runtime_error("failed to create logical device!");
        }
    }

    void DoodleVulkanManager::DestroyLogicalDevice()
    {
        vkDestroyDevice(m_pLogicalDevice, nullptr);
        m_pLogicalDevice = VK_NULL_HANDLE;
    }

    void DoodleVulkanManager::CreateSwapChain(GLFWwindow* pWindow)
    {
        const auto supportDetails = QuerySwapChainSupport(m_pPhysicalDevice);
        const auto surfaceFormat = ChooseSwapChainSurfaceFormat(supportDetails.surfaceFormats);
        const auto presentMode = ChooseSwapChainPresentMode(supportDetails.presentModes);
        const auto extent = ChooseSwapChainExtent(supportDetails.surfaceCapabilities, pWindow);

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
        createInfo.surface = m_pSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        //队列族共享模式
        const auto queueFamily = FindQueueFamilies(m_pPhysicalDevice);
        const uint32_t queueFamilyIndices[] = {queueFamily.graphicsFamilyIndex.value(), queueFamily.presentFamilyIndex.value()};
        if (queueFamily.graphicsFamilyIndex == queueFamily.presentFamilyIndex)
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
        const auto result = vkCreateSwapchainKHR(m_pLogicalDevice, &createInfo, nullptr, &m_pSwapChain);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain!");
        }

        //记录交换链图像数据
        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainExtent = extent;
        vkGetSwapchainImagesKHR(m_pLogicalDevice, m_pSwapChain, &imageCount, nullptr);
        m_swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_pLogicalDevice, m_pSwapChain, &imageCount, m_swapChainImages.data());
    }

    void DoodleVulkanManager::DestroySwapChain()
    {
        vkDestroySwapchainKHR(m_pLogicalDevice, m_pSwapChain, nullptr);
        m_pSwapChain = VK_NULL_HANDLE;
    }

    SwapChainSupportDetails DoodleVulkanManager::QuerySwapChainSupport(VkPhysicalDevice pDevice) const
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
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, m_pSurface, &presentModeCount, nullptr);
        if (presentModeCount > 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, m_pSurface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR DoodleVulkanManager::ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableSurfaceFormats)
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

    VkPresentModeKHR DoodleVulkanManager::ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
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

    VkExtent2D DoodleVulkanManager::ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, GLFWwindow* pWindow)
    {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return surfaceCapabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(pWindow, &width, &height);
            VkExtent2D actualExtent =
            {
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
            return actualExtent;
        }
    }
}
