//
//  DoodleRenderPassLayout.h
//  Doodle
//

#pragma once

#include "Utility/DoodleCommon.h"

#include "DoodleForwardPassData.h"
#include "DoodleShadowPassData.h"

namespace Doodle
{
    /**
     * 渲染 pass 的种类：pass 与着色器两侧共用的键
     *
     * 一个着色器资产可以按 pass 各存一份数据（不同字节码 / 渲染状态）。
     * loader 把描述文件里的 passes.forward / passes.shadow 映射成这个枚举；
     * 各 pass 类在构造时把自己的枚举交给 DoodleRenderPass，基类按它取布局
     * （GetRenderPassLayout）、用 shader.GetPass(这个枚举) 取着色器的那一份。
     *
     * 它不是渲染 pass 的基类，不决定 pass 顺序，也不替代 DoodleForwardPass /
     * DoodleShadowPass —— 真正的 pass 类仍然各自存在，这里只解决「这份布局、
     * 这份着色器数据属于哪种 pass」。
     */
    enum class EDoodleRenderPassType : std::uint8_t
    {
        Forward = 0,
        Shadow = 1,
    };

    /**
     * 管线布局里归 pass 的那部分：每帧常量与逐绘制项数据
     *
     * 每帧常量是每帧写一次、这个 pass 的所有绘制项共用的那份数据：前向是相机与主光源，
     * 阴影是光空间矩阵。它装在 pass 的 UBO 里，对应的描述符集排在管线布局最前面，
     * GLSL 里用 layout(set = 0, ...) 声明。逐绘制项数据则是每条绘制项一份，经 push constant 传。
     *
     * 每种 pass 一份静态数据，没有 Initialize / Destroy、不持有 Vulkan 句柄。两处读它：
     * - DoodleShader::Initialize 反射完 SPIR-V 后，按它核对每个 pass 的每帧常量与 push constant；
     * - DoodleRenderPass 按自己的 pass 类型取它：Initialize 建本 pass 每帧常量的 UBO /
     *   描述符集布局 / 池 / 集合；建管线布局与录制时的 vkCmdPushConstants 都读 drawItemDataRange
     *   （GetDrawItemDataRange）。pass 不另存副本，用到时按类型再取。
     *
     * 它也是 pass 与着色器之间的契约：一个查着色器、一个建资源，两边认同一份定义；
     * 加 shadow / post 时不会出现某处还写着前一个 pass 的绑定形状。
     *
     * set 1（材质贴图）不在这里：它的形状由每个着色器自己声明，规则见
     * DoodleMaterialTextureSlot.h。附件格式也不在这里：它要等附件建好才知道、会随
     * 交换链重建变化，不能按 pass 类型静态定死，见 DoodleRenderPassAttachmentFormats。
     */
    struct DoodleRenderPassLayout
    {
        /**
         * 每帧常量的 UBO 绑定（binding / 类型 / 数量 / 阶段）
         *
         * 只能是单个 UBO（UNIFORM_BUFFER、数量 1）：DoodleRenderPass 按它建布局 / 池 / 集合并写入，不另做检查
         */
        VkDescriptorSetLayoutBinding uboBinding;

        /**
         * 这个 pass 的 UBO 字节数：pass 按它建缓冲，着色器反射出的块不得超过它
         */
        uint32_t uboSize = 0;

        /**
         * 逐绘制项数据块（Doodle...PassDrawItemData）的范围
         *
         * 名字按数据起，与数据块同口径；它现在经 push constant 传，所以类型是
         * VkPushConstantRange。管线布局与录制时的 vkCmdPushConstants 都用这一份，
         * 着色器的 push_constant block 只能落在它之内
         */
        VkPushConstantRange drawItemDataRange{};
    };

    /**
     * 取某种 pass 的布局
     *
     * 返回的引用是静态对象，有效期与进程相同；调用方只读。
     *
     * @throw std::invalid_argument pass 不是已知枚举值
     */
    [[nodiscard]] inline const DoodleRenderPassLayout& GetRenderPassLayout(const EDoodleRenderPassType pass)
    {
        static const DoodleRenderPassLayout forward
        {
            .uboBinding =
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            },
            .uboSize = static_cast<uint32_t>(sizeof(DoodleForwardPassUbo)),
            .drawItemDataRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleForwardPassDrawItemData))
            }
        };

        static const DoodleRenderPassLayout shadow
        {
            .uboBinding =
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr
            },
            .uboSize = static_cast<uint32_t>(sizeof(DoodleShadowPassUbo)),
            .drawItemDataRange =
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<uint32_t>(sizeof(DoodleShadowPassDrawItemData))
            }
        };

        switch (pass)
        {
        case EDoodleRenderPassType::Forward:
            return forward;

        case EDoodleRenderPassType::Shadow:
            return shadow;
        }

        throw std::invalid_argument("unknown pass type");
    }

    static_assert(sizeof(DoodleForwardPassDrawItemData) <= 128, "forward 逐绘制项数据块超过了规范保证的 maxPushConstantsSize 下限 128");
    static_assert(sizeof(DoodleShadowPassDrawItemData) <= 128, "shadow 逐绘制项数据块超过了规范保证的 maxPushConstantsSize 下限 128");
}
