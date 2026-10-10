//
//  DoodleAndroidWindow.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include "DoodleAndroidWindow.h"

#include <utility>

#include <android_native_app_glue.h>

#include <vulkan/vulkan.h>

namespace Doodle
{
    DoodleAndroidWindow::DoodleAndroidWindow(android_app* pApp, AppCommandHandler appCommandHandler)
        : m_pApp(pApp)
        , m_appCommandHandler(std::move(appCommandHandler))
    {
        //接管事件回调。userData 归本类所有，理由见头文件
        m_pApp->userData = this;
        m_pApp->onAppCmd = &DoodleAndroidWindow::HandleAppCmd;
    }

    bool DoodleAndroidWindow::IsRenderable() const
    {
        return m_pApp->window != nullptr && m_focused;
    }

    bool DoodleAndroidWindow::IsCloseRequested() const
    {
        return m_pApp->destroyRequested != 0;
    }

    void DoodleAndroidWindow::PumpEvents()
    {
        int32_t events = 0;
        android_poll_source* pSource = nullptr;

        //超时值每次迭代都要重算，不能提到循环外。
        //
        //ALooper_pollOnce 处理完一个事件就返回，此后状态可能已经变了 ——
        //最典型的是窗口刚回来（INIT_WINDOW 让 IsRenderable 由假变真）。
        //那时必须改用非阻塞把剩下的事件排干，然后回到帧循环去绘制；
        //若还沿用循环外算好的 -1，会继续守着等下一个事件，这一帧就轮不到了
        //
        //没有可画的东西时用 -1 阻塞等待，这不是偷懒：那段期间本就无事可做，
        //空转轮询既烧电，也会让系统判定本进程在忙而把它杀掉。
        //还有一条硬约束 —— Activity 那边换窗口时会 pthread_cond_wait 等我们
        //处理完命令，所以我们必须持续回到 Looper 上，否则那边会死等
        while (ALooper_pollOnce(IsRenderable() ? 0 : -1, nullptr, &events,
                                reinterpret_cast<void**>(&pSource)) >= 0)
        {
            if (pSource != nullptr)
            {
                pSource->process(m_pApp, pSource);
            }

            if (m_pApp->destroyRequested != 0)
            {
                return;
            }
        }
    }

    void DoodleAndroidWindow::WaitUntilDrawable()
    {
        //空实现，而且应该是空的。
        //
        //桌面那份实现要阻塞等待，是因为最小化时窗口仍在、只是像素尺寸变成
        //0×0，交换链建不出来。Android 没有这个中间态：窗口没了就是
        //ANativeWindow 直接消失，走的是驱动层的 Suspend 路径，
        //根本不会带着一个 0×0 的窗口走到这里来
    }

    VkExtent2D DoodleAndroidWindow::GetSurfaceSize() const
    {
        //前置条件：此刻有窗口。调用方只会在已经有交换链的情况下问尺寸，
        //而交换链存在就意味着窗口在 —— 窗口没了的时候那一层已经被 Suspend 拆掉了
        return
        {
            .width = static_cast<uint32_t>(ANativeWindow_getWidth(m_pApp->window)),
            .height = static_cast<uint32_t>(ANativeWindow_getHeight(m_pApp->window))
        };
    }

    std::vector<const char*> DoodleAndroidWindow::GetSurfaceExtensions() const
    {
        return
        {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        };
    }

    VkResult DoodleAndroidWindow::CreateSurface(const VkInstance instance, VkSurfaceKHR* pSurface) const
    {
        VkAndroidSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        //取当下的那个窗口。Suspend 之后又 Resume 时，这里拿到的是新窗口 ——
        //本对象一直没换，换的是它底下的 window 指针
        createInfo.window = m_pApp->window;

        //Android 上 libvulkan.so 是系统库，这个入口直接可用，
        //不需要像桌面那样通过 vkGetInstanceProcAddr 取
        return vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, pSurface);
    }

    void DoodleAndroidWindow::HandleAppCmd(android_app* pApp, const int32_t command)
    {
        const auto pWindow = static_cast<DoodleAndroidWindow*>(pApp->userData);
        if (pWindow != nullptr)
        {
            pWindow->OnAppCmd(command);
        }
    }

    void DoodleAndroidWindow::OnAppCmd(const int32_t command)
    {
        switch (command)
        {
        case APP_CMD_GAINED_FOCUS:
            m_focused = true;
            break;

        case APP_CMD_LOST_FOCUS:
            m_focused = false;
            break;

        default:
            break;
        }

        //其余命令交给驱动层。窗口的来去（INIT_WINDOW / TERM_WINDOW）本层不做处理：
        //那要连带拆建交换链、帧缓冲和同步对象，是 Vulkan 子系统的事，
        //本层只该管窗口自己的状态
        if (m_appCommandHandler)
        {
            m_appCommandHandler(command);
        }
    }
}
