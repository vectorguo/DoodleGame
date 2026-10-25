//
//  DoodleDepthImages.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#include "DoodleDepthImages.h"

#include <stdexcept>

#include "DoodleVulkanDevice.h"

namespace Doodle
{
    void DoodleDepthImages::Initialize(const DoodleVulkanDevice& device, const VkExtent2D extent)
    {
        m_pDevice = &device;

        //一个格式决定整组：同一种用途，没有逐帧不同的理由
        m_depthFormat = FindDepthFormat();

        //切面按格式算：含模板分量的格式必须把模板位一起声明；
        //本类虽不做模板测试，深度与模板仍是同一份内存的两路数据
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencilComponent(m_depthFormat))
        {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        //每帧一张：数量与命令缓冲、imageAvailable 信号量、帧栅栏同一个维度。
        //同一槽位下一次被用到之前，帧栅栏已经保证了上一次使用完成
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            m_images[i].Initialize(device, extent, m_depthFormat,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, aspectMask);
        }
    }

    void DoodleDepthImages::Destroy()
    {
        //每张图自己按「视图 → 图 → 内存」拆；本类只管逐帧走一遍
        for (auto& image : m_images)
        {
            image.Destroy();
        }

        m_pDevice = nullptr;
        m_depthFormat = VK_FORMAT_UNDEFINED;
    }

    VkFormat DoodleDepthImages::FindDepthFormat() const
    {
        //候选表按偏好从高到低：32 位浮点首选，另外两个带模板的兜底。
        //全都不支持时抛出 —— 意味着这台设备画不了带深度的三维场景
        constexpr VkFormat candidates[] =
        {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        for (const VkFormat format : candidates)
        {
            VkFormatProperties2 formatProperties{};
            formatProperties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
            vkGetPhysicalDeviceFormatProperties2(m_pDevice->GetPhysicalDevice(), format,
                                                 &formatProperties);

            //查的是格式能力，不是图像用途。两个枚举名字几乎一样、类型都是 32 位标志位，
            //传错不会报编译错 —— 这条判断用的必须是 VK_FORMAT_FEATURE_ 那一组
            const auto& features = formatProperties.formatProperties.optimalTilingFeatures;
            if ((features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported depth format!");
    }
}
