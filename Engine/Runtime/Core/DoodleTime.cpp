//
//  DoodleTime.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#include "DoodleTime.h"

#include <algorithm>
#include <stdexcept>

namespace Doodle
{
    std::chrono::steady_clock::time_point DoodleTime::s_lastFrameTime{};
    float DoodleTime::s_deltaTime = 0.0f;
    float DoodleTime::s_unscaledDeltaTime = 0.0f;
    float DoodleTime::s_time = 0.0f;
    float DoodleTime::s_unscaledTime = 0.0f;
    int DoodleTime::s_frameCount = 0;
    float DoodleTime::s_timeScale = 1.0f;
    float DoodleTime::s_maximumDeltaTime = 0.1f;

    float DoodleTime::GetDeltaTime()
    {
        return s_deltaTime;
    }

    float DoodleTime::GetUnscaledDeltaTime()
    {
        return s_unscaledDeltaTime;
    }

    float DoodleTime::GetTime()
    {
        return s_time;
    }

    float DoodleTime::GetUnscaledTime()
    {
        return s_unscaledTime;
    }

    int DoodleTime::GetFrameCount()
    {
        return s_frameCount;
    }

    float DoodleTime::GetTimeScale()
    {
        return s_timeScale;
    }

    void DoodleTime::SetTimeScale(const float timeScale)
    {
        // 写成 !(>=) 而不是 <：NaN 与任何数比较都是 false，这样也能一并拦下，
        // 不让它一路污染累计时间
        if (!(timeScale >= 0.0f))
        {
            throw std::invalid_argument("DoodleTime::SetTimeScale: time scale must not be negative");
        }

        s_timeScale = timeScale;
    }

    float DoodleTime::GetMaximumDeltaTime()
    {
        return s_maximumDeltaTime;
    }

    void DoodleTime::SetMaximumDeltaTime(const float maximumDeltaTime)
    {
        // 同上，!(>) 同时拦下 0、负数与 NaN
        if (!(maximumDeltaTime > 0.0f))
        {
            throw std::invalid_argument("DoodleTime::SetMaximumDeltaTime: maximum delta time must be positive");
        }

        s_maximumDeltaTime = maximumDeltaTime;
    }

    void DoodleTime::Update()
    {
        const auto now = std::chrono::steady_clock::now();
        const float rawDeltaSeconds = std::chrono::duration<float>(now - s_lastFrameTime).count();
        s_lastFrameTime = now;

        // 真实时间不钳也不缩放：它就是两帧之间实际过了多久。
        // 切后台再回来时，那段间隔会一次性计进来
        s_unscaledDeltaTime = rawDeltaSeconds;
        s_unscaledTime += rawDeltaSeconds;

        // 玩法时间相反：先按上限钳住真实时长，再乘倍率。顺序不能反 ——
        // maximumDeltaTime 约束的是「一帧最多算多久」，加速时它不该被倍率放大
        s_deltaTime = std::min(rawDeltaSeconds, s_maximumDeltaTime) * s_timeScale;
        s_time += s_deltaTime;

        // 帧序号在帧末才加一：本帧内读到的都是本帧自己的序号，第一帧为 0
        ++s_frameCount;
    }

    void DoodleTime::Reset()
    {
        s_lastFrameTime = std::chrono::steady_clock::now();

        s_deltaTime = 0.0f;
        s_unscaledDeltaTime = 0.0f;
        s_time = 0.0f;
        s_unscaledTime = 0.0f;
        s_frameCount = 0;
    }
}
