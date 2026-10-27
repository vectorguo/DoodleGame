//
//  DoodleWindowsWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <cstdint>

#include "../DoodleWindow.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 <windows.h>：本类的使用者只该看见 DoodleWindow 那套接口，Win32 是这一层的
//实现细节。而且 <windows.h> 带着一批与常用名字撞车的宏 —— 最要命的是 CreateWindow，
//它会被改写成 CreateWindowW。经本头文件传进 DoodleWindowsApplication.cpp 的话，
//那边定义的 CreateWindow 与基类里声明的就不再是同一个函数
//
//HWND 在 STRICT（默认开着）下就是 HWND__*，所以声明这个结构体就够了
struct HWND__;

namespace Doodle
{
    /**
     * Windows 窗口，用 Win32 原生 API 实现
     *
     * 与 macOS 那份一样，这里的窗口与程序同寿：surface 建一次就不用管了，
     * 唯一要处理的是尺寸变化（包括最小化 —— 那时客户区是 0×0）
     *
     * 没有像 macOS 那样走 GLFW。判据同 DoodleMacWindow.h：Windows 上同样没有独立的
     * 生命周期锚点，换哪一套实现都不会让本层多处理一种状态，比的只是代价。
     * 那边自己写要搭 NSApplication 引导和 NSEvent 事件泵；这边是注册窗口类、建窗、
     * PeekMessage 泵消息、vkCreateWin32SurfaceKHR，合起来一两百行。
     * 而 Windows 没有 brew，用 GLFW 就得把它收进仓库随工程一起编 ——
     * 两头一比，原生换来的是除 Vulkan SDK 与 Windows SDK 之外零依赖
     *
     * 行为上对齐 macOS 那份（也就是 GLFW）的语义：
     *   - 尺寸变化只在泵事件时通知驱动层。建窗期间 Windows 会同步发来几次 WM_SIZE，
     *     那时本对象还没挂到窗口上，一律不转发 —— 驱动层的默认实现靠的正是
     *     「窗口一出世就紧接着起 Vulkan，中间没有通知能插进来」
     *   - 关窗只记一个请求，窗口本身留到析构才销毁：表面与交换链都建在它上面
     *   - 尺寸按像素报。进程在 DoodleGame.manifest 里声明了 Per-Monitor V2 的 DPI 感知，
     *     系统不会再把窗口当成 96 DPI 去拉伸，GetClientRect 给的就是物理像素
     */
    class DoodleWindowsWindow final : public DoodleWindow
    {
    public:
        /**
         * 创建并显示窗口，居中放在它落脚的那块显示器上
         *
         * 尺寸按逻辑尺寸理解（96 DPI 下的像素），由窗口所在显示器的缩放换算成物理像素 ——
         * 和 macOS 上按点给尺寸是一个意思，同一组数在两个平台上开出来的窗口一样大。
         * 换算后放不下时夹到显示器的工作区以内
         *
         * @param width 客户区初始宽度（逻辑尺寸）
         * @param height 客户区初始高度（逻辑尺寸）
         * @param title 窗口标题，UTF-8
         */
        DoodleWindowsWindow(const int32_t width, const int32_t height, const char* title); // NOLINT(*-avoid-const-params-in-decls)
        ~DoodleWindowsWindow() override;

        DoodleWindowsWindow(const DoodleWindowsWindow&) = delete;
        DoodleWindowsWindow(DoodleWindowsWindow&&) = delete;
        DoodleWindowsWindow& operator=(const DoodleWindowsWindow&) = delete;

    public:
        void PumpEvents() override;

        void WaitUntilDrawable() override;

        [[nodiscard]] bool IsCloseRequested() const override;

        [[nodiscard]] DoodleInput::RawState GetRawInputState() const override;

        void SetCursorCaptured(const bool captured) override;

        [[nodiscard]] VkExtent2D GetSurfaceSize() const override;

        [[nodiscard]] std::vector<const char*> GetSurfaceExtensions() const override;

        [[nodiscard]] VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const override;

    private:
        /**
         * 窗口过程（WNDPROC）的宿主
         *
         * 声明成不完整的嵌套类型，定义在 .cpp 里。理由是签名：窗口过程得是
         * LRESULT CALLBACK (HWND, UINT, WPARAM, LPARAM)，后面三个都是随位宽变化的整数 typedef
         * （LPARAM 在 32 位上是 long，与 intptr_t 不是同一个类型），没法像 HWND 那样
         * 只前向声明一个结构体。放成静态成员函数就得把 <windows.h> 拉进本头文件
         *
         * 嵌套类型天然能访问本类的私有成员，窗口过程要写的关闭请求因此不必为它开一个公开入口
         */
        struct WindowProcedure;

    private:
        /** 窗口句柄 */
        HWND__* m_hWnd = nullptr;

        /**
         * 有没有人要求关闭
         *
         * 由 WM_CLOSE 置位。不把关窗交给 DefWindowProc：它会当场 DestroyWindow，
         * 而那一刻表面和交换链还建在这个窗口上 —— 窗口得活到 Vulkan 拆完之后，
         * 销毁收在析构里（与 glfwWindowShouldClose 是同一个分工）
         */
        bool m_closeRequested = false;

        /**
         * 本帧输入快照，由 PumpEvents 刷新
         */
        DoodleInput::RawState m_input;

        /**
         * 窗口是否拥有键盘焦点
         *
         * 由 WM_SETFOCUS / WM_KILLFOCUS 维护。GetAsyncKeyState 是全局的，
         * 必须靠它把别的窗口按下的键挡在外面
         */
        bool m_focused = false;

        /**
         * 光标是否处于捕获状态
         */
        bool m_cursorCaptured = false;
    };
}
