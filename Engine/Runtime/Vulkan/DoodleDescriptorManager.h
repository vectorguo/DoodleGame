//
//  DoodleDescriptorManager.h
//  Doodle
//
//  Created by 郭智 on 2026/11/4.
//

#pragma once

#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Rendering/DoodleDrawItem.h"
#include "Rendering/DoodlePassShaderInterface.h"
#include "Rendering/DoodleTexture.h"
#include "Vulkan/DoodleVulkanConfig.h"

namespace Doodle
{
    class DoodleVulkanDevice;

    /**
     * 描述符管理器：材质纹理描述符集
     *
     * 本类拥有 set 1 的布局、按需增长的池、对应的集合、默认 1×1 白图与纹理集合缓存：
     *
     * - set 1：每张纹理一个集合，按 DoodleTexture 指针缓存；没有纹理的材质
     *   使用默认白图的集合。本类按 kMaterialTextureBinding（一张 combined image
     *   sampler）分配这些集合；每个着色器资产另有一份按反射结果建的 set 1 布局，
     *   两者定义相同，所以这些集合能绑到任何着色器的管线上。
     *   第 3 步起 set 1 改为每个材质一个、按材质自己着色器的布局分配。
     *
     * set 0 的每帧常量归各自的 pass：DoodleRenderPass 按每个 pass 的
     * DoodlePassShaderInterface 自己建 UBO / 布局 / 池 / 集合。本类只负责
     * 资产作用域的材质纹理，不再持有一份全工程共用的帧常量。
     *
     * 它不拥有纹理资产与管线：纹理由资产系统保证寿命，管线拿着自己的 set 1
     * 布局句柄去建自己的管线布局。生命周期由 DoodleRenderPipeline 管理：
     * Initialize 排在命令管理器之后（默认白图上传要借传输池）、所有管线之前
     * （管线要读布局）；销毁排在所有管线之后。
     *
     * 每帧的纹理集合解析结果（与 drawItems 等长）保存在内部，下一次调用
     * ResolveMaterialDescriptorSets 前有效。
     */
    class DoodleDescriptorManager
    {
    public:
        /**
         * 建 set 1 布局、纹理描述符池与默认白图
         *
         * 前置条件：命令管理器已经初始化（默认白图要借它的传输池上传）。
         *
         * @param device 已初始化的 Vulkan 设备
         */
        void Initialize(const DoodleVulkanDevice& device);

        /**
         * 销毁材质池、默认白图与所有集合句柄，最后销毁 set 1 布局
         *
         * 前置条件：GPU 已空闲。必须在所有管线之后调用；重复调用幂等
         */
        void Destroy();

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
         * 只声明一种描述符，且不单独释放集合（flags = 0）—— 集合的寿命与池一致，
         * 随池一起失效。
         *
         * descriptorCount 与 maxSets 分开传，而不是取同一个值：Vulkan 里它们是
         * 两个量，当前恰好都满足「每个集合占一个描述符」，将来材质 set 增加
         * 取样器后前者就不再等于后者。
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
         * set 1 的描述符集布局：材质纹理（一张 combined image sampler）
         */
        VkDescriptorSetLayout m_materialDescriptorSetLayout = VK_NULL_HANDLE;

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
