//
//  DoodleAndroidMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include <exception>

#include <android_native_app_glue.h>

#include "DoodleAndroidWindow.h"
#include "../DoodleLog.h"
#include "Vulkan/DoodleVulkanManager.h"

namespace
{
    using namespace Doodle;

    /**
     * android_main 的局部状态
     *
     * 装在一起是为了让 OnAppCommand 能拿到它们 —— 那个回调是 C 函数指针，
     * 没有捕获列表可用，只能经 static 函数 + 传参进来
     */
    struct DoodleAndroidContext
    {
        /** Vulkan 子系统 */
        DoodleVulkanManager vulkan;

        /** 窗口，构造后立刻回填 */
        DoodleAndroidWindow* pWindow = nullptr;

        /** Vulkan 是否已经起来过。第一轮 INIT_WINDOW 是 Initialize，之后都是 Resume */
        bool initialized = false;

        /** 出过错，且不可能自行恢复。帧循环据此退出 */
        bool failed = false;
    };

    /**
     * 处理 APP_CMD_*
     *
     * 窗口状态（是否前台）已经由 DoodleAndroidWindow 消化掉了，
     * 到这里的是需要动 Vulkan 子系统的那些
     */
    void OnAppCommand(android_app& app, DoodleAndroidContext& context, const int32_t command)
    {
        switch (command)
        {
        case APP_CMD_INIT_WINDOW:
            {
                //窗口到手。此刻 app.window 一定可用 —— 胶水层是在调用本回调之前
                //就把它写进去的（见 android_native_app_glue.c 的 pre_exec_cmd）
                if (app.window == nullptr || context.pWindow == nullptr)
                {
                    break;
                }

                try
                {
                    if (context.initialized)
                    {
                        //切后台回来：实例与设备都还在，只需把依附于表面的那几层重建
                        context.vulkan.Resume(*context.pWindow);
                    }
                    else
                    {
                        //第一次拿到窗口，从头起
                        context.vulkan.Initialize(*context.pWindow);
                        context.initialized = true;
                    }
                }
                catch (const std::exception& e)
                {
                    Log::Error(e.what());
                    context.failed = true;
                }
                break;
            }

        case APP_CMD_TERM_WINDOW:
            //窗口要没了。注意此刻 app.window 还指着旧窗口 —— 胶水层是在本回调
            //返回之后才把它置空的（post_exec_cmd）。所以销毁表面的正确时机就在
            //这里：早了窗口还在，晚了就拿着一个已经作废的句柄去销毁
            if (context.initialized)
            {
                try
                {
                    context.vulkan.Suspend();
                }
                catch (const std::exception& e)
                {
                    Log::Error(e.what());
                    context.failed = true;
                }
            }
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            //尺寸或屏幕配置变了。这里只记标志，重建留给帧循环 ——
            //旋转时系统会甩出一串事件，逐个处理会把交换链重建到卡死，
            //这个理由和桌面 resize 回调那边是同一个
            if (context.initialized)
            {
                context.vulkan.NotifyWindowResized();
            }
            break;

        default:
            break;
        }
    }
}

/**
 * Android 侧的进程入口
 *
 * 不是 main。系统 fork 出进程后调 ANativeActivity_onCreate（在 native_app_glue
 * 里），胶水层开一个线程、架好 Looper 与事件队列，最后回调到这里。
 * 所以本函数返回 = 那个线程结束 = 进程退出
 *
 * 与桌面 main 的差别集中在循环里：那边可以简单地「有窗口就一直画」，
 * 这边必须让出 CPU（见 DoodleAndroidWindow::PollEvents），
 * 而且窗口会消失又回来，得靠 APP_CMD_INIT_WINDOW / TERM_WINDOW 驱动
 * Vulkan 子系统的拆建
 */
void android_main(struct android_app* pApp)
{
    DoodleAndroidContext context;

    //构造时就接管了 pApp 的回调，之后所有 APP_CMD_* 都先经这里转发
    DoodleAndroidWindow window(pApp, [&context, pApp](const int32_t command)
    {
        OnAppCommand(*pApp, context, command);
    });
    context.pWindow = &window;

    Log::Info("Doodle: android_main entered");

    try
    {
        while (!window.ShouldClose() && !context.failed)
        {
            window.PollEvents();

            //窗口不在或不在前台时什么都不做。PollEvents 那边会阻塞等事件，
            //所以这里不是忙等
            if (window.IsRenderable())
            {
                context.vulkan.DrawFrame();
            }
        }
    }
    catch (const std::exception& e)
    {
        Log::Error(e.what());
        context.failed = true;
    }

    if (context.initialized)
    {
        context.vulkan.Destroy();
        context.initialized = false;
    }

    //出错时别把一个半死的状态留在屏上：请系统把本 Activity 关掉，
    //用户看到的是「应用退出了」，比黑屏挂着好定位得多
    if (context.failed && pApp->activity != nullptr)
    {
        ANativeActivity_finish(pApp->activity);
    }

    Log::Info("Doodle: android_main leaving");
}
