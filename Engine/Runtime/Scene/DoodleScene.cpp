//
//  DoodleScene.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/22.
//

#include "DoodleScene.h"
#include "Core/DoodleGameObject.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "Asset/DoodleAssetManager.h"
#include "DoodleSceneData.h"
#include "Rendering/DoodleCamera.h"
#include "Rendering/DoodleMeshRenderer.h"
#include "Vulkan/DoodleMesh.h"

namespace
{
    /**
     * 文件里的欧拉角 → Transform 用的四元数
     *
     * 约定 q = Rz * Ry * Rx：先绕 X、再绕 Y、最后绕 Z，三个轴都是世界轴。
     * 单轴测试（例如 [0, 0, 45]）与旋转顺序无关，但多轴组合必须有一份
     * 写死的约定，否则同一份文件在不同实现里会得到不同的朝向。
     *
     * 不用 glm::quat(vec3)：它的分量组合顺序没有写在这边的代码里，
     * 换一个人读时还得去翻 GLM 实现。这里逐个 angleAxis 乘，顺序一目了然。
     */
    glm::quat EulerDegreesToQuaternion(const glm::vec3& degrees)
    {
        const glm::vec3 radians = glm::radians(degrees);
        return glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f))
             * glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f))
             * glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    }
}

namespace Doodle
{
    DoodleScene::DoodleScene(std::string name)
        : m_name(std::move(name))
    {
    }

    DoodleScene::~DoodleScene()
    {
        // 正常路径下拥有者已经在 sweep 里执行过 DestroyReally，状态已经到
        // Destroyed，这里只兜底：还没开始销毁（Alive / PendingDestroy）的才
        // 在析构器里补回调。
        if (m_lifecycleState == ELifecycleState::Alive ||
            m_lifecycleState == ELifecycleState::PendingDestroy)
        {
            DestroyReally();
        }
    }

    void DoodleScene::Update(const DoodleUpdateContext& context)
    {
        // 已请求销毁或正在销毁：本层对象都已标记 PendingDestroy，不再更新，
        // 也不再做组件级 sweep。剩下的回调由 DestroyReally 统一跑，避免同一批
        // 对象被拆成「Update 先收一部分、DestroyReally 再收剩下的」两条路径。
        if (!IsAlive())
        {
            return;
        }

        if (m_active)
        {
            // 快照数量：OnUpdate 里新建的对象本帧不参与更新。
            // 每轮重新按索引取指针，所以 vector 扩容、对象的延迟销毁都安全。
            const auto count = m_gameObjects.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                // 引用的是堆上的 GameObject，不是 vector 里的 unique_ptr：
                // OnUpdate 里允许 CreateGameObject，扩容只会搬走 unique_ptr，
                // 不会搬动它指向的对象。
                DoodleGameObject& gameObject = *m_gameObjects[i];
                if (gameObject.IsAlive())
                {
                    gameObject.Update(context);
                }
            }
        }

        // 场景可能在某个 OnUpdate 里被请求销毁。此刻本层对象已经全部标记，
        // 但回调不能在这里拆：统一交给 DestroyReally，否则同一批对象会出现
        // 「Update 收一部分、DestroyReally 收剩下的」两条路径。
        if (IsAlive())
        {
            // inactive 时对象不更新，但销毁请求仍要兑现：inactive 场景里的对象
            // 不应该等场景重新激活才真正析构。
            SweepPendingGameObjects();
        }
    }

    void DoodleScene::Destroy()
    {
        if (IsAlive())
        {
            m_lifecycleState = ELifecycleState::PendingDestroy;

            // 先标记、不跑回调：请求之后到 DestroyReally 之间，FindGameObject
            // 和外部缓存的指针都应该一致地看到「这些对象已经不可用」。只改标志
            // 不动容器，在 OnUpdate 中途调用也安全。
            for (auto& pGameObject : m_gameObjects)
            {
                pGameObject->Destroy();
            }
        }
    }

    DoodleGameObject& DoodleScene::CreateGameObject(std::string name)
    {
        // 进入销毁流程后创建的对象没有下一次 sweep：要么拖到容器析构时跑回调，
        // 要么在 DestroyReally 已经走完之后又长出来。这是错误用法，直接拦掉。
        if (!IsAlive())
        {
            throw std::logic_error("DoodleScene: cannot create GameObject after Destroy");
        }

        // GameObject 构造函数是 private，std::make_unique 无法访问；用裸 new
        // 立刻交给 unique_ptr。只有一次分配，new 抛出时不会泄漏。
        auto pGameObject = std::unique_ptr<DoodleGameObject>(new DoodleGameObject(*this, std::move(name)));
        DoodleGameObject& gameObject = *pGameObject;
        m_gameObjects.push_back(std::move(pGameObject));
        return gameObject;
    }

    void DoodleScene::InitializeFromData(const DoodleSceneData& sceneData)
    {
        // 与 CreateGameObject 同一个前置条件：进入销毁流程后建不出对象。
        // 提前拦住，也避免白解析一遍资产
        if (!IsAlive())
        {
            throw std::logic_error("DoodleScene: cannot initialize from data after Destroy");
        }

        // 阶段一：把全部网格解析完再动场景。
        //
        // 网格名写错是最常见的内容错误；如果在循环里边查边建，前面几个对象
        // 已经进了场景，错误却抛出在第三个。场景创建后没有「撤销」的公开
        // 入口，所以校验必须排在第一次 CreateGameObject 之前。
        //
        // 只挂相机的对象没有网格，槽位留空；下标始终与 gameObjects 对齐，
        // 阶段二按同一个下标取用
        std::vector<const DoodleMesh*> meshes(sceneData.gameObjects.size(), nullptr);
        DoodleAssetManager& assetManager = DoodleAssetManager::GetInstance();

        for (std::size_t i = 0; i < sceneData.gameObjects.size(); ++i)
        {
            const DoodleSceneObjectData& objectData = sceneData.gameObjects[i];
            if (objectData.meshKey.empty())
            {
                continue;
            }

            const DoodleMesh* pMesh = assetManager.Load<DoodleMesh>(objectData.meshKey);
            if (pMesh == nullptr)
            {
                // Loader 的契约是不返回空指针；这里只是防御，报错信息带上对象名
                throw std::runtime_error("DoodleScene: mesh '" + objectData.meshKey +
                                         "' for GameObject '" + objectData.name + "' is missing");
            }

            meshes[i] = pMesh;
        }

        // 阶段二：数据校验通过，开始建对象
        for (std::size_t i = 0; i < sceneData.gameObjects.size(); ++i)
        {
            const DoodleSceneObjectData& objectData = sceneData.gameObjects[i];

            DoodleGameObject& gameObject = CreateGameObject(objectData.name);

            DoodleTransform& transform = gameObject.GetTransform();
            transform.position = objectData.position;
            transform.rotation = EulerDegreesToQuaternion(objectData.rotationDegrees);
            transform.scale = objectData.scale;

            if (meshes[i] != nullptr)
            {
                gameObject.AddComponent<DoodleMeshRenderer>().SetMesh(meshes[i]);
            }

            // 相机是普通组件：它的位姿就是上面写进 Transform 的那一份，
            // 组件本身不存位置 / 朝向。投影方式来自场景数据
            if (objectData.hasCamera)
            {
                gameObject.AddComponent<DoodleCamera>().SetProjectionType(objectData.cameraProjection);
            }
        }
    }

    void DoodleScene::DestroyGameObject(DoodleGameObject& gameObject)
    {
        // 只接受本场景的对象。对象自己调用 Destroy() 时不需要这条校验；
        // 这里挡的是拿着别的场景对象误调的入口。
        if (&gameObject.GetScene() == this)
        {
            gameObject.Destroy();
        }
    }

    DoodleGameObject* DoodleScene::FindGameObject(const std::string_view name) const
    {
        for (auto& pGameObject : m_gameObjects)
        {
            if (!pGameObject->IsAlive())
            {
                continue;
            }

            if (pGameObject->GetName() == name)
            {
                return pGameObject.get();
            }
        }

        return nullptr;
    }

    std::vector<DoodleGameObject*> DoodleScene::GetGameObjects() const
    {
        std::vector<DoodleGameObject*> gameObjects;
        GetGameObjects(gameObjects);
        return gameObjects;
    }

    void DoodleScene::GetGameObjects(std::vector<DoodleGameObject*>& gameObjects) const
    {
        // 先清空：调用方复用的是同一块缓冲，结果应是本帧的完整快照，
        // 而不是和上一帧叠在一起
        gameObjects.clear();

        for (const auto& pGameObject : m_gameObjects)
        {
            if (pGameObject->IsAlive())
            {
                gameObjects.push_back(pGameObject.get());
            }
        }
    }

    void DoodleScene::DestroyReally()
    {
        if (m_lifecycleState == ELifecycleState::Destroying ||
            m_lifecycleState == ELifecycleState::Destroyed)
        {
            return;
        }
        m_lifecycleState = ELifecycleState::Destroying;

        // 阶段一：先把本层所有对象推进到 PendingDestroy，一个销毁回调都不跑。
        //
        // 为什么不和阶段二合成一个循环：阶段二会跑组件的 OnDisable / OnDestroy，
        // 回调里可能通过 FindGameObject 或缓存的对象指针查别的对象。先全部
        // 标记，可以保证「场景一旦开始销毁，整层对象在查找里都不可见」——
        // 查找结果与对象在容器里的位置无关，也不会出现「前面的已经销毁、后面的
        // 还告诉调用方自己 Alive」这种半销毁视图。
        //
        // 这也覆盖没有先走 Destroy() 的入口：析构兜底、拥有者直接调用
        // DestroyReally 时，对象可能还都是 Alive，这里补上整层的标记。
        //
        // 只改状态、不碰容器。所以即使场景是在 Update 的遍历途中被销毁的，
        // 这里也不会让任何迭代器失效。
        for (auto& pGameObject : m_gameObjects)
        {
            pGameObject->Destroy();
        }

        // 阶段二：统一跑销毁回调。
        //
        // 此刻所有对象都已经是 PendingDestroy，但内存和组件都还在。
        // DestroyReally 内部会把该对象自己的组件逐个推进到 Destroying /
        // Destroyed；因此单个对象回调期间：
        // - 尚未轮到的对象仍然完整（组件还在，也没跑过回调），可以安全访问；
        // - 已经轮过的对象已经是 Destroyed，它的组件容器已清空，不能再通过
        //   缓存指针访问它的组件。
        // 这个先后顺序是有意的：回调里访问兄弟对象时，必须知道它有没有被处理过。
        //
        // 容器在整个阶段保持不动，erase 放到所有回调之后，避免回调里拿到
        // 失效的迭代器或引用。
        for (auto& pGameObject : m_gameObjects)
        {
            pGameObject->DestroyReally();
        }

        // 全部回调结束之后才真正释放对象内存，并把场景置为 Destroyed；
        // 之后场景只剩空壳，等拥有者 erase。
        m_gameObjects.clear();
        m_lifecycleState = ELifecycleState::Destroyed;
    }

    void DoodleScene::SweepPendingGameObjects()
    {
        bool anyPending = false;

        // 阶段一：容器完全不动，所有对象（包括其它待销毁的）都还在。
        // 这样 OnDestroy 里访问尚未轮到的兄弟对象不会悬空。
        //
        // 用数量快照而不是范围 for：sweep 期间场景还是 Alive，组件在
        // OnDestroy 里调用 CreateGameObject 不会被 IsAlive 拦住；一旦扩容，
        // 范围 for 的迭代器就会失效。快照之外新建的对象留到下一帧。
        const auto count = m_gameObjects.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            auto* pGameObject = m_gameObjects[i].get();
            if (pGameObject->IsPendingDestroy())
            {
                anyPending = true;
                pGameObject->DestroyReally();
            }
        }

        if (anyPending)
        {
            // 阶段二：一次压缩，保留存活对象的原有顺序。
            // 只擦已经跑完回调（Destroyed）的对象：如果某个回调把扫描位置
            // 之前的对象标成了 PendingDestroy，它会留到下一轮 sweep，而不是
            // 在 erase 时走 GameObject 析构兜底、把回调插到所有回调之后。
            std::erase_if(m_gameObjects, [](const auto& pGameObject)
            {
                return pGameObject->IsDestroyed();
            });
        }
    }
}
