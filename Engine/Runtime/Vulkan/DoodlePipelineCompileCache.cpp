//
//  DoodlePipelineCompileCache.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#include "DoodlePipelineCompileCache.h"

#include <stdexcept>

#include "DoodleVulkanDevice.h"

namespace Doodle
{
    void DoodlePipelineCompileCache::Initialize(const DoodleVulkanDevice& device, const std::vector<std::byte>& initialData)
    {
        m_pDevice = &device;

        //flags 留 0：不带 EXTERNALLY_SYNCHRONIZED 位，驱动自己做同步，
        //多个线程可以同时拿它建管线。
        //initialDataSize 为 0 时缓存从空开始；不为 0 时驱动会先核对数据头，
        //对不上（换了显卡或驱动）就当空的用
        const VkPipelineCacheCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .flags = 0,
            .initialDataSize = initialData.size(),
            .pInitialData = initialData.empty() ? nullptr : initialData.data()
        };

        if (vkCreatePipelineCache(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pipelineCache) != VK_SUCCESS)
        {
            m_pDevice = nullptr;
            throw std::runtime_error("failed to create pipeline cache!");
        }
    }

    void DoodlePipelineCompileCache::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //已经建好的管线不受影响：管线只在创建那一刻读编译缓存
        vkDestroyPipelineCache(m_pDevice->GetLogicalDevice(), m_pipelineCache, nullptr);
        m_pipelineCache = VK_NULL_HANDLE;
        m_pDevice = nullptr;
    }
}
