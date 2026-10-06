//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include "DoodleRenderPass.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

//前向声明。必须放在全局作用域
struct GLFWwindow;

namespace Doodle
{
    /**
     * Vulkan子系统门面：持有设备层、交换链层与渲染通道层，对外提供统一的Vulkan入口
     */
    class DoodleVulkanManager
    {
    public:
        /**
         * 初始化Vulkan
         */
        void Initialize(GLFWwindow* pWindow);

        /**
         * 销毁Vulkan
         */
        void Destroy();

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] DoodleVulkanDevice& GetDevice()
        {
            return m_device;
        }

        /**
         * 获取Vulkan设备
         */
        [[nodiscard]] const DoodleVulkanDevice& GetDevice() const
        {
            return m_device;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] DoodleSwapChain& GetSwapChain()
        {
            return m_swapChain;
        }

        /**
         * 获取交换链
         */
        [[nodiscard]] const DoodleSwapChain& GetSwapChain() const
        {
            return m_swapChain;
        }

        /**
         * 获取渲染通道
         */
        [[nodiscard]] DoodleRenderPass& GetRenderPass()
        {
            return m_renderPass;
        }

        /**
         * 获取渲染通道
         */
        [[nodiscard]] const DoodleRenderPass& GetRenderPass() const
        {
            return m_renderPass;
        }

    private:
        /**
         * Vulkan设备层：Instance / Surface / 物理设备 / 逻辑设备 / 队列
         */
        DoodleVulkanDevice m_device;

        /**
         * 交换链层：窗口的呈现目标
         */
        DoodleSwapChain m_swapChain;

        /**
         * 渲染通道层：本帧的渲染目标结构，格式取自交换链，须排在交换链之后
         */
        DoodleRenderPass m_renderPass;
    };
}
