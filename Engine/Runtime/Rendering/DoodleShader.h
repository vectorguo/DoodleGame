//
//  DoodleShader.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Asset/DoodleAsset.h"
#include "Rendering/DoodlePassShaderInterface.h"
#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleVertex.h"
#include "Vulkan/DoodleVulkanPipelineConfig.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 一个 pass 的着色器源数据：loader 从描述文件读到的一段字节码 + 渲染状态
     *
     * 顶点 / 实例格式是资产级的（同一份网格数据供所有 pass 使用），不放在这里。
     * 只在加载期存在；DoodleShader::Initialize 会把字节码拷进对应 pass 的描述。
     */
    struct DoodlePassShaderSource
    {
        /**
         * 这段源数据属于哪个 pass
         */
        EDoodlePassType pass = EDoodlePassType::Forward;

        /**
         * 顶点着色器的 SPIR-V
         */
        std::vector<std::byte> vertexBytecode;

        /**
         * 片元着色器的 SPIR-V
         */
        std::vector<std::byte> fragmentBytecode;

        /**
         * 这个 pass 的渲染状态
         */
        DoodleRenderState renderState;
    };

    /**
     * 初始化着色器资产的全部输入
     *
     * 设备、两个格式与各 pass 的源数据放在一起：两个格式单独看都是不自解释的
     * 枚举值，按位置传容易串位。以后加输入（比如 specialization constants）
     * 往这里加字段，Initialize 的签名不动。
     *
     * 必填项用引用：漏填直接编译不过。
     */
    struct DoodleShaderInitInfo
    {
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
         * 每个 pass 的源数据；至少一项，同一个 pass 不能重复。
         * 只在本次调用期间读取 —— 字节码会各拷一份进 pass 描述
         */
        const std::vector<DoodlePassShaderSource>& passSources;
    };

    /**
     * 着色器资产：每个 pass 一对 SPIR-V（顶点 + 片元）+ 渲染状态，以及按反射
     * 结果建出来的 set 1 布局
     *
     * 放在 Rendering：它和网格、纹理、材质一样是「画什么」的内容；SPIR-V 怎么变成
     * 管线是 Vulkan 层（DoodleVulkanPipelineObject / DoodleRenderPass）的事。
     * 由 DoodleShaderLoader 按 Shaders/ 下的 *.shader.json 加载，资产管理器按路径缓存；
     * 着色器资产常驻，大世界流式加载时卸载的是网格、贴图和材质，不是着色器。
     *
     * **本类不持有管线**：管线由渲染 pass 里的管线表统一创建与持有。着色器按 pass
     * 各提供一份「着色器侧描述」（GetPassDescription），由各个 pass 在录制 / 预热时
     * 向自己的管线表要管线。内容相同的两个着色器资产会拿到同一条管线 ——
     * 管线表按字节码内容做键。
     *
     * **set 1 布局按反射建**：Initialize 对每个 pass 反射两段字节码，按着色器真实
     * 声明的 set 1 绑定（类型、数量、阶段）建布局，不在描述文件里手写一遍 ——
     * 手写的那份迟早和 GLSL 对不上。同时按 DoodlePassShaderInterface 核对每个 pass
     * 的接口约定，不符合就加载失败。
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
         * 反射每个 pass 的字节码、核对接口、建 set 1 布局，并记下各 pass 的描述
         *
         * 先反射与核对，全部通过才动设备：不合法的着色器什么都不建。
         * 字节码各拷一份留在描述里 —— pass 还要拿它向管线表要管线。
         *
         * @param info 设备、顶点 / 实例格式与各 pass 的源数据，见 DoodleShaderInitInfo
         *
         * @throw std::runtime_error 字节码反射失败、阶段不对，或违反某个 pass 的
         *        DoodlePassShaderInterface 约定
         * @note 前置条件：尚未初始化
         */
        void Initialize(const DoodleShaderInitInfo& info);

        /**
         * 销毁所有 pass 的 set 1 布局；可以重复调用
         *
         * @note 前置条件：没有人还要用它们建管线布局。已经建好的管线布局不受影响
         */
        void Destroy();

        /**
         * 取某个 pass 的着色器侧描述；这个着色器没有该 pass 时返回 nullptr
         *
         * pass 录制 / 预热时把它交给自己的管线表（DoodleRenderPass::GetPipeline）。
         * 返回的指针在 Destroy 之前一直有效。
         */
        [[nodiscard]] const DoodleVulkanPipelineShaderDescription* GetPassDescription(EDoodlePassType pass) const;

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
         * 核对这个 pass 的接口约定，挑出 set 1 的绑定
         *
         * @return set 1 的布局绑定，按 binding 升序；没有 set 1 时为空
         * @throw std::runtime_error 违反约定（见 DoodlePassShaderInterface）
         */
        [[nodiscard]] static std::vector<VkDescriptorSetLayoutBinding> ValidateInterface(
            const DoodlePassShaderInterface& passInterface,
            const std::vector<ReflectedBinding>& bindings,
            const std::vector<VkPushConstantRange>& pushConstantRanges);

        /**
         * 按 set 1 的绑定建布局
         *
         * @return 建好的布局；materialBindings 为空时返回 VK_NULL_HANDLE
         */
        [[nodiscard]] VkDescriptorSetLayout CreateMaterialSetLayout(
            const std::vector<VkDescriptorSetLayoutBinding>& materialBindings) const;

        /**
         * 一个 pass 的可读名字，只用于报错
         */
        [[nodiscard]] static const char* GetPassName(EDoodlePassType pass);

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
         * 每个 pass 一份着色器侧描述；其中的 materialDescriptorSetLayout 由本类持有，
         * Destroy 时逐个销毁。缺失的 pass 为 std::nullopt
         */
        std::array<std::optional<DoodleVulkanPipelineShaderDescription>, kDoodlePassTypeCount> m_passDescriptions;
    };
}
