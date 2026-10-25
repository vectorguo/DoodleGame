//
//  DoodleApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"
#include "DoodleWindow.h"

#include "Core/DoodleGameObject.h"
#include "Rendering/DoodleMeshRenderer.h"
#include "Scene/DoodleScene.h"
#include "Vulkan/DoodleMeshLibrary.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace Doodle
{
    DoodleApplication* DoodleApplication::s_pInstance = nullptr;

    DoodleApplication::DoodleApplication()
    {
        //登记在构造里：此刻窗口还不存在（它在 Initialize 里才建），
        //所以「窗口在收事件、却问不到应用对象」这个状态压根没有机会出现。
        //这与放在 Initialize 里的区别不只是早晚 —— 那条得靠「哪一层会泵事件」
        //的推理才成立，这条不用推理
        s_pInstance = this;
    }

    DoodleApplication::~DoodleApplication()
    {
        //先认一下是不是自己：万一同进程里先后有过两个应用对象，
        //先析构的那个不该把后一个的登记一起抹掉
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }
    }

    void DoodleApplication::Initialize()
    {
        //窗口先于 Vulkan：表面建在窗口上
        m_pWindow = CreateWindow();

        //子类把窗口交出来是它的分内事，理论上不可能为空。走不到的分支也要拦：
        //不拦的话，Run 里第一次解引用就是一段和根因对不上的崩溃
        if (m_pWindow == nullptr)
        {
            throw std::runtime_error("DoodleApplication: window is null");
        }

        InitializeVulkan();
    }

    void DoodleApplication::Run()
    {
        // 帧时钟的起点：之前是构造与 Initialize，那段时间不属于任何一帧。
        // 覆盖 Run 的平台也要在循环前做同一件事（Android 那份见 DoodleAndroidApplication::Run）
        m_lastFrameTime = std::chrono::steady_clock::now();

        while (!m_pWindow->IsCloseRequested())
        {
            m_pWindow->PumpEvents();
            Tick();
        }
    }

    void DoodleApplication::Destroy()
    {
        // 场景先于 Vulkan：组件的 OnDestroy 以后可能释放 GPU 资源，
        // 必须赶在 Vulkan 各层拆掉之前跑。
        DestroyScenes();

        // 再按 Initialize 的反序拆 Vulkan 与窗口：交换链、表面都依附于窗口，
        // 窗口先没的话，销毁它们就是拿着一个已经作废的句柄去用
        DestroyVulkan();
        m_pWindow.reset();
    }

    void DoodleApplication::NotifyWindowResized()
    {
        m_vulkanManager.NotifyWindowResized();
    }

    void DoodleApplication::Tick()
    {
        const auto now = std::chrono::steady_clock::now();

        // 对「上一帧」求差即可：首帧的上一帧是 Run 进入循环的时刻。它和
        // Suspend / Resume 之后的长间隔是同一类情况，都被下面的上限钳住，
        // 不需要首帧标志
        float deltaSeconds = std::chrono::duration<float>(now - m_lastFrameTime).count();

        // 断点、拖窗口、系统卡顿之后两帧间隔可能很长；不钳上限的话，
        // 组件会在一帧里走出一大段位移，甚至穿过碰撞体。
        deltaSeconds = std::min(deltaSeconds, 0.1f);

        m_lastFrameTime = now;

        // 先更新场景，再收集本帧要画的东西，最后画：渲染读到的是本帧最新的
        // Transform，而收集夹在两者之间，拿到的是 Update 之后的一致快照
        m_sceneManager.Update(DoodleUpdateContext{deltaSeconds});
        CollectDrawItems();
        m_vulkanManager.DrawFrame(m_reusableDrawItems);
    }

    void DoodleApplication::CollectDrawItems()
    {
        m_reusableDrawItems.clear();

        m_sceneManager.GetScenes(m_reusableScenes);
        for (const DoodleScene* pScene : m_reusableScenes)
        {
            pScene->GetGameObjects(m_reusableGameObjects);
            for (const DoodleGameObject* pGameObject : m_reusableGameObjects)
            {
                // 一个对象只取第一个渲染器：GetComponent 的现有语义。
                // 需要同一对象画多份网格时，再扩展成遍历全部组件
                const auto* pRenderer = pGameObject->GetComponent<DoodleMeshRenderer>();
                if (pRenderer != nullptr && pRenderer->IsEnabled())
                {
                    const DoodleMesh* pMesh = pRenderer->GetMesh();
                    if (pMesh != nullptr)
                    {
                        m_reusableDrawItems.push_back(
                     {
                            .pMesh = pMesh,
                            .model = pGameObject->GetTransform().GetMatrix()
                        });
                    }
                }
            }
        }
    }

    void DoodleApplication::InitializeVulkan()
    {
        //传给 Vulkan 各层的是引用：它们只借不放，窗口的持有者仍在本类
        m_vulkanManager.Initialize(*m_pWindow);

        // ---- 第 3 步的临时内容，第 4 步由场景文件加载替换 ----
        //
        // 手工建一个场景与对象，验证「MeshRenderer + Transform → 每帧收集 →
        // DrawFrame」这条路径。位置放在这里，是因为 macOS 的 Initialize 与
        // Android 首次 APP_CMD_INIT_WINDOW 都经过 InitializeVulkan，此刻
        // 网格库已经初始化完成；第 4 步的加载器也接在同一个点
        DoodleScene& scene = m_sceneManager.CreateScene("Demo");
        DoodleGameObject& cube = scene.CreateGameObject("Cube");

        const DoodleMesh* const pCubeMesh =
            m_vulkanManager.GetMeshLibrary().Find(DoodleMeshLibrary::BuiltinCubeName);
        if (pCubeMesh == nullptr)
        {
            throw std::runtime_error("builtin cube mesh is missing from the mesh library!");
        }

        DoodleMeshRenderer& meshRenderer = cube.AddComponent<DoodleMeshRenderer>();
        meshRenderer.SetMesh(pCubeMesh);
    }

    void DoodleApplication::DestroyVulkan()
    {
        m_vulkanManager.Destroy();
    }

    void DoodleApplication::DestroyScenes()
    {
        // 显式销毁，不把回调留给成员析构：SceneManager 的析构只是最后兜底。
        m_sceneManager.DestroyReally();
    }
}
