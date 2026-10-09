//
//  DoodleLog.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include "DoodleLog.h"

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <iostream>
#endif

namespace Doodle
{
    namespace
    {
        //logcat 里用 `adb logcat Doodle:V *:S` 只看本程序
        constexpr const char* kLogTag = "Doodle";
    }

    void Log::Error(const char* message)
    {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
#else
        std::cerr << message << std::endl;
#endif
    }

    void Log::Info(const char* message)
    {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
#else
        std::cout << message << std::endl;
#endif
    }
}
