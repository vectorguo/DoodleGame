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
