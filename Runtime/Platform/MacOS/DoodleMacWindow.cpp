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

    VkResult DoodleMacWindow::CreateSurface(const VkInstance instance, VkSurfaceKHR* pSurface) const
    {
        return glfwCreateWindowSurface(instance, m_pWindow, nullptr, pSurface);
    }
}
