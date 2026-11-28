//
//  DoodleVulkanManager.h
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#pragma once

#include "Utility/DoodleStd.h"

#include "DoodleCommandManager.h"
#include "Rendering/DoodleFrameContext.h"
#include "DoodleSwapChain.h"
#include "DoodleSyncObjects.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    //本层只按引用持有窗口，完整定义留给源文件去取
    class DoodleWindow;
    class DoodleRenderManager;

    /**
     * Vulkan 后端：设备、交换链、命令层、同步对象与帧时序
     *
     * 负责「怎么把一帧交给系统」：初始化、表面生命周期、等栅栏、获取图像、
     * 提交、呈现与交换链重建。**不负责「这一帧怎么画」** —— 命令缓冲里录什么
     * 由 DoodleRenderPipeline 决定；本层只把 BeginFrame 的结果填进 DoodleFrameContext 的状态字段，
     * 并在 EndFrame 里提交与呈现。
     *
     * 由 DoodleRenderManager 持有并编排；应用层不直接驱动它，只在需要后端原语时
     * 通过 GetInstance() 取用。帧时序的边界条件（栅栏复位、acquire 失败时信号量状态、
     * present 后重建、resize 标志）全部留在本类的 BeginFrame / EndFrame /
     * RecreateSwapChain 里，上层只按状态分支。
     *
     * 需要设备 / 交换链原语、手边又拿不到渲染管理器的代码可以走 GetInstance()；
     * 驱动帧仍然只走 DoodleRenderManager —— 那里才是应用面对的入口。
     *
     * 生命周期与程序一致；Destroy / OnSurfaceLost / RecreateSwapChain 都要求 GPU 已空闲，
     * 本类自己会等。
     */
    class DoodleVulkanManager
    {
        friend class DoodleRenderManager;

        /**
         * 只能由 DoodleRenderManager 创建：运行时只有一份，实例由渲染管理器
         * 独占持有。构造即登记为当前实例，析构时清空
         */
        DoodleVulkanManager()
        {
            s_pInstance = this;
        }

    public:
        ~DoodleVulkanManager()
        {
            if (s_pInstance == this)
            {
                s_pInstance = nullptr;
            }
        }

        DoodleVulkanManager(const DoodleVulkanManager&) = delete;
        DoodleVulkanManager& operator=(const DoodleVulkanManager&) = delete;

        /**
         * 当前那一份 Vulkan 后端
         *
         * 有效期与对象同寿（应用构造 → 应用析构）；「能用了」要到 Initialize
         * 成功之后、Destroy 之前。Initialize 之前 / Destroy 之后取到的是空壳，
         * 与 DoodleCommandManager 的口径一致。
         *
         * 只提供设备 / 交换链这类后端原语；帧时序（BeginFrame / EndFrame /
         * 交换链重建）仍归渲染管理器编排
         */
        [[nodiscard]] static DoodleVulkanManager& GetInstance()
        {
            return *s_pInstance;
        }

        /**
         * 初始化后端：设备、交换链、命令层、同步对象
         *
         * @param window 提供表面的窗口；只借不放
         */
        void Initialize(DoodleWindow& window);

        /**
         * 销毁后端：同步对象、命令层、交换链、设备
         *
         * 前置条件：GPU 已空闲。本函数会自己再等一次，保证异步提交都完成
         */
        void Destroy();

        /**
         * 开始一帧：等本槽位栅栏、获取交换链图像、复位栅栏、begin 命令缓冲
         *
         * 帧时序的边界条件：
         * - 表面已被 OnSurfaceLost 拆掉：返回 Skipped，什么都不做；
         * - acquire 返回 out-of-date：返回 RecreateSwapChain，本帧放弃。
         *   此时 imageAvailable 信号量会停在已发出信号状态，绝不能再拿去 acquire
         *   第二次；重建时同步对象整层换新，正是为了绕开这一点；
         * - acquire 其他错误：抛异常；suboptimal 不算错误。
         *
         * @return 帧状态；status 为 Ready 时命令缓冲已经 begin，帧索引 /
         *         图像索引 / 命令缓冲三项才有效
         *
         * @note 前置条件：没有其他帧正在录制
         */
        [[nodiscard]] EFrameStatus BeginFrame(DoodleFrameContext& context);

        /**
         * 结束并提交一帧：end 命令缓冲、提交、呈现
         *
         * @param context BeginFrame 填好状态字段的帧上下文
         * @return Ready 或 RecreateSwapChain；后者表示交换链已经与窗口不匹配，
         *         调用方应重建后再来
         *
         * @throws std::runtime_error end / 提交 / 呈现失败
         *
         * @note 里面的 Vulkan 调用大多是异步的，返回不代表 GPU 已完成
         */
        EFrameStatus EndFrame(const DoodleFrameContext& context);

        /**
         * 重建交换链与同步对象
         *
         * 顺序：窗口可绘制等待 → 关闭检查 → 等 GPU 空闲 → 同步对象 → 交换链
         * → 同步对象。渲染管线的深度图与 pass 不在这里，由 DoodleRenderManager
         * 在本函数返回 true 之后调用。
         *
         * 末尾统一清 m_windowResized：重建完成即代表积压的 resize 通知已兑现。
         *
         * @return false 表示窗口正在关闭，本次不重建；true 表示交换链已换新
         */
        [[nodiscard]] bool RecreateSwapChain();

        /**
         * 等待 GPU 空闲
         *
         * 给「设备资源在渲染器之外销毁」的调用方用：应用销毁资产管理器之前
         * 必须先确保没有在途命令还在引用网格 / 纹理缓冲。本类自己的 Destroy
         * 也会等，但那已经排在资产管理器之后，等不到了
         *
         * @note 返回不代表任何排队中的工作已被取消，只是都完成了
         */
        void WaitForGpuIdle();

        /**
         * 表面没了：拆掉同步对象、交换链与表面本身
         *
         * 设备、命令池保留 —— 它们的寿命不挂在窗口上。
         * 渲染管线持有的深度图由 DoodleRenderManager 在本函数之后拆。
         *
         * @note 期间不得调用 BeginFrame
         */
        void OnSurfaceLost();

        /**
         * 表面回来了：建出新表面并重查呈现队列族的支持情况
         *
         * 只恢复表面与队列选择；交换链与同步对象由随后的 RecreateSwapChain
         * 重建，渲染管线的深度图与 pass 由 DoodleRenderManager 接着恢复。
         *
         * @param window 提供新的表面
         * @note 与 OnSurfaceLost 必须成对。可反复调用（每次切前后台一轮）
         */
        void OnSurfaceRecreated(DoodleWindow& window);

        /**
         * 通知窗口尺寸发生了变化，下一帧需要重建交换链
         *
         * 只记标志，不在回调里当场重建：连续多次 resize 会合并到帧边界处理一次。
         */
        void OnWindowResized()
        {
            m_windowResized = true;
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] DoodleVulkanDevice& GetDevice()
        {
            return m_device;
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] const DoodleVulkanDevice& GetDevice() const
        {
            return m_device;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] DoodleSwapChain& GetSwapChain()
        {
            return m_swapChain;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] const DoodleSwapChain& GetSwapChain() const
        {
            return m_swapChain;
        }

    private:
        /**
         * 当前实例，构造时登记、析构时清空
         */
        static DoodleVulkanManager* s_pInstance;

        /**
         * 窗口，在 Initialize 时绑定
         *
         * 只借不放：窗口的创建与销毁都归驱动层。本层需要它，是因为重建交换链
         * 时要靠它取最新像素尺寸，OnSurfaceLost 之后还要靠它把表面重新建起来
         */
        DoodleWindow* m_pWindow = nullptr;

        /**
         * Vulkan设备层：Instance / Surface / 物理设备 / 逻辑设备 / 队列
         */
        DoodleVulkanDevice m_device;

        /**
         * 交换链层：窗口的呈现目标
         */
        DoodleSwapChain m_swapChain;

        /**
         * 命令管理层：命令池、命令缓冲与传输池
         *
         * BeginFrame / EndFrame 驱动它的绘制命令缓冲；传输池供资源上传使用
         */
        DoodleCommandManager m_commandManager;

        /**
         * 同步对象层：帧信号量与栅栏
         *
         * renderFinished 按交换链图像数量分配，所以它依赖交换链层，
         * 且随交换链一起重建，须排在交换链之后
         */
        DoodleSyncObjects m_syncObjects;

        /**
         * 当前在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1，BeginFrame 取用后立即推进一格
         *
         * 命令缓冲、imageAvailable 信号量、帧栅栏都按它轮转取用。
         * renderFinished 不按它取 —— 那是按交换链图像索引的，两个维度互不相干
         */
        uint32_t m_currentFrame = 0;

        /**
         * 窗口尺寸变化标志
         *
         * 由 OnWindowResized 置位，EndFrame 在呈现之后按它请求重建，
         * RecreateSwapChain 末尾统一清零。不在回调里当场重建，是为了把连续多次
         * resize 合并到帧边界只处理一次
         */
        bool m_windowResized = false;
    };
}
