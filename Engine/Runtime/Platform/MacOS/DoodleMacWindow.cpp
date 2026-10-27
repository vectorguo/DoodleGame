//
//  DoodleMacWindow.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include "DoodleMacWindow.h"
#include "../DoodleApplication.h"

#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Doodle
{
    DoodleMacWindow::DoodleMacWindow(const int32_t width, const int32_t height, const char* title)
    {
        glfwInit();

        //不要 OpenGL 上下文：本工程只走 Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        //可缩放。窗口尺寸变化后交换链就不再与 surface 匹配，
        //需要在帧循环里重建 —— 这个标志是那条路径的触发来源
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        //创建窗口
        m_pWindow = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (m_pWindow == nullptr)
        {
            throw std::runtime_error("failed to create glfw window");
        }

        //尺寸回调注册在具体窗口上，回调里不需要再回过头认窗 ——
        //所以窗口用户指针没有使用者，不必往里存 this
        glfwSetFramebufferSizeCallback(m_pWindow, FramebufferSizeCallback);
    }

    DoodleMacWindow::~DoodleMacWindow()
    {
        if (m_pWindow != nullptr)
        {
            glfwDestroyWindow(m_pWindow);
            m_pWindow = nullptr;
        }
        glfwTerminate();
    }

    void DoodleMacWindow::FramebufferSizeCallback(GLFWwindow* pGlfwWindow, int width, int height)
    {
        //尺寸参数不在这里用：真正的尺寸等重建交换链时现查。
        //这里只需要让驱动层记下「变过了」，把处理留给帧循环 —— 拖动窗口边缘时系统会甩出
        //一连串 resize 事件，逐个处理会把交换链重建到卡死
        //
        //取实例不判空：回调只可能在泵事件时触发，而泵事件只发生在驱动层跑起来之后，
        //此刻实例必然有效。契约写在 DoodleApplication::GetInstance 那边
        DoodleApplication::GetInstance().NotifyWindowResized();
    }

    void DoodleMacWindow::PumpEvents()
    {
        glfwPollEvents();

        //失焦：清空输入并释放捕获。Cmd+Tab 走的就是这条路，
        //少了它，回来时按键会卡在按下状态、光标也不会恢复
        if (glfwGetWindowAttrib(m_pWindow, GLFW_FOCUSED) == 0)
        {
            if (m_cursorCaptured)
            {
                SetCursorCaptured(false);
            }

            m_input = {};
            return;
        }

        //键位：逐帧轮询，正好对应「本帧快照」。GLFW 的键位是逻辑键，
        //非 QWERTY 布局下与 Win32 的 VK 映射会有物理位置差异，
        //要按物理位置统一时再引入 scancode
        m_input.SetKey(DoodleKey::W, glfwGetKey(m_pWindow, GLFW_KEY_W) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::A, glfwGetKey(m_pWindow, GLFW_KEY_A) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::S, glfwGetKey(m_pWindow, GLFW_KEY_S) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::D, glfwGetKey(m_pWindow, GLFW_KEY_D) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::Q, glfwGetKey(m_pWindow, GLFW_KEY_Q) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::E, glfwGetKey(m_pWindow, GLFW_KEY_E) == GLFW_PRESS);
        m_input.SetKey(DoodleKey::LeftShift, glfwGetKey(m_pWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

        m_input.SetMouseButton(DoodleMouseButton::Left,
                               glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        m_input.SetMouseButton(DoodleMouseButton::Right,
                               glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        m_input.SetMouseButton(DoodleMouseButton::Middle,
                               glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);

        //光标位移只在捕获时累计。GLFW_CURSOR_DISABLED 下坐标是虚拟的、
        //可以越过窗口边界，所以能一直转下去
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(m_pWindow, &cursorX, &cursorY);

        if (m_cursorCaptured)
        {
            m_input.mouseDeltaX = static_cast<float>(cursorX - m_lastCursorX);
            m_input.mouseDeltaY = static_cast<float>(cursorY - m_lastCursorY);
        }
        else
        {
            m_input.mouseDeltaX = 0.0f;
            m_input.mouseDeltaY = 0.0f;
        }

        m_lastCursorX = cursorX;
        m_lastCursorY = cursorY;
        m_input.cursorCaptured = m_cursorCaptured;
    }

    DoodleInput::RawState DoodleMacWindow::GetRawInputState() const
    {
        return m_input;
    }

    void DoodleMacWindow::SetCursorCaptured(const bool captured)
    {
        if (captured == m_cursorCaptured)
        {
            return;
        }

        m_cursorCaptured = captured;
        glfwSetInputMode(m_pWindow, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        //切换时以当前光标为新基准：不重置的话，第一次按下右键那一帧会按
        //「光标从上一个位置跳过来」算出一大段位移，视角猛甩
        glfwGetCursorPos(m_pWindow, &m_lastCursorX, &m_lastCursorY);
        m_input.mouseDeltaX = 0.0f;
        m_input.mouseDeltaY = 0.0f;
        m_input.cursorCaptured = captured;
    }

    void DoodleMacWindow::WaitUntilDrawable()
    {
        //先查一次是为了应付「尺寸本来就正常」这个常见情况：glfwWaitEvents 要等
        //到有新事件才返回，而尺寸合法时未必有事件在途 —— 直接进循环的话会在这里
        //挂到下一次输入（点击、resize）才醒，本来该立刻重建的却等了半天
        //
        //循环里先等后查：唤醒之后立刻用最新尺寸判断，尺寸一合法就退出，
        //不需要再多来一个事件把循环顶出去。
        //关闭请求也在循环条件里查 —— 关窗之后尺寸可能仍是 0×0，
        //少了这一条会在这里死等
        int32_t width = 0;
        int32_t height = 0;
        glfwGetFramebufferSize(m_pWindow, &width, &height);
        while (width == 0 || height == 0)
        {
            if (IsCloseRequested())
            {
                break;
            }
            glfwWaitEvents();
            glfwGetFramebufferSize(m_pWindow, &width, &height);
        }
    }

    bool DoodleMacWindow::IsCloseRequested() const
    {
        return glfwWindowShouldClose(m_pWindow) != 0;
    }

    VkExtent2D DoodleMacWindow::GetSurfaceSize() const
    {
        //GLFW 只提供出参形状，转换就留在这一层：最小化时像素尺寸是 0×0，
        //不会是负数，转 uint32_t 不丢信息
        int32_t width = 0;
        int32_t height = 0;
        glfwGetFramebufferSize(m_pWindow, &width, &height);

        return
        {
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height)
        };
    }

    std::vector<const char*> DoodleMacWindow::GetSurfaceExtensions() const
    {
        uint32_t glfwExtensionCount = 0;
        const auto** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions;
        extensions.reserve(glfwExtensionCount);
        for (uint32_t i = 0; i < glfwExtensionCount; ++i)
        {
            extensions.emplace_back(glfwExtensions[i]);
        }
        return extensions;
    }

    VkResult DoodleMacWindow::CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const
    {
        return glfwCreateWindowSurface(instance, m_pWindow, nullptr, pSurface);
    }
}
