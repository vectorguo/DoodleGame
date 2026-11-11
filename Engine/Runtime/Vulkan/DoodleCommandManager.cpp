//
// Created by 郭智 on 2026/10/7.
//

#include <cstddef>
#include <stdexcept>

#include "DoodleCommandManager.h"
#include "DoodleVulkanConfig.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleCommandManager* DoodleCommandManager::s_pInstance = nullptr;

    DoodleCommandManager::DoodleCommandManager()
    {
        //与 DoodleApplication 同一个套路：谁被构造出来谁就是「当前那一份」。
        //本工程由 DoodleVulkanManager 按值持有一份，所以这里只会登记一次
        s_pInstance = this;
    }

    DoodleCommandManager::~DoodleCommandManager()
    {
        //只注销自己 —— 万一将来真的出现第二份，别把别人的登记顺手抹掉
        if (s_pInstance == this)
        {
            s_pInstance = nullptr;
        }
    }

    DoodleCommandManager& DoodleCommandManager::GetInstance()
    {
        return *s_pInstance;
    }

    void DoodleCommandManager::Initialize(const DoodleVulkanDevice& device)
    {
        m_pDevice = &device;

        //命令缓冲从池中分配，池必须先建出来
        CreateRenderCommandPool();
        CreateRenderCommandBuffers();

        //两个池都锚在程序生命周期上，在这里一次建好。
        //传输池必须赶在渲染管线第一次上传之前 —— 那条顺序写在 DoodleRenderManager::Initialize 里
        CreateTransferCommandPool();
    }

    void DoodleCommandManager::Destroy()
    {
        DestroyTransferCommandPool();
        DestroyRenderCommandPool();
    }

    VkCommandBuffer DoodleCommandManager::BeginFrame(const uint32_t frameIndex)
    {
        if (m_renderRecordingCommandBuffer != VK_NULL_HANDLE)
        {
            throw std::logic_error("command manager: a frame is already being recorded");
        }

        const VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0,                    //不承诺任何特殊用法
            .pInheritanceInfo = nullptr    //只对次级有意义，主级必须为空
        };

        //对已录制过的缓冲调用 begin 会隐式重置它，命令缓冲不能追加只能重录
        const VkCommandBuffer commandBuffer = m_renderCommandBuffers[frameIndex];
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        m_renderRecordingCommandBuffer = commandBuffer;
        return commandBuffer;
    }

    void DoodleCommandManager::EndFrame()
    {
        if (m_renderRecordingCommandBuffer == VK_NULL_HANDLE)
        {
            throw std::logic_error("command manager: no frame is being recorded");
        }

        //vkCmd* 全部返回 void，这里是整个录制过程唯一的错误检查点
        const VkResult result = vkEndCommandBuffer(m_renderRecordingCommandBuffer);

        //句柄先清掉再抛：失败时命令缓冲已经不可用，不能留给下一次 BeginFrame 误用
        m_renderRecordingCommandBuffer = VK_NULL_HANDLE;

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void DoodleCommandManager::CreateRenderCommandPool()
    {
        //池绑定的队列族必须与将来提交命令的队列一致。这里录的是绘制命令，
        //将来提交到图形队列，所以取 graphicsFamilyIndex。
        //多数桌面显卡上图形族与呈现族是同一个，填错了不会立刻暴露，
        //换到两个族不重合的设备上才会出问题
        const auto familyIndex = m_pDevice->GetQueueFamilySelection().graphicsFamilyIndex.value();

        const VkCommandPoolCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            //每帧都要在同一个缓冲上重录，需要能单独重置
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = familyIndex
        };

        const auto result = vkCreateCommandPool(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_renderCommandPool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void DoodleCommandManager::DestroyRenderCommandPool()
    {
        //池引用了逻辑设备，必须先于设备层销毁
        vkDestroyCommandPool(m_pDevice->GetLogicalDevice(), m_renderCommandPool, nullptr);
        m_renderCommandPool = VK_NULL_HANDLE;

        //缓冲随池一起释放了，这里只是把句柄数组清空，不要再去逐个销毁
        m_renderCommandBuffers.clear();
        m_renderRecordingCommandBuffer = VK_NULL_HANDLE;
    }

    void DoodleCommandManager::CreateRenderCommandBuffers()
    {
        //每个在途帧一份，用帧索引轮转。数量不取交换链图像数：
        //在途帧数决定「能提前录几帧」，图像数决定「能同时在屏几帧」，两者互不相干
        m_renderCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

        const VkCommandBufferAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_renderCommandPool,
            //主级缓冲才能提交到队列，次级只能被主级调用
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = static_cast<uint32_t>(m_renderCommandBuffers.size())
        };

        //一次分配一批，直接写进 vector 的底层数组
        const auto result = vkAllocateCommandBuffers(m_pDevice->GetLogicalDevice(), &allocInfo, m_renderCommandBuffers.data());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void DoodleCommandManager::CreateTransferCommandPool()
    {
        //队列族与绘制池取同一个：拷贝也提交到图形队列。
        //传输队列（VK_QUEUE_TRANSFER_BIT）是另一条优化路，本项目不走
        const auto familyIndex = m_pDevice->GetQueueFamilySelection().graphicsFamilyIndex.value();

        const VkCommandPoolCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            //告诉驱动这个池里的缓冲寿命很短，会被很快重置或释放。
            //这是关于用法的声明，不是语义开关 —— 它不改变任何状态规则，
            //驱动拿它来挑池的分配策略（比如整池一起分配、不为此做长期规划）
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            //注意这里没有 RESET_COMMAND_BUFFER_BIT，和有绘制缓冲的那个池不一样。
            //那个位决定「能不能重置池里的单个缓冲」，而本池的缓冲根本不需要重置 ——
            //每次用完就地释放（见 BeginTransferCommand），下一次是全新分配、天然处于 initial 状态。
            //加了这个位反而会让「begin 会隐式重置」这条规矩在这里也成立，
            //从而掩盖掉某天有人试着复用一个旧句柄时的错误
            .queueFamilyIndex = familyIndex
        };

        const auto result = vkCreateCommandPool(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_transferCommandPool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create transfer command pool!");
        }
    }

    void DoodleCommandManager::DestroyTransferCommandPool()
    {
        //池引用了逻辑设备，必须先于设备层销毁
        vkDestroyCommandPool(m_pDevice->GetLogicalDevice(), m_transferCommandPool, nullptr);
        m_transferCommandPool = VK_NULL_HANDLE;
        m_transferCommandBuffer = VK_NULL_HANDLE;
    }

    void DoodleCommandManager::BeginTransferCommand()
    {
        //未 Initialize 时池是空的；对象存在不等于能用了，与其带着空句柄
        //继续走、让校验层在几百行外报错，不如当场停下
        if (m_pDevice == nullptr || m_transferCommandPool == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to begin transfer: command buffer layer is not initialized!");
        }

        //同一时刻只允许一条传输。串行上传是当前的调用方式；重入属于调用顺序错误
        if (m_transferCommandBuffer != VK_NULL_HANDLE)
        {
            throw std::logic_error("failed to begin transfer: a transfer command is already recording!");
        }

        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        //现分配一条，用完在 EndTransferCommand 里释放。传输池没有
        //RESET_COMMAND_BUFFER_BIT，已提交的句柄不能再次 begin：
        //  VUID-vkBeginCommandBuffer-commandBuffer-00050
        //  commandBuffer 必须处于 initial 状态
        //现分配现释放同时让池的 TRANSIENT 标志名副其实：寿命只覆盖一次传输
        const VkCommandBufferAllocateInfo allocInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_transferCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate transfer command buffer!");
        }

        constexpr VkCommandBufferBeginInfo beginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            //承诺这份录制只提交一次：驱动可以在录制时少做「将来可能被改」的保留
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(logicalDevice, m_transferCommandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to begin transfer command buffer!");
        }

        //成功 begin 之后才登记为活动传输；失败路径都不留半截状态
        m_transferCommandBuffer = commandBuffer;
    }

    void DoodleCommandManager::FillBufferTransferCommand(VkBuffer source, VkBuffer destination, VkDeviceSize size)
    {
        if (m_transferCommandBuffer == VK_NULL_HANDLE)
        {
            throw std::logic_error("no active transfer command; call BeginTransferCommand first");
        }

        //拷贝区域。三个字段都是 0 起始，只填 size —— 两边都是「整块缓冲从头上开始」。
        //size 不能写 VK_WHOLE_SIZE，VkBufferCopy 里没有这个取值
        const VkBufferCopy2 copyRegion
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size
        };

        //这里是拷贝，不是复制挂载 —— 两者内容是独立的副本，之后各改各的互不影响
        const VkCopyBufferInfo2 copyInfo
        {
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = source,
            .dstBuffer = destination,
            .regionCount = 1,
            .pRegions = &copyRegion
        };
        vkCmdCopyBuffer2(m_transferCommandBuffer, &copyInfo);
    }

    void DoodleCommandManager::FillTextureTransferCommand(VkBuffer source,
                                                         VkImage destination,
                                                         uint32_t width,
                                                         uint32_t height)
    {
        if (m_transferCommandBuffer == VK_NULL_HANDLE)
        {
            throw std::logic_error("no active transfer command; call BeginTransferCommand first");
        }

        //旧布局不要 → 写传输目标 → 拷像素 → 转可采样布局
        RecordImageTransitionToTransferDst(m_transferCommandBuffer, destination);
        RecordCopyBufferToImage(m_transferCommandBuffer, source, destination, width, height);
        RecordImageTransitionToShaderRead(m_transferCommandBuffer, destination);
    }

    void DoodleCommandManager::EndTransferCommand()
    {
        if (m_pDevice == nullptr || m_transferCommandPool == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to end transfer: command buffer layer is not initialized!");
        }

        if (m_transferCommandBuffer == VK_NULL_HANDLE)
        {
            throw std::logic_error("no active transfer command; call BeginTransferCommand first");
        }

        const auto logicalDevice = m_pDevice->GetLogicalDevice();

        if (vkEndCommandBuffer(m_transferCommandBuffer) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(logicalDevice, m_transferCommandPool, 1, &m_transferCommandBuffer);
            m_transferCommandBuffer = VK_NULL_HANDLE;
            throw std::runtime_error("failed to record transfer command buffer!");
        }

        //这次提交不带信号量也不带栅栏：没有第二个队列要同步，host 也不打算
        //异步等它 —— 下面的 vkQueueWaitIdle 会把整个队列等干净
        const VkCommandBufferSubmitInfo commandBufferInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = m_transferCommandBuffer
        };

        const VkSubmitInfo2 submitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferInfo
        };

        if (vkQueueSubmit2(m_pDevice->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(logicalDevice, m_transferCommandPool, 1, &m_transferCommandBuffer);
            m_transferCommandBuffer = VK_NULL_HANDLE;
            throw std::runtime_error("failed to submit transfer command buffer!");
        }

        //这一等管资源寿命，不只是可见性：调用方返回后通常就销毁中转缓冲 /
        //中转图，而拷贝命令可能还在读它们：
        //  VUID-vkDestroyBuffer-buffer-00922  buffer 不得有在途命令引用
        //  VUID-vkFreeMemory-memory-00677     同理
        //可见性不需要额外屏障：vkQueueWaitIdle 等价于提交一个栅栏并等它信号，
        //让这次写的效果变为 available；host 的等待构成 happens-before 边，
        //之后的下一次 vkQueueSubmit 把已 available 的写对设备侧变可见
        vkQueueWaitIdle(m_pDevice->GetGraphicsQueue());

        vkFreeCommandBuffers(logicalDevice, m_transferCommandPool, 1, &m_transferCommandBuffer);
        m_transferCommandBuffer = VK_NULL_HANDLE;
    }

    void DoodleCommandManager::RecordImageTransitionToTransferDst(VkCommandBuffer commandBuffer, VkImage image) const
    {
        //① 进 TRANSFER_DST：刚建出来的图，旧内容不要，源 scope 留空。
        //dst access 只写到 TRANSFER_WRITE —— 接下来对这张图的唯一操作就是拷贝
        const VkImageMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            //整张图、单层单级：DoodleImage 固定单 mip 单层
            .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        const VkDependencyInfo dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleCommandManager::RecordImageTransitionToShaderRead(VkCommandBuffer commandBuffer, VkImage image) const
    {
        //② 交给采样：TRANSFER_DST → SHADER_READ_ONLY。
        //拷贝的写要在 TRANSFER 阶段完成；采样读发生在片元着色器
        const VkImageMemoryBarrier2 barrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            //与上一段相同的单 mip 单层范围
            .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        const VkDependencyInfo dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void DoodleCommandManager::RecordCopyBufferToImage(VkCommandBuffer commandBuffer,
                                                      VkBuffer sourceBuffer,
                                                      VkImage destinationImage,
                                                      uint32_t width,
                                                      uint32_t height) const
    {
        //bufferRowLength / bufferImageHeight 填 0 表示紧密排列；
        //区域是整张图从 (0,0) 开始
        const VkBufferImageCopy2 copyRegion
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = {.width = width, .height = height, .depth = 1}
        };

        const VkCopyBufferToImageInfo2 copyInfo
        {
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
            .srcBuffer = sourceBuffer,
            .dstImage = destinationImage,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &copyRegion
        };
        vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);
    }
}
