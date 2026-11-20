//
//  DoodleRenderPass.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#include "DoodleRenderPass.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "DoodleVulkanDevice.h"
#include "Rendering/DoodleShader.h"
#include "Utility/DoodleHash.h"

namespace Doodle
{
    void DoodleRenderPass::Initialize(const DoodleRenderPassInitInfo& initInfo,
                                      const DoodleVulkanPipelinePassConfig& passConfig)
    {
        //本 pass 的 set 0 与 push 范围按自己的契约取：着色器加载时按同一份核对
        const DoodlePassShaderInterface& passInterface = GetPassShaderInterface(m_passType);

        const std::lock_guard lock(m_mutex);

        m_pDevice = &initInfo.device;
        m_pipelineCompileCache = initInfo.pipelineCompileCache;
        m_pushConstantRange = passInterface.pushConstantRange;
        m_passConfig = passConfig;

        //每个 pass 一份 set 0 常量资源
        CreatePassUboResources(passInterface);
    }

    void DoodleRenderPass::Destroy()
    {
        {
            const std::lock_guard lock(m_mutex);

            //条目里的管线是显式 Destroy 的：DoodleVulkanPipelineObject 的析构不收句柄
            DestroyPipelines();
        }

        //memo 里存的是刚拆掉的管线的地址，跟着清空
        m_pipelinesByShader.clear();

        //管线先走：它们的管线布局引用 set 0 布局。然后拆本 pass 的常量资源；
        //最后交给派生类拆自己的资源 —— 此时 device / 句柄还没清空
        DestroyPassUboResources();
        OnDestroy();

        const std::lock_guard lock(m_mutex);
        m_pushConstantRange = {};
        m_pipelineCompileCache = VK_NULL_HANDLE;
        m_pDevice = nullptr;
    }

    void DoodleRenderPass::WarmupShaders(const std::vector<const DoodleShader*>& shaders)
    {
        //只解析 / 创建管线，不录制：把编译代价和失败点放在加载边界上。
        //与 Render 走同一个 ResolvePipeline，预热过的着色器在第一帧只命中 memo。
        //队列里只会出现初始化成功的着色器；空指针只可能是调用方传错，
        //预热不是校验入口，跳过即可
        for (const DoodleShader* pShader : shaders)
        {
            if (pShader == nullptr)
            {
                continue;
            }

            //队列里可能有只给别的 pass 用的着色器（例如 shadow-only）；
            //没有本 pass 描述就跳过，真正的缺失由录制前的解析拦下
            if (pShader->GetPassDescription(m_passType) == nullptr)
            {
                continue;
            }

            //结果不用：解析本身就会把管线建出来并填进 memo
            static_cast<void>(ResolvePipeline(*pShader));
        }
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

    const DoodleVulkanPipelineObject& DoodleRenderPass::ResolvePipeline(const DoodleShader& shader)
    {
        const auto it = m_pipelinesByShader.find(&shader);
        if (it != m_pipelinesByShader.end())
        {
            return *it->second;
        }

        const DoodleVulkanPipelineShaderDescription* pDescription = shader.GetPassDescription(m_passType);
        if (pDescription == nullptr)
        {
            throw std::invalid_argument("shader '" + shader.GetAssetPath() + "' has no description for this pass");
        }

        //内容相同的两个着色器资产会在这里拿到同一条管线；memo 只省掉每绘制项的
        //内容哈希与逐字节比对，不改变管线表「按内容查找」的语义
        const DoodleVulkanPipelineObject& pipeline = GetPipeline(*pDescription);
        m_pipelinesByShader.emplace(&shader, &pipeline);
        return pipeline;
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

        //set 1 只混定义，不混布局句柄：理由见类注释
        for (const VkDescriptorSetLayoutBinding& binding : shaderDescription.materialBindings)
        {
            DoodleHash::Combine(seed, static_cast<std::size_t>(binding.binding));
            DoodleHash::Combine(seed, static_cast<std::size_t>(binding.descriptorType));
            DoodleHash::Combine(seed, static_cast<std::size_t>(binding.descriptorCount));
            DoodleHash::Combine(seed, static_cast<std::size_t>(binding.stageFlags));
        }
        return seed;
    }

    bool DoodleRenderPass::IsSameKey(const DoodleVulkanPipelineShaderDescription& lhs,
                                     const DoodleVulkanPipelineShaderDescription& rhs)
    {
        //set 1 的定义逐项比：两边都是 DoodleShader 按 binding 升序排好的，
        //pImmutableSamplers 恒为空，不参与
        const auto isSameMaterialBindings = [&lhs, &rhs]
        {
            if (lhs.materialBindings.size() != rhs.materialBindings.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < lhs.materialBindings.size(); ++i)
            {
                const VkDescriptorSetLayoutBinding& a = lhs.materialBindings[i];
                const VkDescriptorSetLayoutBinding& b = rhs.materialBindings[i];
                if (a.binding != b.binding ||
                    a.descriptorType != b.descriptorType ||
                    a.descriptorCount != b.descriptorCount ||
                    a.stageFlags != b.stageFlags)
                {
                    return false;
                }
            }
            return true;
        };

        //vector<std::byte> 的 == 就是逐字节比对；set 1 布局句柄不在这里，理由见类注释
        return lhs.vertexBytecode == rhs.vertexBytecode
            && lhs.fragmentBytecode == rhs.fragmentBytecode
            && lhs.renderState == rhs.renderState
            && lhs.vertexFormat == rhs.vertexFormat
            && lhs.instanceFormat == rhs.instanceFormat
            && isSameMaterialBindings();
    }

    DoodleRenderPass::PipelineItem& DoodleRenderPass::CreatePipelineItem(
        const std::size_t keyHash,
        const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        //set 0 布局在 Initialize 里建好；没有它说明 pass 还没初始化（或已经销毁），
        //建出来的管线在录制时没有可绑的常量集，不如当场说清楚
        if (m_passUboDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::logic_error("graphics pipeline: pass has no set 0 uniform layout");
        }

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
                .passUboDescriptorSetLayout = m_passUboDescriptorSetLayout,
                .pushConstantRange = m_pushConstantRange,
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

    void DoodleRenderPass::CreatePassUboResources(const DoodlePassShaderInterface& passInterface)
    {
        const VkDescriptorSetLayoutBinding& passUboBinding = passInterface.passUboBinding;
        const VkDeviceSize passUboSize = passInterface.passUboSize;

        //set 0 只声明一个 UBO 绑定；布局 / 池 / 集合 / 写入都按它建
        if (passUboBinding.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            passUboBinding.descriptorCount != 1 ||
            passUboBinding.pImmutableSamplers != nullptr ||
            passUboSize == 0)
        {
            throw std::invalid_argument("render pass: set 0 must be a single, non-empty uniform buffer");
        }

        try
        {
            //UBO 每帧更新，走 HostVisible 类型（CPU 直接写），不走设备本地 + 中转。
            //映射由第一次 Write 建立，这里只负责分配
            for (DoodleGraphicsBuffer& buffer : m_passUbos)
            {
                buffer.Initialize(*m_pDevice,
                                  passUboSize,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  DoodleGraphicsBuffer::EMemoryType::HostVisible);
            }

            const VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &passUboBinding
            };
            if (vkCreateDescriptorSetLayout(m_pDevice->GetLogicalDevice(), &layoutInfo, nullptr,
                                            &m_passUboDescriptorSetLayout) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create the pass set 0 layout!");
            }

            const VkDescriptorPoolSize poolSize
            {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = MAX_FRAMES_IN_FLIGHT
            };
            const VkDescriptorPoolCreateInfo poolInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = MAX_FRAMES_IN_FLIGHT,
                .poolSizeCount = 1,
                .pPoolSizes = &poolSize
            };
            if (vkCreateDescriptorPool(m_pDevice->GetLogicalDevice(), &poolInfo, nullptr,
                                       &m_passUboDescriptorPool) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create the pass set 0 descriptor pool!");
            }

            //pSetLayouts 要求「每个集合一个布局句柄」，不会自动广播同一个布局，
            //所以即使所有集合共用同一份布局，也要把它重复 N 次
            std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
            layouts.fill(m_passUboDescriptorSetLayout);
            const VkDescriptorSetAllocateInfo allocInfo
            {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = m_passUboDescriptorPool,
                .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
                .pSetLayouts = layouts.data()
            };
            m_passUboDescriptorSets.fill(VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(m_pDevice->GetLogicalDevice(), &allocInfo,
                                         m_passUboDescriptorSets.data()) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to allocate the pass set 0 descriptor sets!");
            }

            //把第 i 块 UBO 接到第 i 个集合的 binding 上。
            //这一步是 host 侧更新，不是录制命令；集合在首次绘制前写完，之后不再改
            for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            {
                const VkDescriptorBufferInfo bufferInfo
                {
                    .buffer = m_passUbos[i].GetBuffer(),
                    .offset = 0,
                    .range = passUboSize
                };

                const VkWriteDescriptorSet descriptorWrite
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_passUboDescriptorSets[i],
                    .dstBinding = passUboBinding.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &bufferInfo
                };

                vkUpdateDescriptorSets(m_pDevice->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
            }
        }
        catch (...)
        {
            //任一步失败都走同一套清理：池 → 布局 → UBO；然后再把异常交给调用方
            DestroyPassUboResources();
            throw;
        }
    }

    void DoodleRenderPass::DestroyPassUboResources()
    {
        if (m_pDevice != nullptr)
        {
            //池先走：集合的寿命与池一致，随池一起失效
            if (m_passUboDescriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_pDevice->GetLogicalDevice(), m_passUboDescriptorPool, nullptr);
                m_passUboDescriptorPool = VK_NULL_HANDLE;
            }

            //布局只在创建管线布局时被读过一次；管线已经先于这里销毁
            if (m_passUboDescriptorSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(), m_passUboDescriptorSetLayout, nullptr);
                m_passUboDescriptorSetLayout = VK_NULL_HANDLE;
            }
        }

        m_passUboDescriptorSets.fill(VK_NULL_HANDLE);

        //Destroy 内部会先解除持久映射，再销毁 buffer/memory；未初始化时是空操作
        for (DoodleGraphicsBuffer& buffer : m_passUbos)
        {
            buffer.Destroy();
        }
    }
}
