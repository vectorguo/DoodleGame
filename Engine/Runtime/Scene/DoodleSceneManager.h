//
//  DoodleSceneManager.h
//  Doodle
//
//  Created by 郭智 on 2026/10/23.
//

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/DoodleLifecycleState.h"

namespace Doodle
{
    class DoodleApplication;

    //只以引用 / 指针与 unique_ptr 成员出现，析构在源文件定义，这里不需要完整类型
    class DoodleScene;

    /**
     * 场景管理器：场景的独占容器与生命周期驱动
     *
     * 归属链的根：SceneManager → Scene → GameObject → Component。
     * 由 DoodleApplication 持有并逐帧驱动，自己不知道窗口、Vulkan 或输入。
     * 运行时只有一份，游戏层通过 GetInstance() 取用；所有权和生命周期仍在
     * 应用手里，本类不自己创建实例。
     *
     * 可以同时持有多个场景，各自有 active；管理器按创建顺序驱动所有 Alive
     * 的场景。场景之间不嵌套：需要「一组场景一起激活 / 卸载 / 加载」时，
     * 再把子场景容器加进 DoodleScene 做成递归，而不是加分组类型。
     *
     * 销毁与下层同构：生命周期是 Alive → PendingDestroy → Destroying →
     * Destroyed。Destroy() 只推进到 PendingDestroy，DestroyReally() 才跑回调
     * 并清容器，析构只兜底。DestroyReally 只对拥有者 DoodleApplication 开放。
     */
    class DoodleSceneManager
    {
        friend class DoodleApplication;

    private:
        /**
         * 只能由 DoodleApplication 创建：管理器在运行时只有一份，实例由应用
         * 独占持有。构造即登记为当前实例，析构时清空，和 DoodleApplication
         * 的登记方式一致。
         */
        DoodleSceneManager();

    public:
        ~DoodleSceneManager();

        DoodleSceneManager(const DoodleSceneManager&) = delete;
        DoodleSceneManager& operator=(const DoodleSceneManager&) = delete;
        DoodleSceneManager(DoodleSceneManager&&) = delete;
        DoodleSceneManager& operator=(DoodleSceneManager&&) = delete;

        /**
         * 当前场景管理器实例
         *
         * 存在的理由：组件的回调里想创建或查找场景时，手边没有应用对象。
         * 管理器本来就是进程里唯一的那份，与其把引用一层层往下传，不如从
         * 这里取。
         *
         * 有效期就是管理器对象的存活期：构造时登记、析构时清空，成对维护。
         * 管理器仍由 DoodleApplication 独占持有 —— 这里只是访问出口，不是
         * 「第一次调用时自己创建一个」的写法；那样生命周期就不再跟着应用与
         * Vulkan 的拆解顺序走了。
         *
         * 进程里没有管理器时（构造之前、析构之后）取用是未定义行为。
         * 返回引用而不是指针：管理器随应用对象一起存在，应用活着就意味着
         * 它已经登记，那个空状态没有调用者够得着。
         */
        [[nodiscard]] static DoodleSceneManager& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 驱动所有场景更新，并回收本轮被标记销毁的场景
         *
         * 管理器不在 Alive 时不更新场景，也不在本函数里回收；场景的回收
         * 不依赖它是否 active。
         */
        void Update();

        /**
         * 请求销毁所有场景：只推进到 PendingDestroy，不跑回调
         *
         * 应用退出时用；真正的回调由拥有者在 Vulkan 拆掉之前调用
         * DestroyReally 执行。重复调用幂等；进入 Destroying / Destroyed 后
         * 不再接受。
         */
        void Destroy();

        /**
         * 创建场景并返回引用，引用在场景被销毁前一直有效
         *
         * 名字允许重复；不传时用 "NewScene"。DoodleScene 构造函数私有，
         * 只有本管理器能创建，所以场景从诞生起就有合法的管理器归属。
         *
         * 管理器不在 Alive 时调用属于错误用法，抛 std::logic_error。
         */
        DoodleScene& CreateScene(std::string name = "NewScene");

        /**
         * 请求销毁场景；只推进到 PendingDestroy，本管理器 sweep 时统一执行
         *
         * 重复调用幂等。场景在 sweep 之前仍然存在，但不再收 Update，
         * FindScene 也跳过它。运行时只有一个管理器，不再做归属校验。
         */
        void DestroyScene(DoodleScene& scene);

        /**
         * 按名字查找第一个 Alive 的场景，找不到返回 nullptr
         *
         * 名字不唯一；返回的是非拥有视图，场景被销毁后失效。
         */
        [[nodiscard]] DoodleScene* FindScene(const std::string_view name) const;

        /**
         * 取管理器下的所有存活场景，按创建顺序
         *
         * 只返回 Alive 的场景；已请求销毁、等待 sweep 的不在其中。active
         * 不在过滤条件里 —— 它只影响 Update，是否绘制由收集侧决定。
         * 返回的是调用那一刻的快照，之后增删场景不影响已返回的向量。
         *
         * 指针非拥有，寿命由管理器保证。
         * 每帧调用的路径请用接收向量的重载，避免反复分配
         */
        [[nodiscard]] std::vector<DoodleScene*> GetScenes() const;

        /**
         * 取所有存活场景，写进调用方提供的向量
         *
         * 先 clear 再写入；其余语义与返回值重载相同
         */
        void GetScenes(std::vector<DoodleScene*>& scenes) const;

        /**
         * 生命周期是否仍是 Alive
         */
        [[nodiscard]] bool IsAlive() const
        {
            return m_lifecycleState == ELifecycleState::Alive;
        }

        /**
         * 是否已请求销毁所有场景，等待拥有者执行 DestroyReally
         *
         * 只表示「在等回收」；已经进入 Destroying / Destroyed 时返回 false。
         */
        [[nodiscard]] bool IsPendingDestroy() const
        {
            return m_lifecycleState == ELifecycleState::PendingDestroy;
        }

        /**
         * 销毁回调是否已经跑完、场景容器已清空；管理器内存还在
         */
        [[nodiscard]] bool IsDestroyed() const
        {
            return m_lifecycleState == ELifecycleState::Destroyed;
        }

        /**
         * 当前容器里的场景数量
         *
         * 包含已经请求销毁、但还没被 sweep 回收的场景：它们仍然占着容器里的
         * 位置，直到本帧 sweep 完成。想看「可用场景数」需要自己再按 IsAlive()
         * 过滤，或者等这一帧的 Update 结束。
         */
        [[nodiscard]] std::size_t GetSceneCount() const
        {
            return m_scenes.size();
        }

    private:
        /**
         * 真正执行销毁：Alive / PendingDestroy → Destroying → 清空 → Destroyed
         *
         * 只由拥有者 DoodleApplication 在拆 Vulkan 之前调用；析构只兜底。
         * 回调期间容器不动，所有场景都还在。
         */
        void DestroyReally();

        /**
         * 回收本轮被标记销毁的场景
         *
         * 只处理扫描开始时已存在的场景：回调里通过管理器新建的场景从下一轮
         * 开始算。也只擦除已经跑完回调（Destroyed）的场景，没来得及处理的
         * pending 会留到下一轮。
         */
        void SweepPendingScenes();

    private:
        /**
         * 生命周期状态：Alive → PendingDestroy → Destroying → Destroyed
         *
         * Destroy 只推进到 PendingDestroy；DestroyReally 在回调前后推进到
         * Destroying / Destroyed。析构兜底只在还没开始销毁时发生。
         */
        ELifecycleState m_lifecycleState = ELifecycleState::Alive;

        /**
         * 场景容器：由管理器独占
         *
         * 排在最后：即使析构路径没有先清空，成员逆序析构也会先释放场景
         * 再释放其它成员。
         */
        std::vector<std::unique_ptr<DoodleScene>> m_scenes;

        /**
         * 当前实例，由构造置位、析构清空，成对维护
         *
         * 只是指向应用持有的那个成员，不负责创建与释放。
         */
        static DoodleSceneManager* s_pInstance;
    };
}
