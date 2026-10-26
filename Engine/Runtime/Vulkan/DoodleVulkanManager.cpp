//
// Created by 郭智 on 2026/9/5.
//

#include "DoodleVulkanManager.h"

#include "../Platform/DoodleWindow.h"
#include "DoodleDrawItem.h"
#include "DoodleUniformBufferObject.h"
#include "DoodleVulkanUtils.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace Doodle
{
    void DoodleVulkanManager::Initialize(DoodleWindow& window)
    {
        //窗口只借不放，本层不负责它的创建与销毁
        m_pWindow = &window;

        m_device.Initialize(window);
        m_swapChain.Initialize(m_device, window);

        //深度图按交换链尺寸建；图形管线要读它的格式，所以它必须排在管线之前
        m_depthImages.Initialize(m_device, m_swapChain.GetImageExtent());
        m_graphicsPipeline.Initialize(m_device, m_swapChain, m_depthImages);
        m_commandBuffer.Initialize(m_device, m_swapChain, m_graphicsPipeline, m_depthImages);

        //UBO 每帧更新，走 HostVisible 类型（CPU 直接写），不走设备本地 + 中转。
        //映射由第一次 Write 建立，这里只负责分配。
        //描述符池/集必须排在 UBO 与图形管线之后：池要分配集合，集合要读取已经建好的
        //图形管线 set layout，写入时又要有 UBO 句柄
        CreateUniformBuffers();
        CreateDescriptorPool();
        CreateDescriptorSets();

        m_syncObjects.Initialize(m_device, m_swapChain);
    }

    void DoodleVulkanManager::WaitIdle()
    {
        vkDeviceWaitIdle(m_device.GetLogicalDevice());
    }

    void DoodleVulkanManager::Destroy()
    {
        //销毁前必须确认 GPU 已经空闲。帧循环里所有提交都是异步的，
        //跳出主循环那一刻可能还有绘制或呈现在途，此时销毁它们引用的资源是未定义行为。
        //
        //放在这里而不是主循环之后：本函数是唯一的销毁入口，
        //把不变量守在不变量真正起作用的地方，将来多一个调用方也不会漏掉
        WaitIdle();

        //按「使用者先、被使用者后」销毁。
        //
        //命令缓冲先走，因为它录下的绘制命令可能引用描述符集；接着池走，
        //集合随之失效；然后 UBO 才能安全销毁。图形管线持有 set layout，
        //必须排在池之后。其余资源与这几条依赖无关，按原有逆序排列
        m_syncObjects.Destroy();
        m_commandBuffer.Destroy();

        DestroyDescriptorPool();
        DestroyUniformBuffers();

        m_graphicsPipeline.Destroy();
        m_depthImages.Destroy();
        m_swapChain.Destroy();
        m_device.Destroy();
    }

    void DoodleVulkanManager::Suspend()
    {
        //在途的帧可能还在引用即将拆掉的交换链图像，必须先停稳
        vkDeviceWaitIdle(m_device.GetLogicalDevice());

        //逆着创建序拆：同步对象 → 交换链。
        //
        //同步对象其实不引用任何一层（它的 Destroy 只碰设备），排在最先是为了让全场
        //只有一条规矩：建的时候什么序，拆的时候就倒过来。读的人不必逐层去推谁依赖谁
        m_syncObjects.Destroy();
        m_depthImages.Destroy();
        m_swapChain.Destroy();

        //表面本身最后拆。它归设备层管，但失效的时机由窗口决定，
        //所以由本层在收到通知时转达
        m_device.DestroySurface();
    }

    void DoodleVulkanManager::Resume(DoodleWindow& window)
    {
        m_pWindow = &window;

        //先把表面建起来 —— 这是恢复与「交换链过时」唯一不同的地方。
        //顺序与 Initialize 的前半段一致，只是实例与设备已经在了
        m_device.CreateSurface(window);

        //表面换了，present 队列族的支持情况要重查一遍。
        //同一台设备上不会变，但万变了后面所有提交都是错的
        m_device.VerifyQueueFamilySelection();

        //交换链及其下游的重建，与「交换链过时」是同一件事，交给同一个例程办。
        //Suspend 刚拆过，那几层此刻是空的，里面那次拆解与设备等待都落在空处 ——
        //所以这里不必为「已经拆干净了」另开一条路
        RecreateSwapChain();
    }

    void DoodleVulkanManager::DrawFrame(const std::vector<DoodleDrawItem>& drawItems)
    {
        //表面已被 Suspend 拆掉，没有可呈现的目标。
        //驱动层本就不该在这时调进来，但这个检查比崩溃便宜得多 ——
        //Android 上漏掉一次状态判断，代价是一段看不出因果的 tombstone
        if (m_swapChain.GetSwapChain() == VK_NULL_HANDLE)
        {
            return;
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
        //renderFinished 不在这里取——它按图像索引区分，要等②拿到 imageIndex 之后才能取
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

        //交换链已经和窗口对不上了，这次没拿到可用图像。重建之后直接放弃本帧重来 ——
        //必须直接返回：imageIndex 是旧交换链的索引，重建后那个位置已经不是同一张图了
        //
        //注意 imageAvailableSemaphore 此刻的状态：acquire 失败时它会留在「已发出信号」状态，
        //而 vkDeviceWaitIdle 覆盖不到它 —— 那个等待只管队列上的提交，
        //呈现引擎那边发起的信号不在其列。所以它绝不能再拿去 acquire 第二次
        //（会撞 VUID-vkAcquireNextImageKHR-semaphore-01286：必须是 unsignaled）。
        //重建时把同步对象整层换新、连同这个信号量一起销毁重来，正是为了绕开这一点
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapChain();
            return;
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

        //③' 写本帧的 UBO。位置不能更早也不能更晚：
        //    栅栏已等过，说明 GPU 不再读本槽位上一轮的数据；
        //    命令还没开始录，说明本帧还没有引用它。
        //    用同一块 UBO 给两个在途帧共用，会在这两者之间形成读写竞争
        UpdateUniformBuffer(frameIndex);

        //④ 录制本帧命令。命令缓冲层不缓存任何上游句柄，录制时现取，
        //   所以交换链重建之后重录拿到的自然是新句柄。
        //   它自己那条命令缓冲由它按 frameIndex 取 —— 本层交出帧索引即可，
        //   不必先取好句柄再传进去，也就没有「传错槽位」这种可能。
        //   描述符集同理：它归本层持有，Record 只在本帧使用它
        //
        //   返回值就是刚录好的那一条，⑤ 提交要用。提交归门面，
        //   但「本帧录在哪一条」归命令缓冲层 —— 让它顺手交回来，两边就不可能指向不同的槽位
        //
        //   绘制项由调用方（应用层）从场景里收集好交进来，本层不关心它们
        //   从哪来。空列表也会录一条只做布局转换、不画东西的命令缓冲
        const auto commandBuffer = m_commandBuffer.Record(frameIndex, imageIndex,
                                                          drawItems,
                                                          m_descriptorSets[frameIndex]);

        //⑤ 提交到图形队列
        //
        //sync2 之后这里没有平行数组了：每个信号量各自成一个结构体，要等的阶段写在
        //信号量自己身上（原来是和信号量平行、靠下标对齐的 pWaitDstStageMask），
        //命令缓冲也收进 VkCommandBufferSubmitInfo ——
        //「两个数组下标必须一一对应」这件事从「容易写错」变成「写不出来」
        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer;

        VkSemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemaphoreInfo.semaphore = imageAvailableSemaphore;
        //等到图像真的可用再开始写颜色。这里选 COLOR_ATTACHMENT_OUTPUT 而不是 TOP_OF_PIPE，
        //是让顶点着色器之类不碰这张图的工作可以先跑起来。
        //这个取值必须与 RecordImageTransitionToColorAttachment 里那条屏障的 srcStageMask 一致，
        //两边对不上就会出现依赖链断裂。原来对齐的对方是渲染通道里的子通道依赖，
        //动态渲染之后换成了命令缓冲里的屏障 —— 规矩没变，位置变了
        waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        //绘制完成时发出信号，供⑥的呈现等待。
        //这里按图像索引取，而不是用某个固定信号量：能 acquire 到第 imageIndex 张图，
        //说明呈现引擎已经用完它，也就必然完成了对「属于它的那个信号量」的等待，
        //此刻它必定是 unsignaled，signal 它才合法。
        //全场共用一个就会出现「上一帧的呈现还没消费掉就又被 signal」的竞争
        const auto renderFinishedSemaphore = m_syncObjects.GetRenderFinishedSemaphore(imageIndex);
        VkSemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSemaphoreInfo.semaphore = renderFinishedSemaphore;
        //signal 这一格的 stageMask 说的是「等到哪些阶段结束才发信号」，不是「谁在等」。
        //取 ALL_COMMANDS：出口那条布局转换也被排在「本批工作结束之前」，信号量必须等它
        signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

        //最后一个参数把栅栏挂到这次提交上：GPU 画完时它会被置为已发出信号，
        //供下一帧的①等待
        if (vkQueueSubmit2(m_device.GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        //⑥ 把这张图像交回交换链呈现。等的是⑤里刚发出信号的那个信号量，
        //   也就是「绘制已完成」，保证屏幕上不会出现半成品。
        //呈现这边没有 sync2 版本：VkPresentInfoKHR 收的还是裸句柄，没有阶段要填
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

        const VkSwapchainKHR swapChains[] = {m_swapChain.GetSwapChain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        const auto presentResult = vkQueuePresentKHR(m_device.GetPresentQueue(), &presentInfo);

        //重建判断必须放在呈现之后。提前退出的话，acquire 唤醒的那个
        //imageAvailable 信号量没人消费，它对应的图像也永远不会归还交换链 ——
        //下一轮再去 signal 一个已处于信号态的二值信号量是未定义行为。
        //走到这里时两个信号量都已经被正常等待过，状态是干净的
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_windowResized)
        {
            //m_windowResized 由 RecreateSwapChain 末尾统一清零 —— 重建完成了，
            //积压的 resize 通知就算兑现
            RecreateSwapChain();
        }
        else if (presentResult != VK_SUCCESS)
        {
            throw std::runtime_error("failed to present swap chain image!");
        }
    }

    void DoodleVulkanManager::RecreateSwapChain()
    {
        //窗口像素尺寸为 0（桌面上的最小化）时建不出交换链，在这里阻塞到能画为止。
        //桌面实现里是 glfwWaitEvents / WaitMessage 循环；Android 没有这个状态，实现是空的
        m_pWindow->WaitUntilDrawable();

        //等待也会被关闭请求打断（见各平台 WaitUntilDrawable）：最小化着从任务栏把窗口关掉，
        //回到这里时尺寸仍是 0×0。帧循环下一轮就会退出，这次不重建 —— 拿 0×0 去建交换链和
        //深度图是非法调用（Windows 上实测，校验层报 VUID-VkSwapchainCreateInfoKHR-imageExtent-01689）。
        //判断放在拆旧的之前：旧的那套原样留着，由 Destroy 照常拆掉
        if (m_pWindow->IsCloseRequested())
        {
            return;
        }

        //帧循环里的提交都是异步的，此刻可能还有绘制或呈现在途，
        //此时销毁它们引用的资源是未定义行为
        vkDeviceWaitIdle(m_device.GetLogicalDevice());

        //按依赖倒序销毁
        m_syncObjects.Destroy();
        m_depthImages.Destroy();
        m_swapChain.Destroy();

        //按依赖正序创建，顺序与 Initialize 里这几层的一致。
        //
        //图形管线不在这里重建。它把颜色与深度格式按值编译进了管线，格式真变了
        //它就不再匹配 —— 但那个格式要变，得把窗口拖到另一块不同色域的显示器上，
        //是极罕见的情况；而重建代价是把管线整个重编译一遍（几十毫秒）。
        //为一次窗口缩放付这个钱不值得。真到要处理的那天，判据是这里比对格式变没变，
        //变了就重建管线；现在不写，是不给它留一条没人走的路径
        //
        //命令缓冲也不重建：它不缓存任何上游句柄，每帧录制时现取，
        //重建后重录拿到的自然是新的
        m_swapChain.Initialize(m_device, *m_pWindow);
        //深度图的尺寸跟着交换链走，所以它必须在这里重建；它的格式不会因缩放而变，
        //图形管线因此不必跟着重建
        m_depthImages.Initialize(m_device, m_swapChain.GetImageExtent());
        m_syncObjects.Initialize(m_device, m_swapChain);

        //到这里交换链已经照当前窗口尺寸建好了，积压的 resize 通知就算兑现。
        //清零排在末尾而不是开头：上面那句 WaitUntilDrawable 会泵事件，泵的过程中
        //可能再收到一次 resize 回调把它重新置位；而它之后取的尺寸是泵完之后的，
        //所以在这一刻清零，清掉的信息确实已被本轮重建覆盖。
        //不清的话，下一次呈现之后会再重建一次交换链，白做一遍
        m_windowResized = false;

        //m_currentFrame 不用复位：同步对象整层换新，三个栅栏都是「已发出信号」状态，
        //重建后第一帧的①会立刻通过，和程序刚启动时是同一个情形
    }

    void DoodleVulkanManager::CreateUniformBuffers()
    {
        constexpr VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        //每帧一块，不允许共用：在途帧的 GPU 可能还在读上一轮，
        //host 这边却已经开始写下一轮的矩阵
        for (auto& buffer : m_uniformBuffers)
        {
            //只声明 UNIFORM_BUFFER 用途，不需要 TRANSFER_DST：
            //数据由 CPU 直接写，不经过拷贝命令。
            //这里只分配、不配置映射：第一次 Write 时才建立持久映射
            buffer.Initialize(
                m_device,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                DoodleGraphicsBuffer::EMemoryType::HostVisible);
        }
    }

    void DoodleVulkanManager::DestroyUniformBuffers()
    {
        //Destroy 内部会先解除持久映射，再销毁 buffer/memory
        for (auto& buffer : m_uniformBuffers)
        {
            buffer.Destroy();
        }
    }

    void DoodleVulkanManager::UpdateUniformBuffer(const uint32_t frameIndex)
    {
        UniformBufferObject ubo{};

        //相机放在 (2, 2, 2)，看向原点，以世界 Z 轴为上方向。
        //上方向不能与视线平行，否则 lookAt 的叉乘退化
        ubo.view = glm::lookAt(
            glm::vec3(2.0f, 2.0f, 2.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));

        //每帧重新读交换链尺寸：窗口缩放后宽高比自动正确，
        //描述符集与 UBO 对象本身都不需要重建
        const auto extent = m_swapChain.GetImageExtent();
        const auto preTransform = m_swapChain.GetPreTransform();

        //宽高比要用**用户看到的那一组**。交换链做了预旋转时，图像宽高和显示方向是对调的，
        //所以这里再对调回来 —— 透视必须按显示方向算，算完再转，才是同一份画面
        ubo.proj = glm::perspective(
            glm::radians(45.0f),
            DoodleVulkanUtils::GetDisplayAspect(extent, preTransform),
            0.1f,
            10.0f);

        //GLM 默认按 OpenGL 的裁剪空间生成投影；Vulkan 的 Y 轴方向相反。
        //Z 轴方向已经由 CMake 里的 GLM_FORCE_DEPTH_ZERO_TO_ONE 处理，
        //这里只剩 Y 需要翻转。
        //
        //这一步必须在下面的预旋转之前：它负责"换成 Vulkan 的 NDC"，
        //而预旋转是在 Vulkan 的 NDC 里做的
        ubo.proj[1][1] *= -1.0f;

        //最后把整幅画面转到图像自己的朝向上。桌面拿到的是 IDENTITY，这一乘是单位阵
        ubo.proj = DoodleVulkanUtils::MakePreRotation(preTransform) * ubo.proj;

        //HOST_COHERENT 保证不需要手动 flush；下一次 vkQueueSubmit 之后再读取时，
        //这份写入对 GPU 可见。写入位置已在调用处保证：栅栏之后、录制之前。
        //第一次调用会在缓冲内部建立持久映射，之后每帧只是一次 memcpy
        //
        //这里只有相机数据。每个对象的 model 走 push constant，
        //由 DoodleCommandBuffer::Record 在逐个 draw item 时推进命令缓冲
        m_uniformBuffers[frameIndex].Write(&ubo, sizeof(ubo));
    }

    void DoodleVulkanManager::CreateDescriptorPool()
    {
        //每种描述符要多少：每在途帧一个 UBO
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        //不单独释放某个集合：集合寿命与程序一致，随池一起销毁即可
        poolInfo.flags = 0;
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(m_device.GetLogicalDevice(), &poolInfo, nullptr,
                                   &m_descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void DoodleVulkanManager::DestroyDescriptorPool()
    {
        if (m_descriptorPool != VK_NULL_HANDLE)
        {
            //池销毁时，池里分配的描述符集自动失效，不需要逐个 vkFreeDescriptorSets
            vkDestroyDescriptorPool(m_device.GetLogicalDevice(), m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        m_descriptorSets.fill(VK_NULL_HANDLE);
    }

    void DoodleVulkanManager::CreateDescriptorSets()
    {
        //pSetLayouts 要求「每个集合一个布局句柄」，不会自动广播同一个布局，
        //所以即使所有集合共用同一份布局，也要把它重复 N 次
        const auto descriptorSetLayout = m_graphicsPipeline.GetDescriptorSetLayout();
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        m_descriptorSets.fill(VK_NULL_HANDLE);
        if (vkAllocateDescriptorSets(m_device.GetLogicalDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate descriptor sets!");
        }

        //把第 i 块 UBO 接到第 i 个集合的 binding 0。
        //这一步是 host 侧更新，不是录制命令；集合在首次绘制前写完，之后不再改
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_uniformBuffers[i].GetBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr;
            descriptorWrite.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(m_device.GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
        }
    }
}
