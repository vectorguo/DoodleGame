//
// Created by 郭智 on 2026/10/6.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleVulkanPipelineConfig.h"
#include "Rendering/DoodleRenderState.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;
    struct DoodleShaderPass;

    /**
     * 建一条图形管线的全部输入
     *
     * 设备与 pass 侧的共享输入由外层提供，passConfig / shaderPass 是本次
     * 要建什么样的输入。Vulkan 创建管线本来就是「一大包输入」的形态；以后加
     * specialization constants、dynamic state 时往这里加字段，Initialize 签名不动。
     *
     * 必填项用引用：漏填直接编译不过。pipelineCompileCache 允许为空，用值 + 默认值。
     * passUboDescriptorSetLayout 为空时 Initialize 会抛 invalid_argument（没有 set 0
     * 的管线在录制时没有意义）。
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
         * 本 pass 的 set 0 布局：这个 pass 的所有管线共用它，换管线时不必重绑 set 0
         */
        VkDescriptorSetLayout passUboDescriptorSetLayout;

        /**
         * 本 pass 的 push constant 范围；与 set 0 布局一起决定同一 pass 内管线布局的兼容性
         */
        VkPushConstantRange pushConstantRange{};

        /**
         * 附件格式与采样数
         */
        const DoodleVulkanPipelinePassConfig& passConfig;

        /**
         * 着色器侧输入；只在本次调用期间读取，不保存
         */
        const DoodleShaderPass& shaderPass;
    };

    /**
     * 一条图形管线：按参数建出来的管线布局 + 管线
     *
     * 只拥有这两样东西。一条管线由三部分输入决定：
     * - 着色器侧输入（DoodleShaderPass）：两段字节码、set 1 布局、渲染状态、
     *   顶点与实例格式；
     * - pass 侧配置（DoodleVulkanPipelinePassConfig）：附件格式与采样数；
     * - pass 侧共享：本 pass 的 set 0 布局、本 pass 的 push constant 范围、
     *   驱动编译缓存（VkPipelineCache）。
     *
     * 前两样是管线的创建输入。着色器侧那份定义在 DoodleShaderPass.h，
     * pass 侧那份定义在 DoodleVulkanPipelineConfig.h。
     *
     * 本类不读文件、不认识门面、也不是单例：字节由要管线的一方读好传进来，
     * 设备与布局都是参数。同一份输入建几次都一样，谁来建、建几条由
     * 持有它的 pass（DoodleRenderPass）决定 —— 本类不再是「全工程那一条管线」
     *
     * 同一个 pass 内所有管线共用 set 0 布局与 push constant 范围，是为了布局兼容：
     * Vulkan 规定两个管线布局只有在 set 0..N 的布局都相同、且 push constant 范围
     * 也相同时，才对 set N 兼容。共用这两样，换管线时不必重新绑定 set 0。
     * 跨 pass 不承诺兼容 —— 不同 pass 各自重绑自己的 set 0。
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
         * @param info 设备、两个共享句柄与着色器侧输入，见 DoodleVulkanPipelineInitInfo
         *
         * @throw std::invalid_argument 输入不合法（字节码为空、
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
         * 由持有它的 pass 在附件格式变化时调用
         *
         * @param pipelineCompileCache 驱动编译缓存，可为 VK_NULL_HANDLE
         * @param passConfig 新的附件格式与采样数
         * @param shaderPass 必须与 Initialize 时那份内容相同（条目里存的副本）
         *
         * @throw std::logic_error 尚未初始化
         * @throw std::runtime_error 模块 / 管线创建失败
         *
         * @note 前置条件：GPU 已空闲 —— 旧管线可能还被在途的命令引用着
         */
        void Rebuild(VkPipelineCache pipelineCompileCache,
                     const DoodleVulkanPipelinePassConfig& passConfig,
                     const DoodleShaderPass& shaderPass);

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
         * 检查着色器侧输入里 Vulkan 不会替我们检查、或者检查了也只报在别处的那几条
         *
         * @throw std::invalid_argument 不合法
         */
        static void ValidateShaderPass(const DoodleShaderPass& shaderPass);

        /**
         * 建管线布局：set 0（+ set 1）与本 pass 的 push constant 范围
         *
         * materialDescriptorSetLayout 为空时只有 set 0；pushConstantRange.size == 0
         * 时不声明 push constant
         */
        void CreatePipelineLayout(VkDescriptorSetLayout passUboDescriptorSetLayout,
                                  VkDescriptorSetLayout materialDescriptorSetLayout,
                                  VkPushConstantRange pushConstantRange);

        /**
         * 销毁管线布局，并把句柄置空
         */
        void DestroyPipelineLayout();

        /**
         * 按 pass 配置与着色器侧输入建一条管线，返回句柄
         *
         * 返回句柄而不是直接写成员：调用方决定什么时候装上它。
         * 前置条件：m_pipelineLayout 已创建
         */
        [[nodiscard]] VkPipeline CreateGraphicsPipeline(VkPipelineCache pipelineCompileCache,
                                                        const DoodleVulkanPipelinePassConfig& passConfig,
                                                        const DoodleShaderPass& shaderPass) const;

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

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 管线布局：set 0（+ set 1）与本 pass 的 push constant 范围
         */
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

        /**
         * 图形管线
         */
        VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    };
}
