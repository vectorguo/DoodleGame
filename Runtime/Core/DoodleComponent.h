//
//  DoodleComponent.h
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#pragma once

#include "DoodleUpdateContext.h"

namespace Doodle
{
    class DoodleGameObject;

    /**
     * 组件基类：挂在 GameObject 上、随宿主生命周期走的最小单元
     *
     * 生命周期回调（全部是虚函数、默认空实现）：
     *   OnAwake   只调用一次，在组件进入容器、owner 已设置之后
     *   OnEnable  在 OnAwake 之后调用（若组件处于启用状态）；之后每次 SetEnabled(true) 再触发
     *   OnUpdate  每帧调用，仅当组件启用且没有被标记删除
     *   OnDisable 每次 SetEnabled(false) 触发；销毁前若仍处于启用状态也补一次
     *   OnDestroy 销毁前调用一次
     *
     * 回调的顺序与时机由 DoodleGameObject 保证，派生类只覆盖关心的那几个。
     * 不写成纯虚函数：大多数组件只实现 OnUpdate，纯虚只会制造一堆空壳。
     *
     * 组件不能脱离宿主使用。只能通过 GameObject::AddComponent<T>() 创建：
     * 那里会设置 owner，并在入容器之后调用 OnAwake。构造函数保持 public 以
     * 配合 make_unique，但直接构造出来的组件没有 owner，不能拿来算 view、
     * 改位姿或依赖任何宿主状态。
     *
     * OnUpdate 里可以自由调用 AddComponent / RemoveComponent：前者立即生效，
     * 后者只做标记，真正的销毁推迟到本轮更新遍历结束之后。
     *
     * 其余回调的约束：
     * - OnDisable / OnDestroy 不要增删组件。它们是在 sweep 阶段被调用的，
     *   此时容器正在被扫描；要支持级联删除需要先把 sweep 改成循环处理。
     * - 所有回调都不要抛异常。销毁路径上的异常会直接 std::terminate。
     */
    class DoodleComponent
    {
    public:
        virtual ~DoodleComponent() = default;

        DoodleComponent(const DoodleComponent&) = delete;
        DoodleComponent& operator=(const DoodleComponent&) = delete;
        DoodleComponent(DoodleComponent&&) = delete;
        DoodleComponent& operator=(DoodleComponent&&) = delete;

        virtual void OnAwake() {}
        virtual void OnEnable() {}
        virtual void OnUpdate(const DoodleUpdateContext& context) {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}

        /**
         * 获取宿主，AddComponent 保证非空
         */
        [[nodiscard]] DoodleGameObject& GetGameObject() const
        {
            return *m_pGameObject;
        }

        /**
         * 当前是否处于启用状态且未被标记删除
         */
        [[nodiscard]] bool IsEnabled() const
        {
            return m_enabled && !m_pendingRemove;
        }

        /**
         * 启用 / 停用组件
         *
         * 状态没有变化时什么都不做；已标记删除的组件不再响应。
         * OnEnable / OnDisable 在这里同步触发。
         */
        void SetEnabled(const bool enabled)
        {
            if (m_pendingRemove || m_enabled == enabled)
            {
                return;
            }

            m_enabled = enabled;
            if (m_enabled)
            {
                OnEnable();
            }
            else
            {
                OnDisable();
            }
        }

    protected:
        /**
         * 只能由派生类构造；基类本身没有实例化的意义
         */
        DoodleComponent() = default;

    private:
        friend class DoodleGameObject;

        /**
         * 宿主，由 AddComponent 在入容器之前设置
         */
        DoodleGameObject* m_pGameObject = nullptr;

        /**
         * 启用状态，默认启用
         */
        bool m_enabled = true;

        /**
         * 已请求删除，等待 Update 末尾统一销毁
         *
         * 从置位到真正 erase 之间：不再收 OnUpdate，GetComponent 也查不到，
         * 但对象仍然存活 —— 其它组件缓存的指针在这段时间内仍然有效。
         */
        bool m_pendingRemove = false;
    };
}
