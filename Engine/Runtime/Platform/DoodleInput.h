//
//  DoodleInput.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
    public:
        DoodleInput() = delete;

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
        friend class DoodleApplication;

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
