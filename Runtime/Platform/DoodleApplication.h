//
//  DoodleApplication.h
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//
#pragma once

#include <memory>

#include "DoodleWindow.h"
#include "Vulkan/DoodleVulkanManager.h"

namespace Doodle
{
    /**
     * 桌面驱动层：驱动帧循环、按顺序起停 Vulkan 子系统
     *
     * 「窗口从哪来」不在这里 —— 窗口由平台的入口建好之后交给 Initialize。
     * 这是本层与平台之间唯一的接口：换一个桌面平台，只需要换一个入口和一份
     * DoodleWindow 子类，循环本身一行都不用动
     *
     * 只适用于「窗口与程序同寿」的桌面平台：循环每次都是「泵事件 → 画一帧」，
     * 不需要判断窗口还在不在，也没有 Suspend / Resume。
     * Android 不适用 —— 那边的窗口会消失又回来，事件泵还必须自己做阻塞 /
     * 非阻塞的取舍（见 DoodleAndroidWindow::PollEvents），驱动逻辑在
     * Runtime/Platform/Android/DoodleAndroidMain.cpp 里另有一份。
     * 硬合成一个类只会让两边都长满分支
     */
    class DoodleApplication
    {
    public:
        DoodleApplication() = default;
        ~DoodleApplication() = default;

        DoodleApplication(const DoodleApplication&) = delete;
        DoodleApplication(DoodleApplication&&) = delete;
        DoodleApplication& operator= (const DoodleApplication&) = delete;

    public:
        /**
         * 接管窗口，并按顺序起 Vulkan 各层
         *
         * @param window 已经建好、尚未泵过事件的窗口。所有权移交给本对象，
         *               直到 Destroy 才释放
         *
         * @note 窗口建好到交进来之间不要跑事件循环：那会让尺寸变化回调在
         *       交换链建好之前就置位「需要重建」，第一帧白重建一次
         */
        void Initialize(std::unique_ptr<DoodleWindow> window);

        /**
         * 一直画到窗口关闭
         *
         * @note 必须先调 Initialize
         */
        void Run();

        /**
         * 按依赖倒序销毁
         *
         * @note 必须先调 Initialize
         */
        void Destroy();

        /**
         * 窗口尺寸变了，下一帧重建交换链
         *
         * 平台的入口把它接到窗口的尺寸回调上，本对象不自己监听窗口事件。
         * 只记标志、不在回调里当场重建，理由见 DoodleVulkanManager::NotifyWindowResized
         */
        void NotifyWindowResized();

    private:
        /**
         * 窗口
         *
         * 只借不放的另一端：所有权在 Initialize 时从入口转进来，Destroy 时释放。
         * 类型是接口而不是某个具体窗口 —— 本类不该知道窗口是哪来的
         */
        std::unique_ptr<DoodleWindow> m_pWindow;

        /**
         * Vulkan子系统
         */
        DoodleVulkanManager m_vulkanManager;
    };
}
