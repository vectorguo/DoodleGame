//
//  DoodleAndroidWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include <cstdint>

#include "../DoodleWindow.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 <android_native_app_glue.h>：本类的使用者只该看见
//DoodleWindow 那套接口加下面两个自有方法，胶水层是这一层的实现细节
struct android_app;

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
        static void HandleAppCmd(android_app* pApp, int32_t command);

        /**
         * 处理一条命令：先更新本层自己的状态，再全部转交驱动层
         *
         * 「全部」是实情：前后台由本层消化（m_focused），但命令照样往下走，
         * 由驱动层挑它关心的那几条 —— 本层不替它做取舍
         */
        void OnAppCmd(int32_t command);

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
    };
}
