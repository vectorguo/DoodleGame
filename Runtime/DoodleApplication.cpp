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
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_pWindow = glfwCreateWindow(m_windowWidth, m_windowHeight, "Doodle", nullptr, nullptr);
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
        DoodleVulkanManager::Instance().Initialize(m_pWindow);
    }

    /**
     * 销毁Vulkan
     */
    void DoodleApplication::DestroyVulkan()
    {
        DoodleVulkanManager::Instance().Destroy();
    }
}
