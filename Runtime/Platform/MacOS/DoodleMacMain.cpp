//
//  DoodleMacMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include <cstdlib>
#include <exception>
#include <iostream>

#include "DoodleMacApplication.h"

using namespace Doodle;

/**
 * macOS 侧的进程入口
 *
 * 与 Android 那份（Runtime/Platform/Android/DoodleAndroidMain.cpp）的差别：
 * 那边没有 main，入口是胶水层回调过来的 android_main，而且窗口会消失又回来，
 * 驱动逻辑只能自己写；这边是普通 main，窗口与程序同寿，
 * 本函数只做三件事 —— 建对象、按顺序调 Initialize / Run / Destroy、
 * 把异常翻成退出码，连窗口都不用它建（见 DoodleMacApplication::CreateWindow）
 */
int main()
{
    try
    {
        DoodleMacApplication app;
        app.Initialize();
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
