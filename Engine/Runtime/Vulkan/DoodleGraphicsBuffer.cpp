//
//  DoodleGraphicsBuffer.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/16.
//

#include "DoodleGraphicsBuffer.h"
#include "DoodleCommandBuffer.h"
#include "DoodleVulkanDevice.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleGraphicsBuffer::Initialize(const DoodleVulkanDevice& device,
                                          const VkDeviceSize size,
                                          const VkBufferUsageFlags usage,
                                          const EMemoryType memoryType)
    {
        //两条前置条件在这里断掉，不留给校验层：size 为 0 建不出合法 VkBuffer；
        //重复初始化会把旧句柄覆盖掉，而调用方再也没有办法释放它们
        if (size == 0)
        {
            throw std::runtime_error("cannot initialize buffer: size must be greater than zero!");
        }

        if (m_pDevice != nullptr || m_buffer != VK_NULL_HANDLE || m_bufferMemory != VK_NULL_HANDLE)
        {
            throw std::logic_error("cannot initialize buffer: it is already initialized!");
        }

        const auto logicalDevice = device.GetLogicalDevice();

        //第一步：建对象。先放在局部量里，全部成功之后才写进成员 ——
        //中途任何一步失败，对象仍停在「未初始化」，可以重试，也不会留下半成品
        VkBuffer buffer = VK_NULL_HANDLE;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        //只有图形队列用它，独占即可。将来引入独立的传输队列做上传时，
        //或者这块缓冲要跨队列族共享时，这个字段才需要重新考虑
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create buffer!");
        }

        //第二步：问驱动「要多大、有哪些内存类型用得了」
        VkMemoryRequirements memoryRequirements;
        vkGetBufferMemoryRequirements(logicalDevice, buffer, &memoryRequirements);

        //第三步：选内存类型并分配。注意用 memoryRequirements.size，不是上面那个 size ——
        //驱动可能要得比数据本身多（内部对齐、元数据），照数据大小分配就是越界。
        //那个更大的尺寸不记下来：它只在分配这一刻有用，之后处处该用请求尺寸
        //
        //FindMemoryType 找不到合适类型时会抛；此时 buffer 已经建出来了，
        //必须收掉再抛，否则调用方拿不到句柄，也就没有机会释放它
        uint32_t memoryTypeIndex = 0;
        try
        {
            memoryTypeIndex = device.FindMemoryType(memoryRequirements.memoryTypeBits, ToMemoryPropertyFlags(memoryType));
        }
        catch (...)
        {
            vkDestroyBuffer(logicalDevice, buffer, nullptr);
            throw std::runtime_error("failed to find memory type!");
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;

        //分配失败要把第一步建出来的对象收掉再抛。
        //半成品留在场上，调用方没有任何办法把它认出来并释放 ——
        //它只知道 Initialize 抛了，不知道抛在哪一步
        VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
        if (vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(logicalDevice, buffer, nullptr);
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        //第四步：把两者绑起来。末尾那个 0 是内存内的偏移 —— 这块内存专为这一个缓冲
        //分配，所以从 0 开始。将来多个缓冲共用一块内存时，这里才会出现非零值，
        //而且那个值必须是本缓冲 memoryRequirements.alignment 的整数倍
        if (vkBindBufferMemory(logicalDevice, buffer, bufferMemory, 0) != VK_SUCCESS)
        {
            vkDestroyBuffer(logicalDevice, buffer, nullptr);
            vkFreeMemory(logicalDevice, bufferMemory, nullptr);
            throw std::runtime_error("failed to bind buffer memory!");
        }

        //到这里四个对象都建好了，才写进成员。之后这个对象才算是「已初始化」
        m_pDevice = &device;
        m_size = size;
        m_memoryType = memoryType;
        m_buffer = buffer;
        m_bufferMemory = bufferMemory;
    }

    void DoodleGraphicsBuffer::Initialize(const DoodleVulkanDevice& device,
                                          const VkDeviceSize size,
                                          const VkBufferUsageFlags usage,
                                          const EMemoryType memoryType,
                                          const void* pData)
    {
        if (pData == nullptr)
        {
            throw std::runtime_error("cannot initialize buffer with data: initial data pointer is null!");
        }

        if (memoryType == EMemoryType::DeviceLocal)
        {
            //正式缓冲要当下面那次拷贝的目标，所以 TRANSFER_DST 由本函数补上 ——
            //调用方只需要报「这份数据将来用来干什么」（VERTEX_BUFFER / INDEX_BUFFER 之类）
            Initialize(device, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryType);

            //中转：反过来，要能当源（TRANSFER_SRC），且内存得 CPU 碰得到。
            //
            //局部量，不是成员 —— 它建出来、写完、拷贝完、立刻销毁，寿命只覆盖本函数。
            //做成成员反而会凭空引入「它此刻是否有效」这个状态问题，而这个状态问题
            //本来不存在。它同时也是一份不变量：只要它还活着，数据就还没搬完
            DoodleGraphicsBuffer stagingBuffer;
            stagingBuffer.Initialize(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, EMemoryType::HostVisible);

            //先把数据写进中转，再录拷贝命令 —— 这个顺序是规范给的免费保证：
            //写在前、提交在后，拷贝命令读到的必然是写完的数据，中间不需要任何屏障。
            //详见 Write 末尾
            stagingBuffer.Write(pData, size);

            //到这里 CPU 的事做完了，剩下这次搬运由 GPU 自己完成。
            //入口与实现都在本类上，命令池从命令缓冲层的单例借
            stagingBuffer.CopyTo(*this, size);

            //销毁中转。这一步必须排在 CopyTo 之后 —— 它返回时拷贝才真正做完，
            //提前销毁就是销毁一份还在被命令引用的缓冲
            stagingBuffer.Destroy();
        }
        else
        {
            //HostVisible：分配后直接写，不经过中转
            Initialize(device, size, usage, memoryType);
            Write(pData, size);
        }
    }

    void DoodleGraphicsBuffer::Destroy()
    {
        //映射是 CPU 与内存之间的临时关系，释放内存前先解除它。
        //Destroy 自己兜住这条规则，调用方不必在每处销毁前手写 vkUnmapMemory
        if (m_pDevice != nullptr)
        {
            Unmap();
        }

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
    }

    void DoodleGraphicsBuffer::CopyTo(const DoodleGraphicsBuffer& dstBuffer, VkDeviceSize size) const
    {
        //命令层负责传输池与一次性命令缓冲的生灭；取不到就是调用时机错了：
        //与其带着空句柄继续往下走、让校验层在几百行外报错，不如当场停下
        const auto* pCommandBuffer = DoodleCommandBuffer::GetInstance();
        if (pCommandBuffer == nullptr)
        {
            throw std::runtime_error("failed to copy buffer: command buffer layer is not available!");
        }

        //分配 / 提交 / 等待 / 释放都在命令层，本函数只录「拷什么」
        const VkCommandBuffer transferCommandBuffer = pCommandBuffer->BeginOneTimeTransfer();

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

        pCommandBuffer->EndOneTimeTransfer(transferCommandBuffer);
    }

    void DoodleGraphicsBuffer::CopyFrom(const DoodleGraphicsBuffer& srcBuffer, VkDeviceSize size)
    {
        //同一件事换个主语。机制只留一份：写成两份实现，
        //将来改其中一份（补同步、改错误处理）必然漏掉另一份
        srcBuffer.CopyTo(*this, size);
    }

    void DoodleGraphicsBuffer::Write(const void* pData, const VkDeviceSize size)
    {
        //前置条件在这里断掉，不留给校验层：
        //  - 未初始化：没有内存可写
        //  - 不是 HostVisible：DeviceLocal 的 CPU 根本映射不到，写它必须走中转
        //  - 越界：memcpy 会踩坏后面的数据，校验层也查不到
        if (m_pDevice == nullptr || m_buffer == VK_NULL_HANDLE)
        {
            throw std::runtime_error("cannot write buffer: it is not initialized!");
        }

        if (m_memoryType != EMemoryType::HostVisible)
        {
            throw std::runtime_error("cannot write buffer: memory is not host-visible!");
        }

        if (size > m_size)
        {
            throw std::runtime_error("write size exceeds buffer size!");
        }

        if (pData == nullptr)
        {
            throw std::runtime_error("cannot write buffer: source data is null!");
        }

        if (size == 0)
        {
            return;
        }

        //第一次写才建立映射，之后一直复用。映射整块缓冲而不是这次写的范围，
        //否则下一次写得更长时会越过映射边界
        if (m_pMappedData == nullptr)
        {
            Map();
        }

        //HOST_COHERENT 保证写完不需要手动 flush；「GPU 什么时候读到」由下一次
        //vkQueueSubmit 负责，所以这里只 memcpy，不插屏障。
        //同步不归映射管：调用方必须保证此刻 GPU 没有在读/写这块缓冲
        std::memcpy(m_pMappedData, pData, static_cast<size_t>(size));
    }

    void DoodleGraphicsBuffer::Map()
    {
        //只由 Write 调用：调用前已经确认初始化且类型是 HostVisible。
        //已映射时直接返回，保持「一块缓冲最多映射一次」这条不变量
        if (m_pMappedData == nullptr)
        {
            const auto result = vkMapMemory(m_pDevice->GetLogicalDevice(), m_bufferMemory, 0, m_size, 0, &m_pMappedData);
            if (result != VK_SUCCESS)
            {
                m_pMappedData = nullptr;
                throw std::runtime_error("failed to map buffer memory!");
            }
        }
    }

    void DoodleGraphicsBuffer::Unmap()
    {
        if (m_pMappedData != nullptr)
        {
            vkUnmapMemory(m_pDevice->GetLogicalDevice(), m_bufferMemory);
            m_pMappedData = nullptr;
        }
    }

    VkMemoryPropertyFlags DoodleGraphicsBuffer::ToMemoryPropertyFlags(const EMemoryType memoryType)
    {
        switch (memoryType)
        {
            case EMemoryType::DeviceLocal:
                return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            case EMemoryType::HostVisible:
                return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            default:
                throw std::runtime_error("unknown buffer memory profile!");
        }
    }
}
