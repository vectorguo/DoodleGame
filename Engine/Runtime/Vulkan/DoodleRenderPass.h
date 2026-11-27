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
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsBuffer.h"
#include "DoodleRenderPassAttachmentFormats.h"
#include "DoodleRenderPassLayout.h"
#include "DoodleShaderPass.h"
#include "DoodleVulkanConfig.h"
#include "DoodleVulkanPipelineObject.h"

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
     * 这里只放调用方真正提供的东西。本 pass 每帧常量的绑定与大小、逐绘制项数据的
     * 范围不在这里：基类按自己的 pass 类型从 DoodleRenderPassLayout 取。附件格式
     * 也不在这里：它由派生类按自己的附件现算（GetAttachmentFormats），不是调用方输入。
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
     * 渲染 pass 基类：持有一种 pass 的全部管线与本 pass 的每帧常量资源
     *
     * 一个实例对应一种 pass（EDoodleRenderPassType）。具体是哪种由派生类决定：
     * DoodleForwardPass 是前向主 pass；以后阴影、延迟、后处理各派生一个。派生类在
     * 构造时报上自己的类型，负责自己的附件与 Render 录制，并按自己的附件报出格式
     * （GetAttachmentFormats：颜色 / 深度附件格式与采样数）；基类按类型取 DoodleRenderPassLayout，
     * 负责每个 pass 一份的每帧常量（UBO / 描述符集）与逐绘制项数据范围，以及「着色器 → 管线」
     * 的解析、查找、持有与按附件格式原地重建。
     *
     * **附件格式**：真相在派生类的附件来源里，派生类通过 GetAttachmentFormats 报。
     * 基类记一份表里管线建的时候用的（m_attachmentFormats）：Initialize 时问一次记下，之后新建的
     * 管线都按它建，表里始终是同一份格式。附件重建之后调 OnAttachmentsRecreated，
     * 现取一份和它比，变了才原地重建全部管线。建管线只读这份记录、不碰附件。
     *
     * **每个着色器在本 pass 一条管线**：键是着色器资产指针。渲染状态、顶点 / 实例
     * 格式、set 1 布局都是着色器描述的一部分，一个着色器在一个 pass 只有一份描述，
     * 不存在同一着色器要多条管线的情况。多个材质共用同一个着色器时，ResolvePipeline
     * 返回同一条管线；材质之间不同的是 set 1 与逐绘制项数据，不是管线。
     *
     * **按资产指针做键的前提**：着色器资产常驻，地址稳定（见 DoodleShader 的类注释）。
     * 同样的字节码出现在两个着色器资产里属于资产配置的重复，由资产侧的检查拦下；
     * 管线表不按内容去重，也因此不必对字节码做哈希与逐字节比对。
     *
     * **管线引用资产里的 pass 数据**：每条管线只记着色器 pass 数据的地址、不拷贝，
     * 原地重建时直接读资产里那份，所以资产要活到它的管线不再重建为止。退出时资产
     * 管理器先于渲染器销毁，那之后只剩 Destroy，不会再重建。以后卸载着色器时，
     * 要先把它的管线从各 pass 的表里拆掉，再销毁资产。
     *
     * **管线对象稳定**：每条管线单独堆上分配（unique_ptr），ResolvePipeline 返回的
     * 引用在 Destroy 之前一直有效。附件格式变化时 OnAttachmentsRecreated 原地重建，
     * 交出去的引用照样有效。
     *
     * **常驻，不淘汰**：管线数量等于本 pass 有描述的着色器数量，与世界多大无关，
     * 没有回收的必要。着色器资产同样常驻：大世界流式加载时卸载的是网格、贴图和
     * 材质，不是着色器。
     *
     * **线程**：只有渲染线程使用，不加锁（材质加载不再碰管线表，见 DoodleMaterial 的
     * 类注释）。以后要做后台并行预编译，再和任务系统一起定加锁范围 —— 管线表、附件
     * 格式都要重新过一遍。
     *
     * **生命周期**：由 DoodleRenderPipeline 持有，排在描述符管理器与驱动编译缓存之后建、
     * 之前拆 —— 原地重建时还要用到每帧常量的描述符集布局与驱动编译缓存。Destroy 先拆管线，
     * 再拆本 pass 的常量资源（池 → 布局 → UBO）。派生 pass 目前没有自己要拆的资源
     * （前向 pass 的附件是借的）；以后哪个 pass 自己持有资源（比如阴影 pass 自己建
     * shadow map），在 Destroy 末尾加派生类的清理钩子，派生类不要覆盖 Destroy。
     *
     * 不可复制：表里的管线各自持有句柄
     */
    class DoodleRenderPass
    {
    protected:
        /**
         * @param passType 这个 pass 是哪一种：基类按它取布局（GetRenderPassLayout）与着色器的描述
         */
        explicit DoodleRenderPass(const EDoodleRenderPassType passType)
            : m_passType(passType)
        {
        }

    public:
        virtual ~DoodleRenderPass() = default;

        DoodleRenderPass(const DoodleRenderPass&) = delete;
        DoodleRenderPass& operator=(const DoodleRenderPass&) = delete;

        /**
         * 销毁这个 pass 的全部管线与常量资源
         *
         * 可以重复调用，从未初始化时调用也安全。派生类不要覆盖本函数（见类注释）。
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
         * 单位是着色器而不是绘制项：每个着色器在本 pass 只有一条管线，
         * 与绘制项数量无关。默认实现按着色器解析本 pass 的管线（与录制共用
         * ResolvePipeline，预热过的着色器在录制时只剩一次查表），没有本 pass
         * 描述的着色器跳过 —— 队列里可能有别的 pass 专用的着色器。还不能建管线的
         * pass（附件格式未定）覆盖成空实现；要按网格或深度状态解析的 pass，
         * 由那个 pass 自己覆盖。
         *
         * @param shaders 刚初始化成功、管线还没建的着色器
         *
         * @throw 管线建不出来时向上抛（与 ResolvePipeline 同口径），由渲染管理器
         *        在取帧之前抛给应用
         *
         * @note 可重复调用；已经建过的管线不会重建。前置条件：已初始化、未 Destroy
         */
        virtual void WarmupShaders(const std::vector<const DoodleShader*>& shaders);

        /**
         * 附件重建之后调用：按派生类现取的附件格式和表里管线建的时候用的那份比，
         * 变了就原地重建这个 pass 的每一条管线
         *
         * 交换链重建（以后还有阴影图换格式）之后由 DoodleRenderPipeline 调用，每个 pass 都
         * 通知，不管这次重建的是不是它的附件。只变尺寸、或附件没动时格式相同，管线不动
         * （视口与剪裁是动态状态）。每条管线只换管线本身，管线布局与对象地址都不变，
         * 之前交出去的引用照样有效。
         *
         * 某一条建失败时异常向上抛：失败之前的几条已经是新格式，失败的那条和之后的
         * 还是旧格式（每条各自保证「建不出新的就留着旧的」），记录的附件格式保持旧值。
         * 这条路只会因设备级故障走到，调用方随后走销毁流程，不再继续画
         *
         * @note 前置条件：GPU 已空闲 —— 旧管线可能还被在途的命令引用着；派生类的附件
         *       已经重建好；表里的着色器资产都还活着（管线按资产里的 pass 数据重建）
         */
        void OnAttachmentsRecreated();

        /**
         * 这个 pass 里有几条管线
         *
         * 验收与以后的调试界面都要看它：每个在本 pass 有描述的着色器一条
         */
        [[nodiscard]] std::size_t GetPipelineCount() const
        {
            return m_pipelines.size();
        }

    protected:
        /**
         * 绑定这个 pass 共用的服务、按本 pass 的契约建常量资源，并记下附件格式；
         * 此时还没有任何管线
         *
         * 由派生类在自己的 Initialize 里调用，把调用方给的设备 / 编译缓存原样交给这里；
         * 每帧常量的绑定与大小、逐绘制项数据的范围由基类按构造时的 pass 类型从
         * DoodleRenderPassLayout 取。附件格式由基类在这里向派生类要一次（GetAttachmentFormats）
         * 记下，所以派生类要先绑好自己的附件来源再调这里。基类不关心这个 pass 的颜色 /
         * 深度附件从哪来。
         *
         * @param initInfo 设备与编译缓存，都必须活得比本 pass 久
         *
         * @throw std::runtime_error 常量资源建不出来；失败时已经建出的部分会被收掉
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleRenderPassInitInfo& initInfo);

        /**
         * 取某个着色器在本 pass 里的管线；没解析过就建一条
         *
         * 键是着色器资产。资产常驻、地址稳定（见类注释），表的大小只跟着着色器
         * 数量走；OnAttachmentsRecreated 原地重建不改变管线对象地址，已返回的引用一直有效。
         *
         * @throw std::invalid_argument 着色器没有本 pass 的描述；其余同
         *        DoodleVulkanPipelineObject::Initialize（描述不合法或管线建不出来，
         *        失败时表里不留任何东西）
         */
        [[nodiscard]] const DoodleVulkanPipelineObject& ResolvePipeline(const DoodleShader& shader);

        /**
         * 把每帧常量写进这一帧对应的 UBO
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

        /**
         * 本 pass 的颜色 / 深度附件格式与采样数，由派生类按自己的附件现取
         *
         * 基类只在两处问：Initialize 时记下第一份（m_attachmentFormats），附件重建后
         * OnAttachmentsRecreated 再取一份来比。两处附件都在：调基类 Initialize 之前
         * 派生类已经绑好附件来源，比对在附件重建之后。还没有附件的 pass（阴影骨架）
         * 返回占位值
         */
        [[nodiscard]] virtual DoodleRenderPassAttachmentFormats GetAttachmentFormats() const = 0;

        /**
         * 本 pass 逐绘制项数据的范围：录制时 vkCmdPushConstants 用它
         *
         * 直接取契约、不另存副本；建管线布局用的也是这一份
         */
        [[nodiscard]] VkPushConstantRange GetDrawItemDataRange() const
        {
            return GetRenderPassLayout(m_passType).drawItemDataRange;
        }

        /**
         * 本帧要用的每帧常量描述符集：派生 pass 在绘制前绑定它
         */
        [[nodiscard]] VkDescriptorSet GetPassUboDescriptorSet(const uint32_t frameIndex) const
        {
            return m_passUboDescriptorSets[frameIndex];
        }

    private:
        /**
         * 建一条管线并插进表里
         *
         * 只由 ResolvePipeline 在未命中时调用。shaderPass 是着色器资产里的那份，
         * 管线记下它的地址。失败时把刚插入的条目摘掉再抛
         */
        [[nodiscard]] const DoodleVulkanPipelineObject& CreatePipeline(const DoodleShader& shader, const DoodleShaderPass& shaderPass);

        /**
         * 销毁表里的每条管线并清空
         */
        void DestroyPipelines();

        /**
         * 按本 pass 的契约建出每帧常量的 UBO、描述符集布局、池与集合（每在途帧一个）
         *
         * 任一步失败都把已经建出的部分收掉再抛。调用前 m_pDevice 已经绑定。
         */
        void CreatePassUboResources();

        /**
         * 销毁每帧常量的池、描述符集布局与 UBO；空句柄 / 未初始化时是空操作
         *
         * 顺序：池（集合随之失效）→ 布局 → UBO。必须在管线之后调用
         */
        void DestroyPassUboResources();

    private:
        /**
         * 这个 pass 是哪一种；构造时定下，之后不变
         */
        const EDoodleRenderPassType m_passType;

        /**
         * 表里管线建的时候用的附件格式与采样数：Initialize 时按 GetAttachmentFormats 记下，
         * 新建的管线都按它建；格式变了，OnAttachmentsRecreated 把管线全部重建成功后才换
         *
         * 不是附件此刻的格式（那是 GetAttachmentFormats）：附件重建之后、比对之前两者可能不同 ——
         * 要不要重建正是靠这份旧值判断
         */
        DoodleRenderPassAttachmentFormats m_attachmentFormats;

        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 每帧常量的 UBO：本 pass 每在途帧一块，HostVisible 类型
         *
         * 不随交换链重建：窗口缩放只改投影矩阵的内容，不改缓冲对象。
         * 数量必须与 MAX_FRAMES_IN_FLIGHT 一致
         */
        std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT> m_passUbos;

        /**
         * 本 pass 每帧常量的描述符集布局；这个 pass 的所有管线布局都用它
         */
        VkDescriptorSetLayout m_passUboDescriptorSetLayout = VK_NULL_HANDLE;

        /**
         * 每帧常量的描述符池；集合的寿命与池一致，随池一起失效
         */
        VkDescriptorPool m_passUboDescriptorPool = VK_NULL_HANDLE;

        /**
         * 每帧常量的描述符集：每在途帧一个，各自把该帧的 UBO 接到 binding 0
         */
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_passUboDescriptorSets{};

        /**
         * 驱动编译缓存，不持有
         */
        VkPipelineCache m_pipelineCompileCache = VK_NULL_HANDLE;

        /**
         * 管线表：着色器 → 本 pass 的管线
         *
         * 键是着色器资产指针：资产常驻、地址稳定（见类注释），规模只跟着着色器
         * 数量走。管线原地重建不改变对象地址，已返回的引用一直有效；Destroy 时
         * 连同条目一起清空
         */
        std::unordered_map<const DoodleShader*, std::unique_ptr<DoodleVulkanPipelineObject>> m_pipelines;
    };
}
