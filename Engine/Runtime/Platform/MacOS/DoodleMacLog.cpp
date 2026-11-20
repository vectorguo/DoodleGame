//
//  DoodleMacLog.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/16.
//

#include "../DoodleLog.h"

#include <iostream>

namespace Doodle
{
    /**
     * macOS 的日志出口就是标准错误 / 标准输出
     *
     * 从终端或 IDE 启动时落在启动它的那个窗口里；双击 .app 时没人接。
     * 这里没有像 Windows 那样再加一条文件出口：那边是因为 DoodleGame.exe
     * 链成 WINDOWS 子系统后完全没有控制台，这里开发时基本都是带着终端跑的
     */

    void Log::Error(const char* message)
    {
        std::cerr << message << std::endl;
    }

    void Log::Info(const char* message)
    {
        std::cout << message << std::endl;
    }
}
