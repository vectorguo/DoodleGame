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
#include "Rendering/DoodleTexture.h"
#include "Vulkan/DoodleVulkanConfig.h"

namespace Doodle
{
    class DoodleVulkanDevice;
    class DoodleMaterial;

    /**
     * 描述符管理器：材质的 set 1
     *
     * 本类拥有材质集合的池（按需增长）、按材质缓存的集合与默认 1×1 白图：
     *
     * - set 1：每个材质一个集合，按 DoodleMaterial 指针缓存。布局不归本类 ——
     *   用材质着色器的 set 1 布局分配（DoodleShader::GetMaterialDescriptorSetLayout），
     *   着色器声明了几张贴图就写几张：材质配了的写材质的贴图，没配的写默认白图。
     *   着色器没有声明贴图时没有 set 1，缓存 VK_NULL_HANDLE，录制侧不绑。
     *   着色器的 set 1 布局按它所有 pass 合并，所以同一个集合在每个 pass 里都能绑。
     *
     * 每帧常量归各自的 pass：DoodleRenderPass 按每个 pass 的
     * DoodleRenderPassLayout 自己建 UBO / 布局 / 池 / 集合。本类只负责
     * 资产作用域的材质贴图。
     *
     * 它不拥有材质、贴图、着色器与管线：它们由资产系统保证寿命，管线拿着着色器的
     * set 1 布局去建自己的管线布局。生命周期由 DoodleRenderPipeline 管理：
     * Initialize 排在命令管理器之后（默认白图上传要借传输池）；销毁排在所有 pass 之后。
     *
     * 材质加载完成后只读（setter 只有加载器在用）：集合在第一次画这个材质时写好，
     * 之后不再改。资产系统当前不卸载材质；若将来支持卸载或运行时换贴图，必须在这里
     * 同步移除条目，否则指针复用会把新材质映射到旧集合。
     *
     * 每帧的集合解析结果（与 drawItems 等长）保存在内部，下一次调用
     * ResolveMaterialDescriptorSets 前有效。
     */
    class DoodleDescriptorManager
    {
    public:
        /**
         * 建第一块材质描述符池与默认白图
         *
         * 前置条件：命令管理器已经初始化（默认白图要借它的传输池上传）。
         *
         * @param device 已初始化的 Vulkan 设备
         */
        void Initialize(const DoodleVulkanDevice& device);

        /**
         * 销毁材质池（集合随之失效）与默认白图
         *
         * 前置条件：GPU 已空闲。必须在所有 pass 之后调用；重复调用幂等
         */
        void Destroy();

        /**
         * 为每条绘制项解析 set 1，返回与 drawItems 等长的集合数组
         *
         * 第一次用到的材质在这里分配集合、写好贴图并缓存。材质的着色器没有声明贴图时，
         * 对应位置是 VK_NULL_HANDLE。返回值引用内部数组，下一次调用前有效。
         *
         * @param drawItems 本帧的绘制项；pMaterial 必须非空（收集侧保证）
         * @throw std::logic_error 材质没有着色器
         * @throw std::runtime_error 集合分配失败
         */
        [[nodiscard]] const std::vector<VkDescriptorSet>& ResolveMaterialDescriptorSets(const std::vector<DoodleDrawItem>& drawItems);

    private:
        /**
         * 建一组描述符池
         *
         * 只声明一种描述符，且不单独释放集合（flags = 0）—— 集合的寿命与池一致，
         * 随池一起失效。
         *
         * descriptorCount 与 maxSets 分开传，而不是取同一个值：Vulkan 里它们是
         * 两个量 —— 材质集合有几张贴图由着色器决定，一个集合占的描述符数不固定。
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
         * 追加一块材质描述符池；池满时再由 GetOrCreateMaterialDescriptorSet 调用
         */
        void AddMaterialDescriptorPool();

        /**
         * 销毁所有材质描述符池
         */
        void DestroyMaterialDescriptorPools();

        /**
         * 建 1×1 白图，作为材质没配的贴图槽的默认绑定
         */
        void CreateDefaultTexture();

        /**
         * 销毁默认白图
         */
        void DestroyDefaultTexture();

        /**
         * 取一个材质的 set 1；没有就按它着色器的布局分配并写入
         *
         * 池块满了会追加新块再试，不设总量上限。着色器没有声明贴图时返回并缓存
         * VK_NULL_HANDLE
         */
        [[nodiscard]] VkDescriptorSet GetOrCreateMaterialDescriptorSet(const DoodleMaterial& material);

    private:
        /**
         * Vulkan 设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 材质描述符池块。按需增长：一块满了就追加下一块
         */
        std::vector<VkDescriptorPool> m_materialDescriptorPools;

        /**
         * 默认白图：材质没配的贴图槽都绑它，让着色器只有一条采样路径
         */
        DoodleTexture m_defaultTexture;

        /**
         * 材质 → 它的 set 1。按 DoodleMaterial 指针缓存，一个材质只建一个集合；
         * 着色器没有声明贴图的材质映射到 VK_NULL_HANDLE
         */
        std::unordered_map<const DoodleMaterial*, VkDescriptorSet> m_materialDescriptorSets;

        /**
         * 本帧每条绘制项对应的材质集合，与 drawItems 等长
         *
         * 只在两次 ResolveMaterialDescriptorSets 之间有效；集合本身由
         * m_materialDescriptorSets 持有
         */
        std::vector<VkDescriptorSet> m_drawItemMaterialDescriptorSets;
    };
}
