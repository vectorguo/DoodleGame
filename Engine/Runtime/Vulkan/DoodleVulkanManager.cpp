//
//  DoodleVulkanManager.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/9.
//

#include "DoodleVulkanManager.h"

#include "../Platform/DoodleWindow.h"
#include "DoodleVulkanConfig.h"

#include <cstdint>
#include <stdexcept>

namespace Doodle
{
    DoodleVulkanManager* DoodleVulkanManager::s_pInstance = nullptr;

    void DoodleVulkanManager::Initialize(DoodleWindow& window)
    {
        //窗口只借不放，本层不负责它的创建与销毁
        m_pWindow = &window;

        m_device.Initialize(window);
        m_swapChain.Initialize(m_device, window);

        //命令层只建命令池与缓冲；它不认识交换链、深度图或管线
        m_commandManager.Initialize(m_device);

        m_syncObjects.Initialize(m_device, m_swapChain);
    }

    void DoodleVulkanManager::Destroy()
    {
        //销毁前必须确认 GPU 已经空闲。帧循环里所有提交都是异步的，
        //跳出主循环那一刻可能还有绘制或呈现在途，此时销毁它们引用的资源是未定义行为。
        //
        //渲染管线持有的深度图 / 描述符 / 管线由 DoodleRenderManager 在本函数之前
        //拆掉；本函数之后只剩设备级资源
        WaitForGpuIdle();

        //按「使用者先、被使用者后」销毁：同步对象 → 命令层 → 交换链 → 设备
        m_syncObjects.Destroy();
        m_commandManager.Destroy();
        m_swapChain.Destroy();
        m_device.Destroy();
    }

    EFrameStatus DoodleVulkanManager::BeginFrame(DoodleFrameContext& context)
    {
        //每帧复用同一个状态对象，先把后端字段重置，避免上一次的残值被误读。
        //输入字段（相机 / 光源 / 绘制项）归 DoodleRenderManager 与调用方，本层只填状态字段
        context.status = EFrameStatus::Skipped;
        context.frameIndex = 0;
        context.imageIndex = 0;
        context.commandBuffer = VK_NULL_HANDLE;

        //表面已被 OnSurfaceLost 拆掉，没有可呈现的目标。
        //驱动层本就不该在这时调进来，但这个检查比崩溃便宜得多 ——
        //Android 上漏掉一次状态判断，代价是一段看不出因果的 tombstone
        if (m_swapChain.GetSwapChain() == VK_NULL_HANDLE)
        {
            return context.status;
        }

        const auto logicalDevice = m_device.GetLogicalDevice();

        //本帧用哪一套 per-frame 资源，全部由 frameIndex 决定。
        //
        //在函数开头一次取出并推进，函数体里只认 frameIndex 这个局部量。
        //把推进放在末尾的话，m_currentFrame 会在函数执行到一半时悄悄变掉 ——
        //读代码的人得一直记着「下面这行用的是新的还是旧的」，很容易看漏。
        //先取值再推进，语义就是「m_currentFrame 指向本帧该用的槽位」，
        //取模让它始终落在 0 .. MAX_FRAMES_IN_FLIGHT-1，同一个槽位要隔满一轮才会再被碰到
        const uint32_t frameIndex = m_currentFrame;
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        //句柄先取到局部变量：getter 都是按值返回，直接对返回值取地址取到的是临时量的地址。
        //renderFinished 不在这里取——它按图像索引区分，要等拿到 imageIndex 之后才能取
        const auto inFlightFence = m_syncObjects.GetInFlightFence(frameIndex);
        const auto imageAvailableSemaphore = m_syncObjects.GetImageAvailableSemaphore(frameIndex);

        //① 等待本槽位上一轮的工作结束。栅栏发出信号说明 GPU 已经用完这一槽位的
        //   命令缓冲和 imageAvailable 信号量，可以安全复用。
        //   注意这里等的是「本槽位的上一轮」，不是「上一帧」：索引轮转，
        //   第 k 帧等的是第 k-MAX_FRAMES_IN_FLIGHT 帧留下的栅栏，那时 GPU 早就结束了。
        //   所以正常情况下这一句不会真的阻塞，CPU 不必等 GPU 画完当前帧就能接着录下一帧
        //   —— 多帧在途要换的正是这一点。
        //   必须在 vkResetFences 之前等待——顺序反过来的话，
        //   wait 会立刻返回，紧接着就会去覆写 GPU 还在用的命令缓冲
        vkWaitForFences(logicalDevice, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        //② 从交换链取一张图像。imageIndex 每次调用都可能不同，
        //   它决定本帧画到哪个帧缓冲，不要写死
        uint32_t imageIndex = 0;
        const auto acquireResult = vkAcquireNextImageKHR(
            logicalDevice,
            m_swapChain.GetSwapChain(),
            UINT64_MAX,
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex);

        //交换链已经和窗口对不上了，这次没拿到可用图像。交给调用方重建，本帧放弃 ——
        //必须直接返回：imageIndex 是旧交换链的索引，重建后那个位置已经不是同一张图了
        //
        //注意 imageAvailableSemaphore 此刻的状态：acquire 失败时它会留在「已发出信号」状态，
        //而 vkDeviceWaitIdle 覆盖不到它 —— 那个等待只管队列上的提交，
        //呈现引擎那边发起的信号不在其列。所以它绝不能再拿去 acquire 第二次
        //（会撞 VUID-vkAcquireNextImageKHR-semaphore-01286：必须是 unsignaled）。
        //重建时把同步对象整层换新、连同这个信号量一起销毁重来，正是为了绕开这一点
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            context.status = EFrameStatus::RecreateSwapChain;
            return context.status;
        }

        //VK_SUBOPTIMAL_KHR 不算错误：交换链略有偏差，但图像可用。
        //既然已经拿到图了，画完呈现出去比推倒重来划算，等帧尾的呈现之后再重建
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        //③ 到这里才把栅栏降下。vkResetFences 的含义是「我承诺马上提交一份工作」，
        //   所以它必须紧贴着真正提交的那一步 —— 上面任何一条提前返回的路径都会让这个
        //   承诺落空，栅栏停在未发出信号状态，下一轮等它就永远等不到
        vkResetFences(logicalDevice, 1, &inFlightFence);

        //命令缓冲的 begin 归命令层；录什么由渲染管线决定
        const auto commandBuffer = m_commandManager.BeginFrame(frameIndex);

        //同步句柄不进帧上下文：它们只在本层用，EndFrame 会按 frameIndex / imageIndex
        //从同步对象层现取。这里只把渲染侧需要的三样交出去
        context.status = EFrameStatus::Ready;
        context.frameIndex = frameIndex;
        context.imageIndex = imageIndex;
        context.commandBuffer = commandBuffer;
        return context.status;
    }

    EFrameStatus DoodleVulkanManager::EndFrame(const DoodleFrameContext& context)
    {
        //命令缓冲的 end 归命令层；之后本函数只负责提交与呈现，不再看它录了什么
        m_commandManager.EndFrame();

        //同步句柄按 context 里的索引现取 —— 它们只在后端内部使用，不经过调用方。
        //renderFinished 按图像索引取：能 acquire 到第 imageIndex 张图，说明呈现引擎
        //已经用完它，此刻它必定是 unsignaled，signal 它才合法；全场共用一个会出现
        //「上一帧的呈现还没消费掉就又被 signal」的竞争
        const auto inFlightFence = m_syncObjects.GetInFlightFence(context.frameIndex);
        const auto imageAvailableSemaphore = m_syncObjects.GetImageAvailableSemaphore(context.frameIndex);
        const auto renderFinishedSemaphore = m_syncObjects.GetRenderFinishedSemaphore(context.imageIndex);

        //⑤ 提交到图形队列
        //
        //sync2 之后这里没有平行数组了：每个信号量各自成一个结构体，要等的阶段写在
        //信号量自己身上（原来是和信号量平行、靠下标对齐的 pWaitDstStageMask），
        //命令缓冲也收进 VkCommandBufferSubmitInfo ——
        //「两个数组下标必须一一对应」这件事从「容易写错」变成「写不出来」
        const VkCommandBufferSubmitInfo commandBufferInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = context.commandBuffer
        };

        //等到图像真的可用再开始写颜色。这里选 COLOR_ATTACHMENT_OUTPUT 而不是 TOP_OF_PIPE，
        //是让顶点着色器之类不碰这张图的工作可以先跑起来。
        //这个取值必须与 DoodleForwardPass::RecordImageTransitionToColorAttachment 里
        //那条屏障的 srcStageMask 一致，两边对不上就会出现依赖链断裂。原来对齐的对方
        //是渲染通道里的子通道依赖，动态渲染之后换成了命令缓冲里的屏障 —— 规矩没变，
        //位置从命令层挪到了 pass，取值仍要成对
        const VkSemaphoreSubmitInfo waitSemaphoreInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = imageAvailableSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        //signal 这一格的 stageMask 说的是「等到哪些阶段结束才发信号」，不是「谁在等」。
        //取 ALL_COMMANDS：出口那条布局转换也被排在「本批工作结束之前」，信号量必须等它
        const VkSemaphoreSubmitInfo signalSemaphoreInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderFinishedSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
        };

        const VkSubmitInfo2 submitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitSemaphoreInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalSemaphoreInfo
        };

        //最后一个参数把栅栏挂到这次提交上：GPU 画完时它会被置为已发出信号，
        //供下一帧的①等待
        if (vkQueueSubmit2(m_device.GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        //⑥ 把这张图像交回交换链呈现。等的是⑤里刚发出信号的那个信号量，
        //   也就是「绘制已完成」，保证屏幕上不会出现半成品。
        //呈现这边没有 sync2 版本：VkPresentInfoKHR 收的还是裸句柄，没有阶段要填
        const VkSwapchainKHR swapChains[] = {m_swapChain.GetSwapChain()};
        const VkPresentInfoKHR presentInfo
        {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = swapChains,
            .pImageIndices = &context.imageIndex
        };

        const auto presentResult = vkQueuePresentKHR(m_device.GetPresentQueue(), &presentInfo);

        //重建判断必须放在呈现之后。提前退出的话，acquire 唤醒的那个
        //imageAvailable 信号量没人消费，它对应的图像也永远不会归还交换链 ——
        //下一轮再去 signal 一个已处于信号态的二值信号量是未定义行为。
        //走到这里时两个信号量都已经被正常等待过，状态是干净的。
        //m_windowResized 由 RecreateSwapChain 末尾统一清零
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_windowResized)
        {
            return EFrameStatus::RecreateSwapChain;
        }
        if (presentResult != VK_SUCCESS)
        {
            throw std::runtime_error("failed to present swap chain image!");
        }

        return EFrameStatus::Ready;
    }

    bool DoodleVulkanManager::RecreateSwapChain()
    {
        //窗口像素尺寸为 0（桌面上的最小化）时建不出交换链，在这里阻塞到能画为止。
        //桌面实现里是 glfwWaitEvents / WaitMessage 循环；Android 没有这个状态，实现是空的
        m_pWindow->WaitUntilDrawable();

        //等待也会被关闭请求打断（见各平台 WaitUntilDrawable）：最小化着从任务栏把窗口关掉，
        //回到这里时尺寸仍是 0×0。帧循环下一轮就会退出，这次不重建 —— 拿 0×0 去建交换链
        //是非法调用（Windows 上实测，校验层报 VUID-VkSwapchainCreateInfoKHR-imageExtent-01689）。
        //判断放在拆旧的之前：旧的那套原样留着，由 Destroy 照常拆掉
        if (m_pWindow->IsCloseRequested())
        {
            return false;
        }

        //帧循环里的提交都是异步的，此刻可能还有绘制或呈现在途，
        //此时销毁它们引用的资源是未定义行为
        vkDeviceWaitIdle(m_device.GetLogicalDevice());

        //按依赖倒序销毁
        m_syncObjects.Destroy();
        m_swapChain.Destroy();

        //按依赖正序创建。
        //
        //命令缓冲不重建：它不缓存任何上游句柄，每帧录制时现取，
        //重建后重录拿到的自然是新的
        m_swapChain.Initialize(m_device, *m_pWindow);
        m_syncObjects.Initialize(m_device, m_swapChain);

        //到这里交换链已经照当前窗口尺寸建好了，积压的 resize 通知就算兑现。
        //清零排在末尾而不是开头：上面那句 WaitUntilDrawable 会泵事件，泵的过程中
        //可能再收到一次 resize 回调把它重新置位；而它之后取的尺寸是泵完之后的，
        //所以在这一刻清零，清掉的信息确实已被本轮重建覆盖。
        //不清的话，下一次呈现之后会再重建一次交换链，白做一遍
        m_windowResized = false;

        //m_currentFrame 不用复位：同步对象整层换新，三个栅栏都是「已发出信号」状态，
        //重建后第一帧的①会立刻通过，和程序刚启动时是同一个情形
        return true;
    }

    void DoodleVulkanManager::WaitForGpuIdle()
    {
        vkDeviceWaitIdle(m_device.GetLogicalDevice());
    }

    void DoodleVulkanManager::OnSurfaceLost()
    {
        //在途的帧可能还在引用即将拆掉的交换链图像，必须先停稳
        vkDeviceWaitIdle(m_device.GetLogicalDevice());

        //逆着创建序拆：同步对象 → 交换链。
        //
        //同步对象其实不引用任何一层（它的 Destroy 只碰设备），排在最先是为了让全场
        //只有一条规矩：建的时候什么序，拆的时候就倒过来。读的人不必逐层去推谁依赖谁
        m_syncObjects.Destroy();
        m_swapChain.Destroy();

        //表面本身最后拆。它归设备层管，但失效的时机由窗口决定，
        //所以由本层在收到通知时转达
        m_device.DestroySurface();
    }

    void DoodleVulkanManager::OnSurfaceRecreated(DoodleWindow& window)
    {
        m_pWindow = &window;

        //先把表面建起来 —— 这是恢复与「交换链过时」唯一不同的地方。
        //顺序与 Initialize 的前半段一致，只是实例与设备已经在了
        m_device.CreateSurface(window);

        //表面换了，present 队列族的支持情况要重查一遍。
        //同一台设备上不会变，但万变了后面所有提交都是错的
        m_device.VerifyQueueFamilySelection();

        //交换链与同步对象不在这里重建：DoodleRenderManager 会在本函数之后
        //调 RecreateSwapChain，再恢复渲染管线的深度图与 pass
    }
}
