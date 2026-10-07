//
// Created by 郭智 on 2026/10/7.
//

#include "DoodleCommandBuffer.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleCommandBuffer::Initialize(const DoodleVulkanDevice& device,
                                         const DoodleSwapChain& swapChain,
                                         const DoodleRenderPass& renderPass,
                                         const DoodleGraphicsPipeline& graphicsPipeline,
                                         const DoodleFrameBuffer& frameBuffer)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;
        m_pRenderPass = &renderPass;
        m_pGraphicsPipeline = &graphicsPipeline;
        m_pFrameBuffer = &frameBuffer;

        //命令缓冲从池中分配，池必须先建出来
        CreateCommandPool();
        CreateCommandBuffer();
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
        m_pCommandBuffer = VK_NULL_HANDLE;
    }

    void DoodleCommandBuffer::CreateCommandBuffer()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_pCommandPool;
        //主级缓冲才能提交到队列，次级只能被主级调用
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        const auto result = vkAllocateCommandBuffers(m_pDevice->GetLogicalDevice(), &allocInfo, &m_pCommandBuffer);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void DoodleCommandBuffer::RecordCommandBuffer(VkCommandBuffer pCommandBuffer, const uint32_t imageIndex)
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

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_pRenderPass->GetRenderPass();
        //imageIndex 来自 vkAcquireNextImageKHR，索引到本次拿到的那张图像对应的帧缓冲。
        //绑错会画到一张不会被呈现的图上，表现为撕裂或闪烁，且每次运行都可能不一样
        renderPassInfo.framebuffer = m_pFrameBuffer->GetFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        //渲染区域应与附件尺寸一致，不一致时驱动要额外处理边界，会变慢
        renderPassInfo.renderArea.extent = imageExtent;

        //VkClearValue 是 union，逐层聚合初始化：VkClearValue → .color → float32[4]，三重大括号不能少
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        //开始渲染通道
        vkCmdBeginRenderPass(pCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

        //结束渲染通道
        vkCmdEndRenderPass(pCommandBuffer);

        //vkCmd* 全部返回 void，这里是整个录制过程唯一的错误检查点
        if (vkEndCommandBuffer(pCommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }
}
