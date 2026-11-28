//
//  DoodleInputRawState.h
//  Doodle
//
//  Created by 郭智 on 2026/11/11.
//

#pragma once

#include "Utility/DoodleCommon.h"

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
     * 平台层每帧填写的原始状态
     *
     * 这是 DoodleWindow → DoodleInput 的内部数据形状。它单独成头而不是嵌在
     * DoodleInput 里，是为了让 DoodleWindow 能只认识这份数据、不认识 DoodleInput：
     * 输入层反过来要调窗口切光标捕获，两边都 include 对方就会成环。
     *
     * 游戏层不要直接用它；对外查询走 DoodleInput 的静态方法。
     */
    struct DoodleInputRawState
    {
        /**
         * 单帧最多同时报告的触摸点数量
         *
         * Android 设备常见上限就是 10；超出的事件会被忽略
         */
        static constexpr std::size_t kMaxTouches = 10;

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
}
