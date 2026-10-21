//
//  DoodleGameObject.h
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#pragma once

#include <memory>
#include <type_traits>
#include <vector>

#include "DoodleComponent.h"
#include "DoodleTransform.h"
#include "DoodleUpdateContext.h"

namespace Doodle
{
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
     * 一个 GameObject 目前不携带层级、激活状态和序列化；这些等真正需要时再加。
     */
    class DoodleGameObject
    {
    public:
        DoodleGameObject() = default;

        /**
         * 销毁所有组件：先 OnDisable（仍启用的），再 OnDestroy，最后释放对象
         *
         * 走逆序（与添加顺序相反），和 C++ 成员析构的直觉一致。
         * 此刻 vector 尚未析构，所有组件都还活着，OnDestroy 里访问兄弟组件安全。
         * 已经 RemoveComponent 但还没 sweep 的组件也在这里补上回调。
         */
        ~DoodleGameObject();

        DoodleGameObject(const DoodleGameObject&) = delete;
        DoodleGameObject& operator=(const DoodleGameObject&) = delete;
        DoodleGameObject(DoodleGameObject&&) = delete;
        DoodleGameObject& operator=(DoodleGameObject&&) = delete;

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
         */
        template <typename T>
        T& AddComponent();

        /**
         * 请求删除组件；重复调用幂等
         *
         * 只设置标记，不立刻从容器里移除：
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
         * 按类型查找第一个未标记删除的组件，找不到返回 nullptr
         *
         * 返回的是非拥有视图：生命周期仍由 GameObject 掌握，调用方不能 delete，
         * 也不应把它保存到组件被删除之后。
         */
        template <typename T>
        [[nodiscard]] T* GetComponent();

        /**
         * 按类型查找第一个未标记删除的组件（const 版本）
         */
        template <typename T>
        [[nodiscard]] const T* GetComponent() const;

        /**
         * 更新所有组件
         *
         * 只处理本轮开始时已存在的组件，按添加顺序调用 OnUpdate；标记删除或
         * 停用的组件跳过。遍历用下标而不是迭代器，且每轮重新按索引取指针，
         * 所以 OnUpdate 里 AddComponent 引起的 vector 扩容是安全的。
         *
         * 遍历结束后统一处理本轮产生的删除请求。
         */
        void Update(const DoodleUpdateContext& context);

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
         * 统一销毁本轮被标记删除的组件
         *
         * 先对它们执行 OnDisable（仍启用的）与 OnDestroy，回调期间容器完全
         * 不动、其它组件都还活着；最后一次性 erase，保留存活组件的原有顺序。
         */
        void SweepPendingRemovals();

    private:
        /**
         * 位姿。排在组件容器之前，保证组件析构时宿主对象仍然完整
         */
        DoodleTransform m_transform;

        /**
         * 组件容器：由本对象独占，元素地址稳定
         */
        std::vector<std::unique_ptr<DoodleComponent>> m_components;
    };

#pragma region 模板函数定义
    template <typename T>
    T& DoodleGameObject::AddComponent()
    {
        static_assert(std::is_base_of_v<DoodleComponent, T>, "AddComponent<T> 的 T 必须派生自 DoodleComponent");
        static_assert(std::is_default_constructible_v<T>, "组件必须提供可访问的无参构造函数");

        auto pComponent = std::make_unique<T>();

        // 先取引用再入容器：push_back 可能让 vector 扩容，但组件对象在堆上，
        // 引用和地址都不受影响。接下来 OnAwake 里即使再 AddComponent 也安全
        T& component = *pComponent;
        m_components.push_back(std::move(pComponent));

        // 调用OnAwake和OnEnable
        component.m_pOwner = this;
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
            if (pComponent->m_pendingRemove)
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
            if (pComponent->m_pendingRemove)
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
