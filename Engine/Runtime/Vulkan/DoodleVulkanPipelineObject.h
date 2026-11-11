//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleVulkanPipelineConfig.h"
#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleShaderData.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 建一条图形管线的全部输入
     *
     * 设备与两个共享句柄由外层提供，passConfig / shaderDescription 是本次要建
     * 什么样的描述。Vulkan 创建管线本来就是「一大包输入」的形态；以后加
     * specialization constants、dynamic state 时往这里加字段，Initialize 签名不动。
     *
     * 必填项用引用：漏填直接编译不过。pipelineCompileCache 允许为空，用值 + 默认值。
     * frameUboDescriptorSetLayout 为空时 Initialize 会抛 invalid_argument（没有 set 0 的管线
     * 在录制时没有意义）。
     */
    struct DoodleVulkanPipelineInitInfo
    {
        /**
         * 已初始化的 Vulkan 设备
         */
        const DoodleVulkanDevice& device;

        /**
         * 驱动编译缓存，可为 VK_NULL_HANDLE（不用缓存）
         */
        VkPipelineCache pipelineCompileCache = VK_NULL_HANDLE;

        /**
         * frame UBO 的描述符集布局：整帧共享的 UBO。必须是全工程那一个句柄，
         * 否则与其它管线的布局不兼容
         */
        VkDescriptorSetLayout frameUboDescriptorSetLayout;

        /**
         * 附件格式与采样数
         */
        const DoodleVulkanPipelinePassConfig& passConfig;

        /**
         * 着色器侧描述；只在本次调用期间读取，不保存
         */
        const DoodleVulkanPipelineShaderDescription& shaderDescription;
    };

    /**
     * 一条图形管线：按参数建出来的管线布局 + 管线
     *
     * 只拥有这两样东西。一条管线由三部分输入决定：
     * - 着色器侧描述（DoodleVulkanPipelineShaderDescription）：字节码、set 1 布局、
     *   渲染状态、顶点与实例格式；
     * - pass 侧配置（DoodleVulkanPipelinePassConfig）：附件格式与采样数；
     * - 全局共享：set 0 布局（描述符管理器持有，全工程一个）、push constant
     *   范围（kPushConstantRange）、驱动编译缓存（VkPipelineCache）。
     *
     * 前两样是管线的创建输入，定义在 DoodleVulkanPipelineConfig.h（原因见该文件开头）。
     *
     * 本类不读文件、不认识门面、也不是单例：字节由要管线的一方读好传进来，
     * 设备与布局都是参数。同一份输入建几次都一样，谁来建、建几条由
     * 持有它的 pass（DoodleRenderPass）决定 —— 本类不再是「全工程那一条管线」
     *
     * 所有管线共用 set 0 布局与 push constant 范围，是为了布局兼容：Vulkan 规定
     * 两个管线布局只有在 set 0..N 的布局都相同、且 push constant 范围也相同时，
     * 才对 set N 兼容。共用这两样，录制时换管线不必重新绑定 set 0
     *
     * 不可复制：复制一份就是两个对象各自去销毁同一组句柄
     */
    class DoodleVulkanPipelineObject
    {
    public:
        DoodleVulkanPipelineObject() = default;

        DoodleVulkanPipelineObject(const DoodleVulkanPipelineObject&) = delete;
        DoodleVulkanPipelineObject& operator=(const DoodleVulkanPipelineObject&) = delete;

        /**
         * 建管线布局与管线
         *
         * @param info 设备、两个共享句柄与这次要建的描述，见 DoodleVulkanPipelineInitInfo
         *
         * @throw std::invalid_argument 描述本身不合法（字节码为空、
         *        深度偏移因子不是有限值、set 0 布局为空……）
         * @throw std::runtime_error 布局 / 模块 / 管线创建失败
         *
         * @note 前置条件：尚未初始化。失败时已经建出来的部分会被收掉，
         *       对象回到未初始化状态
         */
        void Initialize(const DoodleVulkanPipelineInitInfo& info);

        /**
         * 换一套 pass 配置，重建管线本身；管线布局与对象地址不变
         *
         * 附件格式只影响管线：布局里是 set 布局与 push constant 范围，与格式无关。
         * 所以布局原样保留（也就不再读 set 1 布局），持有本对象引用的一方不受影响。
         * 先建新的、成功了再拆旧的：建失败时旧管线原样可用，异常向上抛
         *
         * 由管线缓存在附件格式变化时调用
         *
         * @param pipelineCompileCache 驱动编译缓存，可为 VK_NULL_HANDLE
         * @param passConfig 新的附件格式与采样数
         * @param shaderDescription 必须与 Initialize 时那份内容相同（管线缓存条目里存的副本）
         *
         * @throw std::logic_error 尚未初始化
         * @throw std::runtime_error 模块 / 管线创建失败
         *
         * @note 前置条件：GPU 已空闲 —— 旧管线可能还被在途的命令引用着
         */
        void Rebuild(VkPipelineCache pipelineCompileCache,
                     const DoodleVulkanPipelinePassConfig& passConfig,
                     const DoodleVulkanPipelineShaderDescription& shaderDescription);

        /**
         * 销毁管线与管线布局
         *
         * 可以重复调用：句柄为空的部分直接跳过。前置条件：GPU 不再引用这条管线
         */
        void Destroy();

        /**
         * 管线布局：录制时绑定描述符集、推 push constant 都要用它来解释
         */
        [[nodiscard]] VkPipelineLayout GetPipelineLayout() const
        {
            return m_pipelineLayout;
        }

        /**
         * 图形管线
         */
        [[nodiscard]] VkPipeline GetGraphicsPipeline() const
        {
            return m_graphicsPipeline;
        }

    private:
        /**
         * 检查着色器侧描述里 Vulkan 不会替我们检查、或者检查了也只报在别处的那几条
         *
         * @throw std::invalid_argument 不合法
         */
        static void ValidateShaderDescription(const DoodleVulkanPipelineShaderDescription& shaderDescription);

        /**
         * 建管线布局：set 0（+ set 1）与 kPushConstantRange
         *
         * materialDescriptorSetLayout 为空时只有 set 0
         */
        void CreatePipelineLayout(VkDescriptorSetLayout frameUboDescriptorSetLayout, VkDescriptorSetLayout materialDescriptorSetLayout);

        /**
         * 销毁管线布局，并把句柄置空
         */
        void DestroyPipelineLayout();

        /**
         * 按 pass 配置与着色器侧描述建一条管线，返回句柄
         *
         * 返回句柄而不是直接写成员：调用方决定什么时候装上它。
         * 前置条件：m_pipelineLayout 已创建
         */
        [[nodiscard]] VkPipeline CreateGraphicsPipeline(VkPipelineCache pipelineCompileCache,
                                                        const DoodleVulkanPipelinePassConfig& passConfig,
                                                        const DoodleVulkanPipelineShaderDescription& shaderDescription) const;

        /**
         * 销毁图形管线，并把句柄置空
         */
        void DestroyGraphicsPipeline();

        /**
         * ECullMode → VkCullModeFlags
         */
        [[nodiscard]] static VkCullModeFlags ToVkCullMode(ECullMode cullMode);

        /**
         * ECompareOp → VkCompareOp
         */
        [[nodiscard]] static VkCompareOp ToVkCompareOp(ECompareOp compareOp);

        /**
         * EPrimitiveTopology → VkPrimitiveTopology
         */
        [[nodiscard]] static VkPrimitiveTopology ToVkPrimitiveTopology(EPrimitiveTopology topology);

        /**
         * 一个颜色附件的混合设置：EBlendMode 展开成 Vulkan 的混合因子
         */
        [[nodiscard]] static VkPipelineColorBlendAttachmentState MakeColorBlendAttachment(EBlendMode blendMode);

    public:
        /**
         * 全工程唯一的 push constant 范围：所有管线布局都用它，录制时
         * vkCmdPushConstants 也用它
         *
         * 只定义这一次。管线布局与 push 两边各写一份的话，改阶段时漏改一处，
         * push 的 stageFlags 与布局里的范围就对不上（VUID-vkCmdPushConstants-offset-01795 /
         * -01796）；范围不同的两个管线布局也不再兼容，换管线时 set 0 会失效。
         *
         * 内容这一步不变：localToWorld 在顶点阶段读、baseColor 在片元阶段读，
         * 所以阶段同时覆盖两者；大小直接取 DoodlePushConstantObject，布局改了自动跟上
         * （数据块与偏移断言在 Rendering/DoodleShaderData.h）。80 字节，低于规范保证的
         * maxPushConstantsSize 下限 128
         */
        static constexpr VkPushConstantRange kPushConstantRange
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(DoodlePushConstantObject)
        };

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 管线布局：set 0（+ set 1）与 kPushConstantRange
         */
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

        /**
         * 图形管线
         */
        VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    };
}
