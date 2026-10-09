//
//  DoodleMacOSMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

#include "../DoodleApplication.h"
#include "DoodleMacOSWindow.h"

using namespace Doodle;

namespace
{
    //初始窗口尺寸（像素）与标题。放在入口而不是驱动层：它们是「这次启动要开个
    //多大的窗」的参数，不是驱动逻辑的一部分。每个桌面平台的入口各有一份
    constexpr int32_t kWindowWidth = 1280;
    constexpr int32_t kWindowHeight = 800;
    constexpr const char* kWindowTitle = "Doodle";
}

/**
 * macOS 侧的进程入口
 *
 * 与 Android 那份（Runtime/Platform/Android/DoodleAndroidMain.cpp）的差别：
 * 那边没有 main，入口是胶水层回调过来的 android_main，而且窗口会消失又回来，
 * 驱动逻辑只能自己写；这边是普通 main，窗口与程序同寿，
 * 「建窗口」之外的事全交给 DoodleApplication
 */
int main()
{
    try
    {
        DoodleApplication app;

        //本平台唯一要自己做的事：把窗口建出来。建完立刻交出去 ——
        //中间不要跑事件循环，理由见 DoodleApplication::Initialize 的注释
        auto window = std::make_unique<DoodleMacOSWindow>(
            kWindowWidth, kWindowHeight, kWindowTitle,
            [&app] { app.NotifyWindowResized(); });

        app.Initialize(std::move(window));
        app.Run();
        app.Destroy();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
