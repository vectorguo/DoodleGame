//
//  DoodleRenderPass.h
//  Doodle
//
//  Created by 郭智 on 2026/11/8.
//

#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <vulkan/vulkan_core.h>

#include "DoodleGraphicsPipeline.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 渲染 pass 基类：持有一个 pass 配置下的全部管线
     *
     * 一个实例对应一种 pass 配置（颜色 / 深度附件格式与采样数）。具体是哪种 pass
     * 由派生类决定：DoodleForwardPass 是前向主 pass；以后阴影、延迟、后处理各派生
     * 一个。派生类负责自己的资源与 pass 配置，基类负责「描述 → 管线」的查找、
     * 持有与批量重建。
     *
     * **查找键按内容计算**：字节码内容、渲染状态、顶点格式、实例格式。哈希只用来
     * 分桶，命中后还要逐字节比对，所以哈希碰撞、对象地址被复用都不会拿错管线。
     * 内容相同的两个着色器自动共用一条管线；以后做热重载、字节码变了，自然拿到新管线。
     * set 1 布局不进键：字节码相同时反射出的布局定义也相同，而 Vulkan 认为定义相同的
     * 布局彼此兼容（每个着色器资产的布局句柄不同，但定义一样）。
     *
     * **管线数量跟着「着色器 × 渲染状态 × pass」走，不跟着材质数量走。**
     * 多个材质共用同一个着色器时，GetPipeline 返回同一条管线；材质之间不同的是
     * set 1 与 push constant 的数据，不是管线。
     *
     * **PipelineItem 稳定**：每个条目单独堆上分配（unique_ptr），GetPipeline 返回的
     * 引用在 Destroy 之前一直有效 —— 材质会长期保存它。附件格式变化时 RebuildPipelines
     * 原地重建，交出去的引用照样有效。
     *
     * **常驻，不淘汰**：管线数量只取决于「着色器 × 渲染状态 × pass」，与世界多大
     * 无关，没有回收的必要。着色器资产同样常驻：大世界流式加载时卸载的是网格、
     * 贴图和材质，不是着色器。
     *
     * **加锁**：查找与插入都在锁内。现在只有主线程调用，但大世界一定会做后台加载，
     * 届时加载线程也会来要管线。这一步直接在锁内建管线（同一时刻只编一条）；
     * 并行编译怎么做，等以后和任务系统一起定。驱动编译缓存本身是内部同步的，不拖后腿。
     *
     * **生命周期**：由门面或渲染图持有，排在描述符管理器与驱动编译缓存之后建、
     * 之前拆 —— 原地重建时还要用到 set 0 布局与驱动编译缓存。Destroy 先拆管线，
     * 再调派生类的 OnDestroy 拆这个 pass 自己的资源；派生类不要覆盖 Destroy。
     * 以后着色器资产会比本类先销毁，于是这里的管线布局会比着色器的 set 1 布局活得久：
     * 这是 Vulkan 允许的，set 布局只在创建管线布局时被读取一次，之后不再被访问；
     * 原地重建也保留管线布局、不再读 set 1 布局。
     *
     * 不可复制：PipelineItem 里的管线各自持有句柄
     */
    class DoodleRenderPass
    {
        /**
         * 一条管线记录：重建所需的全部输入 + 建出来的管线
         *
         * 描述按值拷一份（字节码副本、渲染状态、顶点 / 实例格式、set 1 布局句柄）：
         * 调用方的那份可能早就释放了，原地重建时要用的是这里的。
         * set 1 布局句柄只在建管线布局时用过一次，原地重建保留布局、不再读它，
         * 所以着色器资产先于本 pass 销毁之后它悬空也无妨
         */
        struct PipelineItem
        {
            DoodlePipelineShaderDescription shaderDescription;
            DoodleGraphicsPipeline pipeline;
        };

    protected:
        DoodleRenderPass() = default;

    public:
        virtual ~DoodleRenderPass() = default;

        DoodleRenderPass(const DoodleRenderPass&) = delete;
        DoodleRenderPass& operator=(const DoodleRenderPass&) = delete;

        /**
         * 销毁这个 pass 的全部管线，再让派生类拆自己的资源
         *
         * 可以重复调用。派生类通过 OnDestroy 挂接自己的清理，不要覆盖本函数。
         * 前置条件：GPU 已空闲、没有线程还在用这个 pass
         */
        virtual void Destroy();

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
        void RebuildPipelines(const DoodlePipelinePassConfig& passConfig);

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
         *        见 DoodleGraphicsPipeline::Initialize。失败时表里不留任何东西
         *
         * @note 前置条件：已初始化、未 Destroy
         */
        [[nodiscard]] const DoodleGraphicsPipeline& GetPipeline(const DoodlePipelineShaderDescription& shaderDescription);

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
        [[nodiscard]] DoodlePipelinePassConfig GetPassConfig() const;

    protected:
        /**
         * 绑定这个 pass 共用的输入；此时还没有任何管线
         *
         * 由派生类在自己的 Initialize 里调用。派生类先按自己的附件算好 passConfig，
         * 再交给这里；基类不关心这个 pass 的颜色 / 深度附件从哪来。
         *
         * @param device 已初始化的 Vulkan 设备
         * @param pipelineCompileCache 驱动编译缓存，必须活得比本 pass 久
         * @param frameSetLayout set 0 布局（描述符管理器那一个），必须活得比本 pass 久
         * @param passConfig 这种 pass 的附件格式与采样数
         *
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleVulkanDevice& device,
                        VkPipelineCache pipelineCompileCache,
                        VkDescriptorSetLayout frameSetLayout,
                        const DoodlePipelinePassConfig& passConfig);

        /**
         * 派生类拆自己的资源；基类已经先把管线拆完
         *
         * 此时基类的 device 仍然有效（句柄在 OnDestroy 返回之后才清空）。
         * 可能在从未初始化时被调用，也可能被重复调用；实现要能处理这两种情况。
         * 默认什么都不做。
         */
        virtual void OnDestroy()
        {

        }

        /**
         * 这个 pass 绑定的设备；OnDestroy 时仍然有效
         */
        [[nodiscard]] const DoodleVulkanDevice* GetDevice() const
        {
            return m_pDevice;
        }

    private:
        /**
         * 查找键的哈希：只用来分桶
         *
         * 与 IsSameKey 同口径：判等看的成员都要进哈希；set 1 布局两边都不看
         */
        [[nodiscard]] static std::size_t HashKey(const DoodlePipelineShaderDescription& shaderDescription);

        /**
         * 两份描述是不是同一个查找键：字节码逐字节、渲染状态逐成员、
         * 顶点与实例格式，全部相等才算
         */
        [[nodiscard]] static bool IsSameKey(const DoodlePipelineShaderDescription& lhs,
                                            const DoodlePipelineShaderDescription& rhs);

        /**
         * 建一条 PipelineItem 并插进表里
         *
         * 调用方必须已经持有 m_mutex（GetPipeline 在锁内调它，避免两个线程同时
         * 为同一份描述各建一条）。失败时把刚插入的条目摘掉再抛
         */
        [[nodiscard]] PipelineItem& CreatePipelineItem(const std::size_t keyHash,
                                                       const DoodlePipelineShaderDescription& shaderDescription);

        /**
         * 销毁表里的每条管线并清空；调用方必须已经持有 m_mutex
         */
        void DestroyPipelines();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 驱动编译缓存，不持有
         */
        VkPipelineCache m_pipelineCompileCache = VK_NULL_HANDLE;

        /**
         * set 0 布局，不持有；这个 pass 的所有管线布局都用它
         */
        VkDescriptorSetLayout m_frameSetLayout = VK_NULL_HANDLE;

        /**
         * 这种 pass 当前的附件格式与采样数
         */
        DoodlePipelinePassConfig m_passConfig;

        /**
         * 查找键哈希 → 管线记录。哈希只分桶：同一个桶里可能有内容不同的记录，
         * 取的时候逐个 IsSameKey
         */
        std::unordered_multimap<std::size_t, std::unique_ptr<PipelineItem>> m_pipelineItems;

        /**
         * 保护 m_pipelineItems 与 m_passConfig
         */
        mutable std::mutex m_mutex;
    };
}
