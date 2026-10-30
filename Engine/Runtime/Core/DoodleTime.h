//
//  DoodleTime.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <chrono>

namespace Doodle
{
    /**
     * 全局时间查询，用法与命名对齐 Unity 的 Time
     *
     *     DoodleTime::GetDeltaTime()
     *     DoodleTime::GetTime()
     *     DoodleTime::SetTimeScale(0.0f);   // 暂停
     *
     * 查询的是**本帧快照**：DoodleApplication 每帧在输入刷新与场景更新之前
     * 推进一次，同一帧内所有组件看到同一份时间。组件不要自己去问系统时钟 ——
     * 那样一帧里多次取值可能不同，也会把各平台的计时差异带进游戏层。
     *
     * 两组时间：
     *   - GetDeltaTime / GetTime —— 玩法时间。已乘 timeScale，且单帧被
     *     maximumDeltaTime 钳住；timeScale 为 0 时停在原地
     *   - GetUnscaledDeltaTime / GetUnscaledTime —— 真实时间。不乘 timeScale、
     *     也不钳上限，界面动画、耗时统计用它
     *
     * 没有 fixedDeltaTime / fixedTime：本工程还没有固定步长的更新循环
     * （物理与动画事件都在 Update 里），那组值没有驱动者，加了只会是一份
     * 没人推进的空数据。将来做固定步长循环时再补，语义按 Unity 对齐。
     * smoothDeltaTime 同理，有人需要抹平帧率抖动之前不引入平滑策略。
     *
     * 刷新只归 DoodleApplication：时间必须和帧边界对齐，游戏层不要自己调
     * Update / Reset（它们是 private，应用是 friend）。
     *
     * 线程：只在主线程使用。
     */
    class DoodleTime final
    {
        friend class DoodleApplication;

    public:
        DoodleTime() = delete;

        /**
         * 本帧时长，秒
         *
         * 已乘 timeScale，单帧不超过 maximumDeltaTime。位移、冷却、插值这类
         * 玩法步进统一用它，组件在 OnUpdate 里直接取，不再经参数传入
         */
        [[nodiscard]] static float GetDeltaTime();

        /**
         * 本帧的真实时长，秒
         *
         * 不乘 timeScale、不钳上限，就是两帧之间实际过了多久。切后台再回来
         * 时，那段间隔会整个计进第一帧 —— 界面计时之类该接受这一点，
         * 玩法步进则用 GetDeltaTime
         */
        [[nodiscard]] static float GetUnscaledDeltaTime();

        /**
         * 从「游戏开始」累计的玩法时间，秒
         *
         * 「游戏开始」是最近一次 ResetFrameClock，即进入帧循环的时刻。
         * 受 timeScale 影响：暂停时停在原地，这是它与 GetUnscaledTime
         * 的唯一区别
         */
        [[nodiscard]] static float GetTime();

        /**
         * 从「游戏开始」累计的真实时间，秒
         *
         * 不受 timeScale 影响；Suspend / Resume 之间的间隔同样计入
         */
        [[nodiscard]] static float GetUnscaledTime();

        /**
         * 本帧的帧序号，从 0 开始
         *
         * 第一帧里读到 0，每推进一帧在帧末加一。命名对齐 Unity 的
         * Time.frameCount
         */
        [[nodiscard]] static int GetFrameCount();

        /**
         * 全局时间倍率，默认 1
         *
         * 0 暂停，1 正常，大于 1 加速。只作用在玩法时间上，真实时间不受影响
         */
        [[nodiscard]] static float GetTimeScale();

        /**
         * 设置全局时间倍率
         *
         * 只接受非负值：负倍率（倒放）会让累计时间往回走，所有「到现在为止」
         * 的假设都不成立。传入负值抛 std::invalid_argument
         */
        static void SetTimeScale(const float timeScale);

        /**
         * 单帧玩法时长的上限，秒，默认 0.1
         *
         * 沿用迁移前 DoodleApplication::Tick 里的硬编码值。断点、拖窗口、
         * 系统卡顿之后两帧间隔可能很长；不钳的话，组件会在一帧里走出一大段
         * 位移，甚至穿过碰撞体
         */
        [[nodiscard]] static float GetMaximumDeltaTime();

        /**
         * 设置单帧玩法时长的上限
         *
         * 必须为正，否则抛 std::invalid_argument：上限为 0 会让玩法时间永久
         * 停在原地，想暂停应该用 timeScale。只约束玩法时间，真实时间不受它影响
         */
        static void SetMaximumDeltaTime(const float maximumDeltaTime);

    private:

        /**
         * 按系统时钟推进一帧：求差 → 钳上限并乘倍率 → 累计 → 帧序号加一
         *
         * 每帧只调一次，位置在输入刷新与场景更新之前
         */
        static void Update();

        /**
         * 把帧时钟的起点钉在当前时刻，并清零本帧时长、累计时间与帧序号 ——
         * 等价于「这一刻游戏开始」
         *
         * 由 DoodleApplication::ResetFrameClock 在进入循环前调用，不能挪到
         * 构造函数里：构造到进入 Run 之间还有建窗与 Vulkan 初始化，那段时间
         * 不属于任何一帧。timeScale 与 maximumDeltaTime 是配置而不是时间状态，
         * 不随它清零
         */
        static void Reset();

        /**
         * 上一帧的时间戳；Reset 取初值，Update 每帧更新
         */
        static std::chrono::steady_clock::time_point s_lastFrameTime;

        /**
         * 本帧时长（已乘 timeScale、已钳上限），秒
         */
        static float s_deltaTime;

        /**
         * 本帧真实时长，秒
         */
        static float s_unscaledDeltaTime;

        /**
         * 累计玩法时间，秒
         */
        static float s_time;

        /**
         * 累计真实时间，秒
         */
        static float s_unscaledTime;

        /**
         * 帧序号，从 0 开始
         */
        static int s_frameCount;

        /**
         * 全局时间倍率，默认 1
         */
        static float s_timeScale;

        /**
         * 单帧玩法时长上限，默认 0.1 秒
         */
        static float s_maximumDeltaTime;
    };
}
