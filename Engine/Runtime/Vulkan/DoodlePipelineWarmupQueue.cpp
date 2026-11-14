//
//  DoodlePipelineWarmupQueue.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/14.
//

#include "../Vulkan//DoodlePipelineWarmupQueue.h"

namespace Doodle
{
    DoodlePipelineWarmupQueue* DoodlePipelineWarmupQueue::s_pInstance = nullptr;

    void DoodlePipelineWarmupQueue::Push(const DoodleShader& shader)
    {
        const std::lock_guard lock(m_mutex);
        m_pending.push_back(&shader);
    }

    std::vector<const DoodleShader*> DoodlePipelineWarmupQueue::Drain()
    {
        std::vector<const DoodleShader*> drained;

        {
            const std::lock_guard lock(m_mutex);

            //交换而不是拷贝：把已有容量整块交给调用方，队列自己回到空。
            //稳态下两边都没有分配
            drained.swap(m_pending);
        }

        return drained;
    }

    void DoodlePipelineWarmupQueue::Clear()
    {
        const std::lock_guard lock(m_mutex);
        m_pending.clear();
    }
}
