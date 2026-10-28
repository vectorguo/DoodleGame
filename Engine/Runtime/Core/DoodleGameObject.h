//
//  DoodleGameObject.h
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "DoodleComponent.h"
#include "DoodleLifecycleState.h"
#include "DoodleTransform.h"
#include "DoodleUpdateContext.h"

namespace Doodle
{
    class DoodleScene;

    /**
     * 对象：一个 Transform + 一组组件
     *
     * GameObject 是对象身份与容器。它不知道「相机」是什么，只知道自己有一个
     * Transform 和若干组件；任何想让这个对象移动 / 转向的代码都改 Transform，
     * 组件通过 owner 读同一个 Transform。这样「控制对象就能操作相机」是结构
     * 上的事实，而不是约定。
     *
     * 组件用 unique_ptr 独占持有：
     * - 容器要装任意派生类型，只能存基类指针，按值存会被切片；
     * - 组件地址必须稳定。GetComponent 返回引用、应用层会缓存组件指针，
     *   vector 扩容若搬动对象，这些指针全部悬空；unique_ptr 只搬指针本身；
     * - 生命周期与宿主绑定，没有共享所有权，unique_ptr 正是这个语义。
     *
     * 不可拷贝、不可移动：组件持有指回本对象的 owner 指针，拷贝会得到两个
     * 共享同一批组件的宿主，移动会让 owner 指向旧地址。两者都是静默的错误，
     * 所以在类型层面直接禁止。
     *
     * GameObject 由 DoodleScene 独占拥有，只能通过 Scene 的创建接口构造：
     * 构造函数私有，没有默认构造，对象从诞生起就属于一个场景。名字在构造时
     * 一次绑定；场景归属初始绑定，之后允许由所属场景在转移所有权时更换，
     * 但永远非空。名字只是查找与日志标识，允许重复；对象暂时没有 GUID。
     *
     * 销毁分两步：Destroy() 只推进到 PendingDestroy，本轮不再更新；所属场景
     * 在 sweep 里调用 DestroyReally() 跑组件回调并清空组件，随后 erase。生命周期
     * 状态是 Alive → PendingDestroy → Destroying → Destroyed；析构函数只在还没
     * 开始销毁（Alive / PendingDestroy）时兜底，正常路径不在析构器里跑回调。
     *
     * 目前不携带层级、激活状态和序列化；这些等真正需要时再加。
     */
    class DoodleGameObject final
    {
        friend class DoodleScene;

    public:
        /**
         * 析构兜底：若生命周期还停在 Alive / PendingDestroy，在这里补上
         *
         * 正常销毁路径是 Scene sweep 里的 DestroyReally() + erase。只有直接
         * delete、异常路径等没有经过场景回收的情况才会走到这里。
         */
        ~DoodleGameObject();

        DoodleGameObject(const DoodleGameObject&) = delete;
        DoodleGameObject& operator=(const DoodleGameObject&) = delete;
        DoodleGameObject(DoodleGameObject&&) = delete;
        DoodleGameObject& operator=(DoodleGameObject&&) = delete;

        /**
         * 更新所有组件
         *
         * 只处理本轮开始时已存在的组件，按添加顺序调用 OnUpdate；不在 Alive
         * 或已停用的组件跳过。遍历用下标而不是迭代器，且每轮重新按索引取指针，
         * 所以 OnUpdate 里 AddComponent 引起的 vector 扩容是安全的。
         *
         * 若某个 OnUpdate 把宿主对象推进到 PendingDestroy，本轮剩余组件不再
         * 更新；组件删除的 sweep 也跳过，交给场景 sweep 的 DestroyReally 统一
         * 处理（它会覆盖所有剩余组件）。
         */
        void Update(const DoodleUpdateContext& context);

        /**
         * 请求销毁：只推进到 PendingDestroy，不跑组件回调
         *
         * 本轮剩余更新跳过本对象；所属场景在 sweep 里统一执行 DestroyReally
         * 并 erase。从调用到 sweep 之间对象仍然可用（组件还在），但不要再依赖
         * 它下一帧存在。重复调用幂等；已经进入 Destroying / Destroyed 后不再接受。
         *
         * Scene::DestroyGameObject 是带归属校验的入口；本方法用于对象销毁自己，
         * 不需要反向调用 Scene，Core 也不因此依赖 Scene 的头文件。
         */
        void Destroy()
        {
            if (m_lifecycleState != ELifecycleState::Alive)
            {
                return;
            }
            m_lifecycleState = ELifecycleState::PendingDestroy;
        }

        /**
         * 添加组件并返回它的引用，引用在组件被销毁前一直有效
         *
         * 流程：默认构造 → 设置 owner → 入容器 → OnAwake → OnEnable（若启用）。
         * 组件必须有无参构造函数：配置参数走成员默认值、setter 或 OnAwake，
         * 不从构造函数传。
         *
         * 在 OnUpdate 里也可以调用：组件立即构造、立即能找到，但本轮遍历
         * 使用的是快照计数，新组件从下一帧开始收 OnUpdate。
         *
         * 同一个 GameObject 上允许挂多个同类型组件，GetComponent 取第一个。
         * 宿主生命周期不是 Alive 时调用属于错误用法，抛 std::logic_error。
         */
        template <typename T>
        T& AddComponent();

        /**
         * 添加组件，并在 OnAwake 之前用场景配置反序列化
         *
         * 内容装配用（当前是场景加载）：构造 T → 设置 owner →
         * component.Deserialize(config) → 入容器 → OnAwake / OnEnable。
         * 所以 OnAwake 看到的是已经反序列化好的字段。
         *
         * 配置非法时 Deserialize 抛异常，组件还没进容器，直接析构，
         * 不会留下半初始化状态。
         *
         * 场景里用什么类型创建组件由注册表决定；组件实例不保存类型信息。
         */
        template <typename T>
        T& AddComponentFromConfig(const DoodleComponentConfig& config);

        /**
         * 请求删除组件；重复调用幂等
         *
         * 只把组件推进到 PendingDestroy，不立刻从容器里移除：
         * - GetComponent 立即查不到它；
         * - 本轮后续的 OnUpdate 不再调用它；
         * - 但对象活到本轮更新遍历结束，届时统一 OnDisable / OnDestroy / erase。
         *
         * 这样在 OnUpdate 里删自己或删别的组件都不会让遍历失效，也不会让
         * 其它组件缓存的指针在本轮中途悬空。
         *
         * pComponent 必须属于本对象；不满足时静默忽略。
         */
        void RemoveComponent(DoodleComponent* pComponent);

        /**
         * 按类型查找第一个 Alive 的组件，找不到返回 nullptr
         *
         * 返回的是非拥有视图：生命周期仍由 GameObject 掌握，调用方不能 delete，
         * 也不应把它保存到组件被销毁之后。
         */
        template <typename T>
        [[nodiscard]] T* GetComponent();

        /**
         * 按类型查找第一个 Alive 的组件（const 版本）
         */
        template <typename T>
        [[nodiscard]] const T* GetComponent() const;

        /**
         * 对象的显示 / 查找名称
         *
         * 不唯一：同一个场景里允许存在多个同名对象，Scene 按名字查找时返回
         * 第一个 Alive 的。它只是标识，不是身份，也不是跨场景的键。
         */
        [[nodiscard]] const std::string& GetName() const
        {
            return m_name;
        }

        /**
         * 生命周期是否仍是 Alive
         */
        [[nodiscard]] bool IsAlive() const
        {
            return m_lifecycleState == ELifecycleState::Alive;
        }

        /**
         * 是否已请求销毁，等待所属场景在 sweep 里执行 DestroyReally
         *
         * 只表示「在等回收」；已经进入 Destroying / Destroyed 时返回 false。
         * 判断对象是否还可用请用 IsAlive()。
         */
        [[nodiscard]] bool IsPendingDestroy() const
        {
            return m_lifecycleState == ELifecycleState::PendingDestroy;
        }

        /**
         * 销毁回调是否已经跑完；对象内存还在，等所属场景 erase
         */
        [[nodiscard]] bool IsDestroyed() const
        {
            return m_lifecycleState == ELifecycleState::Destroyed;
        }

        /**
         * 所属场景，始终非空
         *
         * 对象可以在场景之间转移，转移由场景的移动接口完成；这里返回引用是因为
         * 任何存活对象都恰好属于一个场景，不存在「暂时不属于任何场景」的状态。
         */
        [[nodiscard]] DoodleScene& GetScene() const
        {
            return *m_pScene;
        }

        /**
         * 对象的位姿，唯一的位姿来源
         */
        [[nodiscard]] DoodleTransform& GetTransform()
        {
            return m_transform;
        }

        /**
         * 对象的位姿，唯一的位姿来源（const 版本）
         */
        [[nodiscard]] const DoodleTransform& GetTransform() const
        {
            return m_transform;
        }

    private:
        /**
         * 只能由所属场景创建
         *
         * 没有默认构造，对象从诞生起就属于一个场景；名字在构造时一次绑定，
         * 场景归属之后由所属场景在转移所有权时更换。SetScene 只对 DoodleScene
         * 开放，外部没有 SetName，GetScene() 因此可以返回引用而不是可能为空的指针。
         *
         * name 按值传递：场景拥有这个名字，构造里直接 move 进成员。调用方给
         * 左值时是一次拷贝，给右值/临时对象时是一次移动；换成 const string&
         * 会强制右值也走拷贝。
         */
        explicit DoodleGameObject(DoodleScene& scene, std::string name = "NewGameObject");

        /**
         * 真正执行销毁：推进到 Destroying → 跑组件回调 → 清空组件 → Destroyed
         *
         * 只由所属场景在 sweep 中调用；析构函数在还没开始销毁时兜底。
         * 这是普通函数而不是析构器，回调执行时 GameObject 和 Scene 都完整；
         * 容器在回调期间保持不动，回调结束后才 clear。
         *
         * 组件在轮到自己时从 Alive 推进到 Destroying，回调跑完置 Destroyed；
         * 还没轮到的组件仍然是 Alive，所以兄弟组件之间在回调里可以互相查找。
         *
         * 回调里禁止增删组件，也禁止通过场景创建 / 销毁 / 移动对象：
         * 场景 sweep 不会在本轮级联重扫这些变化。
         */
        void DestroyReally();

        /**
         * 由所属场景在转移所有权时调用
         *
         * 场景必须同时把本对象的 unique_ptr 从旧容器搬进新容器；只改这里不搬
         * 所有权，会让旧场景继续持有并负责销毁一个已经声称属于新场景的对象。
         */
        void SetScene(DoodleScene& scene)
        {
            m_pScene = &scene;
        }

        /**
         * 统一销毁本轮被标记销毁的组件
         *
         * 先对它们执行 OnDisable（仍启用的）与 OnDestroy，回调期间容器完全
         * 不动、其它组件都还活着；最后一次性 erase，保留存活组件的原有顺序。
         */
        void SweepPendingComponents();

    private:
        /**
         * 显示 / 查找名称；允许重复，不是对象身份
         */
        std::string m_name;

        /**
         * 所属场景，构造时设置；仅由所属场景在转移所有权时更换
         */
        DoodleScene* m_pScene = nullptr;

        /**
         * 位姿。排在组件容器之前，保证组件析构时宿主对象仍然完整
         */
        DoodleTransform m_transform;

        /**
         * 生命周期状态：Alive → PendingDestroy → Destroying → Destroyed
         *
         * 前三个状态由 Destroy / DestroyReally 推进，Destroyed 表示回调已经
         * 跑完、组件容器已清空，只等所属场景 erase。析构兜底只在这之外发生。
         */
        ELifecycleState m_lifecycleState = ELifecycleState::Alive;

        /**
         * 组件容器：由本对象独占，元素地址稳定
         *
         * 必须排在其它成员之后：它是最后析构的，组件析构期间宿主对象仍然完整
         */
        std::vector<std::unique_ptr<DoodleComponent>> m_components;
    };

#pragma region 模板函数定义
    template <typename T>
    T& DoodleGameObject::AddComponent()
    {
        static_assert(std::is_base_of_v<DoodleComponent, T>, "AddComponent<T> 的 T 必须派生自 DoodleComponent");
        static_assert(std::is_default_constructible_v<T>, "组件必须提供可访问的无参构造函数");

        // 进入销毁流程后新增的组件没有下一次 sweep，只会在容器清空时被丢掉。
        if (!IsAlive())
        {
            throw std::logic_error("DoodleGameObject: cannot add component after Destroy");
        }

        auto pComponent = std::make_unique<T>();

        T& component = *pComponent;
        component.m_pGameObject = this;

        m_components.push_back(std::move(pComponent));

        // 调用OnAwake和OnEnable
        component.OnAwake();
        if (component.m_enabled)
        {
            component.OnEnable();
        }
        return component;
    }

    template <typename T>
    T& DoodleGameObject::AddComponentFromConfig(const DoodleComponentConfig& config)
    {
        static_assert(std::is_base_of_v<DoodleComponent, T>, "AddComponent<T> 的 T 必须派生自 DoodleComponent");
        static_assert(std::is_default_constructible_v<T>, "组件必须提供可访问的无参构造函数");

        // 进入销毁流程后新增的组件没有下一次 sweep，只会在容器清空时被丢掉。
        if (!IsAlive())
        {
            throw std::logic_error("DoodleGameObject: cannot add component after Destroy");
        }

        auto pComponent = std::make_unique<T>();

        T& component = *pComponent;
        component.m_pGameObject = this;
        component.Deserialize(config);

        m_components.push_back(std::move(pComponent));

        // 调用OnAwake和OnEnable
        component.OnAwake();
        if (component.m_enabled)
        {
            component.OnEnable();
        }
        return component;
    }

    template <typename T>
    T* DoodleGameObject::GetComponent()
    {
        static_assert(std::is_base_of_v<DoodleComponent, T>, "GetComponent<T> 的 T 必须派生自 DoodleComponent");

        for (auto& pComponent : m_components)
        {
            if (!pComponent->IsAlive())
            {
                continue;
            }

            if (auto* pTyped = dynamic_cast<T*>(pComponent.get()))
            {
                return pTyped;
            }
        }

        return nullptr;
    }

    template <typename T>
    const T* DoodleGameObject::GetComponent() const
    {
        static_assert(std::is_base_of_v<DoodleComponent, T>, "GetComponent<T> 的 T 必须派生自 DoodleComponent");

        for (const auto& pComponent : m_components)
        {
            if (!pComponent->IsAlive())
            {
                continue;
            }

            if (auto* pTyped = dynamic_cast<const T*>(pComponent.get()))
            {
                return pTyped;
            }
        }

        return nullptr;
    }
#pragma endregion
}
