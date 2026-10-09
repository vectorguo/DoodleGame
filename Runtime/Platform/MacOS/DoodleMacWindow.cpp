//
//  DoodleMacWindow.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include "DoodleMacWindow.h"

#include <stdexcept>
#include <utility>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Doodle
{
    DoodleMacWindow::DoodleMacWindow(const int32_t width, const int32_t height, const char* title,
                                     ResizeHandler resizeHandler)
        : m_resizeHandler(std::move(resizeHandler))
    {
        glfwInit();

        //不要 OpenGL 上下文：本工程只走 Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        //可缩放。窗口尺寸变化后交换链就不再与 surface 匹配，
        //需要在帧循环里重建 —— 这个标志是那条路径的触发来源
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_pGlfwWindow = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (m_pGlfwWindow == nullptr)
        {
            throw std::runtime_error("failed to create glfw window");
        }

        //把 this 存进窗口，供静态回调取回（GLFW 是 C 库，回调没有 this 可用）
        glfwSetWindowUserPointer(m_pGlfwWindow, this);
        glfwSetFramebufferSizeCallback(m_pGlfwWindow, FramebufferSizeCallback);
    }

    DoodleMacWindow::~DoodleMacWindow()
    {
        if (m_pGlfwWindow != nullptr)
        {
            glfwDestroyWindow(m_pGlfwWindow);
            m_pGlfwWindow = nullptr;
        }
        glfwTerminate();
    }

    void DoodleMacWindow::FramebufferSizeCallback(GLFWwindow* pGlfwWindow, int width, int height)
    {
        //尺寸参数不在这里用：真正的尺寸等重建交换链时现查。
        //这里只需要记下「变过了」，把处理留给帧循环 —— 拖动窗口边缘时系统会甩出
        //一连串 resize 事件，逐个处理会把交换链重建到卡死
        const auto pWindow = static_cast<DoodleMacWindow*>(glfwGetWindowUserPointer(pGlfwWindow));
        if (pWindow != nullptr && pWindow->m_resizeHandler)
        {
            pWindow->m_resizeHandler();
        }
    }

    bool DoodleMacWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(m_pGlfwWindow) != 0;
    }

    void DoodleMacWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void DoodleMacWindow::WaitForValidFramebufferSize()
    {
        //先查一次是为了应付「尺寸本来就正常」这个常见情况：否则会先白白阻塞在
        //glfwWaitEvents 上，等一个根本没来的事件，看起来就像随机卡了一下。
        //循环里先等后查：唤醒之后立刻用最新尺寸判断，尺寸一合法就退出，
        //不需要再多来一个事件把循环顶出去
        int32_t width = 0;
        int32_t height = 0;
        glfwGetFramebufferSize(m_pGlfwWindow, &width, &height);
        while ((width == 0 || height == 0) && glfwWindowShouldClose(m_pGlfwWindow) == 0)
        {
            glfwWaitEvents();
            glfwGetFramebufferSize(m_pGlfwWindow, &width, &height);
        }
    }

    void DoodleMacWindow::GetFramebufferSize(int32_t& width, int32_t& height) const
    {
        glfwGetFramebufferSize(m_pGlfwWindow, &width, &height);
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
        return glfwCreateWindowSurface(instance, m_pGlfwWindow, nullptr, pSurface);
    }
}
