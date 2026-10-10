//
//  DoodleWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * 平台窗口
     *
     * 把「窗口从哪来」关在实现里，驱动层与 Vulkan 各层只对着这个接口说话。
     *
     * 之所以值得单独成层，判据不是「要不要跨平台」，而是窗口有没有独立的
     * 生命周期锚点 —— 有。而且在 Android 上这个锚点是实打实的：
     * ANativeWindow 会随切后台、锁屏、旋转而消失又回来；桌面上的 GLFWwindow
     * 则与程序同寿，唯一的扰动是尺寸变化。这个差异必须有个地方安放，
     * 否则它会渗进 Vulkan 各层（现在 DoodleVulkanDevice 就在承担它不该承担的
     * surface 生命周期）。
     *
     * 本对象自身是长期存在的（与驱动层同寿）。会来会去的是它底下的*表面*：
     * Android 实现内部记住当前的 ANativeWindow，窗口回来之后重新调
     * CreateSurface 就能拿到基于新窗口的 surface
     *
     * 命名上刻意避开 GLFW 的 framebuffer 一词（它指的是窗口的像素缓冲）：
     * 接口是共用面，用某一套实现的说法命名，换一份实现立刻读不懂了。
     * 所以那边叫 FramebufferSize 的，到这里一律按它实际指的东西叫 drawable 或 surface
     */
    class DoodleWindow
    {
    public:
        DoodleWindow() = default;
        virtual ~DoodleWindow() = default;

        #pragma region 事件

        /**
         * 泵一次事件
         *
         * 会阻塞 —— 这是名字用「泵」而不是「轮询」的原因：有没有活干决定它等不等。
         * 桌面就是 glfwPollEvents，只是排空队列，不等；
         * Android 上没有对应的单一调用：有可画的东西时非阻塞地把事件排干，
         * 没有时阻塞等下一个事件（见实现里的取舍）
         */
        virtual void PumpEvents() = 0;

        /**
         * 等到窗口能画出东西为止
         *
         * 桌面：窗口最小化时像素尺寸是 0×0，那样建不出交换链，
         *       只能阻塞等窗口回来。连续 resize 时也靠它把事件流消化掉
         * Android：没有「最小化」这个状态。窗口没了就是 surface 没了，
         *       走的是驱动层的 Suspend 路径，不会走到这里来。所以实现是空的
         */
        virtual void WaitUntilDrawable() = 0;

        /**
         * 主循环是否该退出：查「有没有人要求关闭」
         *
         * 桌面：窗口被关掉
         * Android：系统要求销毁 Activity（此刻窗口可能已经不在，
         *       所以问的不是窗口，是本轮循环还要不要转下去）
         */
        [[nodiscard]] virtual bool IsCloseRequested() const = 0;

        #pragma endregion

        #pragma region Vulkan

        /**
         * 表面的像素尺寸
         *
         * 单位是像素，不是逻辑点 —— 高 DPI 屏上两者不相等，
         * 而交换链要的是像素
         *
         * 返回 VkExtent2D 而不是走两个 int32_t 出参：它下游唯一的去处就是
         * VkSwapChainCreateInfoKHR::imageExtent，类型先对齐，符号转换就只剩
         * 各平台实现内部那一次（GLFW 与 ANativeWindow 给的都是有符号）。出参
         * 形状本是 GLFW 的 C API 长相，不该由一份实现泄漏到共用接口上
         */
        [[nodiscard]] virtual VkExtent2D GetSurfaceSize() const = 0;

        /**
         * 创建表面所需的实例扩展名
         *
         * 只返回与窗口系统相关的那几个（macOS 走 GLFW 查，Android 是
         * VK_KHR_surface + VK_KHR_android_surface）。
         * 平台专有的其它实例扩展由 DoodleVulkanDevice 自己按需追加。
         */
        [[nodiscard]] virtual std::vector<const char*> GetSurfaceExtensions() const = 0;

        /**
         * 用当前窗口创建表面
         *
         * 失败时返回 Vulkan 的错误码，由调用方决定怎么报错
         */
        [[nodiscard]] virtual VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const = 0;

        #pragma endregion
    };
}
