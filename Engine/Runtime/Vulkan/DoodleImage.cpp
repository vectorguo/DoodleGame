//
//  DoodleImage.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#include "DoodleImage.h"

#include <stdexcept>

#include "DoodleVulkanDevice.h"

namespace Doodle
{
    void DoodleImage::Initialize(const DoodleVulkanDevice& device, const VkExtent2D extent,
                                 const VkFormat format, const VkImageUsageFlags usage,
                                 const VkImageAspectFlags aspectFlags)
    {
        m_pDevice = &device;
        m_format = format;

        CreateImage(extent, usage);
        CreateImageView(aspectFlags);
    }

    void DoodleImage::Destroy()
    {
        //允许重复调用：Destroy 之后 m_pDevice 会置空
        if (m_pDevice == nullptr)
        {
            return;
        }

        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //创建序的反序：视图 → 图 → 内存。视图引用图，图占着内存
        if (m_imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(logicalDevice, m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }

        if (m_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(logicalDevice, m_image, nullptr);
            m_image = VK_NULL_HANDLE;
        }

        if (m_imageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, m_imageMemory, nullptr);
            m_imageMemory = VK_NULL_HANDLE;
        }

        m_pDevice = nullptr;
        m_format = VK_FORMAT_UNDEFINED;
    }

    void DoodleImage::CreateImage(const VkExtent2D extent, const VkImageUsageFlags usage)
    {
        VkImageCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.extent = {extent.width, extent.height, 1};
        createInfo.mipLevels = 1;     //本类固定单级：多级图要改签名，见类注释
        createInfo.arrayLayers = 1;   //同理，单层
        createInfo.format = m_format;
        //格式能力是按平铺查的，图像也必然用最优平铺 —— 驱动最擅长的排布
        createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        //这个字段只是「声明初始状态」，不会真的做任何转换 ——
        //真正的转换是调用方在命令缓冲里录的屏障。取值只能 UNDEFINED 或 PREINITIALIZED
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        createInfo.usage = usage;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;            //本类固定单采样
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;    //只有图形队列碰它

        const auto logicalDevice = m_pDevice->GetLogicalDevice();
        if (vkCreateImage(logicalDevice, &createInfo, nullptr, &m_image) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image!");
        }

        //内存：先问图像要什么，再让设备层挑一个同时满足
        //「它用得了」与「设备本地」的类型
        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(logicalDevice, m_image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        //用驱动要的尺寸，不是数据尺寸 —— 驱动可能要得更多（内部对齐、元数据）
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = m_pDevice->FindMemoryType(
            memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &m_imageMemory) != VK_SUCCESS)
        {
            //不留半成品：调用方只知道 Initialize 抛了，不知道抛在哪一步
            vkDestroyImage(logicalDevice, m_image, nullptr);
            m_image = VK_NULL_HANDLE;
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(logicalDevice, m_image, m_imageMemory, 0);
    }

    void DoodleImage::CreateImageView(const VkImageAspectFlags aspectFlags)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        //切面由调用方给：含模板分量的深度格式必须把模板位一起声明，
        //颜色图则只用 COLOR —— 这个判断属于使用者的语义，不属于本类
        createInfo.subresourceRange.aspectMask = aspectFlags;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        const auto logicalDevice = m_pDevice->GetLogicalDevice();
        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &m_imageView) != VK_SUCCESS)
        {
            //视图失败时把图与内存一起收掉，不留半成品
            vkDestroyImage(logicalDevice, m_image, nullptr);
            m_image = VK_NULL_HANDLE;
            vkFreeMemory(logicalDevice, m_imageMemory, nullptr);
            m_imageMemory = VK_NULL_HANDLE;
            throw std::runtime_error("failed to create image view!");
        }
    }
}
