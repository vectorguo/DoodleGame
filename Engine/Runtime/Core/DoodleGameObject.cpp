//
//  DoodleGameObject.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#include "DoodleGameObject.h"

#include <cstddef>
#include <utility>

namespace Doodle
{
    DoodleGameObject::DoodleGameObject(DoodleScene& scene, std::string name)
        : m_name(std::move(name))
        , m_pScene(&scene)
    {
    }

    DoodleGameObject::~DoodleGameObject()
    {
        // 正常路径下所属场景已经在 sweep 里执行过 DestroyReally，状态已经到
        // Destroyed，这里只兜底：还没开始销毁（Alive / PendingDestroy）的才
        // 在析构器里补回调。
        if (m_lifecycleState == ELifecycleState::Alive ||
            m_lifecycleState == ELifecycleState::PendingDestroy)
        {
            DestroyReally();
        }
    }

    void DoodleGameObject::DestroyReally()
    {
        if (m_lifecycleState == ELifecycleState::Destroying ||
            m_lifecycleState == ELifecycleState::Destroyed)
        {
            return;
        }
        m_lifecycleState = ELifecycleState::Destroying;

        // 逆序执行回调，与添加顺序相反，和 C++ 成员析构的直觉一致。
        // 容器在回调期间完全不动，所有组件对象都还在；尚未轮到的组件仍是
        // Alive，已经处理过的是 Destroyed。
        // 回调里禁止增删组件：这里用的是反向迭代器，容器一变就失效。
        for (auto iter = m_components.rbegin(); iter != m_components.rend(); ++iter)
        {
            // 只取非拥有指针，所有权仍留在容器里。
            // 注意 *iter 是 unique_ptr&，写成 auto pComponent = *iter 会按值
            // 推导并尝试拷贝不可拷贝的 unique_ptr；显式 get() 没有这个问题
            auto* pComponent = iter->get();

            // 轮到自己才推进生命周期。已经处理过的兄弟组件是 Destroyed，
            // 还没轮到的是 Alive —— OnDestroy 里查兄弟组件看到的就是这个视图。
            pComponent->m_lifecycleState = ELifecycleState::Destroying;

            if (pComponent->m_enabled)
            {
                pComponent->m_enabled = false;
                pComponent->OnDisable();
            }
            pComponent->OnDestroy();

            pComponent->m_lifecycleState = ELifecycleState::Destroyed;
        }

        // 回调结束后才析构组件。此刻 GameObject 和 Scene 仍然完整，
        // 组件析构函数读到的是完整宿主；之后对象只剩空壳，等场景 erase。
        m_components.clear();
        m_lifecycleState = ELifecycleState::Destroyed;
    }

    void DoodleGameObject::RemoveComponent(DoodleComponent* pComponent)
    {
        if (pComponent != nullptr && pComponent->m_pGameObject == this)
        {
            // 只标记，不碰容器。真正的 erase 统一在 Update 末尾的 sweep 里做：
            // OnUpdate 期间删谁都不会让正在遍历的 vector 失效，重复调用也幂等。
            // 已经在销毁流程里的组件不再接受新的请求。
            if (pComponent->m_lifecycleState == ELifecycleState::Alive)
            {
                pComponent->m_lifecycleState = ELifecycleState::PendingDestroy;
            }
        }
    }

    void DoodleGameObject::Update()
    {
        const auto count = m_components.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            // 组件在本轮把宿主推进到 PendingDestroy 后，剩余组件不再更新。
            // 组件删除也交给 DestroyReally 统一处理，避免对象临死前再拆一次。
            if (!IsAlive())
            {
                break;
            }

            auto* pComponent = m_components[i].get();
            if (!pComponent->IsEnabled())
            {
                continue;
            }

            pComponent->OnUpdate();
        }

        if (IsAlive())
        {
            SweepPendingComponents();
        }
    }

    void DoodleGameObject::SweepPendingComponents()
    {
        bool anyPending = false;

        // 阶段一：容器完全不动，所有组件（包括其它待销毁的）都还在。
        // 这样 OnDisable / OnDestroy 里访问尚未轮到的兄弟组件不会悬空。
        // 这里没有 erase，容器结构不变，迭代器不会失效。
        for (auto iter = m_components.begin(); iter != m_components.end(); ++iter)
        {
            auto* pComponent = iter->get();
            if (pComponent->m_lifecycleState == ELifecycleState::PendingDestroy)
            {
                anyPending = true;
                pComponent->m_lifecycleState = ELifecycleState::Destroying;

                if (pComponent->m_enabled)
                {
                    pComponent->m_enabled = false;
                    pComponent->OnDisable();
                }
                pComponent->OnDestroy();

                pComponent->m_lifecycleState = ELifecycleState::Destroyed;
            }
        }

        if (anyPending)
        {
            // 阶段二：一次压缩，保留存活组件的原有顺序。
            // unique_ptr 在这里析构，组件对象在全部回调结束之后才真正消失。
            std::erase_if(m_components, [](const auto& pComponent)
            {
                return !pComponent->IsAlive();
            });
        }
    }
}
