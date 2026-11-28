//
//  DoodleShader.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include "Utility/DoodleStd.h"

#include <vulkan/vulkan_core.h>

#include "Asset/DoodleAsset.h"
#include "Rendering/DoodleMaterialTextureSlot.h"
#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleVertex.h"
#include "Vulkan/DoodleRenderPassLayout.h"
#include "Vulkan/DoodleShaderPass.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 初始化着色器资产的全部输入
     *
     * 设备、两个格式与各 pass 的输入放在一起：两个格式单独看都是不自解释的
     * 枚举值，按位置传容易串位。以后加输入（比如 specialization constants）
     * 往这里加字段，Initialize 的签名不动。
     *
     * 必填项用引用：漏填直接编译不过。
     */
    struct DoodleShaderInitInfo
    {
        /**
         * 一个 pass 的输入：属于哪个 pass、它的两段 SPIR-V 与渲染状态
         *
         * 只是初始化输入的一部分，所以嵌在这里：Initialize 把它与外层的两个格式、
         * 整个着色器的 set 1 布局合成这个 pass 的 DoodleShaderPass，
         * 之后就用不到它了。顶点 / 实例格式是资产级的（同一份网格数据供所有 pass
         * 使用），所以在外层、不在这里
         */
        struct PassInitInfo
        {
            /**
             * 属于哪个 pass
             */
            EDoodleRenderPassType passType = EDoodleRenderPassType::Forward;

            /**
             * 这个 pass 的渲染状态
             */
            DoodleRenderState passRenderState;

            /**
             * 顶点着色器的 SPIR-V
             */
            std::vector<std::byte> vertexBytecode;

            /**
             * 片元着色器的 SPIR-V
             */
            std::vector<std::byte> fragmentBytecode;
        };

        /**
         * 已初始化的 Vulkan 设备
         */
        const DoodleVulkanDevice& device;

        /**
         * 顶点格式（资产级，所有 pass 共用）
         */
        EVertexFormat vertexFormat;

        /**
         * 实例格式（资产级，所有 pass 共用）
         */
        EInstanceFormat instanceFormat;

        /**
         * 每个 pass 一项；至少一项，同一个 pass 不能重复，两段字节码的长度都合法
         * （至少一个模块头、按 4 字节对齐）。这几条都由 DoodleShaderLoader 读描述
         * 文件时保证，Initialize 不再核对。
         * 只在本次调用期间读取 —— 字节码会各拷一份进 pass 数据
         */
        const std::vector<PassInitInfo>& passInitInfos;
    };

    /**
     * 着色器资产：每个 pass 一对 SPIR-V（顶点 + 片元）+ 渲染状态，以及按反射
     * 结果建出来的一份 set 1 布局
     *
     * 放在 Rendering：它和网格、纹理、材质一样是「画什么」的内容；SPIR-V 怎么变成
     * 管线是 Vulkan 层（DoodleVulkanPipelineObject / DoodleRenderPass）的事。
     * 由 DoodleShaderLoader 按 Shaders/ 下的 *.shader.json 加载，资产管理器按路径缓存；
     * 着色器资产常驻，大世界流式加载时卸载的是网格、贴图和材质，不是着色器。
     *
     * **只能由 DoodleShaderLoader 创建**：构造、Initialize 与 Destroy 都是私有的，
     * loader 是友元。和网格 / 纹理不同，着色器没有「运行时动态创建」这条路 ——
     * 下游假设着色器常驻、地址稳定：Initialize 把自己的地址投进待预热队列，各 pass
     * 的管线表按着色器地址做键。只有 loader 缓存里的那份满足这两点；在 loader
     * 之外建一份再提前销毁，队列与管线表里就会留下悬空指针，它的 set 1 布局也不归
     * loader 回收。
     *
     * **本类不持有管线**：管线由渲染 pass 里的管线表统一创建与持有。着色器按 pass
     * 各提供一份 DoodleShaderPass（GetPass），由各个 pass 在录制 / 预热时
     * 向自己的管线表要管线。管线表按着色器资产做键：每个着色器在一个 pass 里只有
     * 一条管线。
     *
     * **两份契约**：每个 pass 的每帧常量与 push constant 按 DoodleRenderPassLayout
     * 核对；set 1 按 DoodleMaterialTextureSlot.h 的规则核对。不符合就加载失败。
     *
     * **set 1 按反射建、整个着色器一份**：贴图有几张由 GLSL 自己声明，不在描述文件里
     * 手写一遍 —— 手写的那份迟早和 GLSL 对不上。各 pass 反射出的 set 1 合并成一张
     * 贴图槽表（同一 binding 必须同名同类型，阶段取并集），只建一份布局，每个 pass
     * 的管线布局都用它。这样一个材质只需要一个 set 1，在哪个 pass 里都能绑。
     *
     * 生命周期：资产管理器先于渲染器销毁，所以 set 1 布局会比管线表里用它建的
     * 管线布局、描述符管理器里按它分配的材质集合先销毁 —— Vulkan 允许：管线布局
     * 只在创建时读一次布局；按它分配的集合只是在布局销毁之后不能再更新。那时已经
     * 停止录制，集合既不会再更新、也不会再绑定，最后随描述符池一起释放。
     * 管线表里的管线还记着各 pass 数据的地址（原地重建时要读）：资产销毁之后
     * 管线只会被 Destroy、不会再重建，这些地址不会再被读到。
     */
    class DoodleShader : public DoodleAsset
    {
        friend class DoodleShaderLoader;

    private:
        /**
         * 只能由 DoodleShaderLoader 创建：路径交给 DoodleAsset 保存并校验非空
         *
         * 没有无路径的默认构造：着色器不存在「运行时动态创建」，每一份都来自描述文件
         *
         * @param assetPath 描述文件的路径，如 "Shaders/lit.shader.json"
         */
        explicit DoodleShader(std::string_view assetPath);

        /**
         * 反射每个 pass 的字节码、核对两份契约、建 set 1 布局，并记下各 pass 的描述
         *
         * 先反射与核对，全部通过才动设备：不合法的着色器什么都不建。
         * 字节码各拷一份留在 pass 数据里 —— pass 还要拿它建管线。
         *
         * @param info 设备、顶点 / 实例格式与各 pass 的输入，见 DoodleShaderInitInfo
         *
         * @throw std::runtime_error 字节码反射失败、阶段不对，或违反
         *        DoodleRenderPassLayout / DoodleMaterialTextureSlot.h 的约定
         * @note 前置条件：尚未初始化；passInitInfos 满足 DoodleShaderInitInfo 里的约定 ——
         *       pass 重复会让后一项覆盖前一项
         */
        void Initialize(const DoodleShaderInitInfo& info);

        /**
         * 销毁 set 1 布局、清空各 pass 的描述；可以重复调用。由 loader 的 DestroyAsset 调用
         *
         * @note 前置条件：没有人还要用它建管线布局、分配材质集合，也没有管线还要按它的
         *       pass 数据原地重建（管线只记地址）。已经建好的不受影响
         */
        void Destroy();

    public:
        /**
         * 取某个 pass 的数据；这个着色器没有该 pass 时返回 nullptr
         *
         * pass 录制 / 预热时用它向自己的管线表要管线（DoodleRenderPass::ResolvePipeline）。
         * 返回的指针在 Destroy 之前一直有效。
         */
        [[nodiscard]] const DoodleShaderPass* GetPass(EDoodleRenderPassType pass) const;

        /**
         * 这个着色器的贴图槽，按 binding 升序；没有声明贴图时为空
         *
         * 材质加载器按槽名核对材质文件里的 textures；描述符管理器按槽写材质的 set 1。
         * 在 Destroy 之前一直有效
         */
        [[nodiscard]] std::span<const DoodleMaterialTextureSlot> GetMaterialTextureSlots() const
        {
            return m_materialTextureSlots;
        }

        /**
         * 这个着色器的 set 1 布局，所有 pass 共用；没有声明贴图时为 VK_NULL_HANDLE
         *
         * 描述符管理器按它给用这个着色器的材质分配 set 1
         */
        [[nodiscard]] VkDescriptorSetLayout GetMaterialDescriptorSetLayout() const
        {
            return m_materialDescriptorSetLayout;
        }

    private:
        /**
         * 反射出来的一个描述符绑定；两个阶段都声明了同一个绑定时合并成一项
         */
        struct ReflectedBinding
        {
            uint32_t set = 0;
            uint32_t binding = 0;
            VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            uint32_t descriptorCount = 0;
            VkShaderStageFlags stageFlags = 0;

            /**
             * uniform / storage 块的字节数（最后一个成员的末尾）；其它类型为 0
             */
            uint32_t blockSize = 0;

            /**
             * GLSL 里的变量名；set 1 的贴图按它与材质对上
             */
            std::string name;
        };

        /**
         * 反射一段字节码，把它的描述符绑定合并进 bindings、push constant 用到的区间追加进
         * pushConstantRanges
         *
         * @param bytecode SPIR-V
         * @param expectedStage 这段字节码应当是哪个阶段；入口点必须叫 main
         * @throw std::runtime_error 反射失败、阶段或入口点不对、同一个绑定在两个阶段声明得不一样
         */
        static void ReflectStage(const std::vector<std::byte>& bytecode,
                                 VkShaderStageFlagBits expectedStage,
                                 std::vector<ReflectedBinding>& bindings,
                                 std::vector<VkPushConstantRange>& pushConstantRanges);

        /**
         * 按 pass 契约核对一个 pass 的每帧常量与 push constant，并拒绝每帧常量、材质贴图以外的集合
         *
         * set 1 在这里不看：它按整个着色器合并后另行核对（MergeMaterialBindings /
         * ValidateMaterialBindings）
         *
         * @throw std::runtime_error 违反约定（见 DoodleRenderPassLayout）
         */
        static void ValidatePassBindings(const DoodleRenderPassLayout& passLayout,
                                         const std::vector<ReflectedBinding>& bindings,
                                         const std::vector<VkPushConstantRange>& pushConstantRanges);

        /**
         * 把一个 pass 的 set 1 绑定并进整个着色器的贴图槽表
         *
         * 同一个 binding 在各 pass 里必须同名、同类型、同数量，阶段取并集；
         * 同一个名字不能占两个 binding
         *
         * @throw std::runtime_error 两个 pass 的 set 1 声明对不上
         */
        static void MergeMaterialBindings(const std::vector<ReflectedBinding>& passBindings,
                                          std::vector<ReflectedBinding>& materialBindings);

        /**
         * 按材质契约核对合并后的 set 1，并按 binding 升序排好
         *
         * @throw std::runtime_error 违反约定（见 DoodleMaterialTextureSlot.h）
         */
        static void ValidateMaterialBindings(std::vector<ReflectedBinding>& materialBindings);

        /**
         * 按 set 1 的绑定建布局
         *
         * @return 建好的布局；materialBindings 为空时返回 VK_NULL_HANDLE
         */
        [[nodiscard]] VkDescriptorSetLayout CreateMaterialSetLayout(
            const std::vector<VkDescriptorSetLayoutBinding>& materialBindings) const;

        /**
         * 一个阶段的可读名字，只用于报错
         */
        [[nodiscard]] static const char* GetStageName(VkShaderStageFlagBits stage);

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 每个 pass 一份数据，按 pass 做键；这个着色器没有的 pass 不在表里。
         * 数据里的 materialDescriptorSetLayout 只是 m_materialDescriptorSetLayout 的副本，
         * 不归 pass 数据所有
         *
         * 只在 Initialize 里插入，之后不再增删：节点地址不变，GetPass
         * 返回的指针因此在 Destroy 之前一直有效。查找只用 find / contains ——
         * operator[] 查不到会插入一份空数据，下游会以为这个 pass 存在
         */
        std::map<EDoodleRenderPassType, DoodleShaderPass> m_passes;

        /**
         * 贴图槽表：各 pass 的 set 1 合并后的结果，按 binding 升序
         */
        std::vector<DoodleMaterialTextureSlot> m_materialTextureSlots;

        /**
         * 整个着色器的 set 1 布局，由本类持有，Destroy 时销毁；没有贴图时为 VK_NULL_HANDLE
         */
        VkDescriptorSetLayout m_materialDescriptorSetLayout = VK_NULL_HANDLE;
    };
}
