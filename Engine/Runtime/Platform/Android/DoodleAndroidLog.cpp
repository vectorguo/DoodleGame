//
//  DoodleAndroidLog.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/16.
//

#include "../DoodleLog.h"

#include <android/log.h>

namespace
{
    //logcat 里用 `adb logcat Doodle:V *:S` 只看本程序
    constexpr const char* kLogTag = "Doodle";
}

namespace Doodle
{
    void Log::Error(const char* message)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
    }

    void Log::Info(const char* message)
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
    }
}
