//
// Created by 郭智 on 2026/9/5.
//

#include "DoodleVulkanManager.h"

#include "DoodleVulkanConfig.h"

#include <cstdint>
#include <stdexcept>

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
        //两份顶点数据要赶在命令缓冲第一次录制之前送上 GPU：DrawFrame 里的 Record
        //会把它们绑上去，那时句柄还是空的话校验层当场报错。
        //
        //排在这里还有一条更硬的理由：上传要提交一次拷贝，而提交用的那个传输命令池
        //是上面那一行建出来的。顺序反了就是拿着空池去录命令
        CreateVertexBuffer();
        CreateIndexBuffer();
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
        DestroyIndexBuffer();
        DestroyVertexBuffer();
        m_commandBuffer.Destroy();
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

    void DoodleVulkanManager::DrawFrame()
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

        //④ 录制本帧命令。命令缓冲层不缓存任何上游句柄，录制时现取，
        //   所以交换链重建之后重录拿到的自然是新句柄。
        //   它自己那条命令缓冲由它按 frameIndex 取 —— 本层交出帧索引即可，
        //   不必先取好句柄再传进去，也就没有「传错槽位」这种可能。
        //   顶点与索引缓冲反着来：它们不随交换链重建，所以由本层持有、每帧当参数递进去。
        //   命令缓冲层使用它们，但拥有它们的是这里 —— 谁使用和谁拥有是两件事。
        //   索引数传 m_indices 的条数（6），不是 m_vertices 的条数（4）——
        //   vkCmdDrawIndexed 要的是「按几个索引去取顶点」
        //
        //   返回值就是刚录好的那一条，⑤ 提交要用。提交归门面，
        //   但「本帧录在哪一条」归命令缓冲层 —— 让它顺手交回来，两边就不可能指向不同的槽位
        const auto commandBuffer = m_commandBuffer.Record(frameIndex, imageIndex,
                                                          m_vertexBuffer.GetBuffer(), m_indexBuffer.GetBuffer(),
                                                          static_cast<uint32_t>(m_indices.size()));

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
        //桌面实现里是 glfwWaitEvents 循环；Android 没有这个状态，实现是空的
        m_pWindow->WaitUntilDrawable();

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

    void DoodleVulkanManager::CreateDeviceLocalBuffer(DoodleGraphicsBuffer& buffer,
                                                      VkBufferUsageFlags usage,
                                                      const void* pData,
                                                      VkDeviceSize size)
    {
        //正式缓冲：内存取设备本地，并将用途补上 TRANSFER_DST —— 它要当下面那次拷贝的目标。
        //少了这一位，vkCmdCopyBuffer2 会撞校验层（VUID-vkCmdCopyBuffer2-dstBuffer-00133）
        buffer.Initialize(
            m_device,
            size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        //中转：反过来，要能当源（TRANSFER_SRC），且内存得 CPU 碰得到。
        //
        //局部量，不是成员 —— 它建出来、用完、立刻销毁，寿命只覆盖本函数中间几行。
        //做成成员反而会凭空引入「它此刻是否有效」这个状态问题，而这个状态问题
        //本来不存在。它同时也是一份不变量：只要它还活着，数据就还没搬完
        DoodleGraphicsBuffer stagingBuffer;
        stagingBuffer.Initialize(
            m_device,
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        //先把数据 memcpy 进中转，再录拷贝命令 —— 这个顺序是规范给的免费保证：
        //写在前、提交在后，拷贝命令读到的必然是写完的数据，中间不需要任何屏障。
        //详见 DoodleGraphicsBuffer::Upload 末尾
        stagingBuffer.Upload(pData, size);

        //到这里 CPU 的事做完了，剩下这次搬运由 GPU 自己完成。
        //入口与实现都在 DoodleGraphicsBuffer 上，命令池从命令缓冲层的单例借
        stagingBuffer.CopyTo(buffer, size);

        //销毁中转。这一步必须排在 CopyTo 之后 —— 它返回时拷贝才真正做完，
        //提前销毁就是销毁一份还在被命令引用的缓冲
        stagingBuffer.Destroy();
    }

    void DoodleVulkanManager::CreateVertexBuffer()
    {
        CreateDeviceLocalBuffer(m_vertexBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                m_vertices.data(), sizeof(Vertex) * m_vertices.size());
    }

    void DoodleVulkanManager::DestroyVertexBuffer()
    {
        m_vertexBuffer.Destroy();
    }

    void DoodleVulkanManager::CreateIndexBuffer()
    {
        CreateDeviceLocalBuffer(m_indexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                m_indices.data(), sizeof(uint16_t) * m_indices.size());
    }

    void DoodleVulkanManager::DestroyIndexBuffer()
    {
        m_indexBuffer.Destroy();
    }
}
