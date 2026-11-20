//
//  DoodleLog.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

namespace Doodle
{
    /**
     * 平台日志出口
     *
     * 存在的理由只有一个：不是每个平台都能把 std::cerr 交给用户看。Android 上没有
     * stderr，std::cerr 写进去的东西不进 logcat、也没人接，等于消失；Windows 上
     * DoodleGame.exe 链成 WINDOWS 子系统，没有控制台，std::cerr 同样没人接。
     * 而本工程的错误报告几乎全靠异常消息，那些消息正是出问题时最需要看见的东西 ——
     * 没有这层，看到的就是一片黑屏加零线索。
     *
     * 本头文件只定接口，实现按平台分开，与窗口、文件来源同一规矩
     * （见 CMakeLists.txt 里那三摞平台源码）：
     *   MacOS  / DoodleMacLog.cpp       std::cerr / std::cout，落在启动它的终端里
     *   Windows/ DoodleWindowsLog.cpp   控制台（如果从 cmd 启动）、exe 同目录的
     *                                   DoodleGame.log、调试器输出，三条都写
     *   Android/ DoodleAndroidLog.cpp   __android_log_print，在 logcat 里按 tag 过滤
     *
     * 纯静态类，不可实例化：没有状态、没有 Initialize / Destroy，
     * 级别就是几个静态入口，类在这里只给级别一个归属 —— 不进 Singleton，
     * 也不挂到任何一层上（同 DoodleShaderModule 的理由）
     *
     * 不做成宏、不做格式化：现在只有「把一条已经拼好的消息送出去」这一个需求
     */
    class Log
    {
    public:
        // 纯静态工具类，不允许实例化
        Log() = delete;

        /** 错误级 */
        static void Error(const char* message);

        /** 信息级 */
        static void Info(const char* message);
    };
}
