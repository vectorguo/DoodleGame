//
//  DoodleWindowsWindow.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleWindowsWindow.h"
#include "../DoodleApplication.h"

#include <algorithm>
#include <stdexcept>
#include <string>

//<windows.h> 必须排在本工程所有头文件之后。它的宏只改写出现在它后面的文本：
//排在前面的话，DoodleApplication.h 里的 CreateWindow 在本翻译单元里就成了 CreateWindowW。
//WIN32_LEAN_AND_MEAN / NOMINMAX / VK_USE_PLATFORM_WIN32_KHR 由 CMakeLists.txt 统一定义，
//后者让 <vulkan/vulkan.h> 带上 vulkan_win32.h
#include <windows.h>
#include <vulkan/vulkan.h>

namespace
{
    /**
     * 窗口类名
     *
     * 一个进程只有一个窗口，类跟着窗口走：构造时注册、析构时注销
     */
    constexpr const wchar_t* kWindowClassName = L"DoodleWindow";

    /**
     * UTF-8 转 UTF-16
     *
     * 接口上的标题与 macOS 那份一样收 UTF-8，Win32 的 W 系列 API 要 UTF-16。
     * 不带 MB_ERR_INVALID_CHARS：非法序列替换成 U+FFFD 而不是整体失败 —— 标题只是装饰，
     * 不值得为它让启动失败
     */
    std::wstring ToWide(const char* utf8)
    {
        //长度传 -1：按 '\0' 结尾处理，返回值把结尾的 '\0' 也算进去了
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (length <= 1)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
        wide.pop_back();
        return wide;
    }

    /**
     * 按窗口所在显示器的缩放定尺寸，再居中放进那块显示器的工作区
     *
     * DPI 从窗口自己身上取：此刻它已经落在某块显示器上了（位置是 CW_USEDEFAULT，由系统挑），
     * 那块屏现在的缩放就是该用的缩放。GetDpiForSystem 给的是登录那一刻主屏的值，
     * 登录后改过缩放、或窗口落在副屏上时都不对
     *
     * 夹到工作区以内不是多虑：1080p 的笔记本常开 150% 缩放，工作区折合下来只有
     * 1280×720 左右的逻辑尺寸，1280×800 的客户区再加上标题栏就探出屏幕了
     */
    void PlaceWindow(HWND hWnd, const int32_t width, const int32_t height)
    {
        const UINT dpi = GetDpiForWindow(hWnd);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        const RECT& workArea = monitorInfo.rcWork;

        //给的是客户区尺寸，窗口矩形还要加上边框与标题栏。这两样的粗细也随 DPI 变，
        //所以用 ForDpi 版本，按同一个 DPI 算
        RECT windowRect{0, 0, MulDiv(width, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                        MulDiv(height, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
        AdjustWindowRectExForDpi(&windowRect,
                                 static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE)),
                                 FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_EXSTYLE)),
                                 dpi);

        const LONG workWidth = workArea.right - workArea.left;
        const LONG workHeight = workArea.bottom - workArea.top;
        const LONG windowWidth = std::min(windowRect.right - windowRect.left, workWidth);
        const LONG windowHeight = std::min(windowRect.bottom - windowRect.top, workHeight);

        SetWindowPos(hWnd, nullptr,
                     workArea.left + (workWidth - windowWidth) / 2,
                     workArea.top + (workHeight - windowHeight) / 2,
                     windowWidth, windowHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /**
     * 客户区中心，换算到屏幕坐标
     *
     * 捕获鼠标时用它做回中：每轮把光标放回这里，下一次就从中点算位移，
     * 光标永远不会撞到屏幕边缘
     */
    POINT ClientCenterInScreen(HWND hWnd)
    {
        RECT clientRect{};
        GetClientRect(hWnd, &clientRect);

        POINT center{(clientRect.left + clientRect.right) / 2,
                     (clientRect.top + clientRect.bottom) / 2};
        ClientToScreen(hWnd, &center);
        return center;
    }
}

namespace Doodle
{
    struct DoodleWindowsWindow::WindowProcedure
    {
        static LRESULT CALLBACK Handle(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            //对象指针在构造函数的最后一步才挂上去。在那之前收到的 —— CreateWindowExW、
            //SetWindowPos、ShowWindow 里同步发来的那一批 —— 一律交给默认处理，
            //这正是「建窗期间的 WM_SIZE 不转发」的实现方式
            const auto pWindow = reinterpret_cast<DoodleWindowsWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if (pWindow == nullptr)
            {
                return DefWindowProcW(hWnd, message, wParam, lParam);
            }

            switch (message)
            {
            case WM_CLOSE:
                //只记请求，不让 DefWindowProc 去 DestroyWindow，理由见 m_closeRequested
                pWindow->m_closeRequested = true;
                return 0;

            case WM_SETFOCUS:
                pWindow->m_focused = true;
                return 0;

            case WM_KILLFOCUS:
                //失焦就释放捕获并清空快照：Alt+Tab 回来后不带着卡住的按键，
                //光标也不会一直被锁在客户区里
                pWindow->m_focused = false;
                pWindow->SetCursorCaptured(false);
                pWindow->m_input = {};
                return 0;

            case WM_SETCURSOR:
                //捕获期间不显示光标；释放后交给 DefWindowProc 恢复窗口类光标
                if (pWindow->m_cursorCaptured)
                {
                    ::SetCursor(nullptr);
                    return TRUE;
                }
                break;

            case WM_SIZE:
                //与 macOS 的尺寸回调同一个分工：尺寸参数不在这里用，真正的尺寸等重建交换链时
                //现查；这里只让驱动层记下「变过了」。拖动窗口边缘时系统会甩出一连串 WM_SIZE，
                //逐个处理会把交换链重建到卡死
                //
                //最小化（SIZE_MINIMIZED，客户区 0×0）也走这里：重建时 WaitUntilDrawable
                //会挡住，等窗口回来再建
                //
                //取实例不判空：窗口过程只在泵事件（或泵事件引出的同步调用）时被调，那时驱动层
                //必然活着。契约写在 DoodleApplication::GetInstance 那边
                if (wParam == SIZE_MINIMIZED)
                {
                    //光标锁在一个 0×0 的客户区里没有意义，顺手也清掉按键快照
                    pWindow->SetCursorCaptured(false);
                    pWindow->m_input = {};
                }

                DoodleApplication::GetInstance().NotifyWindowResized();
                return 0;

            case WM_DPICHANGED:
                {
                    //拖到另一块缩放不同的显示器上了。按系统建议的矩形调整，窗口的逻辑尺寸
                    //就保持不变；随后到来的 WM_SIZE 会让驱动层重建交换链。
                    //Per-Monitor V2 下这一步归应用自己做，DefWindowProc 不管
                    const auto* pSuggestedRect = reinterpret_cast<const RECT*>(lParam);
                    SetWindowPos(hWnd, nullptr,
                                 pSuggestedRect->left, pSuggestedRect->top,
                                 pSuggestedRect->right - pSuggestedRect->left,
                                 pSuggestedRect->bottom - pSuggestedRect->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    return 0;
                }

            case WM_SYSCOMMAND:
                //单按 Alt 或 F10 会进入菜单模式：DefWindowProc 开一个模态循环等菜单操作，
                //帧循环就卡在 DispatchMessage 里，画面停住，直到再按一个键或点一下鼠标。
                //本窗口没有菜单，直接吞掉（GLFW 默认也是这么做的）。
                //低四位归系统内部使用，比较前要先屏蔽掉
                if ((wParam & 0xFFF0) == SC_KEYMENU)
                {
                    return 0;
                }
                break;

            default:
                break;
            }

            return DefWindowProcW(hWnd, message, wParam, lParam);
        }
    };

    DoodleWindowsWindow::DoodleWindowsWindow(const int32_t width, const int32_t height, const char* title)
    {
        const HINSTANCE hInstance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &WindowProcedure::Handle;
        windowClass.hInstance = hInstance;
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        //不给光标的话，鼠标从边框移进客户区时会一直保持那个调整尺寸的箭头
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        //背景画刷留空：客户区全由 Vulkan 画，系统先刷一层底色只会在缩放时闪一下白
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClassName;
        if (RegisterClassExW(&windowClass) == 0)
        {
            throw std::runtime_error("failed to register window class");
        }

        //可缩放（WS_OVERLAPPEDWINDOW 带着可拖动的边框和最大化按钮）。窗口尺寸变化后交换链
        //就不再与 surface 匹配，需要在帧循环里重建 —— 与 macOS 那份的 GLFW_RESIZABLE 同义
        //
        //先不显示：尺寸要等知道了落在哪块显示器上才定得下来，见 PlaceWindow
        const std::wstring wideTitle = ToWide(title);
        m_hWnd = CreateWindowExW(0, kWindowClassName, wideTitle.c_str(), WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 nullptr, nullptr, hInstance, nullptr);
        if (m_hWnd == nullptr)
        {
            //构造函数抛出去就不会走析构了，类得在这里自己注销
            UnregisterClassW(kWindowClassName, hInstance);
            throw std::runtime_error("failed to create window");
        }

        PlaceWindow(m_hWnd, width, height);

        //控制台程序没有 WinMain 的 nCmdShow，按约定传 SW_SHOWDEFAULT，
        //让启动方（快捷方式之类）给的显示方式生效
        ShowWindow(m_hWnd, SW_SHOWDEFAULT);

        //但「最小化运行」的快捷方式会让窗口一出来就是最小化的：客户区 0×0，
        //紧接着起 Vulkan 时交换链建不出来。驱动层的默认实现假定窗口一出世就能画，
        //这里兑现它
        if (IsIconic(m_hWnd))
        {
            ShowWindow(m_hWnd, SW_RESTORE);
        }

        //最后一步才把本对象挂到窗口上，此后的消息才会转给驱动层（见 WindowProcedure::Handle）。
        //与 macOS 那份的「先建窗、再注册尺寸回调」是同一个时序
        SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        //ShowWindow 期间可能已经发过 WM_SETFOCUS，那时 USERDATA 还没挂上，
        //窗口过程没有转给本对象。这里补一次当前焦点状态，否则要等到下一次
        //焦点变化才会从 false 变 true，期间的按键全被当成「未聚焦」丢掉
        m_focused = GetFocus() == m_hWnd;
    }

    DoodleWindowsWindow::~DoodleWindowsWindow()
    {
        //退出时可能还捕获着光标：先恢复，再销毁窗口
        SetCursorCaptured(false);

        if (m_hWnd != nullptr)
        {
            //先把对象指针摘下来：DestroyWindow 会同步发来 WM_DESTROY 等消息，
            //那时本对象正在析构，不该再经窗口过程被碰到
            SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, 0);
            DestroyWindow(m_hWnd);
            m_hWnd = nullptr;
        }
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    void DoodleWindowsWindow::PumpEvents()
    {
        //只排空队列，不等 —— 与 glfwPollEvents 一样。
        //hWnd 传空：线程上所有窗口的消息、以及发给线程本身的消息都要取
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            //WM_QUIT 不进窗口过程，只能在这里看到。本程序自己不发它（关窗走的是 WM_CLOSE），
            //收到就是外面要求结束整个进程（任务管理器就会这么做），按关闭请求处理
            if (message.message == WM_QUIT)
            {
                m_closeRequested = true;
                continue;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        //消息处理完之后焦点状态是本轮最终值。失焦时清空，避免把别的窗口
        //按下的键读进来
        if (!m_focused)
        {
            if (m_cursorCaptured)
            {
                SetCursorCaptured(false);
            }

            m_input = {};
            return;
        }

        //GetAsyncKeyState 是全局按键状态，上面的 m_focused 门控是必需的。
        //字母用虚拟键码，按逻辑键映射（与 macOS GLFW 的口径一致）
        m_input.SetKey(DoodleKey::W, (GetAsyncKeyState('W') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::A, (GetAsyncKeyState('A') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::S, (GetAsyncKeyState('S') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::D, (GetAsyncKeyState('D') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::Q, (GetAsyncKeyState('Q') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::E, (GetAsyncKeyState('E') & 0x8000) != 0);
        m_input.SetKey(DoodleKey::LeftShift, (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0);

        m_input.SetMouseButton(DoodleMouseButton::Left, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        m_input.SetMouseButton(DoodleMouseButton::Right, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        m_input.SetMouseButton(DoodleMouseButton::Middle, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);

        m_input.mouseDeltaX = 0.0f;
        m_input.mouseDeltaY = 0.0f;

        if (m_cursorCaptured)
        {
            //捕获时每轮把光标回中，位移永远相对客户区中心。
            //这样光标不会撞到屏幕边缘，能一直转视角
            const POINT center = ClientCenterInScreen(m_hWnd);

            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(m_hWnd, &cursor);

            RECT clientRect{};
            GetClientRect(m_hWnd, &clientRect);

            const float centerX = static_cast<float>(clientRect.left + clientRect.right) * 0.5f;
            const float centerY = static_cast<float>(clientRect.top + clientRect.bottom) * 0.5f;

            //客户区坐标是物理像素；换算成 96 DPI 的逻辑像素，
            //和高 DPI 下 macOS 的点坐标手感对齐
            const float dpiScale = std::max(
                1.0f,
                static_cast<float>(GetDpiForWindow(m_hWnd)) /
                static_cast<float>(USER_DEFAULT_SCREEN_DPI));

            m_input.mouseDeltaX = (static_cast<float>(cursor.x) - centerX) / dpiScale;
            m_input.mouseDeltaY = (static_cast<float>(cursor.y) - centerY) / dpiScale;

            SetCursorPos(center.x, center.y);
        }

        m_input.cursorCaptured = m_cursorCaptured;
    }

    DoodleInput::RawState DoodleWindowsWindow::GetRawInputState() const
    {
        return m_input;
    }

    void DoodleWindowsWindow::SetCursorCaptured(const bool captured)
    {
        if (captured == m_cursorCaptured)
        {
            return;
        }

        m_cursorCaptured = captured;

        if (captured)
        {
            //把光标限制在客户区，隐藏，并回中
            RECT clientRect{};
            GetClientRect(m_hWnd, &clientRect);

            POINT topLeft{clientRect.left, clientRect.top};
            POINT bottomRight{clientRect.right, clientRect.bottom};
            ClientToScreen(m_hWnd, &topLeft);
            ClientToScreen(m_hWnd, &bottomRight);

            const RECT clipRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
            ClipCursor(&clipRect);

            const POINT center = ClientCenterInScreen(m_hWnd);
            SetCursorPos(center.x, center.y);
            ::SetCursor(nullptr);
        }
        else
        {
            ClipCursor(nullptr);
            ::SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }

        //切换时清零位移：第一次按下右键那一帧不能把「光标从旧位置跳过来」
        //算进视角
        m_input.mouseDeltaX = 0.0f;
        m_input.mouseDeltaY = 0.0f;
        m_input.cursorCaptured = captured;
    }

    void DoodleWindowsWindow::WaitUntilDrawable()
    {
        //与 macOS 那份同一个结构。
        //
        //先查一次是为了应付「尺寸本来就正常」这个常见情况：WaitMessage 要等到有新消息才返回，
        //直接进循环的话会在这里挂到下一次输入才醒
        //
        //循环里先等后查：唤醒之后立刻用最新尺寸判断，尺寸一合法就退出。
        //关闭请求也在循环里查 —— 最小化着从任务栏把窗口关掉时，尺寸一直是 0×0，
        //少了这一条会在这里死等
        VkExtent2D size = GetSurfaceSize();
        while (size.width == 0 || size.height == 0)
        {
            if (IsCloseRequested())
            {
                break;
            }
            WaitMessage();
            PumpEvents();
            size = GetSurfaceSize();
        }
    }

    bool DoodleWindowsWindow::IsCloseRequested() const
    {
        return m_closeRequested;
    }

    VkExtent2D DoodleWindowsWindow::GetSurfaceSize() const
    {
        //表面对应的是客户区，边框与标题栏不归交换链。客户区矩形的左上角恒为 (0, 0)，
        //最小化时右下角也是 (0, 0)；不会是负数，转 uint32_t 不丢信息
        RECT clientRect{};
        GetClientRect(m_hWnd, &clientRect);

        return
        {
            .width = static_cast<uint32_t>(clientRect.right - clientRect.left),
            .height = static_cast<uint32_t>(clientRect.bottom - clientRect.top)
        };
    }

    std::vector<const char*> DoodleWindowsWindow::GetSurfaceExtensions() const
    {
        return
        {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };
    }

    VkResult DoodleWindowsWindow::CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const
    {
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.hinstance = GetModuleHandleW(nullptr);
        createInfo.hwnd = m_hWnd;

        //Windows 上的 loader（vulkan-1.lib / vulkan-1.dll）直接导出了这个入口，
        //和 Android 一样不必经 vkGetInstanceProcAddr 去取
        return vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, pSurface);
    }
}
