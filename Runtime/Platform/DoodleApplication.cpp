//
//  DoodleApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//

#include "DoodleApplication.h"

#include <stdexcept>
#include <utility>

namespace Doodle
{
    void DoodleApplication::Initialize(std::unique_ptr<DoodleWindow> window)
    {
        //入口那边先建窗口再交进来，理论上不可能为空。走不到的分支也要拦：
        //不拦的话，Run 里第一次解引用就是一段和根因对不上的崩溃
        if (window == nullptr)
        {
            throw std::runtime_error("DoodleApplication: window is null");
        }

        m_pWindow = std::move(window);

        //窗口只借不放，本层不负责它的创建与销毁
        m_vulkanManager.Initialize(*m_pWindow);
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
        //窗口先没的话销毁它们就是拿着一个已经作废的句柄去用
        m_vulkanManager.Destroy();
        m_pWindow.reset();
    }

    void DoodleApplication::NotifyWindowResized()
    {
        m_vulkanManager.NotifyWindowResized();
    }
}
