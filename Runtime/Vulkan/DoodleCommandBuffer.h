//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsPipeline.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 命令缓冲层：录制本帧要提交给图形队列的全部命令
     *
     * 生命周期与程序一致。命令池与命令缓冲都只建一次，反复重录不重建；
     * 交换链重建时本层也不重建 —— 本层不缓存任何上游层的句柄，
     * 录制时现取，重建后重录拿到的自然是新句柄
     *
     * 只读引用设备层、交换链层与图形管线层，生命周期须短于 DoodleSwapChain
     *
     * 动态渲染之后本层多担一件事：渲染目标的布局转换。
     * 渲染通道时代那是驱动在通道边界自动做的，现在两条屏障都写在这里
     *
     * 命令池与命令缓冲不拆成两个类：命令缓冲从池中分配、随池释放，
     * 两者的生命周期完全重合，没有各自独立的锚点
     */
    class DoodleCommandBuffer
    {
    public:
        /**
         * 初始化命令缓冲层
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         * @param graphicsPipeline 已初始化的图形管线
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const DoodleSwapChain& swapChain,
                        const DoodleGraphicsPipeline& graphicsPipeline);

        /**
         * 销毁命令缓冲层
         */
        void Destroy();

        /**
         * 获取第 frameIndex 个命令缓冲
         *
         * 每帧在途一份：一帧已经在 GPU 上执行时，另一帧要能同时被录制，
         * 共用一份会让录制覆盖掉正在执行的命令
         *
         * @param frameIndex 当前在途帧索引，见 DoodleVulkanManager::m_currentFrame
         */
        [[nodiscard]] VkCommandBuffer GetCommandBuffer(const uint32_t frameIndex) const
        {
            return m_commandBuffers[frameIndex];
        }

        /**
         * 录制一帧的命令
         * @param pCommandBuffer 目标命令缓冲
         * @param imageIndex 当前交换链图像的索引，来自vkAcquireNextImageKHR
         *
         * 交换链的图像与图像视图都按 imageIndex 现取，所以本层不随交换链重建
         *
         * 名字不带 CommandBuffer：类名已经交代了宾语，重复一遍不带来信息
         */
        void Record(VkCommandBuffer pCommandBuffer, uint32_t imageIndex);

    private:
        /**
         * 录入口屏障：取图之后、开画之前，把交换链图像转成能当颜色附件用的布局
         *
         * 动态渲染不做自动布局转换，这件事原来是驱动在渲染通道入口替我们做的。
         *
         * 六格取值（两个布局、两个 access、两个 stage）是这一层对这个渲染目标的
         * 策略，不是可调输入，所以不走参数 ——「为什么取这几个值」写在实现里
         */
        void RecordAcquireBarrier(VkCommandBuffer pCommandBuffer, uint32_t imageIndex);

        /**
         * 录出口屏障：画完之后、交回呈现引擎之前，转回呈现布局
         */
        void RecordPresentBarrier(VkCommandBuffer pCommandBuffer, uint32_t imageIndex);

        /**
         * 创建命令池
         */
        void CreateCommandPool();

        /**
         * 销毁命令池，并把命令池与命令缓冲的句柄一并置空
         *
         * 不需要逐个销毁命令缓冲：缓冲从池中分配，随池一起释放
         */
        void DestroyCommandPool();

        /**
         * 从命令池分配命令缓冲，每个在途帧一个
         *
         * 前置条件：m_pCommandPool 已创建，且 m_commandBuffers 为空。
         * resize 不会重置已有槽位，未先销毁就再次调用会静默覆盖旧句柄并泄漏
         */
        void CreateCommandBuffers();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定，取渲染区域尺寸、图像与图像视图用
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 图形管线层，在 Initialize 时绑定
         */
        const DoodleGraphicsPipeline* m_pGraphicsPipeline = nullptr;

        /**
         * 命令池
         */
        VkCommandPool m_pCommandPool = VK_NULL_HANDLE;

        /**
         * 命令缓冲，每个在途帧一个，用帧索引取用。
         * 随命令池释放，不需要单独销毁
         */
        std::vector<VkCommandBuffer> m_commandBuffers;
    };
}
