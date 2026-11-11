//
//  DoodleDescriptorManager.h
//  Doodle
//
//  Created by 郭智 on 2026/11/4.
//

#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Rendering/DoodleDrawItem.h"
#include "Rendering/DoodleTexture.h"
#include "Vulkan/DoodleGraphicsBuffer.h"
#include "Vulkan/DoodleVulkanConfig.h"

namespace Doodle
{
    class DoodleVulkanDevice;

    /**
     * 描述符管理器：两个描述符集布局、frame UBO 描述符集与材质纹理描述符集
     *
     * 本类拥有两个 set 布局、两组池、对应的集合、默认 1×1 白图与纹理集合缓存：
     *
     * - set 0（frame UBO 描述符集）：每帧一个集合，按 frameIndex 轮转。它的布局是全工程唯一的
     *   那一个句柄 —— 所有管线布局都用它（见 DoodleVulkanPipelineObject 的类注释：
     *   set 0 布局与 push constant 范围都相同，管线之间才对 set 0 兼容，
     *   换管线时不必重新绑定 set 0）。集合本来就由本类分配，布局跟着集合走；
     * - set 1：每张纹理一个集合，按 DoodleTexture 指针缓存；没有纹理的材质
     *   使用默认白图的集合。本类按自己的 set 1 布局（一张 combined image sampler）
     *   分配这些集合；每个着色器资产另有一份按反射结果建的 set 1 布局，两者定义
     *   相同（见 kTextureBinding），所以这些集合能绑到任何着色器的管线上。
     *   第 3 步起 set 1 改为每个材质一个、按材质自己着色器的布局分配。
     *
     * 它不拥有 UBO 缓冲、纹理资产与管线：UBO 句柄在 Initialize 时传入，
     * 纹理由资产系统保证寿命，管线拿着两个布局句柄去建自己的管线布局。
     * 生命周期由 DoodleRenderPipeline 管理：Initialize 排在 UBO 与命令管理器之后、
     * 所有管线之前（管线要读布局）；销毁排在所有管线之后、UBO 之前。
     *
     * 每帧的纹理集合解析结果（与 drawItems 等长）保存在内部，下一次调用
     * ResolveMaterialDescriptorSets 前有效。
     */
    class DoodleDescriptorManager
    {
    public:
        /**
         * set 0 的唯一绑定：binding 0，整帧共享的 UBO（view / proj 与主光源）
         *
         * stageFlags 覆盖顶点与片元两个阶段：顶点阶段读 view / proj，片元阶段读光源字段。
         * 本类按它建 set 0 布局；DoodleShader 加载时也拿它核对着色器反射出来的 set 0 ——
         * 布局定义与着色器声明只在这里写一次
         */
        static constexpr VkDescriptorSetLayoutBinding kFrameUboBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        };

        /**
         * 本类 set 1 布局的唯一绑定：binding 0，一张在片元阶段采样的 combined image sampler
         *
         * 第 3 步之前，set 1 的集合仍由本类按纹理分配（见 ResolveMaterialDescriptorSets），
         * 而每个着色器资产按反射结果建自己的 set 1 布局。两边的布局句柄不同，
         * 但 Vulkan 认为定义相同的布局彼此兼容 —— 本类分配的集合因此能绑到任何一个
         * 着色器的管线上。前提是定义真的相同，所以 DoodleShader 加载时拿它核对；
         * 第 3 步材质按自己着色器的布局分配 set 1 之后，这条约束去掉
         */
        static constexpr VkDescriptorSetLayoutBinding kTextureBinding
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        };

        /**
         * 建两个描述符集布局、两组描述符池、frame UBO 集合与默认白图
         *
         * 前置条件：
         * - UBO 缓冲已经建好（集合要写入它们的句柄）；
         * - 命令管理器已经初始化（默认白图要借它的传输池上传）。
         *
         * @param device 已初始化的 Vulkan 设备
         * @param frameUbos 每帧一块的 UBO，数量必须等于 MAX_FRAMES_IN_FLIGHT
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT>& frameUbos);

        /**
         * 销毁两组池、默认白图与所有集合句柄，最后销毁两个布局
         *
         * 前置条件：GPU 已空闲。必须在所有管线之后、UBO 之前调用；
         * 重复调用幂等
         */
        void Destroy();

        /**
         * frame UBO 的描述符集布局：整帧共享的 UBO
         *
         * 全工程只有这一个句柄，每条管线的布局都用它。所有权在本类：
         * 调用方只读，不能销毁
         */
        [[nodiscard]] VkDescriptorSetLayout GetFrameUboDescriptorSetLayout() const
        {
            return m_frameUboDescriptorSetLayout;
        }

        /**
         * set 1 的描述符集布局：一张 combined image sampler（材质纹理）
         *
         * 本类按它分配纹理集合。管线不用它建布局 —— 管线用的是各自着色器资产
         * 按反射建的那一份，定义与它相同
         */
        [[nodiscard]] VkDescriptorSetLayout GetMaterialDescriptorSetLayout() const
        {
            return m_materialDescriptorSetLayout;
        }

        /**
         * 取本帧的 set 0（frame UBO 描述符集）
         *
         * @param frameIndex 在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1；
         *        由渲染管线按与 UBO / 命令缓冲同一个索引轮转给出
         */
        [[nodiscard]] VkDescriptorSet GetFrameUboDescriptorSet(uint32_t frameIndex) const;

        /**
         * 为每条绘制项解析 set 1，返回与 drawItems 等长的集合数组
         *
         * 第一次用到的纹理在这里分配集合并缓存；没有配置纹理的材质用默认白图。
         * 返回值引用内部数组，下一次调用前有效。
         *
         * @param drawItems 本帧的绘制项；pMaterial 必须非空（收集侧保证）
         */
        [[nodiscard]] const std::vector<VkDescriptorSet>& ResolveMaterialDescriptorSets(const std::vector<DoodleDrawItem>& drawItems);

    private:
        /**
         * 建 set 0 的描述符集布局
         *
         * 它声明 binding 0 是一个顶点与片元阶段都可见的 uniform buffer。
         * 必须在分配每帧集合之前完成 —— 集合按它分配
         */
        void CreateFrameUboDescriptorSetLayout();

        /**
         * 建 set 1 的描述符集布局
         *
         * binding 0 是一个片元阶段可见的 combined image sampler
         */
        void CreateMaterialDescriptorSetLayout();

        /**
         * 销毁一个描述符集布局，并把句柄置空；句柄为空时什么都不做
         *
         * 调用前必须确认从它分配集合的池已经销毁
         */
        void DestroyDescriptorSetLayout(VkDescriptorSetLayout& descriptorSetLayout) const;

        /**
         * 建一组描述符池
         *
         * 两组池共用的部分：只声明一种描述符，且不单独释放集合（flags = 0）——
         * 集合的寿命与池一致，随池一起失效。
         *
         * descriptorCount 与 maxSets 分开传，而不是取同一个值：Vulkan 里它们是
         * 两个量，当前两组池恰好都满足「每个集合占一个描述符」，将来材质 set
         * 增加取样器后前者就不再等于后者。
         *
         * @param type 池内唯一一种描述符类型
         * @param descriptorCount 该类型描述符的总数
         * @param maxSets 池能分配的集合数上限
         * @return 建好的池句柄
         * @throw std::runtime_error 创建失败
         */
        [[nodiscard]] VkDescriptorPool CreateDescriptorPool(VkDescriptorType type,
                                                            const uint32_t descriptorCount,
                                                            const uint32_t maxSets) const;

        /**
         * 销毁一组描述符池，并把句柄清成 VK_NULL_HANDLE
         *
         * 池里的集合随之失效，不需要逐个释放；句柄为空时什么都不做。
         */
        void DestroyDescriptorPool(VkDescriptorPool& descriptorPool) const;

        /**
         * 创建 frame UBO 的描述符池
         */
        void CreateFrameUboDescriptorPool();

        /**
         * 销毁 frame UBO 的描述符池，并清空集合句柄
         */
        void DestroyFrameUboDescriptorPool();

        /**
         * 分配每帧一个 set 0，并把各帧 UBO 写到 binding 0
         */
        void CreateFrameUboDescriptorSets(const std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT>& frameUbos);

        /**
         * 追加一块纹理描述符池；池满时再由 GetOrCreateTextureDescriptorSet 调用
         */
        void AddTextureDescriptorPool();

        /**
         * 销毁所有纹理描述符池
         */
        void DestroyTextureDescriptorPools();

        /**
         * 建 1×1 白图，作为没有配置纹理的材质的默认绑定
         */
        void CreateDefaultTexture();

        /**
         * 销毁默认白图
         */
        void DestroyDefaultTexture();

        /**
         * 取一张纹理的描述符集；没有就分配并写入
         *
         * 池块满了会追加新块再试，不设总量上限
         */
        [[nodiscard]] VkDescriptorSet GetOrCreateTextureDescriptorSet(const DoodleTexture& texture);

    private:
        /**
         * Vulkan 设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * frame UBO 的描述符集布局：整帧共享的 view / proj 与主光源
         *
         * 全工程只有这一个句柄：每帧集合按它分配，每条管线的布局也都引用它。
         * 管线布局只在创建时读一次布局，但这里仍让它活得比所有管线久 ——
         * 销毁顺序由 DoodleRenderPipeline 保证
         */
        VkDescriptorSetLayout m_frameUboDescriptorSetLayout = VK_NULL_HANDLE;

        /**
         * set 1 的描述符集布局：材质纹理（一张 combined image sampler）
         *
         * 每张纹理一个集合，按材质槽引用；与 set 0 分开，避免把每帧数据
         * 复制进每张纹理的集合
         */
        VkDescriptorSetLayout m_materialDescriptorSetLayout = VK_NULL_HANDLE;

        /**
         * frame UBO 描述符集的池
         */
        VkDescriptorPool m_frameUboDescriptorPool = VK_NULL_HANDLE;

        /**
         * 每帧一个 set 0（frame UBO 描述符集），各自把该帧的 UBO 接到 binding 0
         */
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_frameUboDescriptorSets{};

        /**
         * 纹理描述符池块。按需增长：一块满了就追加下一块
         */
        std::vector<VkDescriptorPool> m_textureDescriptorPools;

        /**
         * 默认白图：材质没有纹理时绑定它，让着色器只有一条采样路径
         */
        DoodleTexture m_defaultTexture;

        /**
         * 默认白图的描述符集
         */
        VkDescriptorSet m_defaultTextureDescriptorSet = VK_NULL_HANDLE;

        /**
         * 纹理 → 描述符集。按 DoodleTexture 指针缓存，同一张纹理只建一个集合
         *
         * 资产系统当前不卸载纹理；若将来支持卸载，必须在这里同步移除条目，
         * 否则指针复用会把新纹理映射到旧集合
         */
        std::unordered_map<const DoodleTexture*, VkDescriptorSet> m_textureDescriptorSets;

        /**
         * 本帧每条绘制项对应的纹理集合，与 drawItems 等长
         *
         * 只在两次 ResolveMaterialDescriptorSets 之间有效；集合本身由
         * m_textureDescriptorSets 持有
         */
        std::vector<VkDescriptorSet> m_drawItemMaterialDescriptorSets;
    };
}
