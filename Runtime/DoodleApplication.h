//
//  DoodleManager.h
//  Doodle
//
//  Created by 郭智 on 2026/9/1.
//
#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "Vulkan/DoodleVulkanManager.h"

namespace Doodle
{
    class DoodleApplication
    {
    public:
        DoodleApplication();
        ~DoodleApplication() = default;

        DoodleApplication(const DoodleApplication&) = delete;
        DoodleApplication(DoodleApplication&&) = delete;
        
        DoodleApplication& operator= (const DoodleApplication&) = delete;
        
    public:
        /**
         * DoodleApplication初始化
         */
        void Initialize();

        /**
         * DoodleApplication运行
         */
        void Run();

        /**
         * DoodleApplication销毁
         */
        void Destroy();

    private:
        /**
         * 初始化Window
         */
        void InitWindow();

        /**
         * 销毁Window
         */
        void DestroyWindow();

        /**
         * 窗口尺寸变化回调
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
         * @param pWindow 触发回调的窗口
         * @param width 新的像素宽度，本工程不使用，尺寸由交换链重建时现查
         * @param height 新的像素高度，同上
         */
        static void WindowResizeCallback(GLFWwindow* pWindow, int width, int height);

        /**
         * 初始化Vulkan
         */
        void InitVulkan();

        /**
         * 销毁Vulkan
         */
        void DestroyVulkan();

    private:
        /**
         * 窗口
         */
        GLFWwindow* m_pWindow;

        /**
         * 窗口宽度
         */
        int32_t m_windowWidth;

        /**
         * 窗口高度
         */
        int32_t m_windowHeight;

        /**
         * Vulkan子系统
         */
        DoodleVulkanManager m_vulkanManager;
    };
}
