//
// Created by 郭智 on 2026/10/7.
//

#include "DoodleSyncObjects.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleSyncObjects::Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;

        CreateSyncObjects();
    }

    void DoodleSyncObjects::Destroy()
    {
        DestroySyncObjects();
    }

    void DoodleSyncObjects::CreateSyncObjects()
    {
        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //VkSemaphoreCreateInfo 在当前版本除了 sType 没有必填字段，
        //flags 与 pNext 是留给以后版本或扩展的
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        //栅栏要创建成「已发出信号」状态。第一帧的 vkWaitForFences 会等这个栅栏，
        //可第一帧并不存在上一帧去给它发信号，不加这个 flag 会永久阻塞。
        //这不是在伪造信号：程序启动时确实没有任何在途工作，
        //「已完成」就是那一刻真实的初始状态。
        //帧循环第一次 vkResetFences 之后，它就恢复成普通的未发出信号栅栏
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &m_pImageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateFence(logicalDevice, &fenceInfo, nullptr, &m_pInFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create sync objects!");
        }

        //renderFinished 每个交换链图像一个，而不是全场只用一个。
        //
        //只用一个会踩 VUID-vkQueueSubmit-pSignalSemaphores-00067：这个信号量由本帧的提交
        //发出信号、由呈现等待，而「呈现引擎何时消费掉它」不在 inFlightFence 的覆盖范围内
        //——那个栅栏只保证图形队列自己的提交已完成。于是下一帧提交时，同一个信号量可能
        //还停在 signaled 状态，再 signal 它就是错的。这个竞争真实存在，只是只在特定时序下暴露，
        //表现为开头几帧偶发报错，之后又安静下来。
        //
        //按图像分配能根治：能 acquire 到第 k 张图，就说明呈现引擎已经用完第 k 张图了，
        //也就必然完成了对第 k 个信号量的等待，它此刻必定是 unsignaled。
        //
        //imageAvailable 不需要跟着拆：它是被本帧的提交等待的，
        //而提交完成由 inFlightFence 保证，单个复用是安全的。
        //（也拆不了——acquire 之前还不知道 imageIndex，没法按图像索引挑信号量。）
        const auto imageCount = static_cast<uint32_t>(m_pSwapChain->GetImages().size());
        m_renderFinishedSemaphores.resize(imageCount, VK_NULL_HANDLE);

        for (auto& renderFinishedSemaphore : m_renderFinishedSemaphores)
        {
            if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create sync objects!");
            }
        }
    }

    void DoodleSyncObjects::DestroySyncObjects()
    {
        //所有对象都引用了逻辑设备，必须先于设备层销毁
        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        if (m_pImageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(logicalDevice, m_pImageAvailableSemaphore, nullptr);
            m_pImageAvailableSemaphore = VK_NULL_HANDLE;
        }

        for (auto& renderFinishedSemaphore : m_renderFinishedSemaphores)
        {
            if (renderFinishedSemaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(logicalDevice, renderFinishedSemaphore, nullptr);
            }
        }
        m_renderFinishedSemaphores.clear();

        if (m_pInFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(logicalDevice, m_pInFlightFence, nullptr);
            m_pInFlightFence = VK_NULL_HANDLE;
        }
    }
}
