//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include <cstdint>

#include "DoodleCommandBuffer.h"
#include "DoodleFrameBuffer.h"
#include "DoodleGraphicsPipeline.h"
#include "DoodleRenderPass.h"
#include "DoodleSwapChain.h"
#include "DoodleSyncObjects.h"
#include "DoodleVulkanDevice.h"
#include "../Platform/DoodleWindow.h"

namespace Doodle
{
    /**
     * Vulkan子系统门面：按依赖顺序持有各层，对外提供统一的Vulkan入口
     */
    class DoodleVulkanManager
    {
    public:
        /**
         * 初始化Vulkan
         */
        void Initialize(DoodleWindow& window);

        /**
         * 销毁Vulkan
         */
        void Destroy();

        /**
         * 表面没了：拆掉表面，以及所有依附于它的层
         *
         * Android 上切后台、锁屏、旋转都会走这条路 —— ANativeWindow 消失，
         * 基于它的 VkSurfaceKHR 随之失效，交换链、帧缓冲、按图像分配的
         * 同步对象全部作废。桌面不会调到（窗口与程序同寿）
         *
         * 保留的是与表面无关的那几层：Instance、物理/逻辑设备、队列、
         * 渲染通道、图形管线、命令池。重建它们代价太大且没有必要
         *
         * @note 期间不得调用 DrawFrame
         */
        void Suspend();

        /**
         * 表面回来了：建出新表面，再把被 Suspend 拆掉的那几层重建起来
         *
         * 重建那一半交给 RecreateSwapChain —— 与「交换链过时」是同一条路，
         * 两边只有表面本身不同：这边要新建，那边原样沿用
         *
         * @param window 提供新的表面
         * @note 与 Suspend 必须成对。可反复调用（每次切前后台一轮）
         */
        void Resume(DoodleWindow& window);

        /**
         * 渲染并呈现一帧
         *
         * 主循环每轮调用一次。六步固定：等本槽位上一轮结束、取一张交换链图像、
         * 复位栅栏、录制命令缓冲、提交、呈现。顺序不能调换，
         * 每一步都在为下一步准备前提条件
         *
         * 交换链与窗口不再匹配时（缩放窗口等），本函数内部完成重建：
         * acquire 返回 out-of-date 就立刻重建并放弃本帧；呈现返回 out-of-date /
         * suboptimal，或收到过 resize 通知，则在呈现之后重建。
         * 两处判断各有一个固定位置，都不能挪 —— 理由见实现里的注释
         *
         * 同一时刻允许 MAX_FRAMES_IN_FLIGHT 帧在途：host 录第 k 帧时，
         * GPU 可以还在跑第 k-1 帧，两者用的是各自槽位的资源，不会互相踩踏
         *
         * 放在门面：它把本门面自己持有的几层按顺序驱动一遍，连同它们共有的
         * 重建时机 —— 那正是门面的分内事
         *
         * @note 里面的 Vulkan 调用大多是异步的，返回不代表 GPU 已完成
         */
        void DrawFrame();

        /**
         * 通知窗口尺寸发生了变化，下一帧需要重建交换链
         *
         * 由上层在 GLFW 的 resize 回调里调用。本层不自己监听窗口事件：
         * 回调可能在任意时刻触发，在里面做重建会打乱帧的时序。
         * 这里只记一个标志，把处理留给帧循环，顺带把连续的 resize 事件合并成一次重建
         */
        void NotifyWindowResized()
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

        /**
         * 获取渲染通道
         */
        [[nodiscard]] DoodleRenderPass& GetRenderPass()
        {
            return m_renderPass;
        }

        /**
         * 获取渲染通道
         */
        [[nodiscard]] const DoodleRenderPass& GetRenderPass() const
        {
            return m_renderPass;
        }

        /**
         * 获取图形管线
         */
        [[nodiscard]] DoodleGraphicsPipeline& GetGraphicsPipeline()
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取图形管线
         */
        [[nodiscard]] const DoodleGraphicsPipeline& GetGraphicsPipeline() const
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取帧缓冲
         */
        [[nodiscard]] DoodleFrameBuffer& GetFrameBuffer()
        {
            return m_frameBuffer;
        }

        /**
         * 获取帧缓冲
         */
        [[nodiscard]] const DoodleFrameBuffer& GetFrameBuffer() const
        {
            return m_frameBuffer;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] DoodleCommandBuffer& GetCommandBuffer()
        {
            return m_commandBuffer;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] const DoodleCommandBuffer& GetCommandBuffer() const
        {
            return m_commandBuffer;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] DoodleSyncObjects& GetSyncObjects()
        {
            return m_syncObjects;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] const DoodleSyncObjects& GetSyncObjects() const
        {
            return m_syncObjects;
        }

    private:
        /**
         * 重建交换链，以及所有随它一起失效的层
         *
         * 顺序：交换链 → 帧缓冲 → 同步对象，与 Initialize 同序、与 Destroy 反序。
         * 渲染通道与图形管线不重建 —— 只有交换链格式真的变了才需要，那是极罕见的情况，
         * 代价却是把管线整个重编译一遍。理由详见实现
         *
         * 末尾统一清 m_windowResized：重建完成即代表积压的 resize 通知已兑现。
         * 不放在各调用点，是因为 acquire 提前返回那条路也要求这一刻就兑现它 ——
         * 漏一处就会为同一个尺寸白重建一遍；而会泵事件的 WaitUntilDrawable
         * 正在本函数开头，清零只能排在它后面
         */
        void RecreateSwapChain();

    private:
        /**
         * 窗口，在 Initialize 时绑定
         *
         * 只借不放：窗口的创建与销毁都归驱动层（DoodleApplication 及其平台子类，
         * macOS 见 DoodleMacApplication，Android 见 DoodleAndroidApplication）。
         * 本层需要它，是因为重建交换链时要靠它
         * 取窗口的最新像素尺寸，Suspend 之后还要靠它把表面重新建起来
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
         * 渲染通道层：本帧的渲染目标结构，格式取自交换链，须排在交换链之后
         */
        DoodleRenderPass m_renderPass;

        /**
         * 图形管线层：引用渲染通道，须排在渲染通道之后
         */
        DoodleGraphicsPipeline m_graphicsPipeline;

        /**
         * 帧缓冲层：引用交换链图像视图与渲染通道，须排在两者之后
         */
        DoodleFrameBuffer m_frameBuffer;

        /**
         * 命令缓冲层：录制时引用上述各层，须排在帧缓冲之后
         */
        DoodleCommandBuffer m_commandBuffer;

        /**
         * 同步对象层：帧信号量与栅栏
         *
         * renderFinished 按交换链图像数量分配，所以它依赖交换链层，
         * 且随交换链一起重建，须排在交换链之后
         */
        DoodleSyncObjects m_syncObjects;

        /**
         * 当前在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1，DrawFrame 开头取用后立即推进一格
         *
         * 命令缓冲、imageAvailable 信号量、帧栅栏都按它轮转取用。
         * renderFinished 不按它取 —— 那是按交换链图像索引的，两个维度互不相干
         */
        uint32_t m_currentFrame = 0;

        /**
         * 窗口尺寸变化标志
         *
         * 由 NotifyWindowResized 置位，DrawFrame 在呈现之后消费并清零。
         * 不在回调里当场重建，是为了把连续多次 resize 合并到帧边界只处理一次
         */
        bool m_windowResized = false;
    };
}
