//
//  DoodleDescriptorManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/4.
//

#include "DoodleDescriptorManager.h"

#include <array>
#include <span>
#include <stdexcept>
#include <string>

#include "DoodleVulkanDevice.h"
#include "Rendering/DoodleMaterial.h"
#include "Rendering/DoodleMaterialTextureSlot.h"
#include "Rendering/DoodleShader.h"

namespace Doodle
{
    void DoodleDescriptorManager::Initialize(const DoodleVulkanDevice& device)
    {
        m_pDevice = &device;

        //第一块池先建好，之后按需追加。集合按材质在第一次绘制时分配：
        //布局来自材质的着色器，本类自己不持有 set 1 布局
        AddMaterialDescriptorPool();

        //默认白图：材质没配的贴图槽都绑它。上传要借命令管理器的传输池，
        //所以调用方必须先初始化命令管理器
        CreateDefaultTexture();
    }

    void DoodleDescriptorManager::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //池里的集合引用着默认白图与贴图资产：先拆池，再拆默认白图
        DestroyMaterialDescriptorPools();
        m_materialDescriptorSets.clear();
        m_drawItemMaterialDescriptorSets.clear();
        DestroyDefaultTexture();

        m_pDevice = nullptr;
    }

    const std::vector<VkDescriptorSet>& DoodleDescriptorManager::ResolveMaterialDescriptorSets(const std::vector<DoodleDrawItem>& drawItems)
    {
        //第一次用到的材质在这里分配集合。结果与 drawItems 等长，
        //录制侧按同一下标取用 —— 顺序不能错开
        m_drawItemMaterialDescriptorSets.clear();
        m_drawItemMaterialDescriptorSets.reserve(drawItems.size());
        for (const DoodleDrawItem& drawItem : drawItems)
        {
            m_drawItemMaterialDescriptorSets.push_back(GetOrCreateMaterialDescriptorSet(*drawItem.pMaterial));
        }
        return m_drawItemMaterialDescriptorSets;
    }

    VkDescriptorPool DoodleDescriptorManager::CreateDescriptorPool(
        const VkDescriptorType type,
        const uint32_t descriptorCount,
        const uint32_t maxSets) const
    {
        //池的公共约定：只声明一种描述符，且不单独释放集合（flags = 0）。
        //descriptorCount 与 maxSets 分开传而不是共用同一个值：Vulkan 里它们是
        //两个量，一个材质集合占几个描述符由它的着色器决定
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

    void DoodleDescriptorManager::AddMaterialDescriptorPool()
    {
        //每块池装 MATERIAL_DESCRIPTOR_SETS_PER_POOL 个材质集合；贴图描述符按每个集合
        //最多 kMaxMaterialTextureSlots 张预留 —— 一个集合不管几张贴图，都放得进一块新池。
        //池满时再调本函数追加，块大小只是分配策略，不是总量上限
        auto descriptorPool = CreateDescriptorPool(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                   MATERIAL_DESCRIPTOR_SETS_PER_POOL * kMaxMaterialTextureSlots,
                                                   MATERIAL_DESCRIPTOR_SETS_PER_POOL);
        m_materialDescriptorPools.push_back(descriptorPool);
    }

    void DoodleDescriptorManager::DestroyMaterialDescriptorPools()
    {
        for (VkDescriptorPool& descriptorPool : m_materialDescriptorPools)
        {
            DestroyDescriptorPool(descriptorPool);
        }
        m_materialDescriptorPools.clear();
    }

    void DoodleDescriptorManager::CreateDefaultTexture()
    {
        //1×1 白图：材质没配的贴图槽绑定它。着色器因此只有一条采样路径，
        //不需要在片元里为「有没有贴图」写分支
        constexpr unsigned char kWhitePixel[4] = {255, 255, 255, 255};
        m_defaultTexture.Initialize(*m_pDevice, 1, 1, kWhitePixel, sizeof(kWhitePixel));
    }

    void DoodleDescriptorManager::DestroyDefaultTexture()
    {
        m_defaultTexture.Destroy();
    }

    VkDescriptorSet DoodleDescriptorManager::GetOrCreateMaterialDescriptorSet(const DoodleMaterial& material)
    {
        const auto it = m_materialDescriptorSets.find(&material);
        if (it != m_materialDescriptorSets.end())
        {
            return it->second;
        }

        const DoodleShader* pShader = material.GetShader();
        if (pShader == nullptr)
        {
            throw std::logic_error("draw item material has no shader");
        }

        //着色器没有声明贴图：没有 set 1，录制侧不绑。也缓存下来，之后每帧只查一次表
        const VkDescriptorSetLayout setLayout = pShader->GetMaterialDescriptorSetLayout();
        if (setLayout == VK_NULL_HANDLE)
        {
            m_materialDescriptorSets.emplace(&material, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        //槽数由着色器加载时按材质契约卡住，这里只防御：下面的写入数组按上限开在栈上
        const std::span<const DoodleMaterialTextureSlot> slots = pShader->GetMaterialTextureSlots();
        if (slots.size() > kMaxMaterialTextureSlots)
        {
            throw std::logic_error("shader declares more material textures than kMaxMaterialTextureSlots");
        }

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
        if (!m_materialDescriptorPools.empty())
        {
            result = allocateFromPool(m_materialDescriptorPools.back(), descriptorSet);
        }

        //当前块满了（集合数或贴图描述符数用完）就追加一块再试。池没有 FREE 位，
        //正常只会撞 VK_ERROR_OUT_OF_POOL_MEMORY；FRAGMENTED_POOL 一并处理，
        //避免将来加 FREE 位后漏掉
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            AddMaterialDescriptorPool();
            result = allocateFromPool(m_materialDescriptorPools.back(), descriptorSet);
        }

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate material descriptor set!");
        }

        //每个槽写一张：材质配了就用材质的，没配就用默认白图。
        //贴图上传完成后就是 SHADER_READ_ONLY_OPTIMAL，这里不需要再做布局转换；
        //view 与 sampler 都来自贴图资产。
        //imageInfos 要活到 vkUpdateDescriptorSets 返回 —— 写入结构里存的是它的地址
        std::array<VkDescriptorImageInfo, kMaxMaterialTextureSlots> imageInfos{};
        std::array<VkWriteDescriptorSet, kMaxMaterialTextureSlots> descriptorWrites{};
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const DoodleTexture* pTexture = material.GetTexture(slots[i].name);
            const DoodleTexture& texture = pTexture != nullptr ? *pTexture : m_defaultTexture;

            imageInfos[i] =
            {
                .sampler = texture.GetSampler(),
                .imageView = texture.GetImageView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            descriptorWrites[i] =
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSet,
                .dstBinding = slots[i].binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[i]
            };
        }

        vkUpdateDescriptorSets(m_pDevice->GetLogicalDevice(),
                               static_cast<uint32_t>(slots.size()),
                               descriptorWrites.data(),
                               0,
                               nullptr);
        m_materialDescriptorSets.emplace(&material, descriptorSet);
        return descriptorSet;
    }
}
