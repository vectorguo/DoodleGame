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
    class DoodleGraphicsPipeline;
    class DoodleVulkanDevice;

    /**
     * 描述符管理器：每帧 UBO 描述符集与材质纹理描述符集
     *
     * 本类拥有两组池、对应的集合、默认 1×1 白图与纹理集合缓存：
     *
     * - set 0：每帧一个 UBO 集合，按 frameIndex 轮转；
     * - set 1：每张纹理一个集合，按 DoodleTexture 指针缓存；没有纹理的材质
     *   使用默认白图的集合。
     *
     * 它不拥有 UBO 缓冲、纹理资产与管线布局：UBO 句柄在 Initialize 时传入，
     * 布局从 DoodleGraphicsPipeline 现取，纹理由资产系统保证寿命。生命周期
     * 由 DoodleVulkanManager 管理：必须排在 UBO 与图形管线之前销毁。
     *
     * 每帧的纹理集合解析结果（与 drawItems 等长）保存在内部，下一次调用
     * ResolveTextureDescriptorSets 前有效。
     */
    class DoodleDescriptorManager
    {
    public:
        /**
         * 建两组描述符池、每帧 UBO 集合与默认白图
         *
         * 前置条件：
         * - UBO 缓冲已经建好（集合要写入它们的句柄）；
         * - 图形管线已经初始化（要读两个 set layout）；
         * - 命令管理器已经初始化（默认白图要借它的传输池上传）。
         *
         * @param device 已初始化的 Vulkan 设备
         * @param graphicsPipeline 已初始化的图形管线
         * @param uniformBuffers 每帧一块的 UBO，数量必须等于 MAX_FRAMES_IN_FLIGHT
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const DoodleGraphicsPipeline& graphicsPipeline,
                        const std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT>& uniformBuffers);

        /**
         * 销毁两组池、默认白图与所有集合句柄
         *
         * 前置条件：GPU 已空闲。必须在 UBO 与图形管线销毁之前调用；
         * 重复调用幂等
         */
        void Destroy();

        /**
         * 取本帧的 set 0
         *
         * @param frameIndex 在途帧索引，取值 0 .. MAX_FRAMES_IN_FLIGHT-1；
         *        由门面按与 UBO / 命令缓冲同一个索引轮转给出
         */
        [[nodiscard]] VkDescriptorSet GetFrameDescriptorSet(uint32_t frameIndex) const;

        /**
         * 为每条绘制项解析 set 1，返回与 drawItems 等长的集合数组
         *
         * 第一次用到的纹理在这里分配集合并缓存；没有配置纹理的材质用默认白图。
         * 返回值引用内部数组，下一次调用前有效。
         *
         * @param drawItems 本帧的绘制项；pMaterial 必须非空（收集侧保证）
         */
        [[nodiscard]] const std::vector<VkDescriptorSet>& ResolveTextureDescriptorSets(const std::vector<DoodleDrawItem>& drawItems);

    private:
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
         * 创建每帧 UBO 的描述符池
         */
        void CreateFrameDescriptorPool();

        /**
         * 销毁每帧 UBO 的描述符池，并清空集合句柄
         */
        void DestroyFrameDescriptorPool();

        /**
         * 分配每帧一个 set 0，并把各帧 UBO 写到 binding 0
         */
        void CreateFrameDescriptorSets(const std::array<DoodleGraphicsBuffer, MAX_FRAMES_IN_FLIGHT>& uniformBuffers);

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
         * 图形管线层，在 Initialize 时绑定；两个 set layout 都从它取
         */
        const DoodleGraphicsPipeline* m_pGraphicsPipeline = nullptr;

        /**
         * 每帧 UBO 的描述符池
         */
        VkDescriptorPool m_frameDescriptorPool = VK_NULL_HANDLE;

        /**
         * 每帧一个 set 0，各自把该帧的 UBO 接到 binding 0
         */
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_frameDescriptorSets{};

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
         * 只在两次 ResolveTextureDescriptorSets 之间有效；集合本身由
         * m_textureDescriptorSets 持有
         */
        std::vector<VkDescriptorSet> m_frameTextureDescriptorSets;
    };
}
