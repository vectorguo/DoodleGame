//
// Created by 郭智 on 2026/9/5.
//

#include "DoodleVulkanManager.h"

#include <cstdint>
#include <stdexcept>

namespace Doodle
{
    void DoodleVulkanManager::Initialize(GLFWwindow* pWindow)
    {
        m_device.Initialize(pWindow);
        m_swapChain.Initialize(m_device, pWindow);
        m_renderPass.Initialize(m_device, m_swapChain);
        m_graphicsPipeline.Initialize(m_device, m_renderPass);
        m_frameBuffer.Initialize(m_device, m_swapChain, m_renderPass);
        m_commandBuffer.Initialize(m_device, m_swapChain, m_renderPass, m_graphicsPipeline, m_frameBuffer);
        m_syncObjects.Initialize(m_device, m_swapChain);
    }

    void DoodleVulkanManager::Destroy()
    {
        //销毁前必须确认 GPU 已经空闲。帧循环里所有提交都是异步的，
        //跳出主循环那一刻可能还有绘制或呈现在途，此时销毁它们引用的资源是未定义行为。
        //
        //放在这里而不是主循环之后：本函数是唯一的销毁入口，
        //把不变量守在不变量真正起作用的地方，将来多一个调用方也不会漏掉
        vkDeviceWaitIdle(m_device.GetLogicalDevice());

        //逆序销毁
        m_syncObjects.Destroy();
        m_commandBuffer.Destroy();
        m_frameBuffer.Destroy();
        m_graphicsPipeline.Destroy();
        m_renderPass.Destroy();
        m_swapChain.Destroy();
        m_device.Destroy();
    }

    void DoodleVulkanManager::DrawFrame()
    {
        const auto logicalDevice = m_device.GetLogicalDevice();

        //句柄先取到局部变量：getter 都是按值返回，直接对返回值取地址取到的是临时量的地址。
        //renderFinished 不在这里取——它按图像索引区分，要等②拿到 imageIndex 之后才能取
        const auto inFlightFence = m_syncObjects.GetInFlightFence();
        const auto imageAvailableSemaphore = m_syncObjects.GetImageAvailableSemaphore();

        //① 等待上一帧结束。栅栏发出信号说明 GPU 已经用完命令缓冲和这对信号量，
        //   可以安全复用。必须在 vkResetFences 之前等待——顺序反过来的话，
        //   wait 会立刻返回，紧接着就会去覆写 GPU 还在用的命令缓冲
        vkWaitForFences(logicalDevice, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(logicalDevice, 1, &inFlightFence);

        //② 从交换链取一张图像。imageIndex 每次调用都可能不同，
        //   它决定本帧画到哪个帧缓冲，不要写死
        uint32_t imageIndex = 0;
        //这里不检查返回值：可能返回 VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR，
        //它们表示交换链与窗口不再匹配，属于要重建交换链的正常状态，而非应当终止程序的错误。
        //本工程窗口不可缩放(GLFW_RESIZABLE 为 GLFW_FALSE)，基本不会触发，
        //等以后要做窗口缩放时，和交换链重建逻辑一起补
        vkAcquireNextImageKHR(logicalDevice, m_swapChain.GetSwapChain(), UINT64_MAX,
                              imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        //③ 录制本帧命令。命令缓冲层不缓存句柄，录制时现取，
        //   所以交换链重建之后重录拿到的自然是新句柄
        m_commandBuffer.RecordCommandBuffer(m_commandBuffer.GetCommandBuffer(), imageIndex);

        //④ 提交到图形队列
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        //等到图像真的可用再开始写颜色。这里选 COLOR_ATTACHMENT_OUTPUT 而不是 TOP_OF_PIPE，
        //是让顶点着色器之类不碰这张图的工作可以先跑起来。
        //这个取值必须与 DoodleRenderPass 里那条子通道依赖的 srcStageMask 一致，
        //两边对不上就会出现依赖链断裂
        const VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
        const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        const VkCommandBuffer commandBuffers[] = {m_commandBuffer.GetCommandBuffer()};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = commandBuffers;

        //绘制完成时发出信号，供⑤的呈现等待。
        //这里按图像索引取，而不是用某个固定信号量：能 acquire 到第 imageIndex 张图，
        //说明呈现引擎已经用完它，也就必然完成了对「属于它的那个信号量」的等待，
        //此刻它必定是 unsignaled，signal 它才合法。
        //全场共用一个就会出现「上一帧的呈现还没消费掉就又被 signal」的竞争
        const VkSemaphore signalSemaphores[] = {m_syncObjects.GetRenderFinishedSemaphore(imageIndex)};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        //最后一个参数把栅栏挂到这次提交上：GPU 画完时它会被置为已发出信号，
        //供下一帧的①等待
        if (vkQueueSubmit(m_device.GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        //⑤ 把这张图像交回交换链呈现。等的是④里刚发出信号的那批信号量，
        //   也就是「绘制已完成」，保证屏幕上不会出现半成品
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        const VkSwapchainKHR swapChains[] = {m_swapChain.GetSwapChain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        //返回值同样不检查，理由见②
        vkQueuePresentKHR(m_device.GetPresentQueue(), &presentInfo);
    }
}
