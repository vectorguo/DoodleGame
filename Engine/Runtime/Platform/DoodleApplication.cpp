//
//  DoodleApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"
#include "DoodleFileSystem.h"
#include "DoodleInput.h"
#include "DoodlePackageRegistration.h"
#include "DoodleWindow.h"

#include "Core/DoodleGameObject.h"
#include "Core/DoodleBuiltinComponentRegistration.h"
#include "Core/DoodleTime.h"
#include "Rendering/DoodleCamera.h"
#include "Rendering/DoodleDrawContext.h"
#include "Rendering/DoodleLight.h"
#include "Rendering/DoodleMesh.h"
#include "Rendering/DoodleMeshRenderer.h"
#include "Scene/DoodleScene.h"
#include "Scene/DoodleSceneData.h"

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace
{
    /**
     * 启动场景的路径
     *
     * 相对路径，由 DoodleFileSystem 决定根在哪里：macOS 是工作目录
     * 或 .app/Contents/Resources，Windows 是工作目录或可执行文件所在目录，
     * Android 是 APK assets。
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
        ResetFrameClock();

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
        // 引擎内置组件先注册；包和 GamePlay 再往上叠
        RegisterBuiltinComponents();

        // Engine/Package 的组件类型在内容加载之前统一注册。
        // 放在这里而不是平台入口：桌面 Initialize 与 Android 首次 INIT_WINDOW
        // 都经过本函数，平台与 GamePlay 都不需要关心有哪些包
        RegisterPackageComponents();

        // 输入快照从零开始：桌面 Initialize 与 Android 首次 INIT_WINDOW 都经过这里。
        // Android 从后台回来不走本函数，那一半由窗口在失焦时清空
        DoodleInput::Reset();

        // 前置：窗口与文件系统已经就绪。传给 Vulkan 各层的是引用：
        // 它们只借不放，窗口的持有者仍在本类
        m_vulkanManager.Initialize(*m_pWindow);

        // 资产管理器的内建 loader 只需要文件来源与设备。上传用的传输池由
        // DoodleGraphicsBuffer / DoodleTexture 自己通过 DoodleCommandBuffer
        // 的单例取，不需要从这里把命令缓冲层传下去。
        // 文件来源归应用持有，资产管理器只借引用
        m_assetManager.Initialize(
     {
            .fileSystem = *m_pFileSystem,
            .device = m_vulkanManager.GetDevice()
        });

        // 内容加载放在这里：macOS / Windows 的 Initialize 与 Android 首次
        // APP_CMD_INIT_WINDOW 都经过 InitializeRuntime，这是各平台唯一共同的
        // 「Vulkan 已就绪」时点
        LoadStartupScene();

        // 全部成功才算运行时起来：中途抛异常时保持 false，后续 DestroyRuntime
        // 不会去等一个不存在或半初始化的实例
        m_runtimeInitialized = true;
    }

    void DoodleApplication::DestroyRuntime()
    {
        // 退出时可能还按着右键：先恢复光标，再清空全局输入快照。
        // 放在这里而不是某个平台分支里，桌面与 Android 走同一条收尾路径；
        // 窗口此刻仍然有效，Vulkan 与场景都还没拆
        if (m_pWindow != nullptr)
        {
            m_pWindow->SetCursorCaptured(false);
        }
        DoodleInput::Reset();

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

    void DoodleApplication::ResetFrameClock()
    {
        DoodleTime::Reset();
    }

    void DoodleApplication::Tick()
    {
        // 帧时长（钳上限、乘 timeScale、累计）统一由 DoodleTime 维护，
        // 本层只按帧边界推进一次；下面各步读到的都是同一份本帧时间
        DoodleTime::Update();

        // 输入快照每帧刷新一次，位置在场景更新之前：同一帧内所有组件看到的
        // 是同一份状态。平台层在 PumpEvents 里已经把原始状态轮询好
        DoodleInput::RawState rawInput = m_pWindow->GetRawInputState();

        // 右键控制光标捕获。这是窗口级策略，留在应用层；FreeLook 之类的组件
        // 只查询 DoodleInput，不碰窗口
        if (rawInput.IsMouseButtonDown(DoodleMouseButton::Right) != rawInput.cursorCaptured)
        {
            m_pWindow->SetCursorCaptured(rawInput.IsMouseButtonDown(DoodleMouseButton::Right));

            // 切换捕获时窗口已经把位移清零。重新取一次，避免这一帧把切换前的
            // 旧位移算进视角
            rawInput = m_pWindow->GetRawInputState();
        }

        DoodleInput::Update(rawInput);

        // 先更新场景，再收集本帧要画的东西，最后画：渲染读到的是本帧最新的
        // Transform，而收集夹在两者之间，拿到的是 Update 之后的一致快照
        m_sceneManager.Update();

        // 主相机由 DoodleCamera 自己维护，这里只取结果。没有相机就没有
        // view / proj：绘制项收集也只喂给 DrawFrame，所以本帧连收集一起跳过，
        // 不做没有消费者的遍历。启动场景已经在 LoadStartupScene 里校验过
        // 必须有一台，这里为空只可能是运行期把相机组件停用或销毁了 ——
        // 跳过本帧，而不是拿一个假相机去画
        const DoodleCamera* pMainCamera = DoodleCamera::GetMainCamera();
        if (pMainCamera != nullptr)
        {
            CollectDrawItems();

            // 相机、光源与绘制项在本帧组成一份上下文，渲染门面只认这一份输入。
            // 它只是栈上的引用集合：容器与组件仍归本层，DrawFrame 返回后即可复用
            m_vulkanManager.DrawFrame(DoodleDrawContext
            {
                .camera = *pMainCamera,
                .pLight = DoodleLight::GetMainLight(),
                .drawItems = m_reusableDrawItems,
            });
        }
    }

    void DoodleApplication::LoadStartupScene()
    {
        const DoodleSceneData* pSceneData = m_assetManager.Load<DoodleSceneData>(kStartupScenePath);
        if (pSceneData == nullptr)
        {
            // Loader 的契约是不返回空指针；这里只做防御
            throw std::runtime_error("DoodleApplication: startup scene data is null");
        }

        // 先建空场景再装配内容：数据读不出来时连场景都不会建；初始化在
        // 校验完全部网格之后才开始创建对象，失败时只会留下一个空场景，
        // 由异常路径上的管理器析构兜底
        DoodleScene& scene = m_sceneManager.CreateScene(pSceneData->name);
        scene.InitializeFromData(*pSceneData);

        // 当前渲染只有一个相机入口：场景里没有可用的主相机就没有 view / proj。
        // 这是启动内容错误，在这里当场报出来，比运行起来后对着一个永远不更新的
        // 窗口猜原因便宜。判断走 DoodleCamera::GetMainCamera()，所以 Overlay
        // 相机、以及全部被停用的相机都不算数。相机可以在运行期被停用 / 销毁，
        // 所以每帧仍要取一次，这条检查只负责启动那一刻
        if (DoodleCamera::GetMainCamera() == nullptr)
        {
            throw std::runtime_error("DoodleApplication: startup scene '" + scene.GetName() +
                                     "' has no base camera");
        }
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
                        // 一条绘制项 = 一次 drawIndexed：把网格按材质切出的
                        // 子网格逐段展开，记录子网格下标；材质从 renderer 的
                        // 材质表取。localToWorld 对同一网格的各段相同
                        const auto localToWorld = pGameObject->GetTransform().GetLocalToWorldMatrix();
                        const auto& materials = pRenderer->GetMaterials();
                        const auto& submeshes = pMesh->GetSubmeshes();
                        for (std::size_t i = 0; i < submeshes.size(); ++i)
                        {
                            m_reusableDrawItems.push_back(
                         {
                                .localToWorld = localToWorld,
                                .pMaterial = &materials[submeshes[i].materialIndex],
                                .pMesh = pMesh,
                                .submeshIndex = static_cast<uint32_t>(i)
                            });
                        }
                    }
                }
            }
        }
    }
}
