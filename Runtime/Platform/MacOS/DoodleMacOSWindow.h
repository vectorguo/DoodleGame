//
//  DoodleMacOSWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include <cstdint>
#include <functional>

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
     * 引导、NSEvent 事件泵，以及 WaitForValidFramebufferSize 依赖的
     * 「阻塞等事件」语义
     *
     * 真要换的触发条件：需要 macOS 原生 UI（菜单栏、NSOpenPanel、触摸板手势），
     * 或要把窗口嵌进别人的 NSView 层级。在那之前，只差某一件原生的事时，
     * 可以先走 glfwGetCocoaWindow() / glfwGetCocoaView() 这个逃生口
     */
    class DoodleMacOSWindow final : public DoodleWindow
    {
    public:
        /**
         * 窗口尺寸变化时的回调类型
         *
         * 用 std::function 而不是裸函数指针：GLFW 那边的回调是 C 接口，
         * 没有 this 可用，需要有个地方把「变化了」这件事传出去。
         * 本类自己消化掉 GLFW 的回调机制，对外只暴露这个
         */
        using ResizeHandler = std::function<void()>;

    public:
        /**
         * 创建并显示窗口
         *
         * @param width 初始像素宽度
         * @param height 初始像素高度
         * @param title 窗口标题
         * @param resizeHandler 尺寸变化时调用。允许为空
         */
        DoodleMacOSWindow(int32_t width, int32_t height, const char* title, ResizeHandler resizeHandler);

        /**
         * 销毁窗口，并终止 GLFW
         */
        ~DoodleMacOSWindow() override;

        DoodleMacOSWindow(const DoodleMacOSWindow&) = delete;
        DoodleMacOSWindow(DoodleMacOSWindow&&) = delete;
        DoodleMacOSWindow& operator=(const DoodleMacOSWindow&) = delete;

    public:
        // ---- DoodleWindow ----

        [[nodiscard]] bool ShouldClose() const override;

        void PollEvents() override;

        void WaitForValidFramebufferSize() override;

        void GetFramebufferSize(int32_t& width, int32_t& height) const override;

        [[nodiscard]] std::vector<const char*> GetSurfaceExtensions() const override;

        VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const override;

    private:
        /**
         * GLFW 的尺寸变化回调
         *
         * 必须是静态函数：GLFW 是 C 库，回调是裸函数指针，没有 this 可以绑定，
         * 非静态成员函数的签名对不上。所以用 glfwSetWindowUserPointer 把本对象
         * 存进窗口，在回调里取回来
         *
         * 注册在 glfwSetFramebufferSizeCallback 上（而不是 ...WindowSizeCallback）。
         * GLFW 的「framebuffer」指的是窗口的像素缓冲，这里不用那个词命名，
         * 是因为本工程的 FrameBuffer 已经专指 VkFramebuffer，同名会混淆。
         * 两者确实不同：这个回调按像素尺寸触发，也能捕获窗口没动、只是换了
         * 不同 DPI 显示器的情况
         *
         * @param pGlfwWindow 触发回调的窗口
         * @param width 新的像素宽度，本工程不使用，尺寸由交换链重建时现查
         * @param height 新的像素高度，同上
         */
        static void FramebufferSizeCallback(GLFWwindow* pGlfwWindow, int width, int height);

    private:
        /** GLFW 窗口句柄 */
        GLFWwindow* m_pGlfwWindow = nullptr;

        /** 尺寸变化回调，可能为空 */
        ResizeHandler m_resizeHandler;
    };
}
