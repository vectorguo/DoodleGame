//
//  DoodleGraphicsBuffer.h
//  Doodle
//
//  Created by 郭智 on 2026/10/16.
//
#pragma once

#include <vulkan/vulkan_core.h>

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

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
     * - **拷贝命令在本类，命令池与命令缓冲在命令层**。GPU 侧的拷贝要命令池，
     *   池归命令缓冲层；本类通过那层的单例借到一条一次性命令缓冲，只负责录「拷什么」。
     *   分配 / 提交 / 等待 / 释放由 DoodleCommandBuffer::BeginOneTimeTransfer /
     *   EndOneTimeTransfer 完成。这是一处**刻意的取舍**：原本「资源类不依赖录制层」
     *   的那条线被单例跨过去了，换来的是整个拷贝动作归在调用者自然会去找的地方，
     *   而且不必每拷一次建一个池。代价写在 DoodleCommandBuffer::GetInstance 的注释里
     * - **不知道 mesh、不知道绘制**。它就是一块有大小、有用途的字节区间
     *
     * 内存类型只开放两档（见 EMemoryType），数据进缓冲的路也只有三条：
     *
     * - 初始化期一次性写入 DeviceLocal → 带 pData 的 Initialize（内部走中转拷贝）
     * - 初始化后 GPU 侧拷贝 → CopyFrom / CopyTo
     * - HostVisible 的 CPU 写入 → Write（第一次写自动建立持久映射）
     *
     * 不开放原始 VkMemoryPropertyFlags：本类只支持上面两档的语义，让调用方拼出
     * 「类并不真正处理」的组合（比如 HOST_VISIBLE 不带 COHERENT），只会把错误拖到运行时
     *
     * 名字里的 Graphics 不表示它「只给图形管线用」—— Vulkan 里每个缓冲都可用于图形。
     * 取这个名字是为了与 DoodleCommandBuffer 拉开距离：那两个名字只差中间一个词，
     * 指的却是完全无关的东西（显存里的数据区间 vs 录好的命令流）
     */
    class DoodleGraphicsBuffer
    {
    public:
        /**
         * 内存类型：本类支持的两组内存要求
         *
         * 不是 Vulkan 的 VkMemoryType（那个是物理设备给出的内存类型索引），
         * 而是本类对「这块缓冲住哪、CPU 能不能碰」的封闭分档。
         * 每一档同时规定了分配时要满足的属性位、以及允许用哪些方法写数据
         *
         * 超出这两档的需求（CPU 读回、非一致内存、统一内存直写）等真有调用者时
         * 再加档位 —— 每加一档都要同时定义「要求哪些属性位 + 配套的写入/同步语义」，
         * 而不是把原始 flags 摊回给调用方
         */
        enum class EMemoryType
        {
            /**
             * 设备本地：要求 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
             *
             * 通常落在显存，GPU 访问最快。本类按策略禁止 CPU 映射它：
             * CPU 提供的数据只能在初始化时经中转拷进去，之后由 GPU 侧操作填充。
             * 适合初始化后内容不再变化、GPU 高频读取的数据（顶点、索引、静态贴图）
             */
            DeviceLocal,

            /**
             * CPU 可见：要求 HOST_VISIBLE | HOST_COHERENT
             *
             * CPU 可以直接映射写入，写完不需要手动 flush。适合每帧更新或一次性的
             * 小数据（UBO、中转缓冲）。GPU 读它要过总线，不适合大块静态数据
             */
            HostVisible,
        };

        DoodleGraphicsBuffer() = default;

        //句柄的成对生死全在本类手里，复制一份出来就是两个对象各自去销毁同一对句柄。
        //移动语义等将来真要把它们放进容器时再补 —— 那时得手写，不能 = default，
        //否则被移走的那份仍握着同一对句柄
        DoodleGraphicsBuffer(const DoodleGraphicsBuffer&) = delete;
        DoodleGraphicsBuffer& operator=(const DoodleGraphicsBuffer&) = delete;

        /**
         * 建出缓冲对象、按它的内存需求分配、再绑起来
         *
         * 只负责「建成什么样」，不写任何数据，内容未定义。之后怎么填由类型决定：
         * HostVisible 用 Write；DeviceLocal 用带 pData 的重载、CopyFrom 或 GPU 写入
         *
         * @param device 已初始化的Vulkan设备，取逻辑设备与物理设备（查内存类型用）
         * @param size 缓冲的逻辑尺寸，单位字节，必须大于 0。这是「里面装得下几个字节」，
         *        不是分配尺寸 —— 后者由驱动算，通常更大
         * @param usage 用途标志。决定这一路上驱动怎么摆放它，也决定哪些命令能访问它。
         *        要被拷贝命令写入时必须带 VK_BUFFER_USAGE_TRANSFER_DST_BIT
         * @param memoryType 内存类型，取 DeviceLocal 或 HostVisible
         *
         * @throws std::runtime_error size 为 0，或找不到满足要求的内存类型
         * @throws std::logic_error 本对象已经初始化过（要先 Destroy 再重建）
         *
         * @note 失败时会把已经建出来的部分收干净再抛，不会留下半成品
         */
        void Initialize(const DoodleVulkanDevice& device,
                        VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        EMemoryType memoryType);

        /**
         * 建出缓冲，并在初始化期把初始数据放进去
         *
         * 按类型走两条路：
         *
         * - DeviceLocal：正式缓冲自动补上 TRANSFER_DST，再建一块 host 可见的中转，
         *   Write 进去、用 GPU 拷贝搬过来、等完成、销毁中转。数据从此住在显存里
         * - HostVisible：分配后直接 Write，不经过中转
         *
         * @param device 已初始化的 Vulkan 设备
         * @param size 数据字节数，也是本缓冲的逻辑尺寸
         * @param usage 这份数据的用途，取 VERTEX_BUFFER / INDEX_BUFFER 之类；
         *        DeviceLocal 路径需要的 TRANSFER_DST 由本函数加上，调用方不必自己带
         * @param memoryType 内存类型
         * @param pData 初始数据，不得为空；要空缓冲请用不带 pData 的重载
         *
         * @note DeviceLocal 这条路内部会提交一次拷贝并等待完成。所以只能在初始化期
         *       调用，不得与在途的帧并发，且命令缓冲层的传输池必须已经建好 —— 见 CopyTo
         */
        void Initialize(const DoodleVulkanDevice& device,
                        VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        EMemoryType memoryType,
                        const void* pData);

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
         * 仅 HostVisible 类型可用。第一次调用会在内部建立整块缓冲的持久映射，
         * 之后一直复用、直到 Destroy —— 帧循环里不会发生 map/unmap，
         * 调用方也不需要持有映射指针
         *
         * @param pData 源数据
         * @param size 要写的字节数，不得超过创建时的 size。只覆盖前 size 字节，
         *        其余内容保持不变；size 为 0 时是空操作
         *
         * @note HOST_COHERENT 保证写完不需要手动 flush；下一次 vkQueueSubmit
         *       之后 GPU 可见
         *
         * @note 映射不负责同步：调用方必须保证写入时 GPU 没有在读/写这块缓冲
         *
         * @throws std::runtime_error 缓冲未初始化、类型不是 HostVisible、
         *         源指针为空，或写入超范围
         */
        void Write(const void* pData, VkDeviceSize size);

        /**
         * 把本缓冲的内容整段拷到 dstBuffer，拷完才返回
         *
         * 从 DoodleCommandBuffer 的单例借到一条一次性传输命令缓冲，录拷贝，
         * 再交给命令层提交并等待。池与命令缓冲的生灭在命令层，
         * 「拷什么、什么时候拷」按功能归缓冲
         *
         * @param dstBuffer 目标缓冲。创建时须带 VK_BUFFER_USAGE_TRANSFER_DST_BIT；
         *        本缓冲（源）须带 VK_BUFFER_USAGE_TRANSFER_SRC_BIT
         * @param size 拷贝的字节数。必须同时落在两个缓冲的创建尺寸之内 ——
         *        VkBufferCopy 的 size 不吃 VK_WHOLE_SIZE，那里没有「整块」这个取值
         *
         * @throws std::runtime_error 命令缓冲层的单例取不到，或一次性传输的
         *         分配 / 录制 / 提交失败（包括传输池未初始化）。都是调用时机错了 ——
         *         与其带着空句柄继续往下走、让校验层在几百行外报错，不如当场停下
         *
         * @note 前置条件：不得与在途的帧并发调用。内部等的是整个图形队列，
         * 帧循环正在跑时调它，CPU 会停下来等 GPU 把当前帧画完。
         * 本项目只在初始化期写入，那时一次提交都还没发生
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
         * 建立整块缓冲的持久映射；已映射时是空操作
         *
         * 只由 Write 调用，调用前 Write 已经确认缓冲已初始化、类型是 HostVisible。
         * 映射保留到 Destroy —— 它不归调用方管，也没有单独解除的公开入口
         */
        void Map();

        /**
         * 解除持久映射
         *
         * 未映射时是空操作。只由 Destroy 调用：释放内存之前必须先解除映射
         */
        void Unmap();

        /**
         * 把内存类型翻译成 Vulkan 的属性要求
         *
         * 类型是 API 的口径，属性位是实现的口径，翻译只在这一处发生。
         * FindMemoryType 要求「全部满足」，所以返回的就是必须全中的那组位
         */
        [[nodiscard]] static VkMemoryPropertyFlags ToMemoryPropertyFlags(const EMemoryType memoryType);

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
         * 内存类型，Initialize 时确定
         *
         * 留一份是为了让 Write 能当场判断这块缓冲到底能不能由 CPU 写 ——
         * 否则错误要等到 vkMapMemory 失败，或者更糟，等到校验层报 VUID。
         * 存类型而不是原始属性位：类型就是本类支持的封闭集合，判断只有一种口径
         */
        EMemoryType m_memoryType = EMemoryType::DeviceLocal;

        /**
         * 持久映射的 CPU 指针
         *
         * nullptr 表示还没建立。Write 第一次调用时映射整块缓冲，之后一直复用；
         * Destroy 经 Unmap 把它恢复为 nullptr。它不是 VkDevice 的资源，
         * 只是 vkMapMemory 的返回值，所以不需要单独销毁，解除映射即可
         */
        void* m_pMappedData = nullptr;
    };
}
