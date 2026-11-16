//
// Created by 郭智 on 2026/10/6.
//

#include "DoodleVulkanPipelineObject.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "DoodleDepthImages.h"
#include "DoodleShaderModule.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    void DoodleVulkanPipelineObject::Initialize(const DoodleVulkanPipelineInitInfo& info)
    {
        //描述不合法时什么都不建：先查，再动设备
        ValidateShaderDescription(info.shaderDescription);
        if (info.passUboDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("graphics pipeline: set 0 layout is null");
        }

        m_pDevice = &info.device;

        //布局是管线的输入，先建布局再建管线。中途失败时把已经建出来的收掉，
        //对象回到未初始化状态 —— 管线缓存只在成功之后才收下这个对象，
        //失败的那份会被直接丢弃，没有人再来调 Destroy
        try
        {
            CreatePipelineLayout(info.passUboDescriptorSetLayout,
                                 info.shaderDescription.materialDescriptorSetLayout,
                                 info.pushConstantRange);
            m_graphicsPipeline = CreateGraphicsPipeline(info.pipelineCompileCache,
                                                        info.passConfig,
                                                        info.shaderDescription);
        }
        catch (...)
        {
            DestroyPipelineLayout();
            m_pDevice = nullptr;
            throw;
        }
    }

    void DoodleVulkanPipelineObject::Rebuild(VkPipelineCache pipelineCompileCache,
                                         const DoodleVulkanPipelinePassConfig& passConfig,
                                         const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        if (m_pDevice == nullptr || m_pipelineLayout == VK_NULL_HANDLE)
        {
            throw std::logic_error("graphics pipeline: Rebuild called before Initialize");
        }

        //布局与附件格式无关，原样保留；只换管线本身。
        //新的建出来之前不动旧的：CreateGraphicsPipeline 抛异常时旧管线仍然完好
        const VkPipeline rebuiltPipeline = CreateGraphicsPipeline(pipelineCompileCache, passConfig, shaderDescription);
        DestroyGraphicsPipeline();
        m_graphicsPipeline = rebuiltPipeline;
    }

    void DoodleVulkanPipelineObject::Destroy()
    {
        if (m_pDevice == nullptr)
        {
            return;
        }

        //逆着创建序拆：管线引用布局
        DestroyGraphicsPipeline();
        DestroyPipelineLayout();
        m_pDevice = nullptr;
    }

    void DoodleVulkanPipelineObject::ValidateShaderDescription(const DoodleVulkanPipelineShaderDescription& shaderDescription)
    {
        //字节码长度由 DoodleShaderModule 检查；这里只挡掉空的，让报错说清是哪个阶段
        if (shaderDescription.vertexBytecode.empty())
        {
            throw std::invalid_argument("graphics pipeline: vertex shader bytecode is empty");
        }
        if (shaderDescription.fragmentBytecode.empty())
        {
            throw std::invalid_argument("graphics pipeline: fragment shader bytecode is empty");
        }

        //NaN 不等于自己：带着它的渲染状态在缓存里永远查不到，每要一次就多建一条管线
        const auto& renderState = shaderDescription.renderState;
        if (!std::isfinite(renderState.depthBiasConstantFactor) || !std::isfinite(renderState.depthBiasSlopeFactor))
        {
            throw std::invalid_argument("graphics pipeline: depth bias factors must be finite");
        }
    }

    void DoodleVulkanPipelineObject::CreatePipelineLayout(VkDescriptorSetLayout passUboDescriptorSetLayout,
                                                          VkDescriptorSetLayout materialDescriptorSetLayout,
                                                          VkPushConstantRange pushConstantRange)
    {
        //set 0 负责本 pass 的每帧常量；set 1 负责这个着色器的材质资源
        //（现在是一张纹理）。分成两个集合而不是一个：UBO 按帧轮转，材质按资产共享。
        //
        //同一个 pass 内所有管线共用同一个 set 0 布局与 push constant 范围：
        //Vulkan 的布局兼容规则要求 set 0..N 与 push constant 范围都相同，管线之间
        //才对 set N 兼容，换管线时已经绑好的 set 0 不会失效
        //
        //不需要材质资源的着色器没有 set 1，布局里只放 set 0 —— 对 set 0 依旧兼容
        if (pushConstantRange.size > 0)
        {
            if (pushConstantRange.offset != 0 ||
                pushConstantRange.size % 4 != 0 ||
                pushConstantRange.size > 128)
            {
                throw std::invalid_argument("graphics pipeline: invalid push constant range");
            }
        }

        const VkDescriptorSetLayout setLayouts[]
        {
            passUboDescriptorSetLayout,
            materialDescriptorSetLayout
        };
        const uint32_t setLayoutCount = materialDescriptorSetLayout != VK_NULL_HANDLE ? 2 : 1;

        const VkPipelineLayoutCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = setLayoutCount,
            .pSetLayouts = setLayouts,
            .pushConstantRangeCount = pushConstantRange.size > 0 ? 1u : 0u,
            .pPushConstantRanges = pushConstantRange.size > 0 ? &pushConstantRange : nullptr
        };

        const auto result = vkCreatePipelineLayout(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void DoodleVulkanPipelineObject::DestroyPipelineLayout()
    {
        if (m_pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_pDevice->GetLogicalDevice(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
    }

    VkPipeline DoodleVulkanPipelineObject::CreateGraphicsPipeline(VkPipelineCache pipelineCompileCache,
                                                             const DoodleVulkanPipelinePassConfig& passConfig,
                                                             const DoodleVulkanPipelineShaderDescription& shaderDescription) const
    {
        const VkDevice logicalDevice = m_pDevice->GetLogicalDevice();
        const DoodleRenderState& renderState = shaderDescription.renderState;

        //先把所有可能抛异常的准备做完，再建着色器模块：模块是裸句柄，
        //建出来之后到销毁之前只剩 vkCreateGraphicsPipelines 这一步，中间不会再抛

        //顶点输入：按描述里的顶点格式与实例格式取，不再写死具体顶点类型。
        //管线只吃描述，不关心数据来自哪
        //
        //vertexInput 是按值返回的局部量，取它两个数组的地址喂给管线即可。
        //VkPipelineVertexInputStateCreateInfo 里存的是裸指针、不拷贝内容，
        //所以它必须活到 vkCreateGraphicsPipelines 返回为止 —— 局部量正好满足
        const DoodleVertexInput vertexInput = DoodleVertexInput::Describe(
            shaderDescription.vertexFormat, shaderDescription.instanceFormat);

        //颜色混合：pass 有几个颜色附件就给几份，全部按渲染状态里同一种混合方式。
        //动态渲染下这个数量必须等于 VkPipelineRenderingCreateInfo 的颜色附件数
        //（VUID-VkGraphicsPipelineCreateInfo-renderPass-06055）
        const std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
            passConfig.colorFormats.size(),
            MakeColorBlendAttachment(renderState.blendMode));

        //着色器模块用局部变量管住寿命。vkCreateGraphicsPipelines 是拷贝语义：
        //它把编译结果存进管线对象，之后不再引用模块，所以函数返回前销毁即可。
        //这正是 DoodleShaderModule 做成静态工具类而不是一层的原因
        const VkShaderModule vertShaderModule = DoodleShaderModule::CreateShaderModule(logicalDevice, shaderDescription.vertexBytecode);
        VkShaderModule fragShaderModule = VK_NULL_HANDLE;
        try
        {
            fragShaderModule = DoodleShaderModule::CreateShaderModule(logicalDevice, shaderDescription.fragmentBytecode);
        }
        catch (...)
        {
            vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
            throw;
        }

        const VkPipelineShaderStageCreateInfo vertShaderStageInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName = "main"
        };

        const VkPipelineShaderStageCreateInfo fragShaderStageInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName = "main"
        };

        const VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        //动态状态：视口与剪裁不烘焙进管线，改由录制命令时 vkCmdSetViewport 指定。
        //它们跟着窗口尺寸变，烘焙进去的话每次缩放都要重建所有管线。
        //其余状态（剔除、深度、混合……）都按渲染状态烘焙，理由见 DoodleRenderState
        constexpr VkDynamicState dynamicStates[] =
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        const VkPipelineDynamicStateCreateInfo dynamicState
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
            .pDynamicStates = dynamicStates
        };

        const VkPipelineVertexInputStateCreateInfo vertexInputInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInput.bindings.size()),
            .pVertexBindingDescriptions = vertexInput.bindings.data(),
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.attributes.size()),
            .pVertexAttributeDescriptions = vertexInput.attributes.data()
        };

        //输入装配：三角形列表或线列表，由渲染状态决定
        const VkPipelineInputAssemblyStateCreateInfo inputAssembly
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = ToVkPrimitiveTopology(renderState.topology),
            .primitiveRestartEnable = VK_FALSE
        };

        //视口与剪裁走动态状态，这里只填数量，pViewports / pScissors 留空
        constexpr VkPipelineViewportStateCreateInfo viewportState
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        //光栅化。顶点数组本身仍按「左上 → 右上 → 右下 → 左下」排，
        //但顶点着色器用 GLM 投影时做了 Y 翻转（proj[1][1] *= -1）——
        //那一步把屏幕空间的绕序整体反了过来，所以这里的 frontFace 取逆时针。
        //frontFace、投影约定与顶点顺序三者必须一起看；只改其中一处，画面会整片消失。
        //所以 frontFace 是全局约定、不进渲染状态；剔除哪一面才由着色器决定
        //
        //depthBiasClamp 固定 0：非零要开设备特性 depthBiasClamp，本项目没开。
        //lineWidth 固定 1：大于 1 要开 wideLines，线列表也按 1 像素画
        const VkPipelineRasterizationStateCreateInfo rasterizer
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = ToVkCullMode(renderState.cullMode),
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = renderState.depthBiasEnabled ? VK_TRUE : VK_FALSE,
            .depthBiasConstantFactor = renderState.depthBiasConstantFactor,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = renderState.depthBiasSlopeFactor,
            .lineWidth = 1.0f
        };

        //多重采样：采样数随 pass 走（现在是 1）
        const VkPipelineMultisampleStateCreateInfo multisampling
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = passConfig.sampleCount,
            .sampleShadingEnable = VK_FALSE
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
            .pAttachments = colorBlendAttachments.data(),
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
        };

        //动态渲染：管线不再绑定渲染通道对象，改成在这里声明「我要往什么格式的图上画」。
        //
        //原来这条信息装在 VkRenderPass 里，由 pipelineInfo.renderPass 指过去，
        //兼容性规则在创建期就替你校验了附件格式对不对。现在没有那个对象可比，
        //约束退化成一句：这里声明的格式与数量，必须与录制时 vkCmdBeginRendering
        //给的附件视图一致。对不上校验层会在绘制时报出来，而不是在创建这里
        //
        //格式来自 pass 配置：本类不再引用交换链与深度图组。pColorAttachmentFormats
        //指向的数组只在本次创建调用期间被读取，pass 配置活过这次调用，够用
        //
        //含模板分量时，stencil 那一栏必须一起填，且与深度相同；
        //只填 depth 不填 stencil，校验层会报两者不一致。
        //没有模板分量（或没有深度附件）时保持 VK_FORMAT_UNDEFINED（这里的零值语义）
        const VkFormat depthFormat = passConfig.depthFormat;
        const VkPipelineRenderingCreateInfo renderingCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(passConfig.colorFormats.size()),
            .pColorAttachmentFormats = passConfig.colorFormats.data(),
            .depthAttachmentFormat = depthFormat,
            .stencilAttachmentFormat =
                DoodleDepthImages::HasStencilComponent(depthFormat) ? depthFormat : VK_FORMAT_UNDEFINED
        };

        //深度／模板状态。声明了 depthAttachmentFormat 之后，这一格必须是有效指针 ——
        //规范为 renderPass 为空的情形专门写了这条约束（VUID-VkGraphicsPipelineCreateInfo-renderPass-09033）。
        //pass 没有深度附件时不给（传空指针），渲染状态里的深度设置也就不生效
        const VkPipelineDepthStencilStateCreateInfo depthStencil
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = renderState.depthTestEnabled ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = renderState.depthWriteEnabled ? VK_TRUE : VK_FALSE,
            .depthCompareOp = ToVkCompareOp(renderState.depthCompareOp),
            //边界测试与模板都先不用。注意 depthBounds 是可选设备特性，
            //本项目的 VkPhysicalDeviceFeatures 全零，也就是没开它
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {}
        };
        const VkPipelineDepthStencilStateCreateInfo* pDepthStencil =
            depthFormat != VK_FORMAT_UNDEFINED ? &depthStencil : nullptr;

        //上面这些状态结构体都是栈上的局部变量，pipelineInfo 只记录它们的地址。
        //所以创建调用必须留在本函数作用域内 —— 拆出去就是悬垂指针
        //
        //动态渲染的标志就是 renderPass / subpass 两格：没有渲染通道对象可指，
        //子通道这个概念也随之消失。附件信息改由 pNext 链（renderingCreateInfo）承载
        const VkGraphicsPipelineCreateInfo pipelineInfo
        {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingCreateInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = pDepthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = m_pipelineLayout,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };

        //第二参数是驱动编译缓存：同一份着色器与状态再建一次时，驱动可以直接
        //取上次的编译结果。它默认内部同步，多个线程同时建管线可以共用一个
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        const auto result = vkCreateGraphicsPipelines(logicalDevice, pipelineCompileCache, 1, &pipelineInfo, nullptr, &graphicsPipeline);

        //不管成没成功，模块都已经没用了：成功时编译结果在管线里，失败时没有管线
        vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
        vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create graphics pipeline!");
        }
        return graphicsPipeline;
    }

    void DoodleVulkanPipelineObject::DestroyGraphicsPipeline()
    {
        if (m_graphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_pDevice->GetLogicalDevice(), m_graphicsPipeline, nullptr);
            m_graphicsPipeline = VK_NULL_HANDLE;
        }
    }

    VkCullModeFlags DoodleVulkanPipelineObject::ToVkCullMode(const ECullMode cullMode)
    {
        switch (cullMode)
        {
        case ECullMode::None:
            return VK_CULL_MODE_NONE;
        case ECullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case ECullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        }
        throw std::invalid_argument("unknown cull mode: " + std::to_string(static_cast<int>(cullMode)));
    }

    VkCompareOp DoodleVulkanPipelineObject::ToVkCompareOp(const ECompareOp compareOp)
    {
        switch (compareOp)
        {
        case ECompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case ECompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case ECompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case ECompareOp::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case ECompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case ECompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case ECompareOp::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case ECompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
        }
        throw std::invalid_argument("unknown compare op: " + std::to_string(static_cast<int>(compareOp)));
    }

    VkPrimitiveTopology DoodleVulkanPipelineObject::ToVkPrimitiveTopology(const EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::TriangleList:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case EPrimitiveTopology::LineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        }
        throw std::invalid_argument("unknown primitive topology: " + std::to_string(static_cast<int>(topology)));
    }

    VkPipelineColorBlendAttachmentState DoodleVulkanPipelineObject::MakeColorBlendAttachment(const EBlendMode blendMode)
    {
        //四种都写全 RGBA：混合方式只决定怎么合成，不决定写哪几个通道
        constexpr VkColorComponentFlags colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        //因子与 Unity URP 的同名混合模式一致。alpha 通道统一用
        //「源 alpha 叠在背景上」（ONE, ONE_MINUS_SRC_ALPHA），只有 Additive 是两边相加 ——
        //交换链不读 alpha，但离屏目标以后会被别的 pass 当覆盖率用
        switch (blendMode)
        {
        case EBlendMode::Opaque:
            //不透明：直接覆盖写入，混合单元不参与，其余因子不被读取
            return
            {
                .blendEnable = VK_FALSE,
                .colorWriteMask = colorWriteMask
            };
        case EBlendMode::AlphaBlend:
            //结果 = 源 × 源 alpha + 背景 × (1 − 源 alpha)
            return
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = colorWriteMask
            };
        case EBlendMode::Additive:
            //结果 = 源 × 源 alpha + 背景：只会变亮，与绘制顺序无关
            return
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = colorWriteMask
            };
        case EBlendMode::PremultipliedAlpha:
            //颜色在着色器里已经乘过 alpha：结果 = 源 + 背景 × (1 − 源 alpha)
            return
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = colorWriteMask
            };
        }
        throw std::invalid_argument("unknown blend mode: " + std::to_string(static_cast<int>(blendMode)));
    }
}
