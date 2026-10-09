//
//  DoodleApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleApplication::Initialize()
    {
        //窗口先于 Vulkan：表面建在窗口上
        m_pWindow = CreateWindow();

        //子类把窗口交出来是它的分内事，理论上不可能为空。走不到的分支也要拦：
        //不拦的话，Run 里第一次解引用就是一段和根因对不上的崩溃
        if (m_pWindow == nullptr)
        {
            throw std::runtime_error("DoodleApplication: window is null");
        }

        InitializeVulkan();
    }

    void DoodleApplication::Run()
    {
        while (!m_pWindow->ShouldClose())
        {
            m_pWindow->PollEvents();
            m_vulkanManager.DrawFrame();
        }
    }

    void DoodleApplication::Destroy()
    {
        //先 Vulkan 后窗口，与 Initialize 反序：交换链、表面都依附于窗口，
        //窗口先没的话，销毁它们就是拿着一个已经作废的句柄去用
        DestroyVulkan();
        m_pWindow.reset();
    }

    void DoodleApplication::NotifyWindowResized()
    {
        m_vulkanManager.NotifyWindowResized();
    }

    void DoodleApplication::InitializeVulkan()
    {
        //传给 Vulkan 各层的是引用：它们只借不放，窗口的持有者仍在本类
        m_vulkanManager.Initialize(*m_pWindow);
    }

    void DoodleApplication::DestroyVulkan()
    {
        m_vulkanManager.Destroy();
    }
}
