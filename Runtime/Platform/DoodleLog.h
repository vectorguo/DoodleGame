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
     * 存在的理由只有一个：Android 上没有 stderr。std::cerr 写进去的东西
     * 不进 logcat、也没人接，等于消失。而本工程的错误报告几乎全靠异常消息，
     * 那些消息在第一版 Android 构建里正是最需要看见的东西 ——
     * 没有这层，出问题时看到的就是一片黑屏加零线索。
     *
     * 桌面：照常走 std::cerr / std::cout
     * Android：走 __android_log_print，在 logcat 里按 tag 过滤
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

        /** 错误级。桌面走 std::cerr，Android 走 ANDROID_LOG_ERROR */
        static void Error(const char* message);

        /** 信息级。桌面上走 std::cout，Android 上 tag 相同、级别较低 */
        static void Info(const char* message);
    };
}
