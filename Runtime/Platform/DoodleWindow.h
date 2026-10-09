//
//  DoodleWindow.h
//  Doodle
//
//  Created by 郭智 on 2026/10/8.
//

#pragma once

#include <cstdint>
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
     */
    class DoodleWindow
    {
    public:
        virtual ~DoodleWindow() = default;

        // ---- 驱动层用：事件循环 ----

        /**
         * 主循环是否该退出
         *
         * 桌面：窗口被关掉
         * Android：系统要求销毁 Activity
         */
        [[nodiscard]] virtual bool ShouldClose() const = 0;

        /**
         * 处理掉所有已排队的事件，不阻塞
         *
         * 桌面就是 glfwPollEvents。Android 上没有对应的单一调用，
         * 事件泵由驱动层自己的 Looper 循环承担，这里只做收尾
         */
        virtual void PollEvents() = 0;

        /**
         * 等到 framebuffer 尺寸合法为止
         *
         * 桌面：窗口最小化时 Framebuffer 尺寸会变成 0×0，那样建不出交换链，
         *       只能阻塞等窗口回来。连续 resize 时也靠它把事件流消化掉
         * Android：没有「最小化」这个状态。窗口没了就是 surface 没了，
         *       走的是驱动层的 Suspend 路径，不会走到这里来。所以实现是空的
         */
        virtual void WaitForValidFramebufferSize() = 0;

        // ---- Vulkan 层用 ----

        /**
         * 当前 framebuffer 的像素尺寸
         *
         * 单位是像素，不是逻辑点 —— 高 DPI 屏上两者不相等，
         * 而交换链要的是像素
         */
        virtual void GetFramebufferSize(int32_t& width, int32_t& height) const = 0;

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
        virtual VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* pSurface) const = 0;
    };
}
