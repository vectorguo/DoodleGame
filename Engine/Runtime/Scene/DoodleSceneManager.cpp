//
//  DoodleSceneManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/23.
//

#include "DoodleSceneManager.h"
#include "DoodleScene.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace Doodle
{
    DoodleSceneManager* DoodleSceneManager::s_pInstance = nullptr;

    DoodleSceneManager::DoodleSceneManager()
    {
        // 登记在构造里：管理器是应用的成员，登记之后到析构之前，
        // GetInstance 拿到的都是这一份。
        s_pInstance = this;
    }

    DoodleSceneManager::~DoodleSceneManager()
    {
        // 先撤登记，再跑兜底：析构一旦开始，这个对象就不再是「当前管理器」。
        // 认一下是不是自己，和 DoodleApplication 的析构同理。
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }

        // 正常路径下应用已经在拆 Vulkan 之前调过 DestroyReally，状态已经到
        // Destroyed，这里只兜底：还没开始销毁（Alive / PendingDestroy）的才
        // 在析构器里补回调。
        if (m_lifecycleState == ELifecycleState::Alive ||
            m_lifecycleState == ELifecycleState::PendingDestroy)
        {
            DestroyReally();
        }
    }

    void DoodleSceneManager::Update()
    {
        if (!IsAlive())
        {
            // 管理器本身不在 Alive：所有场景已经被 Destroy 标记过，不再驱动
            // 也不再扫。整棵树的回调交给拥有者的 DestroyReally。
            return;
        }

        // 快照数量：某个场景的 OnUpdate 里新建的场景从下一帧开始更新。
        // 每轮重新按索引取指针，所以 vector 扩容、延迟销毁都安全。
        const auto count = m_scenes.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            auto* pScene = m_scenes[i].get();
            if (!pScene->IsAlive())
            {
                continue;
            }

            pScene->Update();
        }

        // 场景的更新里可能请求销毁管理器自身。和下层一样，不在本函数里回收，
        // 留给拥有者的 DestroyReally。
        if (IsAlive())
        {
            SweepPendingScenes();
        }
    }

    void DoodleSceneManager::Destroy()
    {
        if (IsAlive())
        {
            m_lifecycleState = ELifecycleState::PendingDestroy;

            // 先标记、不跑回调：请求之后到 DestroyReally 之间，FindScene 和
            // 外部缓存的引用都应一致地看到「场景已不可用」。
            for (auto& pScene : m_scenes)
            {
                pScene->Destroy();
            }
        }
    }

    DoodleScene& DoodleSceneManager::CreateScene(std::string name)
    {
        // 进入销毁流程后创建的场景没有下一次 sweep，和下层容器同理。
        if (!IsAlive())
        {
            throw std::logic_error("DoodleSceneManager: cannot create Scene after Destroy");
        }

        // DoodleScene 构造私有，make_unique 访问不到；裸 new 立刻交给 unique_ptr。
        auto pScene = std::unique_ptr<DoodleScene>(new DoodleScene(std::move(name)));
        DoodleScene& scene = *pScene;
        m_scenes.push_back(std::move(pScene));
        return scene;
    }

    void DoodleSceneManager::DestroyScene(DoodleScene& scene)
    {
        // 运行时只有一个管理器，场景引用天然属于它，不再做归属校验。
        // 场景的销毁本身是幂等的；拿到已经销毁的场景引用属于悬空引用，
        // 那是调用方的问题。
        scene.Destroy();
    }

    DoodleScene* DoodleSceneManager::FindScene(const std::string_view name) const
    {
        for (auto& pScene : m_scenes)
        {
            if (!pScene->IsAlive())
            {
                continue;
            }

            if (pScene->GetName() == name)
            {
                return pScene.get();
            }
        }

        return nullptr;
    }

    std::vector<DoodleScene*> DoodleSceneManager::GetScenes() const
    {
        std::vector<DoodleScene*> scenes;
        GetScenes(scenes);
        return scenes;
    }

    void DoodleSceneManager::GetScenes(std::vector<DoodleScene*>& scenes) const
    {
        // 先清空：调用方复用的是同一块缓冲，结果应是本帧的完整快照
        scenes.clear();

        for (const auto& pScene : m_scenes)
        {
            if (pScene->IsAlive())
            {
                scenes.push_back(pScene.get());
            }
        }
    }

    void DoodleSceneManager::DestroyReally()
    {
        if (m_lifecycleState == ELifecycleState::Destroying ||
            m_lifecycleState == ELifecycleState::Destroyed)
        {
            return;
        }
        m_lifecycleState = ELifecycleState::Destroying;

        // 覆盖直接调用 DestroyReally、没有经过 Destroy 请求的情况。
        for (auto& pScene : m_scenes)
        {
            pScene->Destroy();
        }

        // 容器在回调期间完全不动，所有场景都还在；场景内部再拆自己的对象。
        for (auto& pScene : m_scenes)
        {
            pScene->DestroyReally();
        }

        m_scenes.clear();
        m_lifecycleState = ELifecycleState::Destroyed;
    }

    void DoodleSceneManager::SweepPendingScenes()
    {
        // 阶段一：容器不动，所有场景都还在；回调期间访问兄弟场景安全。
        //
        // 用数量快照而不是范围 for：sweep 期间管理器还是 Alive，场景的销毁
        // 回调里如果通过管理器创建新场景，m_scenes 的扩容会让范围 for 的
        // 迭代器失效。快照之外新建的场景留到下一轮。
        const auto count = m_scenes.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            auto* pScene = m_scenes[i].get();
            if (pScene->IsPendingDestroy())
            {
                pScene->DestroyReally();
            }
        }

        // 阶段二：一次压缩，保留存活场景的原有顺序。
        // 只擦已经跑完回调（Destroyed）的场景：如果某个回调把扫描位置之前的
        // 场景标成了 PendingDestroy，它会留到下一轮 sweep，而不是在 erase 时
        // 走析构兜底、把回调插到所有回调之后。
        std::erase_if(m_scenes, [](const auto& pScene)
        {
            return pScene->IsDestroyed();
        });
    }
}
