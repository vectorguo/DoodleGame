//
//  DoodleInput.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include "Utility/DoodleCommon.h"

#include "DoodleInputRawState.h"

namespace Doodle
{
    //只以引用的形式出现在 Update 的签名里，完整定义留给源文件。
    //输入层不 include 窗口接口，DoodleWindow 也不必认识 DoodleInput ——
    //两边共享的只有 DoodleInputRawState
    class DoodleWindow;

    /**
     * 全局输入查询，用法对齐 Unity 的 Input
     *
     *     DoodleInput::GetKey(DoodleKey::W)
     *     DoodleInput::GetKeyDown(DoodleKey::Q)
     *     DoodleInput::GetMouseDelta()
     *
     * 查询的是**本帧快照**：DoodleApplication 每帧在场景更新之前刷新一次，
     * 同一帧内所有组件看到同一份状态。不在查询时去轮询平台 API —— 那样
     * 一帧里多次查询可能得到不同结果，也会把平台差异带进游戏层。
     *
     * 快照的来源是平台窗口：各 DoodleWindow 实现把原生 API 映射成
     * DoodleInputRawState；Update 在帧边界从窗口取一次，顺手应用窗口级策略
     * （右键切换光标捕获），再推进本帧快照。公共头里没有 GLFW、Win32 或 Vulkan。
     *
     * 刷新只归 DoodleApplication：输入快照必须和帧边界对齐，游戏层不要自己调
     * Update / Apply / Reset（它们是 private，应用是 friend）。
     *
     * 线程：只在主线程使用。
     */
    class DoodleInput final
    {
        friend class DoodleApplication;

    public:
        DoodleInput() = delete;

        /**
         * 单帧最多同时报告的触摸点数量
         *
         * Android 设备常见上限就是 10；超出的事件会被忽略
         */
        static constexpr std::size_t kMaxTouches = DoodleInputRawState::kMaxTouches;

        /**
         * 本帧是否按住
         */
        [[nodiscard]] static bool GetKey(const DoodleKey key);

        /**
         * 是否在本帧刚按下（上一帧未按、本帧按住）
         */
        [[nodiscard]] static bool GetKeyDown(const DoodleKey key);

        /**
         * 是否在本帧刚松开（上一帧按住、本帧未按）
         */
        [[nodiscard]] static bool GetKeyUp(const DoodleKey key);

        /**
         * 本帧鼠标键是否按住
         */
        [[nodiscard]] static bool GetMouseButton(const DoodleMouseButton button);

        /**
         * 鼠标键是否在本帧刚按下
         */
        [[nodiscard]] static bool GetMouseButtonDown(const DoodleMouseButton button);

        /**
         * 鼠标键是否在本帧刚松开
         */
        [[nodiscard]] static bool GetMouseButtonUp(const DoodleMouseButton button);

        /**
         * 本帧鼠标位移，单位是逻辑窗口单位
         *
         * 只有光标处于捕获状态时才有意义；未捕获时恒为零。
         * macOS 是点，Windows 已按 DPI 换算成 96 DPI 下的逻辑像素。
         */
        [[nodiscard]] static glm::vec2 GetMouseDelta();

        /**
         * 本帧光标是否处于捕获状态
         *
         * 开关由 Update 在帧边界按右键同步给窗口，机制在窗口侧
         * （见 DoodleWindow::SetCursorCaptured）；这里只报告结果。
         * Unity 里对应的概念是 Cursor.lockState，不在 Input 上。
         */
        [[nodiscard]] static bool IsCursorCaptured();

        /**
         * 本帧触摸点数量，命名对齐 Unity 的 Input.touchCount
         *
         * 包含本帧刚结束（Ended / Canceled）的触摸点；下一帧不再出现。
         * 桌面平台恒为 0。
         */
        [[nodiscard]] static int GetTouchCount();

        /**
         * 取第 index 个触摸点，命名对齐 Unity 的 Input.GetTouch
         *
         * index 必须在 [0, GetTouchCount()) 内，越界抛 std::out_of_range。
         * 返回的是本帧快照的副本。
         */
        [[nodiscard]] static DoodleTouch GetTouch(const int index);

        /**
         * 当前平台是否支持触摸输入
         *
         * Android 为 true；桌面为 false。注意「支持」不等于本帧有触摸点，
         * 本帧是否有触摸仍然看 GetTouchCount。
         */
        [[nodiscard]] static bool IsTouchSupported();

    private:
        /**
         * 从窗口取本帧原始状态，应用窗口级输入策略，再推进快照
         *
         * 每帧只调一次，位置在场景更新之前。切换光标捕获的协议收在这里：
         * 窗口切换时会把位移清零并重建基准，所以切换后要重新取一次，
         * 避免这一帧把切换前的旧位移算进视角。调用方不需要知道这些
         */
        static void Update(DoodleWindow& window);

        /**
         * 用一份原始状态刷新快照，并推进按下/松开边沿
         *
         * 纯映射：不碰窗口、不轮询平台 API。Update 从窗口拿到状态后交给它；
         * 以后若要接非窗口来源（回放、测试），入口也是这里
         */
        static void Apply(const DoodleInputRawState& rawState);

        /**
         * 清空当前与上一帧状态
         *
         * 运行时启动与退出时调用，避免下一个应用实例/下一轮运行继承旧边沿
         */
        static void Reset();

        /**
         * 捕获绑定：目前桌面用右键
         *
         * 这是应用级绑定选择，不是输入层的通用语义。要换中键 / 按键时只改这里
         */
        static constexpr DoodleMouseButton kCaptureButton = DoodleMouseButton::Right;

        /**
         * 本帧状态
         */
        static DoodleInputRawState s_current;

        /**
         * 上一帧状态，用来算 GetKeyDown / GetKeyUp
         */
        static DoodleInputRawState s_previous;
    };
}
