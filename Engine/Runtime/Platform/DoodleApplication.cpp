//
//  DoodleApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"
#include "DoodleFileSystem.h"
#include "DoodleWindow.h"

#include "Core/DoodleGameObject.h"
#include "Rendering/DoodleMeshRenderer.h"
#include "Scene/DoodleScene.h"
#include "Scene/DoodleSceneBuilder.h"
#include "Scene/DoodleSceneData.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string_view>

namespace
{
    /**
     * 启动场景的路径
     *
     * 相对路径，由 DoodleFileSystem 决定根在哪里：桌面是工作目录
     * 或 .app/Contents/Resources，Android 将来是 APK assets。
     * 修启动内容只改这一处（以及文件本身）
     */
    constexpr std::string_view kStartupScenePath = "Scenes/main.json";
}

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
        if (m_pWindow == nullptr)
        {
            //子类把窗口交出来是它的分内事，理论上不可能为空。走不到的分支也要拦：
            //不拦的话，Run 里第一次解引用就是一段和根因对不上的崩溃
            throw std::runtime_error("DoodleApplication: window is null");
        }

        // 文件来源不依赖窗口，先建好；Vulkan、资产与内容由 InitializeRuntime 接手
        InitializeFileSystem();
        InitializeRuntime();
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
        // 运行时（场景 → 资产 → Vulkan）由 DestroyRuntime 统一拆。
        // 文件系统与窗口在它之后释放：资产管理器在析构兜底路径上还可能读文件，
        // 交换链、表面则依附于窗口
        DestroyRuntime();
        DestroyFileSystem();
        m_pWindow.reset();
    }

    void DoodleApplication::NotifyWindowResized()
    {
        m_vulkanManager.NotifyWindowResized();
    }

    void DoodleApplication::InitializeFileSystem()
    {
        m_pFileSystem = CreateFileSystem();
        if (m_pFileSystem == nullptr)
        {
            throw std::runtime_error("DoodleApplication: file system is null");
        }
    }

    void DoodleApplication::InitializeRuntime()
    {
        // 前置：窗口与文件系统已经就绪。传给 Vulkan 各层的是引用：
        // 它们只借不放，窗口的持有者仍在本类
        m_vulkanManager.Initialize(*m_pWindow);

        // 资产管理器的内建 loader 需要设备与命令缓冲层，必须排在 Vulkan 之后。
        // 文件来源归应用持有，资产管理器只借引用
        m_assetManager.Initialize({
            .fileSystem = *m_pFileSystem,
            .device = m_vulkanManager.GetDevice(),
            .commandBuffer = m_vulkanManager.GetCommandBuffer()});

        // 内容加载放在这里：macOS 的 Initialize 与 Android 首次
        // APP_CMD_INIT_WINDOW 都经过 InitializeRuntime，这是两个平台唯一共同的
        // 「Vulkan 已就绪」时点
        LoadStartupScene();

        // 全部成功才算运行时起来：中途抛异常时保持 false，后续 DestroyRuntime
        // 不会去等一个不存在或半初始化的实例
        m_runtimeInitialized = true;
    }

    void DoodleApplication::DestroyRuntime()
    {
        // 场景先于资产：组件里的 MeshRenderer 引用着网格，OnDestroy 必须在
        // 资产还活着时跑完。销毁是显式的，不把回调留给成员析构
        m_sceneManager.DestroyReally();

        // 网格缓冲可能还被在途帧引用：Vulkan 起来过才需要等一次。
        // 等完之后先销毁资产、再拆命令缓冲，是规范允许的顺序：executable 状态的
        // 命令缓冲只要不再提交、也不是 pending，就不阻止它引用的 buffer 先销毁
        if (m_runtimeInitialized)
        {
            m_vulkanManager.WaitIdle();
        }

        // 资产是纯 CPU 侧 / 幂等的，运行时从未起来过也要走一遍
        m_assetManager.Destroy();

        if (m_runtimeInitialized)
        {
            m_vulkanManager.Destroy();
            m_runtimeInitialized = false;
        }
    }

    void DoodleApplication::DestroyFileSystem()
    {
        m_pFileSystem.reset();
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

    void DoodleApplication::LoadStartupScene()
    {
        const DoodleSceneData* pSceneData = m_assetManager.Load<DoodleSceneData>(kStartupScenePath);
        if (pSceneData == nullptr)
        {
            // Loader 的契约是不返回空指针；这里只做防御
            throw std::runtime_error("DoodleApplication: startup scene data is null");
        }

        DoodleSceneBuilder::Build(m_sceneManager, m_assetManager, *pSceneData);
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
}
