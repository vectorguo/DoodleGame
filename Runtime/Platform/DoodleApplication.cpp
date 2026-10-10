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
    DoodleApplication* DoodleApplication::s_pInstance = nullptr;

    DoodleApplication::DoodleApplication()
    {
        //登记在构造里：此刻窗口还不存在（它在 Initialize 里才建），
        //所以「窗口在收事件、却问不到应用对象」这个状态压根没有机会出现。
        //这与放在 Initialize 里的区别不只是早晚 —— 那条得靠「哪一层会泵事件」
        //的推理才成立，这条不用推理
        s_pInstance = this;
    }

    DoodleApplication::~DoodleApplication()
    {
        //先认一下是不是自己：万一同进程里先后有过两个应用对象，
        //先析构的那个不该把后一个的登记一起抹掉
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }
    }

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
        while (!m_pWindow->IsCloseRequested())
        {
            m_pWindow->PumpEvents();
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
