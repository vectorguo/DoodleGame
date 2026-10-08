//
//  DoodleManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"
#include "Vulkan/DoodleVulkanManager.h"

namespace Doodle
{
    /**
     * DoodleApplication构造函数
     */
    DoodleApplication::DoodleApplication() :
        m_pWindow(nullptr),
        m_windowWidth(1280),
        m_windowHeight(800)
    {
    }

    /**
     * DoodleApplication的初始化
     */
    void DoodleApplication::Initialize()
    {
        InitWindow();
        InitVulkan();
    }

    /**
     * DoodleApplication的运行
     */
    void DoodleApplication::Run()
    {
        while (!glfwWindowShouldClose(m_pWindow))
        {
            glfwPollEvents();
            m_vulkanManager.DrawFrame();
        }
    }

    /**
     * DoodleApplication销毁
     */
    void DoodleApplication::Destroy()
    {
        DestroyVulkan();
        DestroyWindow();
    }

    /**
     * 初始化Window
     */
    void DoodleApplication::InitWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        //可缩放。窗口尺寸变化后交换链就不再与 surface 匹配，
        //需要在帧循环里重建 —— 这个标志是那条路径的触发来源
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_pWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "Doodle", nullptr, nullptr);

        //把 this 存进窗口，供静态回调取回（GLFW 是 C 库，回调没有 this 可用）
        glfwSetWindowUserPointer(m_pWindow, this);
        glfwSetFramebufferSizeCallback(m_pWindow, WindowResizeCallback);
    }

    /**
     * 窗口尺寸变化回调
     */
    void DoodleApplication::WindowResizeCallback(GLFWwindow* pWindow, int width, int height)
    {
        //尺寸参数不在这里用：真正的尺寸等重建交换链时现查。
        //这里只需要记下「变过了」，把处理留给帧循环 —— 拖动窗口边缘时系统会甩出
        //一连串 resize 事件，逐个处理会把交换链重建到卡死
        const auto pApplication = static_cast<DoodleApplication*>(glfwGetWindowUserPointer(pWindow));
        pApplication->m_vulkanManager.NotifyWindowResized();
    }

    /**
     * 销毁Window
     */
    void DoodleApplication::DestroyWindow()
    {
        glfwDestroyWindow(m_pWindow);
        glfwTerminate();

        //清空字段数据
        m_pWindow = nullptr;
    }

    /**
     * 初始化Vulkan
     */
    void DoodleApplication::InitVulkan()
    {
        m_vulkanManager.Initialize(m_pWindow);
    }

    /**
     * 销毁Vulkan
     */
    void DoodleApplication::DestroyVulkan()
    {
        m_vulkanManager.Destroy();
    }
}
