//
//  DoodleGameObject.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#include "DoodleGameObject.h"

#include <cstddef>

namespace Doodle
{
    DoodleGameObject::~DoodleGameObject()
    {
        // 逆序销毁，与 C++ 成员析构的直觉一致。
        // 析构函数体结束时 vector 才会析构，所以此刻所有组件都还活着，
        // OnDestroy 里访问兄弟组件、读宿主 Transform 都是安全的。
        // 已标记删除但还没 sweep 的组件也会在这里补上回调
        for (auto iter = m_components.rbegin(); iter != m_components.rend(); ++iter)
        {
            // 只取非拥有指针，所有权仍留在容器里。
            // 注意 *iter 是 unique_ptr&，写成 auto pComponent = *iter 会按值
            // 推导并尝试拷贝不可拷贝的 unique_ptr；显式 get() 没有这个问题
            auto* pComponent = iter->get();
            if (pComponent->m_enabled)
            {
                pComponent->m_enabled = false;
                pComponent->OnDisable();
            }
            pComponent->OnDestroy();
        }
    }

    void DoodleGameObject::RemoveComponent(DoodleComponent* pComponent)
    {
        if (pComponent != nullptr && pComponent->m_pGameObject == this)
        {
            // 只标记，不碰容器。真正的 erase 统一在 Update 末尾的 sweep 里做：
            // OnUpdate 期间删谁都不会让正在遍历的 vector 失效，重复调用也幂等
            pComponent->m_pendingRemove = true;
        }
    }

    void DoodleGameObject::Update(const DoodleUpdateContext& context)
    {
        const auto count = m_components.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            auto* pComponent = m_components[i].get();
            if (pComponent->m_pendingRemove || !pComponent->m_enabled)
            {
                continue;
            }

            pComponent->OnUpdate(context);
        }

        SweepPendingRemovals();
    }

    void DoodleGameObject::SweepPendingRemovals()
    {
        bool anyPending = false;

        // 阶段一：容器完全不动，所有组件（包括其它待删的）都还活着。
        // 这样 OnDisable / OnDestroy 里访问兄弟组件不会悬空。
        // 这里没有 erase，容器结构不变，迭代器不会失效
        for (auto iter = m_components.begin(); iter != m_components.end(); ++iter)
        {
            auto* pComponent = iter->get();
            if (pComponent->m_pendingRemove)
            {
                anyPending = true;

                if (pComponent->m_enabled)
                {
                    pComponent->m_enabled = false;
                    pComponent->OnDisable();
                }
                pComponent->OnDestroy();
            }
        }

        if (anyPending)
        {
            // 阶段二：一次压缩，保留存活组件的原有顺序。
            // unique_ptr 在这里析构，组件对象在全部回调结束之后才真正消失
            std::erase_if(m_components, [](const auto& pComponent)
            {
                return pComponent->m_pendingRemove;
            });
        }
    }
}
