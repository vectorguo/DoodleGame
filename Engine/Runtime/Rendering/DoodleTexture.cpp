//
//  DoodleTexture.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#include "DoodleTexture.h"

#include <stdexcept>

#include "Vulkan/DoodleCommandManager.h"
#include "Vulkan/DoodleGraphicsBuffer.h"
#include "Vulkan/DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleTexture::DoodleTexture(const std::string_view assetPath)
        : DoodleAsset(assetPath)
    {
    }

    void DoodleTexture::Initialize(const DoodleVulkanDevice& device,
                                   const uint32_t width,
                                   const uint32_t height,
                                   const void* pixels,
                                   const VkDeviceSize size)
    {
        m_pDevice = &device;

        //像素与尺寸必须自洽：这里算的是「整张图要多少字节」，
        //与 DoodleGraphicsBuffer::Upload 的越界检查同一条思路
        const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(width) * height * kBytesPerPixel;
        if (width == 0 || height == 0 || pixels == nullptr || size != requiredSize)
        {
            throw std::runtime_error("texture pixels do not match the given size!");
        }

        try
        {
            m_image.Initialize(device,
                               width,
                               height,
                               kTextureFormat,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT);

            UploadPixels(width, height, pixels, size);
            CreateSampler();
        }
        catch (...)
        {
            //loader 的强异常安全契约：失败时不能留下已创建的 GPU 资源。
            //Destroy 对半初始化的状态是安全的（采样器/图像各自检查句柄）
            Destroy();
            throw;
        }
    }

    void DoodleTexture::Destroy()
    {
        //采样器不引用图，但按「后建先拆」的惯例先走；图自己按视图 → 图 → 内存拆
        DestroySampler();
        m_image.Destroy();
        m_pDevice = nullptr;
    }

    void DoodleTexture::UploadPixels(const uint32_t width,
                                     const uint32_t height,
                                     const void* pixels,
                                     const VkDeviceSize size)
    {
        //中转缓冲：CPU 能写、GPU 能读。与顶点/索引缓冲同一套。
        //Initialize 失败时会自己清理，所以不需要在 catch 里 Destroy 未建成的缓冲
        DoodleGraphicsBuffer stagingBuffer;
        stagingBuffer.Initialize(*m_pDevice,
                                 size,
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 DoodleGraphicsBuffer::EMemoryType::HostVisible);
        stagingBuffer.Write(pixels, size);

        //传输命令的 begin / 录制 / 提交 / 等待都在命令层；这里只把
        //「哪块缓冲、哪张图、多大」交给它
        auto& commandManager = DoodleCommandManager::GetInstance();
        commandManager.BeginTransferCommand();
        commandManager.FillTextureTransferCommand(stagingBuffer.GetBuffer(), m_image.GetImage(), width, height);
        commandManager.EndTransferCommand();

        //销毁中转缓冲
        stagingBuffer.Destroy();
    }

    void DoodleTexture::CreateSampler()
    {
        constexpr VkSamplerCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0.0f,
            //各向异性是可选特性，本项目的 VkPhysicalDeviceFeatures 全零（没开 samplerAnisotropy），
            //所以这里必须关掉，maxAnisotropy 也就没有意义
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            //只有一级 mip，上界也取 0；将来生成 mip 时这里要跟着 levelCount 走
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE
        };

        if (vkCreateSampler(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture sampler!");
        }
    }

    void DoodleTexture::DestroySampler()
    {
        if (m_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_pDevice->GetLogicalDevice(), m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }
    }
}
