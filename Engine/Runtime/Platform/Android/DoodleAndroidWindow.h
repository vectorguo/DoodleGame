//
//  DoodleAndroidWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../DoodleWindow.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 <android_native_app_glue.h>：本类的使用者只该看见
//DoodleWindow 那套接口加下面两个自有方法，胶水层是这一层的实现细节
struct android_app;
struct AInputEvent;

namespace Doodle
{
    //前向声明即可：本类只持有它的引用，成员函数的调用在 .cpp 里
    class DoodleAndroidApplication;

    /**
     * Android 窗口，用 NDK 的 native_app_glue 实现
     *
     * 与桌面那份实现的关键差别：这里的窗口会消失又回来。切后台、锁屏、
     * 旋转屏幕都会让 ANativeWindow 没了，基于它的 VkSurfaceKHR 跟着失效。
     * 但本对象一直在 —— 它只是内部换了个 window 指针，所以窗口回来之后
     * 重新调 CreateSurface 就能拿到新表面
     *
     * 事件泵也在这里，而不是像桌面那样由驱动层直接调 GLFW。原因是 Android
     * 的事件循环必须自己做阻塞/非阻塞的取舍（见 PumpEvents 的注释），
     * 而那个取舍依赖「窗口在不在」这个只有本层知道的状态
     */
    class DoodleAndroidWindow final : public DoodleWindow
    {
    public:
        /**
         * 接管 pApp 的事件回调与 userData
         *
         * 构造之后，所有 APP_CMD_* 会先进入本类，再转交驱动层。
         * 也就是说 pApp->userData 归本类所有，驱动层不要再用它
         *
         * 驱动层以引用收下、而不是像原来那样收一个 std::function：
         * APP_CMD_* 是 Android 自己的词汇，不该为了让跨平台的 GetInstance
         * 能调它而塞进 DoodleApplication；而本类的构造函数本来就有必填参数
         * （pApp），多一个引用不花任何代价，依赖还留在签名上看得见
         *
         * @param pApp native_app_glue 给的 android_app，生命周期由系统保证
         * @param application 转交目标。按引用收，不能为空
         */
        DoodleAndroidWindow(android_app* pApp, DoodleAndroidApplication& application);

        ~DoodleAndroidWindow() override = default;

        DoodleAndroidWindow(const DoodleAndroidWindow&) = delete;
        DoodleAndroidWindow(DoodleAndroidWindow&&) = delete;
        DoodleAndroidWindow& operator=(const DoodleAndroidWindow&) = delete;

    public:
        /**
         * 现在能不能画：窗口在，而且应用在前台
         *
         * 名为 Renderable 而不是「窗口存在」，是因为少了后半条判断，
         * 应用退到后台但窗口尚存的那一小段里会继续满速出帧 ——
         * Android 上这是要挨系统杀的
         */
        [[nodiscard]] bool IsRenderable() const;

    public:
        // ---- DoodleWindow ----

        [[nodiscard]] bool IsCloseRequested() const override;

        void PumpEvents() override;

        void WaitUntilDrawable() override;

        [[nodiscard]] DoodleInput::RawState GetRawInputState() const override;

        void SetCursorCaptured(const bool captured) override;

        [[nodiscard]] VkExtent2D GetSurfaceSize() const override;

        [[nodiscard]] std::vector<const char*> GetSurfaceExtensions() const override;

        [[nodiscard]] VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const override;

    private:
        /**
         * 胶水层的命令入口
         *
         * 必须是这个签名：胶水层是 C 代码，回调是裸函数指针，没有 this 可用。
         * 所以构造时把本对象存进 pApp->userData，在这里取回来
         */
        static void HandleAppCmd(android_app* pApp, const int32_t command);

        /**
         * 处理一条命令：先更新本层自己的状态，再全部转交驱动层
         *
         * 「全部」是实情：前后台由本层消化（m_focused），但命令照样往下走，
         * 由驱动层挑它关心的那几条 —— 本层不替它做取舍
         */
        void OnAppCmd(const int32_t command);

        /**
         * 胶水层的输入入口
         *
         * 必须是这个签名：onInputEvent 是 C 函数指针。返回值表示事件是否被消费；
         * 我们只消费触摸事件，按键等交给系统
         */
        static int32_t HandleInputEvent(android_app* pApp, AInputEvent* pEvent);

        /**
         * 处理一条输入事件；返回是否消费
         */
        bool OnInputEvent(const AInputEvent* pEvent);

        /**
         * 新增一个触摸点（DOWN / POINTER_DOWN）
         */
        void AddTouch(const AInputEvent* pEvent, std::size_t pointerIndex, float densityScale);

        /**
         * 更新一个触摸点（MOVE / UP / POINTER_UP）
         *
         * @param ending true 表示这根手指在本事件里抬起
         */
        void UpdateTouch(const AInputEvent* pEvent,
                         std::size_t pointerIndex,
                         float densityScale,
                         bool ending);

        /**
         * 把当前活动触摸点整理成本帧快照，并移除已经结束的手指
         */
        void BuildTouchFrame();

        /**
         * 物理像素 → 逻辑窗口单位的缩放
         *
         * Android 以 160 dpi 为 mdpi 基准；返回 1.0 表示不缩放
         */
        [[nodiscard]] float GetTouchDensityScale() const;

        /**
         * 一个活动触摸点的跨帧状态
         */
        struct TouchState
        {
            int32_t fingerId = -1;

            /** 当前位置（逻辑窗口单位） */
            float x = 0.0f;
            float y = 0.0f;

            /** 上一帧快照时的位置，用来算本帧位移 */
            float lastFrameX = 0.0f;
            float lastFrameY = 0.0f;

            /** 本帧累计位移 */
            float deltaX = 0.0f;
            float deltaY = 0.0f;

            float pressure = 0.0f;
            DoodleTouchPhase phase = DoodleTouchPhase::Stationary;

            /** 本帧快照之后从活动列表移除 */
            bool removeAfterFrame = false;
        };

    private:
        /** 胶水层的应用对象，只借不放，生命周期由系统保证 */
        android_app* m_pApp = nullptr;

        /** 驱动层，只借不放：它构造并持有本对象，所以一定比本对象活得久 */
        DoodleAndroidApplication& m_application;

        /**
         * 应用是否在前台
         *
         * 初值 false：构造之后到 GAINED_FOCUS 到来之前，确实没有前台这个事实。
         * 这里不会因此卡死 —— 焦点变化本身就是一条命令，会唤醒 Looper
         */
        bool m_focused = false;

        /**
         * 本帧输入快照：Android 没有键鼠，触摸点在 PumpEvents 里填充
         */
        DoodleInput::RawState m_input;

        /**
         * 当前按住的手指，跨帧保留；BuildTouchFrame 生成每帧快照
         */
        std::vector<TouchState> m_activeTouches;

        /**
         * 上一输入帧的时间，用来填 DoodleTouch::deltaTime
         */
        std::chrono::steady_clock::time_point m_lastTouchFrameTime;

        /**
         * 本输入帧与上一帧的间隔，秒
         */
        float m_touchFrameDeltaSeconds = 0.0f;
    };
}
