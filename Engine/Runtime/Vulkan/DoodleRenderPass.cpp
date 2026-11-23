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

namespace Doodle
{
    void DoodleRenderPass::Initialize(const DoodleRenderPassInitInfo& initInfo,
                                      const DoodleVulkanPipelinePassConfig& passConfig)
    {
        //本 pass 的 set 0 与 push 范围按自己的契约取：着色器加载时按同一份核对
        const DoodlePassShaderInterface& passInterface = GetPassShaderInterface(m_passType);

        m_pDevice = &initInfo.device;
        m_pipelineCompileCache = initInfo.pipelineCompileCache;
        m_pushConstantRange = passInterface.pushConstantRange;
        m_passConfig = passConfig;

        //每个 pass 一份 set 0 常量资源
        CreatePassUboResources(passInterface);
    }

    void DoodleRenderPass::Destroy()
    {
        //条目里的管线是显式 Destroy 的：DoodleVulkanPipelineObject 的析构不收句柄
        DestroyPipelines();

        //管线先走：它们的管线布局引用 set 0 布局。然后拆本 pass 的常量资源；
        //最后交给派生类拆自己的资源 —— 此时 device / 句柄还没清空
        DestroyPassUboResources();
        OnDestroy();

        m_pushConstantRange = {};
        m_pipelineCompileCache = VK_NULL_HANDLE;
        m_pDevice = nullptr;
    }

    void DoodleRenderPass::WarmupShaders(const std::vector<const DoodleShader*>& shaders)
    {
        //只解析 / 创建管线，不录制：把编译代价和失败点放在加载边界上。
        //与 Render 走同一个 ResolvePipeline，预热过的着色器在第一帧只多一次查表。
        //队列里只会出现初始化成功的着色器；空指针只可能是调用方传错，
        //预热不是校验入口，跳过即可
        for (const DoodleShader* pShader : shaders)
        {
            if (pShader == nullptr)
            {
                continue;
            }

            //队列里可能有只给别的 pass 用的着色器（例如 shadow-only）；
            //没有本 pass 数据就跳过，真正的缺失由录制前的解析拦下
            if (pShader->GetPass(m_passType) == nullptr)
            {
                continue;
            }

            //结果不用：解析本身就会把管线建出来并填进表
            static_cast<void>(ResolvePipeline(*pShader));
        }
    }

    void DoodleRenderPass::RebuildPipelines(const DoodleVulkanPipelinePassConfig& passConfig)
    {
        //每条管线按条目里那份描述、用新的 pass 配置重建；对象不动，交出去的引用有效
        for (auto& entry : m_pipelinesByShader)
        {
            entry.second->pipeline.Rebuild(m_pipelineCompileCache, passConfig, entry.second->shaderPass);
        }

        //全部成功才记下新配置：之后再解析的管线也用它
        m_passConfig = passConfig;
    }

    std::size_t DoodleRenderPass::GetPipelineCount() const
    {
        return m_pipelinesByShader.size();
    }

    DoodleVulkanPipelinePassConfig DoodleRenderPass::GetPassConfig() const
    {
        return m_passConfig;
    }

    const DoodleVulkanPipelineObject& DoodleRenderPass::ResolvePipeline(const DoodleShader& shader)
    {
        const auto it = m_pipelinesByShader.find(&shader);
        if (it != m_pipelinesByShader.end())
        {
            return it->second->pipeline;
        }

        const DoodleShaderPass* pPass = shader.GetPass(m_passType);
        if (pPass == nullptr)
        {
            throw std::invalid_argument("shader '" + shader.GetAssetPath() + "' has no description for this pass");
        }

        return CreatePipelineItem(shader, *pPass).pipeline;
    }

    DoodleRenderPass::PipelineItem& DoodleRenderPass::CreatePipelineItem(
        const DoodleShader& shader,
        const DoodleShaderPass& shaderPass)
    {
        //set 0 布局在 Initialize 里建好；没有它说明 pass 还没初始化（或已经销毁），
        //建出来的管线在录制时没有可绑的常量集，不如当场说清楚
        if (m_passUboDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::logic_error("graphics pipeline: pass has no set 0 uniform layout");
        }

        //先把条目放进表、再建管线：放表这一步只可能因为内存不足失败，
        //那时还什么都没建；建管线失败就把刚放进去的条目摘掉 —— 两条失败路径都不留
        //半截状态，也不会漏掉已经建出来的句柄
        auto pItem = std::make_unique<PipelineItem>();
        pItem->shaderPass = shaderPass;
        const auto inserted = m_pipelinesByShader.emplace(&shader, std::move(pItem));

        try
        {
            inserted.first->second->pipeline.Initialize(
            {
                .device = *m_pDevice,
                .pipelineCompileCache = m_pipelineCompileCache,
                .passUboDescriptorSetLayout = m_passUboDescriptorSetLayout,
                .pushConstantRange = m_pushConstantRange,
                .passConfig = m_passConfig,
                .shaderPass = inserted.first->second->shaderPass
            });
        }
        catch (...)
        {
            //Initialize 失败时自己会收掉已建的部分，这里只需要把条目拿走
            m_pipelinesByShader.erase(inserted.first);
            throw;
        }

        return *inserted.first->second;
    }

    void DoodleRenderPass::DestroyPipelines()
    {
        for (auto& entry : m_pipelinesByShader)
        {
            entry.second->pipeline.Destroy();
        }
        m_pipelinesByShader.clear();
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
