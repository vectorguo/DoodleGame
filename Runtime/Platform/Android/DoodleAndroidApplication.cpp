//
//  DoodleAndroidApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/10.
//

#include "DoodleAndroidApplication.h"

#include <exception>
#include <memory>

#include <android_native_app_glue.h>

#include "DoodleAndroidWindow.h"
#include "../DoodleLog.h"

namespace Doodle
{
    DoodleAndroidApplication::DoodleAndroidApplication(android_app* pApp)
        : m_pApp(pApp)
    {
    }

    std::unique_ptr<DoodleWindow> DoodleAndroidApplication::CreateWindow()
    {
        //把 *this 交给窗口：APP_CMD_* 由此直达 OnAppCommand。
        //走注入而不是 DoodleApplication::GetInstance()，理由见 DoodleAndroidWindow.h
        return std::make_unique<DoodleAndroidWindow>(m_pApp, *this);
    }

    void DoodleAndroidApplication::Initialize()
    {
        //只建窗口对象，不起 Vulkan —— 基类那份默认实现是「建窗即起 Vulkan」，
        //本平台覆盖它正是因为这条顺序不成立：此刻 app.window 还是空的，
        //要等 APP_CMD_INIT_WINDOW
        //
        //窗口本身必须现在就建：它在构造里接管 pApp->onAppCmd，
        //不先建出来的话，之后的命令进不到本类
        m_pWindow = CreateWindow();
    }

    void DoodleAndroidApplication::Run()
    {
        //取回具体类型：IsRenderable 是 Android 窗口特有的，不在 DoodleWindow 接口上。
        //窗口是本类自己刚建出来的，转换安全；所有权仍在 m_pWindow 那一处，
        //这里只是借一个更具体的视角
        const auto pWindow = static_cast<DoodleAndroidWindow*>(m_pWindow.get());

        try
        {
            while (!pWindow->IsCloseRequested() && !m_failed)
            {
                pWindow->PumpEvents();

                //窗口不在或不在前台时什么都不做。PumpEvents 那边会阻塞等事件，
                //所以这里不是忙等
                if (pWindow->IsRenderable())
                {
                    m_vulkanManager.DrawFrame();
                }
            }
        }
        catch (const std::exception& e)
        {
            //帧循环里出错就跑不下去了。记下来交给入口函数收尾 ——
            //本函数不往外抛，那边才能稳定地走到 Destroy 与「请系统关掉 Activity」
            Log::Error(e.what());
            m_failed = true;
        }
    }

    void DoodleAndroidApplication::Destroy()
    {
        //可能一次都没起来过：窗口都没等到就退出了。那时去拆是拿着一个
        //不存在的实例去用
        if (m_initialized)
        {
            DestroyVulkan();
            m_initialized = false;
        }

        m_pWindow.reset();
    }

    bool DoodleAndroidApplication::HasFailed() const
    {
        return m_failed;
    }

    void DoodleAndroidApplication::OnAppCommand(const int32_t command)
    {
        switch (command)
        {
        case APP_CMD_INIT_WINDOW:
            {
                //窗口到手。此刻 app.window 一定可用 —— 胶水层是在调用本回调之前
                //就把它写进去的（见 android_native_app_glue.c 的 pre_exec_cmd）
                if (m_pApp->window == nullptr || m_pWindow == nullptr)
                {
                    break;
                }

                try
                {
                    if (m_initialized)
                    {
                        //切后台回来：实例与设备都还在，只需把依附于表面的那几层重建
                        m_vulkanManager.Resume(*m_pWindow);
                    }
                    else
                    {
                        //第一次拿到窗口，从头起
                        InitializeVulkan();
                        m_initialized = true;
                    }
                }
                catch (const std::exception& e)
                {
                    Log::Error(e.what());
                    m_failed = true;
                }
                break;
            }

        case APP_CMD_TERM_WINDOW:
            //窗口要没了。注意此刻 app.window 还指着旧窗口 —— 胶水层是在本回调
            //返回之后才把它置空的（post_exec_cmd）。所以销毁表面的正确时机就在
            //这里：早了窗口还在，晚了就拿着一个已经作废的句柄去销毁
            if (m_initialized)
            {
                try
                {
                    m_vulkanManager.Suspend();
                }
                catch (const std::exception& e)
                {
                    Log::Error(e.what());
                    m_failed = true;
                }
            }
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            //尺寸或屏幕配置变了。这里只记标志，重建留给帧循环 ——
            //旋转时系统会甩出一串事件，逐个处理会把交换链重建到卡死，
            //这个理由和桌面 resize 回调那边是同一个
            if (m_initialized)
            {
                NotifyWindowResized();
            }
            break;

        default:
            break;
        }
    }
}
