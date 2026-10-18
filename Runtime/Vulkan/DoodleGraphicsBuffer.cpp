//
//  DoodleGraphicsBuffer.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/16.
//

#include "DoodleGraphicsBuffer.h"

#include "DoodleCommandBuffer.h"

#include <cstring>
#include <stdexcept>

namespace Doodle
{
    void DoodleGraphicsBuffer::Initialize(const DoodleVulkanDevice& device,
                                          VkDeviceSize size,
                                          VkBufferUsageFlags usage,
                                          VkMemoryPropertyFlags memoryProperties)
    {
        m_pDevice = &device;
        m_size = size;
        m_memoryProperties = memoryProperties;

        const auto logicalDevice = device.GetLogicalDevice();

        //第一步：建对象。此刻它还没有内存，只是一份「描述」
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        //只有图形队列用它，独占即可。将来引入独立的传输队列做上传时，
        //或者这块缓冲要跨队列族共享时，这个字段才需要重新考虑
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create buffer!");
        }

        //第二步：问驱动「要多大、有哪些内存类型用得了」
        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(logicalDevice, m_buffer, &memoryRequirements);

        //第三步：分配。注意用 memoryRequirements.size，不是上面那个 size ——
        //驱动可能要得比数据本身多（内部对齐、元数据），照数据大小分配就是越界。
        //那个更大的尺寸不记下来：它只在分配这一刻有用，之后处处该用请求尺寸
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = device.FindMemoryType(memoryRequirements.memoryTypeBits,
                                                             memoryProperties);

        //分配失败要把第一步建出来的对象收掉再抛。
        //半成品留在场上，调用方没有任何办法把它认出来并释放 ——
        //它只知道 Initialize 抛了，不知道抛在哪一步
        if (vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &m_bufferMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(logicalDevice, m_buffer, nullptr);
            m_buffer = VK_NULL_HANDLE;
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        //第四步：把两者绑起来。末尾那个 0 是内存内的偏移 —— 这块内存专为这一个缓冲
        //分配，所以从 0 开始。将来多个缓冲共用一块内存时，这里才会出现非零值，
        //而且那个值必须是本缓冲 memoryRequirements.alignment 的整数倍
        vkBindBufferMemory(logicalDevice, m_buffer, m_bufferMemory, 0);
    }

    void DoodleGraphicsBuffer::Destroy()
    {
        //两个对象都引用了逻辑设备，必须先于设备层销毁
        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //先销毁使用者，再释放被使用的内存。顺序反过来的话，校验层会当场报
        //「还有活着的 buffer 绑在这块内存上」—— 绑定关系里 buffer 是内存的使用者
        if (m_buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(logicalDevice, m_buffer, nullptr);
            m_buffer = VK_NULL_HANDLE;
        }

        if (m_bufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logicalDevice, m_bufferMemory, nullptr);
            m_bufferMemory = VK_NULL_HANDLE;
        }

        m_pDevice = nullptr;
        m_size = 0;
        m_memoryProperties = 0;
    }

    void DoodleGraphicsBuffer::Upload(const void* pData, VkDeviceSize size)
    {
        //两条前置条件在这里断掉，不留给校验层。
        //映射要求内存类型带 HOST_VISIBLE（VUID-vkMapMemory-memory-00682），
        //写超范围则是越界 —— 两者都是调用方的用法错误，但在这里说清楚
        //比在几百行外看到一句 "vkMapMemory failed" 有用得多
        //
        //查的是初始化时留下的要求，不是实际选中类型的属性：FindMemoryType 保证了
        //选中的类型满足全部要求，所以两者等价，而留一份更省一次查询
        const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((m_memoryProperties & required) != required)
        {
            throw std::runtime_error("cannot upload from host: buffer memory is not host-visible and coherent!");
        }

        if (size > m_size)
        {
            throw std::runtime_error("upload size exceeds buffer size!");
        }

        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //映射的范围只取要写的那一段（0 .. size），不是整块内存
        void* pMapped = nullptr;
        vkMapMemory(logicalDevice, m_bufferMemory, 0, size, 0, &pMapped);
        memcpy(pMapped, pData, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice, m_bufferMemory);

        //这一句之后不需要任何同步，两个原因各管一段：
        //
        //HOST_COHERENT 管的是「CPU 写完的东西，这块内存自己知道」—— 所以不必手动 flush。
        //而「GPU 什么时候真能读到」是另一件事，规范给了免费保证：在下一次 vkQueueSubmit
        //时完成。所以「写完 → 提交通知 GPU」这个顺序摆对就够了，中间不必插屏障
    }

    void DoodleGraphicsBuffer::CopyTo(const DoodleGraphicsBuffer& dstBuffer, VkDeviceSize size) const
    {
        //命令池在命令缓冲层那边 —— 通过单例借到它。取不到就是调用时机错了：
        //与其带着空句柄继续往下走、让校验层在几百行外报错，不如当场停下
        const auto* pCommandBuffer = DoodleCommandBuffer::GetInstance();
        if (pCommandBuffer == nullptr)
        {
            throw std::runtime_error("failed to copy buffer: command buffer layer is not available!");
        }

        //取到了也可能还没 Initialize —— 对象在构造时就登记了，传输池要到那时才建
        const auto transferCommandPool = pCommandBuffer->GetTransferCommandPool();
        if (transferCommandPool == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to copy buffer: transfer command pool is not created!");
        }

        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //现分配一个命令缓冲，用完就还。
        //
        //不常驻一个是刻意的：命令缓冲一旦「分配 → 录制 → 提交」，就停在 executable
        //状态，而 begin 只在池带 RESET_COMMAND_BUFFER_BIT 时才会隐式重置它 ——
        //传输池没有那个位，所以复用同一个句柄第二次 begin 会撞
        //  VUID-vkBeginCommandBuffer-commandBuffer-00050  commandBuffer 必须处于 initial 状态
        //现分配现释放同时让传输池那个 TRANSIENT 标志名副其实：池里的缓冲
        //确实是一次性的，寿命只覆盖本函数
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = transferCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer transferCommandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &transferCommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate transfer command buffer!");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        //承诺这份录制只提交一次。有了它，驱动可以在录的时候少做一些
        //「这条命令将来可能被改」的保留 —— 这正是本函数要做的事
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        if (vkBeginCommandBuffer(transferCommandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording transfer command buffer!");
        }

        //拷贝区域。三个字段都是 0 起始，只填 size —— 两边都是「整块缓冲从头上开始」。
        //size 不能写 VK_WHOLE_SIZE，VkBufferCopy 里没有这个取值
        VkBufferCopy2 copyRegion{};
        copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;

        //这里是拷贝，不是复制挂载 —— 两者内容是独立的副本，之后各改各的互不影响
        VkCopyBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copyInfo.srcBuffer = GetBuffer();
        copyInfo.dstBuffer = dstBuffer.GetBuffer();
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        vkCmdCopyBuffer2(transferCommandBuffer, &copyInfo);

        if (vkEndCommandBuffer(transferCommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record transfer command buffer!");
        }

        //这次提交不带信号量也不带栅栏：没有第二个队列要同步，host 也不打算异步等它 ——
        //下面那句 vkQueueWaitIdle 会把整个队列等干净，比栅栏更粗，但对一次上传足够
        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = transferCommandBuffer;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;

        if (vkQueueSubmit2(m_pDevice->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit transfer command buffer!");
        }

        //等拷贝真正做完。这一句的作用是**资源寿命**，不是可见性 ——
        //命令提交是异步的，不等到这里，调用方一返回就会去销毁源缓冲，
        //而拷贝命令可能还在读它：
        //  VUID-vkDestroyBuffer-buffer-00922  buffer 不得有在途的命令引用它
        //  VUID-vkFreeMemory-memory-00677     同理，内存不得还被在途的命令引用
        //
        //可见性那一半不需要额外动作，这条路自带了：栅栏信号（vkQueueWaitIdle 定义上
        //等价于提交一个栅栏并等它信号）让这次写的效果变为 available，host 的等待构成
        //happens-before 边，而之后那次 vkQueueSubmit 本身就是一次 visibility 操作，
        //把已经 available 的写对设备侧所有 agent 变可见。
        //
        //所以这里**不需要**内存屏障 —— 教程也没漏。（唯一会需要屏障的情形是
        //把上传与绘制合进同一次提交，那时中间没有「提交」这个可见性操作，将来真这么做时要补上）
        vkQueueWaitIdle(m_pDevice->GetGraphicsQueue());

        vkFreeCommandBuffers(logicalDevice, transferCommandPool, 1, &transferCommandBuffer);
    }

    void DoodleGraphicsBuffer::CopyFrom(const DoodleGraphicsBuffer& srcBuffer, VkDeviceSize size)
    {
        //同一件事换个主语。机制只留一份：写成两份实现，
        //将来改其中一份（补同步、改错误处理）必然漏掉另一份
        srcBuffer.CopyTo(*this, size);
    }
}
