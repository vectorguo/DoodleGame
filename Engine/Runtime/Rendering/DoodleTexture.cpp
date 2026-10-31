//
//  DoodleTexture.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#include "DoodleTexture.h"

#include <stdexcept>

#include "Vulkan/DoodleCommandBuffer.h"
#include "Vulkan/DoodleGraphicsBuffer.h"
#include "Vulkan/DoodleVulkanDevice.h"

namespace Doodle
{
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

        m_image.Initialize(device,
                        {.width = width, .height = height},
                           kTextureFormat,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);

        UploadPixels(width, height, pixels, size);
        CreateSampler();
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
        //中转缓冲：CPU 能写、GPU 能读。与顶点/索引缓冲同一套
        DoodleGraphicsBuffer stagingBuffer;
        stagingBuffer.Initialize(*m_pDevice,
                                 size,
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 DoodleGraphicsBuffer::EMemoryType::HostVisible);
        stagingBuffer.Write(pixels, size);

        //传输池与一次性命令缓冲的生灭归命令层；取不到就是调用时机错了
        const auto* pCommandBuffer = DoodleCommandBuffer::GetInstance();
        if (pCommandBuffer == nullptr)
        {
            throw std::runtime_error("failed to upload texture: command buffer layer is not available!");
        }

        //分配 / 提交 / 等待 / 释放都在命令层，这里只录这张图的三步：
        //旧布局不要 → 写传输目标 → 拷像素 → 转可采样布局
        const VkCommandBuffer transferCommandBuffer = pCommandBuffer->BeginOneTimeTransfer();
        RecordImageTransitionToTransferDst(transferCommandBuffer);
        RecordCopyBufferToImage(transferCommandBuffer, stagingBuffer.GetBuffer(), width, height);
        RecordImageTransitionToShaderRead(transferCommandBuffer);
        pCommandBuffer->EndOneTimeTransfer(transferCommandBuffer);

        stagingBuffer.Destroy();
    }

    void DoodleTexture::RecordImageTransitionToTransferDst(VkCommandBuffer commandBuffer) const
    {
        //① 进 TRANSFER_DST：刚建出来的图，旧内容不要，源 scope 留空。
        //dst access 只写到 TRANSFER_WRITE —— 接下来对这张图的唯一操作就是拷贝
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image.GetImage();

        //整张图、单层单级：DoodleImage 固定单 mip 单层
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleTexture::RecordImageTransitionToShaderRead(VkCommandBuffer commandBuffer) const
    {
        //② 交给采样：TRANSFER_DST → SHADER_READ_ONLY。
        //拷贝的写要在 TRANSFER 阶段完成；采样读发生在片元着色器
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image.GetImage();

        //与上一段相同的单 mip 单层范围
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleTexture::RecordCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                VkBuffer sourceBuffer,
                                                uint32_t width,
                                                uint32_t height) const
    {
        //bufferRowLength / bufferImageHeight 填 0 表示紧密排列；
        //区域是整张图从 (0,0) 开始
        VkBufferImageCopy2 copyRegion{};
        copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {width, height, 1};

        VkCopyBufferToImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copyInfo.srcBuffer = sourceBuffer;
        copyInfo.dstImage = m_image.GetImage();
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);
    }

    void DoodleTexture::CreateSampler()
    {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        createInfo.mipLodBias = 0.0f;
        //各向异性是可选特性，本项目的 VkPhysicalDeviceFeatures 全零（没开 samplerAnisotropy），
        //所以这里必须关掉，maxAnisotropy 也就没有意义
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0f;
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        createInfo.minLod = 0.0f;
        //只有一级 mip，上界也取 0；将来生成 mip 时这里要跟着 levelCount 走
        createInfo.maxLod = 0.0f;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.unnormalizedCoordinates = VK_FALSE;

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
