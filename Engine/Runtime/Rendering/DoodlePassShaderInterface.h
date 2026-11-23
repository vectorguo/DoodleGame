//
//  DoodlePassShaderInterface.h
//  Doodle
//

#pragma once

#include <cstdint>
#include <stdexcept>

#include <vulkan/vulkan_core.h>

#include "../Vulkan/DoodleForwardPassData.h"
#include "../Vulkan/DoodleShadowPassData.h"

namespace Doodle
{
    /**
     * 着色器侧的 pass 身份
     *
     * 一个着色器资产可以按 pass 各存一份数据（不同字节码 / 渲染状态）。
     * loader 把描述文件里的 passes.forward / passes.shadow 映射成这个枚举；
     * 各 pass 类在构造时把自己的枚举交给 DoodleRenderPass，基类按它取契约、
     * 用 shader.GetPass(这个枚举) 取着色器的那一份。
     *
     * 它不是渲染 pass 的基类，不决定 pass 顺序，也不替代 DoodleForwardPass /
     * DoodleShadowPass —— 真正的 pass 类仍然各自存在，这里只解决「这份着色器
     * 数据属于哪个 pass」。
     */
    enum class EDoodlePassType : std::uint8_t
    {
        Forward = 0,
        Shadow = 1,
    };

    /**
     * 一个 pass 对它的着色器提出的接口契约：set 0 与 push constant
     *
     * 纯数据，没有 Initialize / Destroy、不持有 Vulkan 句柄。两处读它：
     * - DoodleShader::Initialize 反射完 SPIR-V 后，按它核对每个 pass 的 set 0 与 push constant；
     * - DoodleRenderPass::Initialize 按自己的 pass 类型取它，建本 pass 的 set 0 UBO /
     *   布局 / 池 / 集合，并记下 push 范围 —— 管线布局与 vkCmdPushConstants 都用这一份。
     *
     * 一个查着色器、一个建资源，两边认同一份定义；加 shadow / post 时不会出现
     * 某处还写着前一个 pass 的绑定形状。
     *
     * set 1（材质贴图）不在这里：它的形状由每个着色器自己声明，规则见
     * DoodleMaterialShaderInterface.h。
     */
    struct DoodlePassShaderInterface
    {
        /**
         * set 0 的 UBO 绑定（binding / 类型 / 数量 / 阶段）
         */
        VkDescriptorSetLayoutBinding passUboBinding;

        /**
         * 这个 pass 的 UBO 字节数：pass 按它建缓冲，着色器反射出的块不得超过它
         */
        uint32_t passUboSize = 0;

        /**
         * 这个 pass 的 push constant 范围：管线布局与 vkCmdPushConstants 都用它
         */
        VkPushConstantRange pushConstantRange{};
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
            .passUboSize = static_cast<uint32_t>(sizeof(DoodleForwardPassUbo)),
            .pushConstantRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleForwardPassDrawItemData))
            }
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
            .passUboSize = static_cast<uint32_t>(sizeof(DoodleShadowPassUbo)),
            .pushConstantRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleShadowPassDrawItemData))
            }
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

    static_assert(sizeof(DoodleForwardPassDrawItemData) <= 128, "forward 逐绘制项数据块超过了规范保证的 maxPushConstantsSize 下限 128");
    static_assert(sizeof(DoodleShadowPassDrawItemData) <= 128, "shadow 逐绘制项数据块超过了规范保证的 maxPushConstantsSize 下限 128");
}
