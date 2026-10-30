//
//  DoodleInput.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

namespace Doodle
{
    /**
     * 游戏层使用的键位
     *
     * 只列当前玩法真正用到的键。要加键时在这里追加，再到各平台的窗口实现里映射，
     * 不要把一个平台的键码直接塞进来
     */
    enum class DoodleKey : std::uint8_t
    {
        W,
        A,
        S,
        D,
        Q,
        E,
        LeftShift,

        Count
    };

    /**
     * 鼠标按键
     */
    enum class DoodleMouseButton : std::uint8_t
    {
        Left,
        Right,
        Middle,

        Count
    };

    /**
     * 触摸阶段，命名对齐 Unity 的 TouchPhase
     */
    enum class DoodleTouchPhase : std::uint8_t
    {
        Began,
        Moved,
        Stationary,
        Ended,
        Canceled,
    };

    /**
     * 一个触摸点，字段参考 Unity 的 Touch
     *
     * 坐标单位是**逻辑窗口单位**（Android 已按屏幕密度换算），不是物理像素；
     * 原点在窗口左上角，+Y 向下。和 DoodleInput 的鼠标位移同一套单位/方向，
     * 与 Unity 的「左下角原点、+Y 向上」不同。
     *
     * 坐标是窗口（显示方向）的，不是交换链图像自己的；Android 的预旋转
     * 不影响这里。
     *
     * 这是本帧快照：phase / deltaPosition / deltaTime 都是相对上一帧的状态。
     * 手指在两帧之间按下又抬起这种极短触摸，可能只留下最后的 Ended，不会
     * 单独出现 Began —— 输入层按帧聚合事件，不逐事件回调。
     */
    struct DoodleTouch
    {
        /**
         * 手指 ID，同一根手指按下到抬起期间保持不变
         */
        int32_t fingerId = -1;

        /**
         * 当前位置，逻辑窗口单位
         */
        glm::vec2 position{0.0f};

        /**
         * 相对上一帧的位移，逻辑窗口单位
         */
        glm::vec2 deltaPosition{0.0f};

        /**
         * 本输入帧的时间，秒
         */
        float deltaTime = 0.0f;

        /**
         * 压力，0..1
         */
        float pressure = 0.0f;

        /**
         * 本帧阶段
         */
        DoodleTouchPhase phase = DoodleTouchPhase::Canceled;
    };

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
     * 平台相关的原始轮询在各自的 DoodleWindow 实现里（GLFW / Win32 / Android），
     * 本类只负责「原始状态 → 本帧状态 + 按下/松开边沿」。公共头里没有
     * GLFW、Win32 或 Vulkan。
     *
     * 刷新只归 DoodleApplication：输入快照必须和帧边界对齐，游戏层不要自己调
     * Update / Reset（它们是 private，应用是 friend）。
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
        static constexpr std::size_t kMaxTouches = 10;

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
         * 捕获的开关在窗口 / 应用层（见 DoodleWindow::SetCursorCaptured），
         * 这里只报告状态。Unity 里对应的概念是 Cursor.lockState，不在 Input 上。
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
        [[nodiscard]] static DoodleTouch GetTouch(int index);

        /**
         * 当前平台是否支持触摸输入
         *
         * Android 为 true；桌面为 false。注意「支持」不等于本帧有触摸点，
         * 本帧是否有触摸仍然看 GetTouchCount。
         */
        [[nodiscard]] static bool IsTouchSupported();

        /**
         * 平台层每帧填写的原始状态
         *
         * 这是 DoodleWindow → DoodleApplication → DoodleInput 的内部数据形状，
         * 游戏层不要直接用它；对外查询用上面的静态方法。
         */
        struct RawState
        {
            std::array<bool, static_cast<std::size_t>(DoodleKey::Count)> keys{};
            std::array<bool, static_cast<std::size_t>(DoodleMouseButton::Count)> mouseButtons{};

            float mouseDeltaX = 0.0f;
            float mouseDeltaY = 0.0f;
            bool cursorCaptured = false;

            /**
             * 本帧触摸点，只有前 touchCount 个有效
             */
            std::array<DoodleTouch, kMaxTouches> touches{};
            int32_t touchCount = 0;

            /**
             * 平台是否有触摸能力；Android 为 true
             */
            bool touchSupported = false;

            void SetKey(const DoodleKey key, const bool down)
            {
                keys[static_cast<std::size_t>(key)] = down;
            }

            [[nodiscard]] bool IsKeyDown(const DoodleKey key) const
            {
                return keys[static_cast<std::size_t>(key)];
            }

            void SetMouseButton(const DoodleMouseButton button, const bool down)
            {
                mouseButtons[static_cast<std::size_t>(button)] = down;
            }

            [[nodiscard]] bool IsMouseButtonDown(const DoodleMouseButton button) const
            {
                return mouseButtons[static_cast<std::size_t>(button)];
            }
        };

    private:
        /**
         * 用本帧的原始状态刷新快照，并推进按下/松开边沿
         *
         * 每帧只调一次，位置在场景更新之前
         */
        static void Update(const RawState& rawState);

        /**
         * 清空当前与上一帧状态
         *
         * 运行时启动与退出时调用，避免下一个应用实例/下一轮运行继承旧边沿
         */
        static void Reset();

        /**
         * 本帧状态
         */
        static RawState s_current;

        /**
         * 上一帧状态，用来算 GetKeyDown / GetKeyUp
         */
        static RawState s_previous;
    };
}
