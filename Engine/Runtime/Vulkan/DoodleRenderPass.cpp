//
//  DoodleRenderPass.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#include "DoodleRenderPass.h"

#include <utility>

#include "DoodleVulkanDevice.h"
#include "Utility/DoodleHash.h"

namespace Doodle
{
    void DoodleRenderPass::Initialize(const DoodleRenderPassInitInfo& initInfo, const DoodleVulkanPipelinePassConfig& passConfig)
    {
        const std::lock_guard lock(m_mutex);

        m_pDevice = &initInfo.device;
        m_pipelineCompileCache = initInfo.pipelineCompileCache;
        m_frameUboDescriptorSetLayout = initInfo.frameUboDescriptorSetLayout;
        m_passConfig = passConfig;
    }

    void DoodleRenderPass::Destroy()
    {
        {
            const std::lock_guard lock(m_mutex);

            //条目里的管线是显式 Destroy 的：DoodleVulkanPipelineObject 的析构不收句柄
            DestroyPipelines();
        }

        //基类已经把管线拆完；此时 device / 句柄还没清空，派生类可以安全地
        //拆自己的资源。OnDestroy 里不要再要管线（表已经空了）
        OnDestroy();

        const std::lock_guard lock(m_mutex);
        m_frameUboDescriptorSetLayout = VK_NULL_HANDLE;
        m_pipelineCompileCache = VK_NULL_HANDLE;
        m_pDevice = nullptr;
    }

    void DoodleRenderPass::RebuildPipelines(const DoodleVulkanPipelinePassConfig& passConfig)
    {
        const std::lock_guard lock(m_mutex);

        //每条管线按条目里那份描述、用新的 pass 配置重建；对象不动，交出去的引用有效
        for (auto& [keyHash, pItem] : m_pipelineItems)
        {
            pItem->pipeline.Rebuild(m_pipelineCompileCache, passConfig, pItem->shaderDescription);
        }

        //全部成功才记下新配置：之后再 GetPipeline 的管线也用它
        m_passConfig = passConfig;
    }

    const DoodleVulkanPipelineObject& DoodleRenderPass::GetPipeline(const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        //哈希放在锁外算：字节码有几 KB，没必要让别的线程等这一段
        const std::size_t keyHash = HashKey(shaderDescription);

        const std::lock_guard lock(m_mutex);

        //哈希只分桶，同一个桶里逐个比内容 —— 哈希碰撞、或者调用方复用了同一块内存
        //装另一份字节码，都不会拿错管线
        const auto [rangeBegin, rangeEnd] = m_pipelineItems.equal_range(keyHash);
        for (auto it = rangeBegin; it != rangeEnd; ++it)
        {
            if (IsSameKey(it->second->shaderDescription, shaderDescription))
            {
                return it->second->pipeline;
            }
        }

        return CreatePipelineItem(keyHash, shaderDescription).pipeline;
    }

    std::size_t DoodleRenderPass::GetPipelineCount() const
    {
        const std::lock_guard lock(m_mutex);
        return m_pipelineItems.size();
    }

    DoodleVulkanPipelinePassConfig DoodleRenderPass::GetPassConfig() const
    {
        const std::lock_guard lock(m_mutex);
        return m_passConfig;
    }

    std::size_t DoodleRenderPass::HashKey(const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        //两段字节码各自哈希再按顺序混：同一份字节换个阶段，结果也不同
        std::size_t seed = 0;
        DoodleHash::Combine(seed, DoodleHash::BytesToHashValue(shaderDescription.vertexBytecode));
        DoodleHash::Combine(seed, DoodleHash::BytesToHashValue(shaderDescription.fragmentBytecode));
        DoodleHash::Combine(seed, shaderDescription.renderState.GetHashValue());
        DoodleHash::Combine(seed, static_cast<std::size_t>(shaderDescription.vertexFormat));
        DoodleHash::Combine(seed, static_cast<std::size_t>(shaderDescription.instanceFormat));
        return seed;
    }

    bool DoodleRenderPass::IsSameKey(const DoodleVulkanPipelineShaderDescription& lhs,
                                     const DoodleVulkanPipelineShaderDescription& rhs)
    {
        //vector<std::byte> 的 == 就是逐字节比对；set 1 布局不在这里，理由见类注释
        return lhs.vertexBytecode == rhs.vertexBytecode
            && lhs.fragmentBytecode == rhs.fragmentBytecode
            && lhs.renderState == rhs.renderState
            && lhs.vertexFormat == rhs.vertexFormat
            && lhs.instanceFormat == rhs.instanceFormat;
    }

    DoodleRenderPass::PipelineItem& DoodleRenderPass::CreatePipelineItem(
        const std::size_t keyHash,
        const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        //先把条目放进表、再建管线：放表这一步只可能因为内存不足失败，
        //那时还什么都没建；建管线失败就把刚放进去的条目摘掉 —— 两条失败路径都不留
        //半截状态，也不会漏掉已经建出来的句柄。整个过程都在锁内，别的线程看不到
        //一个还没建好的条目
        auto pItem = std::make_unique<PipelineItem>();
        pItem->shaderDescription = shaderDescription;
        const auto inserted = m_pipelineItems.emplace(keyHash, std::move(pItem));

        try
        {
            inserted->second->pipeline.Initialize(
            {
                .device = *m_pDevice,
                .pipelineCompileCache = m_pipelineCompileCache,
                .frameUboDescriptorSetLayout = m_frameUboDescriptorSetLayout,
                .passConfig = m_passConfig,
                .shaderDescription = inserted->second->shaderDescription
            });
        }
        catch (...)
        {
            //Initialize 失败时自己会收掉已建的部分，这里只需要把条目拿走
            m_pipelineItems.erase(inserted);
            throw;
        }

        return *inserted->second;
    }

    void DoodleRenderPass::DestroyPipelines()
    {
        for (auto& [keyHash, pItem] : m_pipelineItems)
        {
            pItem->pipeline.Destroy();
        }
        m_pipelineItems.clear();
    }
}
