//
//  DoodleMacWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include "../DoodleWindow.h"

//前向声明。必须放在全局作用域。
//头文件里不引入 GLFW：本类的使用者只该看见 DoodleWindow 那套接口，
//GLFW 是这一层的实现细节。定义在 .cpp 里
struct GLFWwindow;

namespace Doodle
{
    /**
     * macOS 窗口，用 GLFW 实现
     *
     * 与 Android 那份实现的关键差别：这里的窗口与程序同寿。
     * 所以 surface 建一次就不用管了，唯一要处理的是尺寸变化
     *
     * 没有像 Android 那样改用系统原生 API：本层成立与否的判据是
     * 「窗口有没有独立的生命周期锚点」（见 DoodleWindow.h），macOS 上这个
     * 锚点不存在，换 Cocoa 只会换掉实现细节，不会让本层多处理任何一种状态。
     * 而 GLFW 在 Cocoa 上做的事本就是「给 NSView 挂 CAMetalLayer，再调
     * vkCreateMetalSurfaceEXT」（在本机 GLFW 3.5.1 的 dylib 里验过符号与
     * 扩展名），自己写等于照抄这段 AppKit 代码，还要连带搭起 NSApplication
     * 引导、NSEvent 事件泵，以及 WaitUntilDrawable 依赖的
     * 「阻塞等事件」语义
     *
     * 真要换的触发条件：需要 macOS 原生 UI（菜单栏、NSOpenPanel、触摸板手势），
     * 或要把窗口嵌进别人的 NSView 层级。在那之前，只差某一件原生的事时，
     * 可以先走 glfwGetCocoaWindow() / glfwGetCocoaView() 这个逃生口
     */
    class DoodleMacWindow final : public DoodleWindow
    {
    public:
        /**
         * 创建并显示窗口
         *
         * @param width 初始像素宽度
         * @param height 初始像素高度
         * @param title 窗口标题
         */
        DoodleMacWindow(int32_t width, int32_t height, const char* title);
        ~DoodleMacWindow() override;

        DoodleMacWindow(const DoodleMacWindow&) = delete;
        DoodleMacWindow(DoodleMacWindow&&) = delete;
        DoodleMacWindow& operator=(const DoodleMacWindow&) = delete;

    public:
        void PumpEvents() override;

        void WaitUntilDrawable() override;

        [[nodiscard]] bool IsCloseRequested() const override;

        [[nodiscard]] VkExtent2D GetSurfaceSize() const override;

        [[nodiscard]] std::vector<const char*> GetSurfaceExtensions() const override;

        [[nodiscard]] VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const override;

    private:
        /**
         * GLFW 的尺寸变化回调
         *
         * 必须是静态函数：GLFW 是 C 库，回调是裸函数指针，没有 this 可以绑定，
         * 非静态成员函数的签名对不上。本类不为它往窗口里存 this，而是向
         * DoodleApplication::GetInstance() 要当前驱动层 —— 至于它什么时候
         * 有效，见那边的说明
         *
         * 注册在 glfwSetFramebufferSizeCallback 上（而不是 ...WindowSizeCallback）。
         * 这个函数名沿用 GLFW 的说法，是因为它紧贴 GLFW 回调机制、只在
         * .cpp 里注册一次；对外的接口名不跟着用它（那边是 GetSurfaceSize /
         * WaitUntilDrawable），理由见 DoodleWindow.h 的命名说明。
         * 两者确实不同：这个回调按像素尺寸触发，也能捕获窗口没动、只是换了
         * 不同 DPI 显示器的情况
         *
         * 三个参数都不使用：回调注册在哪个窗口上，就只可能由那个窗口触发，
         * 本类不需要再拿它做身份判断；尺寸则留给交换链重建时现查，
         * 理由见实现里的注释
         */
        static void FramebufferSizeCallback(GLFWwindow* pGlfwWindow, int width, int height);

    private:
        /** GLFW 窗口句柄 */
        GLFWwindow* m_pWindow = nullptr;
    };
}
