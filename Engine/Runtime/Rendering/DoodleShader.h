//
//  DoodleShader.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Asset/DoodleAsset.h"
#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleVertex.h"
#include "Vulkan/DoodleVulkanPipelineConfig.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 着色器资产：一对 SPIR-V（顶点 + 片元）+ 渲染状态 + 顶点 / 实例格式，
     * 以及按反射结果建出来的 set 1 布局
     *
     * 放在 Rendering：它和网格、纹理、材质一样是「画什么」的内容；SPIR-V 怎么变成
     * 管线是 Vulkan 层（DoodleVulkanPipelineObject / DoodleRenderPass）的事。
     * 由 DoodleShaderLoader 按 Shaders/ 下的 *.shader.json 加载，资产管理器按路径缓存；
     * 着色器资产常驻，大世界流式加载时卸载的是网格、贴图和材质，不是着色器。
     *
     * **本类不持有管线**：管线由渲染 pass 里的管线表统一创建与持有。着色器只提供
     * 「着色器侧描述」（GetForwardPassDescription），由各个 pass 在录制 / 预热时
     * 向自己的管线表要管线。内容相同的两个着色器资产会拿到同一条管线 ——
     * 管线表按字节码内容做键。
     *
     * **set 1 布局按反射建**：Initialize 反射两段字节码，按着色器真实声明的
     * set 1 绑定（类型、数量、阶段）建布局，不在描述文件里手写一遍 —— 手写的那份
     * 迟早和 GLSL 对不上。同时核对两条全工程约定，不符合就加载失败：
     * - set 0 只能是 DoodleDescriptorManager::kFrameUboBinding 那一个 UBO（所有管线共用
     *   set 0，换管线才不必重绑）；块大小不超过 DoodleUniformBufferObject；
     * - push constant 只能落在 DoodleVulkanPipelineObject::kPushConstantRange 之内。
     * 材质 set 1 独立之前还有一条临时约束：set 1 必须与
     * DoodleDescriptorManager::kTextureBinding 定义相同（一张片元阶段采样的纹理），
     * 因为纹理集合仍由描述符管理器按纹理分配。
     *
     * 现在只有前向 pass。阴影 / 深度预渲染落地时，描述文件每个 pass 一节、本类按 pass
     * 各给一份着色器侧描述，每个 pass 按自己的那份描述取管线 —— 材质不感知 pass。
     *
     * 生命周期：资产管理器先于渲染器销毁，所以 set 1 布局会比管线表里用它建的
     * 管线布局先销毁 —— Vulkan 允许：set 布局只在创建管线布局时被读取一次。
     */
    class DoodleShader : public DoodleAsset
    {
    public:
        /**
         * 运行时动态创建：没有资产路径，不能被序列化
         */
        DoodleShader() = default;

        /**
         * 文件资产：路径交给 DoodleAsset 保存并校验非空
         *
         * @param assetPath 描述文件的路径，如 "Shaders/lit.shader.json"
         */
        explicit DoodleShader(std::string_view assetPath);

        /**
         * 反射两段字节码、核对约定、建 set 1 布局，并记下前向 pass 的着色器侧描述
         *
         * 先反射与核对，全部通过才动设备：不合法的着色器什么都不建。
         * 字节码各拷一份留在描述里 —— pass 还要拿它向管线表要管线
         *
         * @param device 已初始化的 Vulkan 设备
         * @param vertexBytecode 顶点着色器的 SPIR-V
         * @param fragmentBytecode 片元着色器的 SPIR-V
         * @param renderState 前向 pass 的渲染状态
         * @param vertexFormat 顶点格式
         * @param instanceFormat 实例格式
         *
         * @throw std::runtime_error 字节码反射失败、阶段不对，或违反上面的约定
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleVulkanDevice& device,
                        const std::vector<std::byte>& vertexBytecode,
                        const std::vector<std::byte>& fragmentBytecode,
                        const DoodleRenderState& renderState,
                        EVertexFormat vertexFormat,
                        EInstanceFormat instanceFormat);

        /**
         * 销毁 set 1 布局；可以重复调用
         *
         * @note 前置条件：没有人还要用它建管线布局。已经建好的管线布局不受影响
         */
        void Destroy();

        /**
         * 前向 pass 的着色器侧描述：字节码、渲染状态、顶点 / 实例格式与本着色器的
         * set 1 布局
         *
         * 前向 pass 录制 / 预热时把它交给自己的管线表（DoodleRenderPass::GetPipeline）
         */
        [[nodiscard]] const DoodleVulkanPipelineShaderDescription& GetForwardPassDescription() const
        {
            return m_forwardPassDescription;
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
         * 核对 set 0 与 push constant 符合全工程约定，挑出 set 1 的绑定
         *
         * @return set 1 的布局绑定，按 binding 升序
         * @throw std::runtime_error 违反约定（见类注释）
         */
        [[nodiscard]] static std::vector<VkDescriptorSetLayoutBinding> ValidateInterface(
            const std::vector<ReflectedBinding>& bindings,
            const std::vector<VkPushConstantRange>& pushConstantRanges);

        /**
         * 按 set 1 的绑定建布局，句柄写进 m_forwardPassDescription.materialDescriptorSetLayout
         */
        void CreateMaterialSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& materialBindings);

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
         * 前向 pass 的着色器侧描述。其中的 materialDescriptorSetLayout 由本类持有，Destroy 时销毁
         */
        DoodleVulkanPipelineShaderDescription m_forwardPassDescription;
    };
}
