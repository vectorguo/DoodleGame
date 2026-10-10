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

    void DoodleApplication::Initialize()
    {
        //登记得赶在建窗口之前：尺寸回调是在窗口的构造里挂上的，
        //顺序反过来就会有一段「窗口已经在收事件、却还问不到驱动层」的空档。
        //
        //放在函数最开头、与 Destroy 末尾的清空配成一对括号，区间取最宽 ——
        //不去依赖「初始化期间哪些调用会泵事件」这条会过期的推理：
        //眼下 Initialize 里确实一处都没泵（全树只有 PumpEvents 与 WaitUntilDrawable
        //会），但那是各层实现凑出来的巧合，不是本类写下的约定
        s_pInstance = this;

        //窗口先于 Vulkan：表面建在窗口上
        m_pWindow = CreateWindow();
        if (m_pWindow == nullptr)
        {
            //子类把窗口交出来是它的分内事，理论上不可能为空。走不到的分支也要拦：
            //不拦的话，Run 里第一次解引用就是一段和根因对不上的崩溃
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

        //清在最后，与 Initialize 开头的登记配成一对括号：Destroy 返回之后
        //窗口已经没了，也不会再有人泵事件，此时实例确实不该再被问到。
        //上面那两行若抛异常，指针会留在原地 —— 那种情况下进程已经不该继续，
        //硬清掉反而会把「还有一层没拆」这个事实一并抹去
        s_pInstance = nullptr;
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
