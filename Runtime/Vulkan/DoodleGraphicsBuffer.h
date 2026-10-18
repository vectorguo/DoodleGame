//
//  DoodleGraphicsBuffer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/16.
//
#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 一块 GPU 缓冲：VkBuffer 对象 + 它占的那块 VkDeviceMemory
     *
     * 这两者在 Vulkan 里是两个独立对象：VkBuffer 只是一份「描述」（多大、用来干什么、
     * 怎么共享），它自己不含一个字节的存储；内存要另外分配，再用 vkBindBufferMemory
     * 把两者绑起来。绑上之后两者同生共死，但销毁的先后是固定的 —— 见 Destroy
     *
     * 本类把「建对象 → 查需求 → 分配 → 绑定」这四步收在一处。收在一处的理由不是省行数，
     * 是这几步共享一组不变量（分配尺寸取驱动要的而非数据大小、销毁时 buffer 必须先走），
     * 抄第二遍就会漏掉其中一条
     *
     * 它不管什么：
     *
     * - **不管内存池**。现在一块缓冲独占一块内存，偏移恒为 0。将来多个缓冲共用一块内存时，
     *   内存的来源会换成「池 + 偏移」，那时改的是 Initialize 的签名和这里多一个 offset 字段，
     *   调用点不动
     * - **拷贝命令在本类，命令池不在**。GPU 侧的拷贝要命令池，而池归命令缓冲层；
     *   本类通过那层的单例借到池，分配 / 录制 / 提交 / 等待都在 CopyTo / CopyFrom 里做完。
     *   这是一处**刻意的取舍**：原本「资源类不依赖录制层」的那条线被单例跨过去了，
     *   换来的是整个拷贝动作归在调用者自然会去找的地方，而且不必每拷一次建一个池。
     *   代价与替代方案写在 CopyTo / CopyFrom 和 DoodleCommandBuffer::GetInstance 的注释里
     * - **不知道 mesh、不知道绘制**。它就是一块有大小、有用途的字节区间
     *
     * 名字里的 Graphics 不表示它「只给图形管线用」—— Vulkan 里每个缓冲都可用于图形。
     * 取这个名字是为了与 DoodleCommandBuffer 拉开距离：那两个名字只差中间一个词，
     * 指的却是完全无关的东西（显存里的数据区间 vs 录好的命令流）
     */
    class DoodleGraphicsBuffer
    {
    public:
        DoodleGraphicsBuffer() = default;

        //句柄的成对生死全在本类手里，复制一份出来就是两个对象各自去销毁同一对句柄。
        //移动语义等将来真要把它们放进容器时再补 —— 那时得手写，不能 = default，
        //否则被移走的那份仍握着同一对句柄
        DoodleGraphicsBuffer(const DoodleGraphicsBuffer&) = delete;
        DoodleGraphicsBuffer& operator=(const DoodleGraphicsBuffer&) = delete;

        /**
         * 建出缓冲对象、按它的内存需求分配、再绑起来
         *
         * @param device 已初始化的Vulkan设备，取逻辑设备与物理设备（查内存类型用）
         * @param size 缓冲的逻辑尺寸，单位字节。这是「里面装得下几个字节」，
         *        不是分配尺寸 —— 后者由驱动算，通常更大
         * @param usage 用途标志。决定这一路上驱动怎么摆放它，也决定谁能读它。
         *        要被拷贝命令写入时必须带 VK_BUFFER_USAGE_TRANSFER_DST_BIT
         * @param memoryProperties 要求的内存属性。缺哪一位都不接受：
         *        查内存类型时用的是「必须全部满足」，不是「满足其一」
         *
         * @note 失败时会把已经建出来的部分收干净再抛，不会留下半成品
         */
        void Initialize(const DoodleVulkanDevice& device,
                        VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags memoryProperties);

        /**
         * 销毁缓冲对象与它占的内存
         *
         * @note 前置条件：GPU 已空闲。缓冲可能还在被在途的命令引用着，
         * 此刻销毁是未定义行为 —— 由 DoodleVulkanManager::Destroy 的 vkDeviceWaitIdle 保证
         */
        void Destroy();

        /**
         * 从 CPU 内存写一段数据进这块缓冲
         *
         * 只对本进程可映射的内存有意义，所以实际只在中转缓冲上用得到 ——
         * 设备本地的缓冲 CPU 根本碰不到，给它送数据必须借助中转 + 拷贝命令
         *
         * @param pData 源数据
         * @param size 要写的字节数，不得超过创建时的 size
         *
         * @note 内存属性里必须同时有 HOST_VISIBLE 与 HOST_COHERENT。
         * 前者是能映射的前提，后者管的是「CPU 写完的东西，这块内存自己知道」——
         * 少了它就得手动 flush，而本项目还没有需要走那条路的缓冲
         */
        void Upload(const void* pData, VkDeviceSize size);

        /**
         * 把本缓冲的内容整段拷到 dstBuffer，拷完才返回
         *
         * 整个动作都在本函数里：从 DoodleCommandBuffer 的单例借到传输池，
         * 分配命令缓冲、录拷贝、提交、等它做完、释放。池归命令层，
         * 但「拷什么、什么时候拷」按功能归缓冲
         *
         * @param dstBuffer 目标缓冲。创建时须带 VK_BUFFER_USAGE_TRANSFER_DST_BIT；
         *        本缓冲（源）须带 VK_BUFFER_USAGE_TRANSFER_SRC_BIT
         * @param size 拷贝的字节数。必须同时落在两个缓冲的创建尺寸之内 ——
         *        VkBufferCopy 的 size 不吃 VK_WHOLE_SIZE，那里没有「整块」这个取值
         *
         * @throws std::runtime_error 命令缓冲层的单例取不到，或它的传输池还没建
         *         （Initialize 未调用）。两种都是调用时机错了 —— 与其带着空句柄
         *         继续往下走、让校验层在几百行外报错，不如当场停下
         *
         * @note 前置条件：不得与在途的帧并发调用。内部等的是整个图形队列，
         * 帧循环正在跑时调它，CPU 会停下来等 GPU 把当前帧画完。
         * 本项目只在初始化期上传，那时一次提交都还没发生
         */
        void CopyTo(const DoodleGraphicsBuffer& dstBuffer, VkDeviceSize size) const;

        /**
         * 把 srcBuffer 的内容整段拷进本缓冲
         *
         * 与 CopyTo 是同一件事的两个方向：这里只换了个主语，实现委托给
         * `srcBuffer.CopyTo(*this, size)` —— 拷贝的机制只有一份
         *
         * @param srcBuffer 源缓冲。创建时须带 VK_BUFFER_USAGE_TRANSFER_SRC_BIT；
         *        本缓冲（目标）须带 VK_BUFFER_USAGE_TRANSFER_DST_BIT
         * @param size 拷贝的字节数；约束、异常与调用时机同 CopyTo
         */
        void CopyFrom(const DoodleGraphicsBuffer& srcBuffer, VkDeviceSize size);

        /**
         * 获取VkBuffer句柄
         *
         * 绑定缓冲、录制绘制命令都要它 —— 这两件事都不归本类管；
         * 本类只在 CopyTo / CopyFrom 里用它
         */
        [[nodiscard]] VkBuffer GetBuffer() const
        {
            return m_buffer;
        }

        /**
         * 获取创建时请求的尺寸
         *
         * @note 不是分配尺寸。驱动给的分配通常更大（内部对齐、元数据），
         * 但拷贝区域与映射范围都必须落在请求尺寸之内
         */
        [[nodiscard]] VkDeviceSize GetSize() const
        {
            return m_size;
        }

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 缓冲对象。不含存储，只是描述
         */
        VkBuffer m_buffer = VK_NULL_HANDLE;

        /**
         * 缓冲占的那块内存
         *
         * 与 m_buffer 是两个独立对象：销毁句柄不会自动归还内存，须成对管理
         */
        VkDeviceMemory m_bufferMemory = VK_NULL_HANDLE;

        /**
         * 创建时请求的尺寸，取用见 GetSize
         */
        VkDeviceSize m_size = 0;

        /**
         * 分配时要求的内存属性
         *
         * 留一份是为了让 Upload 能当场判断这块缓冲到底能不能映射 ——
         * 否则错误要等到 vkMapMemory 失败，或者更糟，等到校验层报 VUID
         */
        VkMemoryPropertyFlags m_memoryProperties = 0;
    };
}
