//
//  DoodleAndroidMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include <android_native_app_glue.h>

#include "DoodleAndroidApplication.h"
#include "../DoodleLog.h"

using namespace Doodle;

/**
 * Android 侧的进程入口
 *
 * 不是 main。系统 fork 出进程后调 ANativeActivity_onCreate（在 native_app_glue
 * 里），胶水层开一个线程、架好 Looper 与事件队列，最后回调到这里。
 * 所以本函数返回 = 那个线程结束 = 进程退出
 *
 * 形状与桌面那份（Runtime/Platform/MacOS/DoodleMacMain.cpp）现在是一样的：
 * 建对象、按顺序调 Initialize / Run / Destroy。差别在内容而不在骨架 ——
 * 窗口会消失又回来，得靠 APP_CMD_INIT_WINDOW / TERM_WINDOW 驱动 Vulkan 子系统的
 * 拆建，那套时刻表在 DoodleAndroidApplication 里（见那边的类注释）
 */
void android_main(struct android_app* pApp)
{
    DoodleAndroidApplication application(pApp);

    Log::Info("Doodle: android_main entered");

    application.Initialize();
    application.Run();
    application.Destroy();

    //出错时别把一个半死的状态留在屏上：请系统把本 Activity 关掉，
    //用户看到的是「应用退出了」，比黑屏挂着好定位得多
    if (application.HasFailed() && pApp->activity != nullptr)
    {
        ANativeActivity_finish(pApp->activity);
    }

    Log::Info("Doodle: android_main leaving");
}
