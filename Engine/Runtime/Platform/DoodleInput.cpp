//
//  DoodleInput.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleInput.h"

#include <stdexcept>

namespace Doodle
{
    DoodleInput::RawState DoodleInput::s_current{};
    DoodleInput::RawState DoodleInput::s_previous{};

    bool DoodleInput::GetKey(const DoodleKey key)
    {
        return s_current.IsKeyDown(key);
    }

    bool DoodleInput::GetKeyDown(const DoodleKey key)
    {
        return s_current.IsKeyDown(key) && !s_previous.IsKeyDown(key);
    }

    bool DoodleInput::GetKeyUp(const DoodleKey key)
    {
        return !s_current.IsKeyDown(key) && s_previous.IsKeyDown(key);
    }

    bool DoodleInput::GetMouseButton(const DoodleMouseButton button)
    {
        return s_current.IsMouseButtonDown(button);
    }

    bool DoodleInput::GetMouseButtonDown(const DoodleMouseButton button)
    {
        return s_current.IsMouseButtonDown(button) && !s_previous.IsMouseButtonDown(button);
    }

    bool DoodleInput::GetMouseButtonUp(const DoodleMouseButton button)
    {
        return !s_current.IsMouseButtonDown(button) && s_previous.IsMouseButtonDown(button);
    }

    glm::vec2 DoodleInput::GetMouseDelta()
    {
        return {s_current.mouseDeltaX, s_current.mouseDeltaY};
    }

    bool DoodleInput::IsCursorCaptured()
    {
        return s_current.cursorCaptured;
    }

    int DoodleInput::GetTouchCount()
    {
        return s_current.touchCount;
    }

    DoodleTouch DoodleInput::GetTouch(const int index)
    {
        if (index < 0 || index >= s_current.touchCount)
        {
            throw std::out_of_range("DoodleInput::GetTouch: index out of range");
        }

        return s_current.touches[static_cast<std::size_t>(index)];
    }

    bool DoodleInput::IsTouchSupported()
    {
        return s_current.touchSupported;
    }

    void DoodleInput::Update(const RawState& rawState)
    {
        s_previous = s_current;
        s_current = rawState;

        //「只有捕获时才有位移」这条规则由输入层统一兜住：各平台窗口即使漏了清零，
        //游戏层也拿不到未捕获状态下的位移
        if (!s_current.cursorCaptured)
        {
            s_current.mouseDeltaX = 0.0f;
            s_current.mouseDeltaY = 0.0f;
        }
    }

    void DoodleInput::Reset()
    {
        s_current = {};
        s_previous = {};
    }
}
