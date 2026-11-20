//
//  DoodleRenderPass.h
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsBuffer.h"
#include "DoodleVulkanConfig.h"
#include "DoodleVulkanPipelineObject.h"
#include "Rendering/DoodlePassShaderInterface.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;
    class DoodleShader;
    struct DoodleFrameContext;

    /**
     * 本帧各 pass 共用的输入
     *
     * 只装跨 pass 共享的东西；pass 自己的附件与常量资源都在 pass 内部（拥有或借用）。
     * 帧索引、图像索引、命令缓冲与绘制项都在 DoodleFrameContext 里，由 Render 的
     * 第一个参数提供。与 FrameContext 一样，只在本次 Render 期间有效。
     *
     * 命名口径：DoodleFrameContext 是整帧的输入与状态；本类是整帧共享的渲染资源；
     * 初始化输入用 ...InitInfo。
     */
    struct DoodleRenderPassContext
    {
        /**
         * 每条绘制项对应的 set 1，与 frameContext.drawItems 等长、下标一一对应
         *
         * 由 DoodleDescriptorManager 按材质缓存，DoodleRenderPipeline 每帧解析一次；
         * 所有用材质贴图的 pass 共用同一份结果 —— 材质的 set 1 布局按整个着色器
         * 合并，在哪个 pass 的管线上都能绑。材质的着色器没有声明贴图时为
         * VK_NULL_HANDLE，pass 不绑 set 1
         */
        const std::vector<VkDescriptorSet>& materialDescriptorSets;
    };

    /**
     * 初始化 pass 时由调用方提供的共享服务
     *
     * 里面全是借来的句柄，不归本 pass 所有，必须活得比它久。派生类原样收下
     * 再转交给基类；以后基类要多用一项共享服务，往这里加字段即可，
     * 各派生 pass 的 Initialize 签名与调用点都不用动。
     *
     * 必填项用引用：漏填直接编译不过；可空的句柄用值 + 默认值。
     *
     * 这里只放调用方真正提供的东西。本 pass 的 set 0 绑定、UBO 大小与 push 范围
     * 不在这里：基类按自己的 pass 类型从 DoodlePassShaderInterface 取。passConfig
     * 也不在这里：它的生产者是派生类（附件格式由派生类自己的资源决定），不是调用方
     * 输入。放进来的话会变成「调用方填了也会被覆盖」的字段。
     */
    struct DoodleRenderPassInitInfo
    {
        /**
         * 已初始化的 Vulkan 设备
         */
        const DoodleVulkanDevice& device;

        /**
         * 驱动编译缓存
         */
        VkPipelineCache pipelineCompileCache = VK_NULL_HANDLE;
    };

    /**
     * 渲染 pass 基类：持有一个 pass 配置下的全部管线与本 pass 的每帧常量资源
     *
     * 一个实例对应一种 pass 配置（颜色 / 深度附件格式与采样数）。具体是哪种 pass
     * 由派生类决定：DoodleForwardPass 是前向主 pass；以后阴影、延迟、后处理各派生
     * 一个。派生类在构造时报上自己的 EDoodlePassType，负责自己的附件、pass 配置与
     * Render 录制；基类按这个类型取 DoodlePassShaderInterface，负责每个 pass 一份的
     * set 0 UBO / 描述符集 / push 范围，以及「着色器 → 管线」的解析、查找、持有与
     * 批量重建。
     *
     * **查找键按内容计算**：字节码内容、渲染状态、顶点格式、实例格式、set 1 的绑定
     * 定义。哈希只用来分桶，命中后还要逐项比对，所以哈希碰撞、对象地址被复用都不会
     * 拿错管线。内容相同的两个着色器自动共用一条管线；以后做热重载、字节码变了，
     * 自然拿到新管线。set 1 布局按定义进键、不按句柄：set 1 按整个着色器的所有 pass
     * 合并，同一份字节码可能配上不同的 set 1；而定义相同的布局在 Vulkan 里彼此兼容
     * （每个着色器资产的布局句柄不同，但定义一样就能共用管线）。
     *
     * **管线数量跟着「着色器 × 渲染状态 × pass」走，不跟着材质数量走。**
     * 多个材质共用同一个着色器时，GetPipeline 返回同一条管线；材质之间不同的是
     * set 1 与 push constant 的数据，不是管线。
     *
     * **PipelineItem 稳定**：每个条目单独堆上分配（unique_ptr），GetPipeline 返回的
     * 引用在 Destroy 之前一直有效 —— 着色器 memo（ResolvePipeline）会长期保存它。
     * 附件格式变化时 RebuildPipelines 原地重建，交出去的引用照样有效。
     *
     * **常驻，不淘汰**：管线数量只取决于「着色器 × 渲染状态 × pass」，与世界多大
     * 无关，没有回收的必要。着色器资产同样常驻：大世界流式加载时卸载的是网格、
     * 贴图和材质，不是着色器。
     *
     * **加锁**：管线表的查找与插入都在锁内。现在只有渲染线程调用（材质加载不再碰
     * 管线表，见 DoodleMaterial 的类注释）；以后要做后台并行预编译，再和任务系统一起定。
     * 这一步直接在锁内建管线（同一时刻只编一条）；驱动编译缓存本身是内部同步的，
     * 不拖后腿。着色器 memo 只在渲染线程读写，不加锁。
     *
     * **生命周期**：由 DoodleRenderPipeline 持有，排在描述符管理器与驱动编译缓存之后建、
     * 之前拆 —— 原地重建时还要用到 set 0 布局与驱动编译缓存。Destroy 先拆管线，
     * 再拆本 pass 的常量资源（池 → 布局 → UBO），再调派生类的 OnDestroy 拆这个
     * pass 自己的资源；派生类不要覆盖 Destroy。
     *
     * 不可复制：PipelineItem 里的管线各自持有句柄
     */
    class DoodleRenderPass
    {
        /**
         * 一条管线记录：重建所需的全部输入 + 建出来的管线
         *
         * 描述按值拷一份（字节码副本、渲染状态、顶点 / 实例格式、set 1 布局句柄与定义）：
         * 调用方的那份可能早就释放了，原地重建时要用的是这里的。
         * set 1 布局句柄只在建管线布局时用过一次，原地重建保留布局、不再读它，
         * 所以着色器资产先于本 pass 销毁之后它悬空也无妨
         */
        struct PipelineItem
        {
            DoodleVulkanPipelineShaderDescription shaderDescription;
            DoodleVulkanPipelineObject pipeline;
        };

    protected:
        /**
         * @param passType 这个 pass 是哪一种：基类按它取着色器契约与着色器的描述
         */
        explicit DoodleRenderPass(const EDoodlePassType passType)
            : m_passType(passType)
        {
        }

    public:
        virtual ~DoodleRenderPass() = default;

        DoodleRenderPass(const DoodleRenderPass&) = delete;
        DoodleRenderPass& operator=(const DoodleRenderPass&) = delete;

        /**
         * 销毁这个 pass 的全部管线、常量资源，再让派生类拆自己的资源
         *
         * 可以重复调用。派生类通过 OnDestroy 挂接自己的清理，不要覆盖本函数。
         * 前置条件：GPU 已空闲、没有线程还在用这个 pass
         */
        virtual void Destroy();

        /**
         * 把一个 pass 录进给定的命令缓冲
         *
         * 帧级数据（命令缓冲、帧 / 图像索引、绘制项）从 frameContext 取；
         * 本帧共享的材质描述符集从 passContext 取。pass 自己的附件与常量资源
         * 由派生类持有（拥有或借用）。附件怎么转布局、用哪条管线、怎么绑描述符
         * 与顶点缓冲、发多少 draw，都是这个 pass 的事；命令缓冲的 begin / end
         * 与提交归 DoodleVulkanManager。
         *
         * @param frameContext 整帧的输入与状态；只在本次调用期间有效
         * @param passContext 本帧各 pass 共用的材质描述符集；只在本次调用期间有效
         *
         * @note 前置条件：命令缓冲已经 begin、尚未 end；GPU 不会在本 pass
         *       录制期间改写它引用的附件
         */
        virtual void Render(const DoodleFrameContext& frameContext, const DoodleRenderPassContext& passContext) = 0;

        /**
         * 预先建出这批着色器在这个 pass 里会用到的管线，不录任何命令
         *
         * 由 DoodleRenderPipeline 在取帧之前每帧调用，输入是待预热队列
         * 排空的那批「刚初始化成功的着色器」。每帧从队列重算预期状态，不依赖
         * 调用方记住「预热过了」；稳态队列为空，本函数一次都不会被调用。
         * 只解析管线，不碰附件、描述符集与命令缓冲。
         *
         * 单位是着色器而不是绘制项：管线的身份只取决于「着色器 × 渲染状态 × pass」，
         * 与绘制项数量无关。默认实现按着色器解析本 pass 的管线（与录制共用
         * ResolvePipeline，预热过的着色器在录制时只剩一次 memo 查表），没有本 pass
         * 描述的着色器跳过 —— 队列里可能有别的 pass 专用的着色器。还不能建管线的
         * pass（附件格式未定）覆盖成空实现；要按网格或深度状态解析的 pass，
         * 由那个 pass 自己覆盖。
         *
         * @param shaders 刚初始化成功、管线还没建的着色器
         *
         * @throw 管线建不出来时向上抛（与 GetPipeline 同口径），由渲染管理器
         *        在取帧之前抛给应用
         *
         * @note 可重复调用；已经建过的管线不会重建。前置条件：已初始化、未 Destroy
         */
        virtual void WarmupShaders(const std::vector<const DoodleShader*>& shaders);

        /**
         * 换一套 pass 配置，原地重建这个 pass 的每一条管线
         *
         * 附件格式变了（交换链重建换了格式、阴影图换了格式）时由派生类调用。
         * 每条管线只换管线本身，管线布局与对象地址都不变，之前交出去的引用照样有效。
         *
         * 不比对新旧配置：要不要重建由调用方判断（DoodleForwardPass 自己比对），
         * 这样相同格式也能强制走一遍这条路。
         *
         * 某一条建失败时异常向上抛：失败之前的几条已经是新格式，失败的那条和之后的
         * 还是旧格式（每条各自保证「建不出新的就留着旧的」），记录的 pass 配置保持旧值。
         * 这条路只会因设备级故障走到，调用方随后走销毁流程，不再继续画
         *
         * @note 前置条件：GPU 已空闲 —— 旧管线可能还被在途的命令引用着
         */
        void RebuildPipelines(const DoodleVulkanPipelinePassConfig& passConfig);

        /**
         * 取一条管线；没有就建一条
         *
         * 名字不体现「创建」：调用方只需要知道「给我这个描述对应的管线」。
         * 首次未命中会创建管线（可能编译、可能抛异常）；之后同一份描述只查表。
         * 返回的引用在 Destroy 之前一直有效。
         *
         * 线程安全：哈希在锁外算，查表与创建在锁内
         *
         * @throw std::invalid_argument / std::runtime_error 描述不合法或管线建不出来，
         *        见 DoodleVulkanPipelineObject::Initialize。失败时表里不留任何东西
         *
         * @note 前置条件：已初始化、未 Destroy
         */
        [[nodiscard]] const DoodleVulkanPipelineObject& GetPipeline(const DoodleVulkanPipelineShaderDescription& shaderDescription);

        /**
         * 这个 pass 里有几条管线
         *
         * 验收与以后的调试界面都要看它：同一个着色器配了几种渲染状态、
         * 有没有因为描述写法不同而白建了重复的管线
         */
        [[nodiscard]] std::size_t GetPipelineCount() const;

        /**
         * 当前的 pass 配置（按值返回，取的时候在锁内）
         */
        [[nodiscard]] DoodleVulkanPipelinePassConfig GetPassConfig() const;

    protected:
        /**
         * 绑定这个 pass 共用的服务、按本 pass 的契约建常量资源，并记下 pass 配置；
         * 此时还没有任何管线
         *
         * 由派生类在自己的 Initialize 里调用。派生类先按自己的附件算好 passConfig，
         * 再连同调用方给的设备 / 编译缓存一起交给这里；set 0 绑定、UBO 大小与 push
         * 范围由基类按构造时的 pass 类型从 DoodlePassShaderInterface 取。基类不关心
         * 这个 pass 的颜色 / 深度附件从哪来。
         *
         * @param initInfo 设备与编译缓存，都必须活得比本 pass 久
         * @param passConfig 这种 pass 的附件格式与采样数
         *
         * @throw std::invalid_argument / std::runtime_error set 0 绑定不合法或
         *        常量资源建不出来；失败时已经建出的部分会被收掉
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleRenderPassInitInfo& initInfo, const DoodleVulkanPipelinePassConfig& passConfig);

        /**
         * 派生类拆自己的资源；基类已经先把管线和常量资源拆完
         *
         * 此时基类的 device 仍然有效（句柄在 OnDestroy 返回之后才清空）。
         * 可能在从未初始化时被调用，也可能被重复调用；实现要能处理这两种情况。
         * 默认什么都不做。
         */
        virtual void OnDestroy() {}

        /**
         * 取某个着色器在本 pass 里的管线；没解析过就向管线表要一条
         *
         * 着色器 memo 只是内容键查表的缓存：着色器资产常驻（见类注释），地址稳定，
         * 表的大小只跟着着色器数量走；RebuildPipelines 原地重建不改变管线对象地址，
         * 所以 memo 不需要失效。内容相同的两个着色器资产在这里拿到同一条管线
         *
         * @throw std::invalid_argument 着色器没有本 pass 的描述；其余同 GetPipeline
         */
        [[nodiscard]] const DoodleVulkanPipelineObject& ResolvePipeline(const DoodleShader& shader);

        /**
         * 这个 pass 是哪一种
         */
        [[nodiscard]] EDoodlePassType GetPassType() const
        {
            return m_passType;
        }

        /**
         * 这个 pass 绑定的设备；OnDestroy 时仍然有效
         */
        [[nodiscard]] const DoodleVulkanDevice* GetDevice() const
        {
            return m_pDevice;
        }

        /**
         * 本 pass 的 push constant 范围：录制时 vkCmdPushConstants 用它
         */
        [[nodiscard]] VkPushConstantRange GetPushConstantRange() const
        {
            return m_pushConstantRange;
        }

        /**
         * 本帧的 set 0 描述符集：派生 pass 在绘制前绑定它
         */
        [[nodiscard]] VkDescriptorSet GetPassUboDescriptorSet(const uint32_t frameIndex) const
        {
            return m_passUboDescriptorSets[frameIndex];
        }

        /**
         * 把本帧的常量写进 set 0 对应的 UBO
         *
         * 调用位置必须在帧栅栏之后、本 pass 录制之前（见各 pass 的 Render）。
         *
         * @param frameIndex 在途帧索引
         * @param pData 源数据
         * @param size 字节数，不得超过创建时的大小
         */
        void WritePassUbo(const uint32_t frameIndex, const void* pData, const VkDeviceSize size)
        {
            m_passUbos[frameIndex].Write(pData, size);
        }

    private:
        /**
         * 查找键的哈希：只用来分桶
         *
         * 与 IsSameKey 同口径：判等看的成员都要进哈希；set 1 两边都只看定义、不看句柄
         */
        [[nodiscard]] static std::size_t HashKey(const DoodleVulkanPipelineShaderDescription& shaderDescription);

        /**
         * 两份描述是不是同一个查找键：字节码逐字节、渲染状态逐成员、
         * 顶点与实例格式、set 1 的绑定定义，全部相等才算
         */
        [[nodiscard]] static bool IsSameKey(const DoodleVulkanPipelineShaderDescription& lhs,
                                            const DoodleVulkanPipelineShaderDescription& rhs);

        /**
         * 建一条 PipelineItem 并插进表里
         *
         * 调用方必须已经持有 m_mutex（GetPipeline 在锁内调它，避免两个线程同时
         * 为同一份描述各建一条）。失败时把刚插入的条目摘掉再抛
         */
        [[nodiscard]] PipelineItem& CreatePipelineItem(const std::size_t keyHash,
                                                       const DoodleVulkanPipelineShaderDescription& shaderDescription);

        /**
         * 销毁表里的每条管线并清空；调用方必须已经持有 m_mutex
         */
        void DestroyPipelines();

        /**
         * 按本 pass 的契约建出 set 0 UBO、布局、池与每帧集合
         *
         * 任一步失败都把已经建出的部分收掉再抛。调用前 m_pDevice 已经绑定。
         */
        void CreatePassUboResources(const DoodlePassShaderInterface& passInterface);

        /**
         * 销毁 set 0 的池、布局与 UBO；空句柄 / 未初始化时是空操作
         *
         * 顺序：池（集合随之失效）→ 布局 → UBO。必须在管线之后调用
         */
        void DestroyPassUboResources();

    private:
        /**
         * 这个 pass 是哪一种；构造时定下，之后不变
         */
        const EDoodlePassType m_passType;

        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 驱动编译缓存，不持有
         */
        VkPipelineCache m_pipelineCompileCache = VK_NULL_HANDLE;

        /**
         * 本 pass 的 push constant 范围
         */
        VkPushConstantRange m_pushConstantRange{};

        /**
         * 本 pass 每帧一块 set 0 UBO，HostVisible 类型
         *
         * 不随交换链重建：窗口缩放只改投影矩阵的内容，不改缓冲对象。
         * 数量必须与 MAX_FRAMES_IN_FLIGHT 一致
         */
        std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT> m_passUbos;

        /**
         * 本 pass 的 set 0 布局；这个 pass 的所有管线布局都用它
         */
        VkDescriptorSetLayout m_passUboDescriptorSetLayout = VK_NULL_HANDLE;

        /**
         * set 0 的描述符池；集合的寿命与池一致，随池一起失效
         */
        VkDescriptorPool m_passUboDescriptorPool = VK_NULL_HANDLE;

        /**
         * 每在途帧一个 set 0，各自把该帧的 UBO 接到 binding 0
         */
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_passUboDescriptorSets{};

        /**
         * 这种 pass 当前的附件格式与采样数
         */
        DoodleVulkanPipelinePassConfig m_passConfig;

        /**
         * 查找键哈希 → 管线记录。哈希只分桶：同一个桶里可能有内容不同的记录，
         * 取的时候逐个 IsSameKey
         */
        std::unordered_multimap<std::size_t, std::unique_ptr<PipelineItem>> m_pipelineItems;

        /**
         * 着色器 → 本 pass 的管线
         *
         * 只是「着色器描述 → 管线」这张内容键查表的 memo：着色器资产常驻，
         * 按资产地址做键是安全的，规模只跟着着色器数量走。管线原地重建不改变
         * 对象地址，所以不需要失效；Destroy 时跟着管线一起清空
         */
        std::unordered_map<const DoodleShader*, const DoodleVulkanPipelineObject*> m_pipelinesByShader;

        /**
         * 保护 m_pipelineItems 与 m_passConfig
         */
        mutable std::mutex m_mutex;
    };
}
