//
//  DoodleAndroidWindow.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#include "DoodleAndroidWindow.h"

#include <android/configuration.h>
#include <android/input.h>
#include <android_native_app_glue.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstddef>

#include "DoodleAndroidApplication.h"

namespace Doodle
{
    DoodleAndroidWindow::DoodleAndroidWindow(android_app* pApp, DoodleAndroidApplication& application)
        : m_pApp(pApp)
        , m_application(application)
    {
        //接管事件回调。userData 归本类所有，理由见头文件
        m_pApp->userData = this;
        m_pApp->onAppCmd = &DoodleAndroidWindow::HandleAppCmd;
        m_pApp->onInputEvent = &DoodleAndroidWindow::HandleInputEvent;

        m_lastTouchFrameTime = std::chrono::steady_clock::now();
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
        //每轮输入帧先算时间间隔，并把所有活动手指的阶段恢复成 Stationary；
        //随后处理到的事件会把阶段改成 Began / Moved / Ended / Canceled
        const auto touchFrameTime = std::chrono::steady_clock::now();
        m_touchFrameDeltaSeconds = std::chrono::duration<float>(
            touchFrameTime - m_lastTouchFrameTime).count();
        m_lastTouchFrameTime = touchFrameTime;

        for (TouchState& touch : m_activeTouches)
        {
            touch.phase = DoodleTouchPhase::Stationary;
            touch.deltaX = 0.0f;
            touch.deltaY = 0.0f;
        }

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

        //事件处理完了，整理成本帧快照
        BuildTouchFrame();
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

    DoodleInput::RawState DoodleAndroidWindow::GetRawInputState() const
    {
        //键鼠快照在 Android 上恒为空；触摸能力是平台属性，失焦清空快照后
        //也必须保持 true，所以在返回的副本上补
        DoodleInput::RawState state = m_input;
        state.touchSupported = true;
        return state;
    }

    void DoodleAndroidWindow::SetCursorCaptured(const bool /*captured*/)
    {
        //Android 没有桌面光标捕获。保留接口是为了三端实现同一份 DoodleWindow 契约
    }

    int32_t DoodleAndroidWindow::HandleInputEvent(android_app* pApp, AInputEvent* pEvent)
    {
        const auto pWindow = static_cast<DoodleAndroidWindow*>(pApp->userData);
        if (pWindow == nullptr)
        {
            return 0;
        }

        return pWindow->OnInputEvent(pEvent) ? 1 : 0;
    }

    bool DoodleAndroidWindow::OnInputEvent(const AInputEvent* pEvent)
    {
        //只消费触摸；按键（返回键、音量键等）交给系统
        if (AInputEvent_getType(pEvent) != AINPUT_EVENT_TYPE_MOTION)
        {
            return false;
        }

        const int32_t action = AMotionEvent_getAction(pEvent);
        const int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        const std::size_t pointerIndex = static_cast<std::size_t>(
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        const std::size_t pointerCount = AMotionEvent_getPointerCount(pEvent);
        const float densityScale = GetTouchDensityScale();

        switch (actionMasked)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            AddTouch(pEvent, pointerIndex, densityScale);
            return true;

        case AMOTION_EVENT_ACTION_MOVE:
            for (std::size_t i = 0; i < pointerCount; ++i)
            {
                UpdateTouch(pEvent, i, densityScale, false);
            }
            return true;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            UpdateTouch(pEvent, pointerIndex, densityScale, true);
            return true;

        case AMOTION_EVENT_ACTION_CANCEL:
            for (TouchState& touch : m_activeTouches)
            {
                touch.phase = DoodleTouchPhase::Canceled;
                touch.removeAfterFrame = true;
            }
            return true;

        default:
            return false;
        }
    }

    void DoodleAndroidWindow::AddTouch(const AInputEvent* pEvent,
                                       const std::size_t pointerIndex,
                                       const float densityScale)
    {
        if (m_activeTouches.size() >= DoodleInput::kMaxTouches)
        {
            return;
        }

        const int32_t fingerId = AMotionEvent_getPointerId(pEvent, pointerIndex);

        //极端情况下同一根手指的 DOWN 重复到达；已经在跟踪就不再新增
        for (const TouchState& touch : m_activeTouches)
        {
            if (touch.fingerId == fingerId)
            {
                return;
            }
        }

        const float x = AMotionEvent_getX(pEvent, pointerIndex) / densityScale;
        const float y = AMotionEvent_getY(pEvent, pointerIndex) / densityScale;

        TouchState touch;
        touch.fingerId = fingerId;
        touch.x = x;
        touch.y = y;
        touch.lastFrameX = x;
        touch.lastFrameY = y;
        touch.pressure = std::clamp(
            AMotionEvent_getPressure(pEvent, pointerIndex), 0.0f, 1.0f);
        touch.phase = DoodleTouchPhase::Began;
        m_activeTouches.push_back(touch);
    }

    void DoodleAndroidWindow::UpdateTouch(const AInputEvent* pEvent,
                                          const std::size_t pointerIndex,
                                          const float densityScale,
                                          const bool ending)
    {
        const int32_t fingerId = AMotionEvent_getPointerId(pEvent, pointerIndex);
        const float x = AMotionEvent_getX(pEvent, pointerIndex) / densityScale;
        const float y = AMotionEvent_getY(pEvent, pointerIndex) / densityScale;

        for (TouchState& touch : m_activeTouches)
        {
            if (touch.fingerId != fingerId)
            {
                continue;
            }

            //位移相对上一帧快照的位置，同一帧里多条 MOVE 也只累计到「本帧总位移」
            touch.deltaX = x - touch.lastFrameX;
            touch.deltaY = y - touch.lastFrameY;
            touch.x = x;
            touch.y = y;
            touch.pressure = std::clamp(
                AMotionEvent_getPressure(pEvent, pointerIndex), 0.0f, 1.0f);

            if (ending)
            {
                touch.phase = DoodleTouchPhase::Ended;
                touch.removeAfterFrame = true;
            }
            else if (touch.phase != DoodleTouchPhase::Began)
            {
                //刚开始的那一帧保持 Began；之后按是否移动区分 Moved / Stationary
                touch.phase = (touch.deltaX != 0.0f || touch.deltaY != 0.0f)
                                  ? DoodleTouchPhase::Moved
                                  : DoodleTouchPhase::Stationary;
            }

            return;
        }

        //没收到 DOWN 就收到 MOVE：补记一根手指，避免整段触摸丢失
        if (!ending)
        {
            AddTouch(pEvent, pointerIndex, densityScale);
        }
    }

    void DoodleAndroidWindow::BuildTouchFrame()
    {
        m_input.touchCount = 0;

        for (const TouchState& touch : m_activeTouches)
        {
            if (m_input.touchCount >= static_cast<int32_t>(DoodleInput::kMaxTouches))
            {
                break;
            }

            DoodleTouch& output = m_input.touches[static_cast<std::size_t>(m_input.touchCount)];
            ++m_input.touchCount;

            output.fingerId = touch.fingerId;
            output.position = glm::vec2(touch.x, touch.y);
            output.deltaPosition = glm::vec2(touch.deltaX, touch.deltaY);
            output.deltaTime = m_touchFrameDeltaSeconds;
            output.pressure = touch.pressure;
            output.phase = touch.phase;
        }

        //本帧快照写完再推进「上一帧位置」，然后移除已经结束的手指
        for (TouchState& touch : m_activeTouches)
        {
            touch.lastFrameX = touch.x;
            touch.lastFrameY = touch.y;
        }

        std::erase_if(m_activeTouches,
                      [](const TouchState& touch) { return touch.removeAfterFrame; });
    }

    float DoodleAndroidWindow::GetTouchDensityScale() const
    {
        //android_app::config 由胶水层按当前 Configuration 维护；
        //ANativeActivity 本身没有 config 成员
        if (m_pApp == nullptr || m_pApp->config == nullptr)
        {
            return 1.0f;
        }

        const int32_t density = AConfiguration_getDensity(m_pApp->config);
        if (density <= 0)
        {
            return 1.0f;
        }

        //160 dpi 是 Android 的 mdpi 基准；结果就是 dp / 逻辑窗口单位
        return static_cast<float>(density) / 160.0f;
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

    VkResult DoodleAndroidWindow::CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const
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
            //失焦清空键鼠与触摸快照；手指在切后台时不可能还按着，
            //留着会变成永远不会结束的 Ended 触摸
            m_input = {};
            m_input.touchSupported = true;
            m_activeTouches.clear();
            break;

        case APP_CMD_TERM_WINDOW:
            //窗口没了，触摸的坐标基准也失效，直接丢弃活动手指
            m_activeTouches.clear();
            m_input.touches = {};
            m_input.touchCount = 0;
            break;

        default:
            break;
        }

        //其余命令全部交给驱动层。窗口的来去（INIT_WINDOW / TERM_WINDOW）中，
        //交换链 / 帧缓冲 / 同步对象由 Vulkan 子系统处理；本层只清理自己的
        //输入状态，然后把命令照样转下去
        m_application.OnAppCommand(command);
    }
}
