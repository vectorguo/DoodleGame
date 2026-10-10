//
// Created by 郭智 on 2026/10/7.
//

#include <stdexcept>

#include "DoodleCommandBuffer.h"
#include "DoodleVulkanConfig.h"

namespace Doodle
{
    void DoodleCommandBuffer::Initialize(const DoodleVulkanDevice& device,
                                         const DoodleSwapChain& swapChain,
                                         const DoodleGraphicsPipeline& graphicsPipeline)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;
        m_pGraphicsPipeline = &graphicsPipeline;

        //命令缓冲从池中分配，池必须先建出来
        CreateCommandPool();
        CreateCommandBuffers();
    }

    void DoodleCommandBuffer::Destroy()
    {
        DestroyCommandPool();
    }

    void DoodleCommandBuffer::CreateCommandPool()
    {
        //池绑定的队列族必须与将来提交命令的队列一致。这里录的是绘制命令，
        //将来提交到图形队列，所以取 graphicsFamilyIndex。
        //多数桌面显卡上图形族与呈现族是同一个，填错了不会立刻暴露，
        //换到两个族不重合的设备上才会出问题
        const auto familyIndex = m_pDevice->GetQueueFamilySelection().graphicsFamilyIndex.value();

        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        //每帧都要在同一个缓冲上重录，需要能单独重置
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = familyIndex;

        const auto result = vkCreateCommandPool(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pCommandPool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void DoodleCommandBuffer::DestroyCommandPool()
    {
        //池引用了逻辑设备，必须先于设备层销毁
        vkDestroyCommandPool(m_pDevice->GetLogicalDevice(), m_pCommandPool, nullptr);
        m_pCommandPool = VK_NULL_HANDLE;

        //缓冲随池一起释放了，这里只是把句柄数组清空，不要再去逐个销毁
        m_commandBuffers.clear();
    }

    void DoodleCommandBuffer::CreateCommandBuffers()
    {
        //每个在途帧一份，用帧索引轮转。数量不取交换链图像数：
        //在途帧数决定「能提前录几帧」，图像数决定「能同时在屏几帧」，两者互不相干
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_pCommandPool;
        //主级缓冲才能提交到队列，次级只能被主级调用
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        //一次分配一批，直接写进 vector 的底层数组
        const auto result = vkAllocateCommandBuffers(m_pDevice->GetLogicalDevice(), &allocInfo, m_commandBuffers.data());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void DoodleCommandBuffer::Record(VkCommandBuffer pCommandBuffer, const uint32_t imageIndex)
    {
        //录制期间用到的句柄全部现取，本层不缓存副本，
        //这样交换链重建之后重录，拿到的就是新句柄
        const auto imageExtent = m_pSwapChain->GetImageExtent();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;                    //不承诺任何特殊用法
        beginInfo.pInheritanceInfo = nullptr;   //只对次级有意义，主级必须为空

        //对已录制过的缓冲调用 begin 会隐式重置它，命令缓冲不能追加只能重录
        if (vkBeginCommandBuffer(pCommandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        //① 取图之后、开画之前：把这张图转到「能当颜色附件用」的布局。
        //这一步原来归渲染通道入口（附件描述里的 initialLayout + 那条子通道依赖管时机），
        //动态渲染不做任何自动转换，两件事都落在这个函数里那条屏障上 ——
        //六格取值与理由写在它自己的实现里
        RecordAcquireBarrier(pCommandBuffer, imageIndex);

        //② 开始渲染。原来要指一个 VkRenderPass 加一个 VkFramebuffer，现在改成把
        //这批结构直接当参数写出来：视图、布局、load/store、清屏值。
        //
        //注意 imageLayout 是陈述句 ——「这张图**现在**处于什么布局」，不是转换请求。
        //上面那条屏障把它转成什么，这里就必须写什么；写成别的布局是未定义行为，
        //而且常见表现是「在自家驱动上碰巧能跑」
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_pSwapChain->GetImageViews()[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        //VkClearValue 是 union，逐层聚合初始化：VkClearValue → .color → float32[4]，三重大括号不能少
        colorAttachment.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        //渲染区域应与附件尺寸一致，不一致时驱动要额外处理边界，会变慢
        renderingInfo.renderArea.extent = imageExtent;
        renderingInfo.layerCount = 1;   //图像数组的层数，与 imageArrayLayers 一致，不是图像张数
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        //开始渲染。begin 与 end 必须成对，且落在同一个命令缓冲里
        vkCmdBeginRendering(pCommandBuffer, &renderingInfo);

        //绑定管线
        vkCmdBindPipeline(pCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pGraphicsPipeline->GetGraphicsPipeline());

        //视口与剪裁在管线层被声明为动态状态，没有烘焙进管线，只能在这里补上。
        //交换链尺寸变化时只影响这里，不用重建管线
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(imageExtent.width);
        viewport.height = static_cast<float>(imageExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(pCommandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = imageExtent;
        vkCmdSetScissor(pCommandBuffer, 0, 1, &scissor);

        //顶点数据目前硬编码在顶点着色器里，没有顶点缓冲，只画 3 个顶点。
        //这个 3 必须与着色器里 positions 数组的长度一致
        vkCmdDraw(pCommandBuffer, 3, 1, 0, 0);

        //结束渲染
        vkCmdEndRendering(pCommandBuffer);

        //③ 画完、交回呈现引擎之前：转回呈现布局 —— 原来是渲染通道 finalLayout 的活。
        //为什么这一条的六格与入口那条不对称，写在它自己的实现里
        RecordPresentBarrier(pCommandBuffer, imageIndex);

        //vkCmd* 全部返回 void，这里是整个录制过程唯一的错误检查点
        if (vkEndCommandBuffer(pCommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void DoodleCommandBuffer::RecordAcquireBarrier(const VkCommandBuffer pCommandBuffer, const uint32_t imageIndex)
    {
        //取图之后、开画之前。
        //
        //这一步原来是渲染通道的活：附件描述里 initialLayout 到 COLOR_ATTACHMENT_OPTIMAL 的
        //转换由驱动在通道入口自动做，那条 VkSubpassDependency 只是把转换时机从 TOP_OF_PIPE
        //推迟到颜色附件输出阶段。动态渲染不做任何自动转换，于是「转换」与「时机」两件事都
        //落到这条屏障上 —— 六格取值就是把那条依赖原样翻译过来。
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

        //oldLayout 取 UNDEFINED 而不是 PRESENT_SRC_KHR：后者等于声明「上一帧结尾把它转成了
        //呈现布局」，于是录制开始依赖既往状态，首帧与重建后各要一套说法。UNDEFINED 是
        //「旧内容不要了」，反正紧接着就要清屏 —— 每帧从同一条路重算期望态，三条路径收敛
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        //不转移队列族所有权：同族时交换链用 EXCLUSIVE、异族时用 CONCURRENT
        //（见 DoodleSwapChain.cpp），两条路都不需要转移
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_pSwapChain->GetImages()[imageIndex];

        //整张图、单层单级：交换链图像没有 mip 层级，也不是数组
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        //srcAccessMask 为 0：上一次碰这张图的是呈现引擎的读，它不在这个内存模型里，
        //access mask 表达不了它 —— 我们要的是执行顺序，不是「谁写过它」的可见性。
        //dstAccessMask 则是被保护的具体操作：接下来对颜色附件的写
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        //两个 stage 都取 COLOR_ATTACHMENT_OUTPUT：src 侧必须与 DrawFrame 里
        //pWaitDstStageMask 的取值一致，转换才会排在 acquire 信号量被等待之后 ——
        //否则呈现引擎可能还在读这张图，这时动它的布局就是数据竞争；dst 侧是接下来
        //要写它的阶段。
        //
        //旧式屏障用 vkCmdPipelineBarrier，不是 vkCmdPipelineBarrier2：本项目的提交路径
        //还是 VkSubmitInfo 那一代，屏障跟着它走同一套语言，免得两种同步方言并存
        vkCmdPipelineBarrier(pCommandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void DoodleCommandBuffer::RecordPresentBarrier(const VkCommandBuffer pCommandBuffer, const uint32_t imageIndex)
    {
        //画完、交回呈现引擎之前。这一步原来是渲染通道 finalLayout 的活。
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        //队列族与范围同入口那条：不转移所有权、整张图单层单级
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_pSwapChain->GetImages()[imageIndex];
        barrier.subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        //srcAccessMask 用 COLOR_ATTACHMENT_WRITE 把刚画的颜色推到可见：转布局本身要动
        //这份数据，写成 0 的话呈现可能读到半成品（撕裂、花屏、偶发的半帧）。
        //这一格与入口那条的 0 不对称是刻意的 —— 出口这次源 scope 里真的有写。
        //dstAccessMask 为 0：接手的是呈现引擎，它不吃内存可见性这套，只认执行序
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;

        //dstStage 一路到 BOTTOM_OF_PIPE：后面没有别的阶段要等，转换只需在提交结束之前
        //完成，而 renderFinished 信号量正是在批次末尾 signal 的 —— 跨引擎那一段由它兜底。
        //
        //BOTTOM_OF_PIPE 是旧式里表达「后面没有阶段要等」的替身写法（旧式没有「空阶段」
        //这个值）。迁到 sync2 时这一格要换成 VK_PIPELINE_STAGE_2_NONE —— 四个字段的含义
        //本身一一对应，但这一格不是全局替换能带到的
        vkCmdPipelineBarrier(pCommandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}
