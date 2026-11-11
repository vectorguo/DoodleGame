//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 命令管理层：管理命令池与命令缓冲，负责一帧的 begin / end 与传输命令
     *
     * 本层管两样东西：
     *
     * - **每帧的绘制命令缓冲**（BeginFrame / EndFrame）：每个在途帧一条，反复重录
     *   不重建。录什么由 DoodleRenderPipeline 决定 —— 本层只负责命令缓冲的生灭，
     *   不认识附件、管线、绘制项。交换链重建时本层也不重建：缓冲不缓存任何上游句柄，
     *   重录时由 pass 现取
     * - **传输命令池与传输命令**：拷贝 / 上传用的池。池留在这里，因为它和绘制池
     *   是同一种东西；「从池里分配命令缓冲、录制、提交、等待、释放」也在这里
     *   （BeginTransferCommand / FillBufferTransferCommand /
     *   FillTextureTransferCommand / EndTransferCommand）。资源类只提供要拷的句柄与尺寸，
     *   谁拥有池，谁负责命令缓冲的生灭
     *
     * 生命周期与程序一致。两个命令池都只在 Initialize 里建一次。
     * 只读引用设备层，生命周期须短于 DoodleVulkanDevice。
     *
     * 本类在进程内是唯一的一份：构造时登记、析构时注销，
     * 供 DoodleGraphicsBuffer::CopyTo、纹理上传这类资源侧动作找到它 ——
     * 理由与代价见 GetInstance
     *
     * 命令池与命令缓冲不拆成两个类：命令缓冲从池中分配、随池释放，
     * 两者的生命周期完全重合，没有各自独立的锚点
     */
    class DoodleCommandManager
    {
    public:
        /**
         * 构造时把自己登记为「当前那一份」，析构时注销（见 GetInstance）
         */
        DoodleCommandManager();
        ~DoodleCommandManager();

        //登记关系是一对一的：复制一份出来，s_pInstance 只会指向其中一个，
        //另一份对 GetInstance 不可见 —— 那不是这个类想要的语义
        DoodleCommandManager(const DoodleCommandManager&) = delete;
        DoodleCommandManager& operator=(const DoodleCommandManager&) = delete;

        /**
         * 取当前那一份命令管理层
         *
         * 存在的理由只有一个：`DoodleGraphicsBuffer` 的拷贝与纹理上传按功能归属
         * 在资源类上，却需要本层的传输池 —— 而池归本层管。与其让调用方沿着每一层
         * 把引用传下来，不如给本层开一个和 DoodleApplication / DoodleAssetManager
         * 同形的全局入口
         *
         * 返回引用而不是指针：本对象是 DoodleVulkanManager 的成员，而后者又是
         * DoodleApplication 的成员，存在期与应用一致；所有合法调用（资源上传、
         * CopyTo）都在应用生命周期内。构造之前 / 析构之后取用是未定义行为，
         * 与 DoodleAssetManager / DoodleSceneManager 的口径一致
         *
         * @note 「取到了」不等于「能用了」：对象在 Initialize 之前就已经构造，
         * 命令池却要到那时才有。真正的前置条件由 BeginTransferCommand 自己判断
         *
         * @note 代价要记清楚：这层依赖是**隐式**的 —— 调用点看不出它依赖命令层，
         * 也看不出命令层此刻是否可用。所以只给真正需要全局入口的地方开这个口子，
         * 别把别的层照这个模式铺开；层与层之间能注入就注入
         * （参见 DoodleAndroidWindow 为什么收引用而不是调 GetInstance）
         */
        [[nodiscard]] static DoodleCommandManager& GetInstance();

        /**
         * 初始化命令管理层
         * @param device 已初始化的Vulkan设备
         */
        void Initialize(const DoodleVulkanDevice& device);

        /**
         * 销毁命令管理层
         */
        void Destroy();

        /**
         * 开始录制本帧的命令缓冲
         *
         * 按 frameIndex 取本槽位那一条并 begin。录什么由 DoodleRenderPipeline 决定：
         * 本层只负责命令缓冲的生灭与 begin / end，不认识附件、管线或绘制项。
         *
         * @param frameIndex 本帧用哪一条命令缓冲，取值 0 .. MAX_FRAMES_IN_FLIGHT-1
         * @return 已 begin 的命令缓冲，交给 DoodleRenderPipeline::Render
         *
         * @throws std::runtime_error begin 失败
         * @throws std::logic_error 已经有一条绘制命令在录制
         *
         * @note 前置条件：本槽位的帧栅栏已经等过 —— 否则 GPU 可能还在读这条缓冲的
         *       上一轮内容。本层不自己等栅栏，时序归 DoodleVulkanManager
         */
        [[nodiscard]] VkCommandBuffer BeginFrame(uint32_t frameIndex);

        /**
         * 结束本帧的命令录制
         *
         * @throws std::runtime_error end 失败；失败时也会清掉活动句柄
         * @throws std::logic_error 没有正在录制的绘制命令
         *
         * @note 之后由 DoodleVulkanManager 把 BeginFrame 返回的句柄提交
         */
        void EndFrame();

        /**
         * 开始一条传输命令
         *
         * 从传输池分配一条命令缓冲并 begin，句柄记在本层；之后的
         * FillBufferTransferCommand / FillTextureTransferCommand 都录到它上面。
         * 用传输池而不是绘制池：绘制缓冲活满整个程序、反复重录；这条录一次就提交并释放
         *
         * 同一时刻只允许一条传输命令。资源上传是初始化期串行执行的，这个约束
         * 与调用方式一致；已经有活动传输时再 Begin 属于调用顺序错误，当场抛
         *
         * @throws std::runtime_error 本层尚未 Initialize（传输池不存在），
         *         或分配 / begin 失败。失败时会释放已经分配的部分
         * @throws std::logic_error 已经有一条传输命令在录制
         */
        void BeginTransferCommand();

        /**
         * 在当前传输命令里录一次整段 buffer → buffer 拷贝
         *
         * 名字与 FillTextureTransferCommand 对称：Buffer 对 Texture，一眼能看出
         * 填的是哪类传输。两侧都从偏移 0 开始，拷贝 size 字节。
         * size 不能写 VK_WHOLE_SIZE，VkBufferCopy 里没有这个取值
         *
         * @throws std::logic_error 没有活动的传输命令（没有先 Begin）
         */
        void FillBufferTransferCommand(VkBuffer source,
                                       VkBuffer destination,
                                       VkDeviceSize size);

        /**
         * 在当前传输命令里录一次纹理上传
         *
         * 三步：UNDEFINED → TRANSFER_DST → buffer → image 拷贝 →
         * TRANSFER_DST → SHADER_READ_ONLY。目标固定单 mip、单层、颜色切面；
         * 源缓冲按 RGBA8 紧密排列。将来接深度纹理 / mip / 数组纹理时再扩参数
         *
         * @throws std::logic_error 没有活动的传输命令（没有先 Begin）
         */
        void FillTextureTransferCommand(VkBuffer source,
                                        VkImage destination,
                                        uint32_t width,
                                        uint32_t height);

        /**
         * 结束、提交并等待当前传输命令，然后释放它
         *
         * 等整个图形队列空闲：一次上传不追求异步，而且调用方紧接着通常要
         * 销毁中转资源，必须确认 GPU 不再引用它们。提交本身提供可见性，
         * 不需要额外内存屏障；将来把上传与绘制合进同一次提交时才要补
         *
         * @throws std::runtime_error 本层尚未 Initialize，或 end / 提交失败。
         *         end / 提交失败时会先释放命令缓冲、清空活动句柄再抛
         * @throws std::logic_error 没有活动的传输命令（没有先 Begin）
         */
        void EndTransferCommand();

    private:
        /**
         * 录一次布局转换：刚建出来的图 → TRANSFER_DST
         *
         * 旧内容不要，源 scope 留空；dst access 只写到 TRANSFER_WRITE ——
         * 接下来对这张图的唯一操作就是拷贝
         *
         * subresource 固定单 mip、单层、颜色切面 —— 与 DoodleImage 的单级单层
         * 不变量一致；将来接 mip 时，这里和采样器的 LOD 一起改
         */
        void RecordImageTransitionToTransferDst(VkCommandBuffer commandBuffer, VkImage image) const;

        /**
         * 录一次布局转换：TRANSFER_DST → SHADER_READ_ONLY
         *
         * 拷贝的写要在 TRANSFER 阶段完成；采样读发生在片元着色器
         */
        void RecordImageTransitionToShaderRead(VkCommandBuffer commandBuffer, VkImage image) const;

        /**
         * 录「中转缓冲 → 整张图像」的拷贝
         *
         * 名字对齐 vkCmdCopyBufferToImage2：Record + 命令名。
         * bufferRowLength / bufferImageHeight 填 0 表示紧密排列；
         * 区域是整张图从 (0,0) 开始
         */
        void RecordCopyBufferToImage(VkCommandBuffer commandBuffer,
                                     VkBuffer sourceBuffer,
                                     VkImage destinationImage,
                                     uint32_t width,
                                     uint32_t height) const;

        /**
         * 创建渲染命令池
         */
        void CreateRenderCommandPool();

        /**
         * 销毁渲染命令池，并把池与命令缓冲的句柄一并置空
         *
         * 不需要逐个销毁命令缓冲：缓冲从池中分配，随池一起释放
         */
        void DestroyRenderCommandPool();

        /**
         * 从渲染命令池分配命令缓冲，每个在途帧一个
         *
         * 前置条件：m_renderCommandPool 已创建，且 m_renderCommandBuffers 为空。
         * resize 不会重置已有槽位，未先销毁就再次调用会静默覆盖旧句柄并泄漏
         */
        void CreateRenderCommandBuffers();

        /**
         * 创建传输命令池
         *
         * 前置条件：m_pDevice 已绑定
         */
        void CreateTransferCommandPool();

        /**
         * 销毁传输命令池
         *
         * 不需要逐个销毁从中分配的命令缓冲：缓冲从池中分配，随池一起释放。
         * 池引用了逻辑设备，必须先于设备层销毁
         */
        void DestroyTransferCommandPool();

    private:
        /**
         * 当前这一份，构造时登记、析构时注销，取用见 GetInstance
         */
        static DoodleCommandManager* s_pInstance;

        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 渲染命令池
         */
        VkCommandPool m_renderCommandPool = VK_NULL_HANDLE;

        /**
         * 渲染命令缓冲，每个在途帧一个，用帧索引取用。
         * 随渲染命令池释放，不需要单独销毁
         */
        std::vector<VkCommandBuffer> m_renderCommandBuffers;

        /**
         * 当前正在录制的那条渲染命令；BeginFrame 置上，EndFrame 清空
         *
         * 同一时刻最多一条：一帧只往一条命令缓冲里录，多个 pass 顺序录进同一条
         */
        VkCommandBuffer m_renderRecordingCommandBuffer = VK_NULL_HANDLE;

        /**
         * 传输命令池：一次性拷贝命令专用
         *
         * 与 m_renderCommandPool 分开，是因为两个池里的缓冲寿命差着量级：绘制缓冲活满整个程序、
         * 反复重录，传输缓冲录一次、提交一次就还回来。池是驱动做分配决策的依据，
         * 两种用法混在一个池里，两边都拿不到合适的策略 ——
         * 建池时那个 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 说的正是这件事
         *
         * 不带 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：这里的缓冲
         * 走的是「分配 → 录 → 提交 → 释放」，不需要重置，见 BeginTransferCommand 里的注释
         */
        VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;

        /**
         * 当前活动传输命令；BeginTransferCommand 置上，EndTransferCommand 清空
         *
         * 同一时刻最多一条：传输都是初始化期串行上传，单句柄足够；
         * 也正因为记在这里，Begin / End 不需要把句柄交给调用方
         */
        VkCommandBuffer m_transferCommandBuffer = VK_NULL_HANDLE;
    };
}
