//
//  DoodlePassShaderInterface.h
//  Doodle
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "DoodleForwardPassData.h"
#include "DoodleShadowPassData.h"

namespace Doodle
{
    /**
     * 着色器侧的 pass 身份
     *
     * 一个着色器资产可以按 pass 各存一份管线描述（不同字节码 / 渲染状态 /
     * set 1 布局）。loader 把描述文件里的 passes.forward / passes.shadow 映射成
     * 这个枚举；各 pass 类用 shader.GetPassDescription(自己的枚举) 取自己那条。
     *
     * 它不是渲染 pass 的基类，不决定 pass 顺序，也不替代 DoodleForwardPass /
     * DoodleShadowPass —— 真正的 pass 类仍然各自存在，这里只解决「这份着色器
     * 描述属于哪个 pass」。
     */
    enum class EDoodlePassType : std::uint8_t
    {
        Forward = 0,
        Shadow = 1,
    };

    /**
     * 当前支持的 pass 数量：DoodleShader 按枚举索引存描述
     */
    inline constexpr std::size_t kDoodlePassTypeCount = 2;

    static_assert(static_cast<std::size_t>(EDoodlePassType::Shadow) + 1 == kDoodlePassTypeCount,
                  "EDoodlePassType 变了：kDoodlePassTypeCount 必须跟着更新");

    /**
     * 材质 set 1 的唯一绑定：binding 0，一张在片元阶段采样的 combined image sampler
     *
     * DoodleDescriptorManager 按它建材质布局并分配纹理集合；DoodlePassShaderInterface
     * 也把它列为允许的 set 1 绑定。两处用同一个常量，定义不会漂移。
     */
    inline constexpr VkDescriptorSetLayoutBinding kMaterialTextureBinding
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };

    /**
     * 一个 pass 对它的着色器提出的接口契约
     *
     * 纯数据，没有 Initialize / Destroy、不持有 Vulkan 句柄。三处共享同一份定义：
     * - DoodleShader::Initialize 反射完 SPIR-V 后按它校验；
     * - DoodleRenderPass::Initialize 按它建本 pass 的 set 0 UBO 布局/池/集合与管线布局；
     * - DoodleDescriptorManager 按 kMaterialTextureBinding 建 set 1 布局。
     *
     * 这样「加载期校验」「pass 建资源」「管线布局」只认一份定义；加 shadow / post
     * 时不会出现某处还写着前一个 pass 的绑定形状。
     */
    struct DoodlePassShaderInterface
    {
        /**
         * set 0 的 UBO 绑定（binding / 类型 / 数量 / 阶段）
         */
        VkDescriptorSetLayoutBinding passUboBinding;

        /**
         * 这个 pass 的 UBO 块最大字节数：着色器反射出的块不得超过它
         */
        uint32_t maxPassUboBlockSize = 0;

        /**
         * 这个 pass 的 push constant 范围：管线布局与 vkCmdPushConstants 都用它
         */
        VkPushConstantRange pushConstantRange{};

        /**
         * set 1 允许的材质绑定（当前只有一张纹理）
         */
        std::vector<VkDescriptorSetLayoutBinding> materialBindings;

        /**
         * set 1 是否必须声明：forward 必须恰好一张；shadow 允许零张（不透明）
         * 或一张（alpha 镂空）
         */
        bool materialBindingsRequired = true;
    };

    /**
     * 取某个 pass 的着色器接口契约
     *
     * 返回的引用是静态对象，有效期与进程相同；调用方只读。
     *
     * @throw std::invalid_argument pass 不是已知枚举值
     */
    [[nodiscard]] inline const DoodlePassShaderInterface& GetPassShaderInterface(const EDoodlePassType pass)
    {
        static const DoodlePassShaderInterface forward
        {
            .passUboBinding =
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            },
            .maxPassUboBlockSize = static_cast<uint32_t>(sizeof(DoodleForwardPassUbo)),
            .pushConstantRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleForwardPassPushConstants))
            },
            .materialBindings = { kMaterialTextureBinding },
            .materialBindingsRequired = true
        };

        static const DoodlePassShaderInterface shadow
        {
            .passUboBinding =
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr
            },
            .maxPassUboBlockSize = static_cast<uint32_t>(sizeof(DoodleShadowPassUbo)),
            .pushConstantRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleShadowPassPushConstants))
            },
            .materialBindings = { kMaterialTextureBinding },
            .materialBindingsRequired = false
        };

        switch (pass)
        {
        case EDoodlePassType::Forward:
            return forward;

        case EDoodlePassType::Shadow:
            return shadow;
        }

        throw std::invalid_argument("unknown pass type");
    }

    static_assert(sizeof(DoodleForwardPassPushConstants) <= 128,
                  "forward push constant 超过了规范保证的 maxPushConstantsSize 下限 128");
    static_assert(sizeof(DoodleShadowPassPushConstants) <= 128,
                  "shadow push constant 超过了规范保证的 maxPushConstantsSize 下限 128");
}
