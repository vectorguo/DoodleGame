//
//  DoodleScene.h
//  Doodle
//
//  Created by 郭智 on 2026/10/22.
//

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/DoodleLifecycleState.h"
#include "Core/DoodleUpdateContext.h"

namespace Doodle
{
    //只以引用 / 指针与 unique_ptr 成员出现，析构在源文件定义，这里不需要完整类型
    class DoodleGameObject;

    /**
     * 场景：GameObject 的独占容器与生命周期驱动
     *
     * 只负责 CPU 侧的对象所有权、active、Update 驱动和延迟销毁；相机、
     * MeshRenderer、渲染收集都不在这里。
     *
     * GameObject 用 unique_ptr 独占持有，并通过 GetScene() 反指本场景。
     * 生命周期是 Alive → PendingDestroy → Destroying → Destroyed：
     * - Destroy() 只推进到 PendingDestroy，直属 GameObject 也全部先标记；
     * - DestroyReally() 才跑回调并清容器，结束后置 Destroyed，由拥有者
     *   （DoodleSceneManager）在 sweep 里统一调用；析构只在还没开始销毁时兜底。
     *
     * 场景由 DoodleSceneManager 独占拥有，自己不再持有子场景，也不保存
     * 管理器的反指：运行时只有一个管理器，归属由管理器的 unique_ptr 容器表达。
     * 需要「一组场景一起激活 / 卸载 / 加载」时，再把子场景容器加进本类做成
     * 递归，而不是引入 MainScene / SubScene 两个派生类型。
     */
    class DoodleScene final
    {
        friend class DoodleSceneManager;

    private:
        /**
         * 只能由场景管理器创建
         *
         * 没有默认构造，场景从诞生起就由管理器的 unique_ptr 容器持有；
         * 场景本身不保存管理器指针。
         */
        explicit DoodleScene(std::string name = "NewScene");

    public:
        /**
         * 析构兜底：若生命周期还停在 Alive / PendingDestroy，在这里补上
         *
         * 正常路径是管理器的 DestroyReally() + 释放；只有直接 delete、
         * 异常路径等没有经过回收流程的情况才会走到这里。
         */
        ~DoodleScene();

        DoodleScene(const DoodleScene&) = delete;
        DoodleScene& operator=(const DoodleScene&) = delete;
        DoodleScene(DoodleScene&&) = delete;
        DoodleScene& operator=(DoodleScene&&) = delete;

        /**
         * 驱动本场景的 GameObject 更新，并回收本轮被标记销毁的对象
         *
         * inactive 时不更新对象，但 sweep 仍然执行；生命周期不是 Alive 的
         * 场景直接返回，剩下的回调交给 DestroyReally 统一处理。
         */
        void Update(const DoodleUpdateContext& context);

        /**
         * 请求销毁：只推进到 PendingDestroy，不跑组件回调
         *
         * 置位后本场景不再收 Update、也不能再创建对象；直属 GameObject 全部
         * 先标记 PendingDestroy。拥有者在安全点调用 DestroyReally 统一回收。
         * 重复调用幂等；已经进入 Destroying / Destroyed 后不再接受。
         */
        void Destroy();

        /**
         * 创建对象并返回引用，引用在对象被销毁前一直有效
         *
         * 名字允许重复；不传时用 "NewGameObject"。GameObject 的构造函数是
         * private，只有场景能创建它，所以对象从诞生起就有合法的场景归属。
         *
         * 在 GameObject 的 OnUpdate 里也可以调用：对象立即创建、立即可查找，
         * 但本帧的更新遍历使用快照数量，新对象从下一帧开始收 Update。
         *
         * 场景已请求销毁后调用属于错误用法，抛 std::logic_error：此时创建
         * 出来的对象没有下一次 sweep，只会拖到容器析构时才跑回调。
         */
        DoodleGameObject& CreateGameObject(std::string name = "NewGameObject");

        /**
         * 请求销毁对象；只推进到 PendingDestroy，sweep 时统一执行
         *
         * 不属于本场景的对象静默忽略；重复调用幂等。
         * 对象在 sweep 之前仍然存在，但不再收 Update，FindGameObject 也跳过它。
         */
        void DestroyGameObject(DoodleGameObject& gameObject);

        /**
         * 按名字查找第一个 Alive 的对象，找不到返回 nullptr
         *
         * 名字不唯一，因此同名对象存在时返回创建顺序里的第一个；这不是
         * 对象身份，跨场景或需要稳定身份时等 GUID。
         * 返回的是非拥有视图，对象被销毁后失效。
         */
        [[nodiscard]] DoodleGameObject* FindGameObject(const std::string_view name) const;

        /**
         * 取本场景存活的 GameObject，按创建顺序
         *
         * 只返回 Alive 的对象：已请求销毁、正在等 sweep 回收的不在其中 ——
         * 这与 Update 的跳过规则一致，避免「逻辑上已经没了、画面上还在」。
         * 返回的是调用那一刻的快照，之后新建 / 销毁对象不会改变已返回的向量。
         *
         * 指针非拥有，寿命由本场景保证；对象被销毁后指针失效。
         * 每帧调用的路径请用接收向量的重载，避免反复分配
         */
        [[nodiscard]] std::vector<DoodleGameObject*> GetGameObjects() const;

        /**
         * 取本场景存活的 GameObject，写进调用方提供的向量
         *
         * 先 clear 再写入，传进来的是跨帧复用的缓冲时，结果恰好是本帧的
         * 存活对象；其余语义与返回值重载相同
         */
        void GetGameObjects(std::vector<DoodleGameObject*>& gameObjects) const;

        /**
         * 场景名
         *
         * 只用于查找与日志；允许重复，不是身份，也不是跨场景的键。
         */
        [[nodiscard]] const std::string& GetName() const
        {
            return m_name;
        }

        /**
         * 是否参与对象更新
         *
         * 只反映 active 开关，不代表生命周期：已经进入 PendingDestroy 的场景
         * 也可能仍返回 true。判断场景是否还可用请用 IsAlive()。
         * 改这个标志不影响组件的 enabled，也不触发 OnEnable / OnDisable
         * （见 SetActive）。
         */
        [[nodiscard]] bool IsActive() const
        {
            return m_active;
        }

        /**
         * 只改 active 标志，不触碰组件的 enabled，也不触发 OnEnable / OnDisable
         */
        void SetActive(const bool active)
        {
            m_active = active;
        }

        /**
         * 生命周期是否仍是 Alive
         */
        [[nodiscard]] bool IsAlive() const
        {
            return m_lifecycleState == ELifecycleState::Alive;
        }

        /**
         * 是否已请求销毁，等待拥有者执行 DestroyReally
         *
         * 只表示「在等回收」；已经进入 Destroying / Destroyed 时返回 false。
         * 判断场景是否还可用请用 IsAlive()。
         */
        [[nodiscard]] bool IsPendingDestroy() const
        {
            return m_lifecycleState == ELifecycleState::PendingDestroy;
        }

        /**
         * 销毁回调是否已经跑完、直属对象已清空；场景内存还在，等拥有者释放
         */
        [[nodiscard]] bool IsDestroyed() const
        {
            return m_lifecycleState == ELifecycleState::Destroyed;
        }

    private:
        /**
         * 真正执行销毁：Alive / PendingDestroy → Destroying → 清空 → Destroyed
         *
         * 只由拥有者在 sweep 中调用；析构函数在还没开始销毁时兜底。
         * 这是普通函数而不是析构器，回调执行时 Scene 和所有对象都完整。
         *
         * 分两阶段执行：先把整层 GameObject 全部标记为 PendingDestroy（让
         * 回调里的 FindGameObject 看到一致的「全都不可用」视图），再逐个跑
         * 销毁回调；容器在所有回调结束后才清空。
         *
         * 回调里禁止创建 / 销毁 / 移动对象：sweep 不会级联重扫这些变化。
         */
        void DestroyReally();

        /**
         * 统一销毁本轮被标记销毁的对象
         *
         * 先对它们执行 OnDisable（仍启用的）与 OnDestroy，回调期间容器完全
         * 不动、其它对象都还活着；最后一次性 erase，保留存活对象的原有顺序。
         *
         * 只处理扫描开始时已存在的对象：回调里新建的对象从下一轮开始算。
         * 也只擦除已经跑完回调（Destroyed）的对象，没来得及处理的 pending
         * 会留到下一轮，而不是在 erase 时走析构兜底。
         */
        void SweepPendingGameObjects();

    private:
        /**
         * 场景名，只用于查找与日志；允许重复，不是身份
         */
        std::string m_name;

        /**
         * 生命周期状态：Alive → PendingDestroy → Destroying → Destroyed
         *
         * Destroy 只推进到 PendingDestroy；DestroyReally 在回调前后推进到
         * Destroying / Destroyed。析构兜底只在还没开始销毁时发生。
         */
        ELifecycleState m_lifecycleState = ELifecycleState::Alive;

        /**
         * 是否参与对象更新；不影响 sweep，也不改变生命周期状态的推进
         */
        bool m_active = true;

        /**
         * 对象容器：由本场景独占
         *
         * 排在最后：正常析构路径没有先清空时，成员逆序析构会让它先于其它
         * 场景成员被释放，对象的销毁回调仍能读到场景的名字和状态。
         */
        std::vector<std::unique_ptr<DoodleGameObject>> m_gameObjects;
    };
}
