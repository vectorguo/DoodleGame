//
//  DoodleDescriptorManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/4.
//

#include "DoodleDescriptorManager.h"

#include <stdexcept>
#include <string>

#include "DoodleVulkanDevice.h"
#include "Rendering/DoodleMaterial.h"

namespace Doodle
{
    void DoodleDescriptorManager::Initialize(const DoodleVulkanDevice& device)
    {
        m_pDevice = &device;

        //材质布局先建：集合按布局分配，管线布局也要读它
        CreateMaterialDescriptorSetLayout();

        //纹理集合与默认白图：默认白图上传要借命令管理器的传输池，
        //所以调用方必须先初始化命令管理器
        AddTextureDescriptorPool();
        CreateDefaultTexture();
        m_defaultTextureDescriptorSet = GetOrCreateTextureDescriptorSet(m_defaultTexture);
    }

    void DoodleDescriptorManager::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //纹理池引用默认白图与纹理资产，先拆池，再拆默认白图；
        //布局最后拆：从它分配的集合已经随池一起失效
        DestroyTextureDescriptorPools();
        m_textureDescriptorSets.clear();
        m_drawItemMaterialDescriptorSets.clear();
        DestroyDefaultTexture();

        DestroyDescriptorSetLayout(m_materialDescriptorSetLayout);

        m_pDevice = nullptr;
    }

    const std::vector<VkDescriptorSet>& DoodleDescriptorManager::ResolveMaterialDescriptorSets(const std::vector<DoodleDrawItem>& drawItems)
    {
        //第一次用到的纹理在这里分配集合；没有配置纹理的材质用默认白图。
        //结果与 drawItems 等长，录制侧按同一下标取用 —— 顺序不能错开
        m_drawItemMaterialDescriptorSets.clear();
        m_drawItemMaterialDescriptorSets.reserve(drawItems.size());
        for (const DoodleDrawItem& drawItem : drawItems)
        {
            const DoodleTexture* pTexture = drawItem.pMaterial->GetTexture();
            m_drawItemMaterialDescriptorSets.push_back(
                pTexture != nullptr ? GetOrCreateTextureDescriptorSet(*pTexture)
                                    : m_defaultTextureDescriptorSet);
        }
        return m_drawItemMaterialDescriptorSets;
    }

    void DoodleDescriptorManager::CreateMaterialDescriptorSetLayout()
    {
        //set 1 binding 0：一张被采样的纹理，只有片元阶段采样（顶点阶段不读它）；
        //采样器随纹理一起写入，不是编译期固定的（pImmutableSamplers 为空）。
        //
        //与各 pass 的 set 0 分开的理由是更新频率和数量：set 0 由每个 pass 每帧
        //一份，随帧轮转；这里每个材质一张纹理，按资产共享。混进同一个集合意味着
        //每张纹理都要复制 N 份常量，或者每帧重写所有材质集合
        constexpr VkDescriptorSetLayoutCreateInfo layoutInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &kMaterialTextureBinding
        };

        if (vkCreateDescriptorSetLayout(m_pDevice->GetLogicalDevice(), &layoutInfo,
                                        nullptr, &m_materialDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture descriptor set layout!");
        }
    }

    void DoodleDescriptorManager::DestroyDescriptorSetLayout(VkDescriptorSetLayout& descriptorSetLayout) const
    {
        if (descriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(), descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    VkDescriptorPool DoodleDescriptorManager::CreateDescriptorPool(
        const VkDescriptorType type,
        const uint32_t descriptorCount,
        const uint32_t maxSets) const
    {
        //池的公共约定：只声明一种描述符，且不单独释放集合（flags = 0）。
        //descriptorCount 与 maxSets 分开传而不是共用同一个值：Vulkan 里它们是
        //两个量，当前恰好都满足「每个集合占一个描述符」，将来材质 set
        //增加取样器后前者就不再等于后者
        const VkDescriptorPoolSize poolSize
        {
            .type = type,
            .descriptorCount = descriptorCount
        };

        const VkDescriptorPoolCreateInfo poolInfo
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = 0,
            .maxSets = maxSets,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize
        };

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(m_pDevice->GetLogicalDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool (type " +
                                     std::to_string(static_cast<int>(type)) +
                                     ", maxSets " + std::to_string(maxSets) + ")");
        }
        return descriptorPool;
    }

    void DoodleDescriptorManager::DestroyDescriptorPool(VkDescriptorPool& descriptorPool) const
    {
        if (descriptorPool != VK_NULL_HANDLE)
        {
            //池销毁时，池里分配的描述符集自动失效，不需要逐个 vkFreeDescriptorSets
            vkDestroyDescriptorPool(m_pDevice->GetLogicalDevice(), descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
    }

    void DoodleDescriptorManager::AddTextureDescriptorPool()
    {
        //每块池装 TEXTURE_DESCRIPTOR_SETS_PER_POOL 个 combined image sampler 集合。
        //池满时再调本函数追加，块大小只是分配策略，不是总量上限
        auto descriptorPool = CreateDescriptorPool(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                   TEXTURE_DESCRIPTOR_SETS_PER_POOL,
                                                   TEXTURE_DESCRIPTOR_SETS_PER_POOL);
        m_textureDescriptorPools.push_back(descriptorPool);
    }

    void DoodleDescriptorManager::DestroyTextureDescriptorPools()
    {
        for (VkDescriptorPool& descriptorPool : m_textureDescriptorPools)
        {
            DestroyDescriptorPool(descriptorPool);
        }
        m_textureDescriptorPools.clear();

        m_defaultTextureDescriptorSet = VK_NULL_HANDLE;
    }

    void DoodleDescriptorManager::CreateDefaultTexture()
    {
        //1×1 白图：材质没有配置纹理时绑定它。着色器因此只有一条采样路径，
        //不需要在片元里为「有没有纹理」写分支
        constexpr unsigned char kWhitePixel[4] = {255, 255, 255, 255};
        m_defaultTexture.Initialize(*m_pDevice, 1, 1, kWhitePixel, sizeof(kWhitePixel));
    }

    void DoodleDescriptorManager::DestroyDefaultTexture()
    {
        m_defaultTexture.Destroy();
    }

    VkDescriptorSet DoodleDescriptorManager::GetOrCreateTextureDescriptorSet(const DoodleTexture& texture)
    {
        const auto it = m_textureDescriptorSets.find(&texture);
        if (it != m_textureDescriptorSets.end())
        {
            return it->second;
        }

        const VkDescriptorSetLayout setLayout = m_materialDescriptorSetLayout;

        //池块是条件选择的，所以把「用某一块池分配」收进一个小函数：
        //每次分配仍然是一次完整、const 的 VkDescriptorSetAllocateInfo
        const auto allocateFromPool = [&](VkDescriptorPool descriptorPool, VkDescriptorSet& outSet) -> VkResult
        {
            const VkDescriptorSetAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &setLayout
            };
            return vkAllocateDescriptorSets(m_pDevice->GetLogicalDevice(), &allocInfo, &outSet);
        };

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkResult result = VK_ERROR_OUT_OF_POOL_MEMORY;

        //先试当前池块。还没有池时保持 OUT_OF_POOL_MEMORY，走下面的追加路径
        if (!m_textureDescriptorPools.empty())
        {
            result = allocateFromPool(m_textureDescriptorPools.back(), descriptorSet);
        }

        //当前块满了就追加一块再试。池没有 FREE 位，正常只会撞
        //VK_ERROR_OUT_OF_POOL_MEMORY；FRAGMENTED_POOL 一并处理，
        //避免将来加 FREE 位后漏掉
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            AddTextureDescriptorPool();
            result = allocateFromPool(m_textureDescriptorPools.back(), descriptorSet);
        }

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate texture descriptor set!");
        }

        //纹理上传完成后就是 SHADER_READ_ONLY_OPTIMAL，这里不需要再做布局转换。
        //view 与 sampler 都来自纹理资产；同一张纹理被多个材质引用时共用这个集合
        const VkDescriptorImageInfo imageInfo
        {
            .sampler = texture.GetSampler(),
            .imageView = texture.GetImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        const VkWriteDescriptorSet descriptorWrite
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo
        };

        vkUpdateDescriptorSets(m_pDevice->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
        m_textureDescriptorSets.emplace(&texture, descriptorSet);
        return descriptorSet;
    }
}
