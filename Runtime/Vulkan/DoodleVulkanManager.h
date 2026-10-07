//
// Created by 郭智 on 2026/9/5.
//

#pragma once

#include <cstdint>

#include "DoodleCommandBuffer.h"
#include "DoodleFrameBuffer.h"
#include "DoodleGraphicsPipeline.h"
#include "DoodleRenderPass.h"
#include "DoodleSwapChain.h"
#include "DoodleSyncObjects.h"
#include "DoodleVulkanDevice.h"

//前向声明。必须放在全局作用域
struct GLFWwindow;

namespace Doodle
{
    /**
     * Vulkan子系统门面：按依赖顺序持有各层，对外提供统一的Vulkan入口
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
         * 渲染并呈现一帧
         *
         * 主循环每轮调用一次。六步固定：等本槽位上一轮结束、取一张交换链图像、
         * 录制命令缓冲、提交、呈现、推进槽位。顺序不能调换，每一步都在为下一步准备前提条件
         *
         * 同一时刻允许 MAX_FRAMES_IN_FLIGHT 帧在途：host 录第 k 帧时，
         * GPU 可以还在跑第 k-1 帧，两者用的是各自槽位的资源，不会互相踩踏
         *
         * 放在门面而不是上层：它一行窗口相关的东西都不碰，
         * 只是把本门面自己持有的几层按顺序驱动一遍 —— 那正是门面的分内事
         *
         * @note 里面的 Vulkan 调用大多是异步的，返回不代表 GPU 已完成
         */
        void DrawFrame();

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

        /**
         * 获取图形管线
         */
        [[nodiscard]] DoodleGraphicsPipeline& GetGraphicsPipeline()
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取图形管线
         */
        [[nodiscard]] const DoodleGraphicsPipeline& GetGraphicsPipeline() const
        {
            return m_graphicsPipeline;
        }

        /**
         * 获取帧缓冲
         */
        [[nodiscard]] DoodleFrameBuffer& GetFrameBuffer()
        {
            return m_frameBuffer;
        }

        /**
         * 获取帧缓冲
         */
        [[nodiscard]] const DoodleFrameBuffer& GetFrameBuffer() const
        {
            return m_frameBuffer;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] DoodleCommandBuffer& GetCommandBuffer()
        {
            return m_commandBuffer;
        }

        /**
         * 获取命令缓冲层
         */
        [[nodiscard]] const DoodleCommandBuffer& GetCommandBuffer() const
        {
            return m_commandBuffer;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] DoodleSyncObjects& GetSyncObjects()
        {
            return m_syncObjects;
        }

        /**
         * 获取同步对象层
         */
        [[nodiscard]] const DoodleSyncObjects& GetSyncObjects() const
        {
            return m_syncObjects;
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

        /**
         * 图形管线层：引用渲染通道，须排在渲染通道之后
         */
        DoodleGraphicsPipeline m_graphicsPipeline;

        /**
         * 帧缓冲层：引用交换链图像视图与渲染通道，须排在两者之后
         */
        DoodleFrameBuffer m_frameBuffer;

        /**
         * 命令缓冲层：录制时引用上述各层，须排在帧缓冲之后
         */
        DoodleCommandBuffer m_commandBuffer;

        /**
         * 同步对象层：帧信号量与栅栏
         *
         * renderFinished 按交换链图像数量分配，所以它依赖交换链层，
         * 且随交换链一起重建，须排在交换链之后
         */
        DoodleSyncObjects m_syncObjects;

        /**
         * 当前在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1，每帧结束时推进一格
         *
         * 命令缓冲、imageAvailable 信号量、帧栅栏都按它轮转取用。
         * renderFinished 不按它取 —— 那是按交换链图像索引的，两个维度互不相干
         */
        uint32_t m_currentFrame = 0;
    };
}
