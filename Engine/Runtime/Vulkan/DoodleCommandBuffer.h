//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "Rendering/DoodleDrawItem.h"

namespace Doodle
{
    //四者只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleDepthImages;
    class DoodleGraphicsPipeline;
    class DoodleSwapChain;
    class DoodleVulkanDevice;

    /**
     * 命令缓冲层：把命令录下来，送到队列上
     *
     * 本层管两样东西：
     *
     * - **每帧的绘制流**（Record）：每个在途帧一个命令缓冲，反复重录不重建。
     *   交换链重建时本层也不重建 —— 本层不缓存任何上游层的句柄，
     *   录制时现取，重建后重录拿到的自然是新句柄
     * - **传输命令池与一次性传输**：拷贝 / 上传用的池。池留在这里，因为它和绘制池
     *   是同一种东西；「从池里分配命令缓冲、提交、等待、释放」也在这里
     *   （BeginOneTimeTransfer / EndOneTimeTransfer）。资源类只负责录「拷什么」，
     *   谁拥有池，谁负责命令缓冲的生灭
     *
     * 生命周期与程序一致。两个命令池都只在 Initialize 里建一次；
     * 交换链重建时本层不重建，见上
     *
     * 只读引用设备层、交换链层与图形管线层，生命周期须短于 DoodleSwapChain
     *
     * 动态渲染之后本层多担一件事：渲染目标的布局转换。
     * 渲染通道时代那是驱动在通道边界自动做的，现在三条屏障都写在这里
     * （颜色入口、深度入口、颜色出口）
     *
     * 本类在进程内是唯一的一份：构造时登记、析构时注销，
     * 供 DoodleGraphicsBuffer::CopyTo、纹理上传这类资源侧动作找到它 ——
     * 理由与代价见 GetInstance
     *
     * 命令池与命令缓冲不拆成两个类：命令缓冲从池中分配、随池释放，
     * 两者的生命周期完全重合，没有各自独立的锚点
     */
    class DoodleCommandBuffer
    {
    public:
        /**
         * 构造时把自己登记为「当前那一份」，析构时注销（见 GetInstance）
         */
        DoodleCommandBuffer();
        ~DoodleCommandBuffer();

        //登记关系是一对一的：复制一份出来，s_pInstance 只会指向其中一个，
        //另一份对 GetInstance 不可见 —— 那不是这个类想要的语义
        DoodleCommandBuffer(const DoodleCommandBuffer&) = delete;
        DoodleCommandBuffer& operator=(const DoodleCommandBuffer&) = delete;

        /**
         * 取当前那一份命令缓冲层；没有时返回 nullptr
         *
         * 存在的理由只有一个：`DoodleGraphicsBuffer` 的拷贝与纹理上传按功能归属
         * 在资源类上，却需要本层的传输池 —— 而池归本层管。与其让调用方沿着每一层
         * 把引用传下来，不如给本层开一个和 DoodleApplication 同形的全局入口
         *
         * 与 DoodleApplication::GetInstance 的区别只有一处：那边返回引用
         * （它假定调用时对象必然活着），这里返回指针 —— 因为「取不到就失败」
         * 是拷贝失败时的既定语义，返回引用会把「取不到」变成崩溃
         *
         * @note 和 DoodleApplication 一样，本工程同一进程只有一份。
         * 但「取到了」不等于「能用了」：对象在 Initialize 之前就已经构造，
         * 命令池却要到那时才有。真正的前置条件由 BeginOneTimeTransfer 自己判断
         *
         * @note 代价要记清楚：这层依赖是**隐式**的 —— 调用点看不出它依赖命令层，
         * 也看不出命令层此刻是否可用。所以只给真正需要全局入口的地方开这个口子，
         * 别把别的层照这个模式铺开；层与层之间能注入就注入
         * （参见 DoodleAndroidWindow 为什么收引用而不是调 GetInstance）
         */
        [[nodiscard]] static DoodleCommandBuffer* GetInstance();

        /**
         * 初始化命令缓冲层
         * @param device 已初始化的Vulkan设备
         * @param swapChain 已初始化的交换链
         * @param graphicsPipeline 已初始化的图形管线
         * @param depthImages 已初始化的深度图组，按 frameIndex 取本帧那一张
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const DoodleSwapChain& swapChain,
                        const DoodleGraphicsPipeline& graphicsPipeline,
                        const DoodleDepthImages& depthImages);

        /**
         * 销毁命令缓冲层
         */
        void Destroy();

        /**
         * 录制一帧的命令
         * @param frameIndex 本帧用哪一条命令缓冲。由本层按它自己取 ——
         *        调用方交出帧索引即可，不必先取好句柄再传进来
         * @param imageIndex 当前交换链图像的索引，来自vkAcquireNextImageKHR
         * @param drawItems 本帧要画的一组绘制项。每个元素给出网格、子网格下标、
         *        材质与模型矩阵；空表示本帧只清屏不画东西 —— 那是合法状态，
         *        不是错误。用 const vector& 而不是 span：当前唯一调用方传的就是 vector，
         *        视图类型没有实际收益；出现第二种容器 / 子区间时再泛化
         * @param descriptorSet 本帧要绑定的描述符集，必须与 frameIndex 同槽位。
         *        它把该帧的 UBO（view / proj / 主光源）接到 binding 0；所有权在门面，本层只使用
         * @return 录好的那条命令缓冲，供调用方提交
         *
         * 把句柄交回去而不是让调用方自己再按索引取一次：录的和提交的必须是同一条，
         * 而这个对应关系只有本层知道。让它成为返回值，两边就不可能指向不同的槽位
         *
         * 交换链的图像与图像视图都按 imageIndex 现取，所以本层不随交换链重建
         *
         * 这组对象与描述符集都走参数、不走成员：本层使用它们，但不拥有它们。
         * 绘制项每帧由应用层从场景收集后交进来，描述符集的寿命锚在程序上。
         * 塞成成员等于把「每帧都会变的东西」和「只认程序生命周期的东西」混在一起，
         * 凭空多出「重建时要不要跟着换」这种问题。「谁使用」和「谁拥有」是两件事
         *
         * 组里每个绘制项共用同一条管线、同一份视口、同一个描述符集；
         * 随绘制项变化的只有 localToWorld 与材质基色（push constant）、顶点 / 索引缓冲、
         * 以及子网格下标对应的 firstIndex / indexCount。所以共用状态在循环外只设一次，
         * 循环里只录随绘制项变的那几样。
         *
         * 一个对象可能有多个子网格，收集侧会把它展开成多条绘制项 ——
         * 本层按每条项的 submeshIndex 回网格取区间，不关心它按什么材质分了段
         *
         * 绘制顺序就是数组顺序：本帧没有透明对象，不需要按深度排序，
         * 深度测试自己会把前后关系画对。等引入透明混合时，排序是收集侧的
         * 责任，不是本层 —— 本层照着顺序录，不替调用方重排
         *
         * 索引类型不在此列：本项目的索引就是 uint32_t，这个事实写死在实现里，
         * 不做成参数把一个常量串过每一层。代价是它与 DoodleMesh 的索引类型绑在一起；
         * 将来换索引宽度时两处要一起改
         *
         * 名字不带 CommandBuffer：类名已经交代了宾语，重复一遍不带来信息
         */
        [[nodiscard]] VkCommandBuffer Record(const uint32_t frameIndex,
                                             const uint32_t imageIndex,
                                             const std::vector<DoodleDrawItem>& drawItems,
                                             VkDescriptorSet descriptorSet);

        /**
         * 开始一条一次性传输命令缓冲
         *
         * 分配 + begin 在这里做完，返回的句柄交给调用方录制。用传输池而不是
         * 绘制池：绘制缓冲活满整个程序、反复重录；这条录一次就提交并释放。
         *
         * 调用方录完必须交给 EndOneTimeTransfer —— 本层不持有这条命令缓冲，
         * 中途抛异常就没人释放了。中间只该调用不抛异常的录制函数
         *
         * @throws std::runtime_error 本层尚未 Initialize（传输池不存在），
         *         或分配 / begin 失败。失败时会释放已经分配的部分
         */
        [[nodiscard]] VkCommandBuffer BeginOneTimeTransfer() const;

        /**
         * 结束、提交并等待一条一次性传输命令缓冲，然后释放它
         *
         * 等整个图形队列空闲：一次上传不追求异步，而且调用方紧接着通常要
         * 销毁中转资源，必须确认 GPU 不再引用它们。提交本身提供可见性，
         * 不需要额外内存屏障；将来把上传与绘制合进同一次提交时才要补
         *
         * @param commandBuffer 必须来自 BeginOneTimeTransfer，且尚未提交
         *
         * @throws std::runtime_error 本层尚未 Initialize，或 end / 提交失败。
         *         end / 提交失败时会先释放命令缓冲再抛
         */
        void EndOneTimeTransfer(VkCommandBuffer commandBuffer) const;

    private:
        /**
         * 录一次布局转换：取图之后、开画之前，把交换链图像转成能当颜色附件用的布局
         *
         * 名字说的是效果（这张图变成了什么），不是机制（录进去的是屏障）——
         * 读 Record 的人扫到这一行就该知道图像到哪儿了
         *
         * 动态渲染不做自动布局转换，这件事原来是驱动在渲染通道入口替我们做的。
         * 六格取值（两个布局、两个 access、两个 stage）是这一层对这个渲染目标的
         * 策略，不是可调输入，所以不走参数
         */
        void RecordImageTransitionToColorAttachment(VkCommandBuffer commandBuffer, const uint32_t imageIndex) const;

        /**
         * 录一次布局转换：取图之后、开画之前，把本帧的深度图转成能当深度附件用的布局
         *
         * 每帧收 frameIndex，因为深度图跟着在途帧轮转，不跟交换链图像走。
         * 这条屏障的源 scope 是空的：同一槽位上一次使用（k-2 帧）由帧栅栏保证已完成，
         * 它只负责转布局，不负责串两帧
         */
        void RecordImageTransitionToDepthAttachment(VkCommandBuffer commandBuffer, const uint32_t frameIndex) const;

        /**
         * 录一次布局转换：画完之后、交回呈现引擎之前，转回呈现布局
         */
        void RecordImageTransitionToPresent(VkCommandBuffer commandBuffer, const uint32_t imageIndex) const;

        /**
         * 创建命令池
         */
        void CreateCommandPool();

        /**
         * 销毁命令池，并把命令池与命令缓冲的句柄一并置空
         *
         * 不需要逐个销毁命令缓冲：缓冲从池中分配，随池一起释放
         */
        void DestroyCommandPool();

        /**
         * 从命令池分配命令缓冲，每个在途帧一个
         *
         * 前置条件：m_commandPool 已创建，且 m_commandBuffers 为空。
         * resize 不会重置已有槽位，未先销毁就再次调用会静默覆盖旧句柄并泄漏
         */
        void CreateCommandBuffers();

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
        static DoodleCommandBuffer* s_pInstance;

        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 交换链层，在 Initialize 时绑定，取渲染区域尺寸、图像与图像视图用
         */
        const DoodleSwapChain* m_pSwapChain = nullptr;

        /**
         * 图形管线层，在 Initialize 时绑定
         */
        const DoodleGraphicsPipeline* m_pGraphicsPipeline = nullptr;

        /**
         * 深度图组，在 Initialize 时绑定，按 frameIndex 取本帧那一张
         */
        const DoodleDepthImages* m_pDepthImages = nullptr;

        /**
         * 命令池
         */
        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        /**
         * 命令缓冲，每个在途帧一个，用帧索引取用。
         * 随命令池释放，不需要单独销毁
         */
        std::vector<VkCommandBuffer> m_commandBuffers;

        /**
         * 传输命令池：一次性拷贝命令专用
         *
         * 与 m_commandPool 分开，是因为两个池里的缓冲寿命差着量级：绘制缓冲活满整个程序、
         * 反复重录，传输缓冲录一次、提交一次就还回来。池是驱动做分配决策的依据，
         * 两种用法混在一个池里，两边都拿不到合适的策略 ——
         * 建池时那个 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 说的正是这件事
         *
         * 不带 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：这里的缓冲
         * 走的是「分配 → 录 → 提交 → 释放」，不需要重置，见 BeginOneTimeTransfer 里的注释
         */
        VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    };
}
