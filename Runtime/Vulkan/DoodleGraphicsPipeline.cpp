//
// Created by 郭智 on 2026/10/6.
//

#include "DoodleGraphicsPipeline.h"

#include <stdexcept>
#include <vector>

#include "DoodleDepthImages.h"
#include "DoodleShaderModule.h"
#include "DoodleSwapChain.h"
#include "DoodleVulkanDevice.h"
#include "DoodleVertex.h"

namespace Doodle
{
    void DoodleGraphicsPipeline::Initialize(const DoodleVulkanDevice& device,
                                            const DoodleSwapChain& swapChain,
                                            const DoodleDepthImages& depthImages)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;
        m_pDepthImages = &depthImages;

        //描述符集布局是管线布局的输入，后者又是图形管线的输入，三层依次创建
        CreateDescriptorSetLayout();
        CreatePipelineLayout();
        CreateGraphicsPipeline();
    }

    void DoodleGraphicsPipeline::Destroy()
    {
        //逆着创建序拆：管线引用布局，布局引用描述符集布局。
        //描述符池必须已经先销毁，否则池里的集合仍然引用着描述符集布局
        DestroyGraphicsPipeline();
        DestroyPipelineLayout();
        DestroyDescriptorSetLayout();
    }

    void DoodleGraphicsPipeline::CreateDescriptorSetLayout()
    {
        //binding 0 对应顶点着色器里 layout(set = 0, binding = 0) 的 UBO 块。
        //stageFlags 只写顶点阶段：片元着色器没有读它，声明得越准确，驱动越省事
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // 只对采样器描述符有意义

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(m_pDevice->GetLogicalDevice(), &layoutInfo,
                                        nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void DoodleGraphicsPipeline::DestroyDescriptorSetLayout()
    {
        if (m_descriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_pDevice->GetLogicalDevice(), m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void DoodleGraphicsPipeline::CreatePipelineLayout()
    {
        //先放一套描述符集布局；push constant 暂时不用，数量留 0。
        //图形管线必须有一个管线布局，现在它开始承载真实接口，不再只是空壳
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = 1;
        createInfo.pSetLayouts = &m_descriptorSetLayout;
        createInfo.pushConstantRangeCount = 0;
        createInfo.pPushConstantRanges = nullptr;

        const auto result = vkCreatePipelineLayout(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pipelineLayout);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void DoodleGraphicsPipeline::DestroyPipelineLayout()
    {
        if (m_pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_pDevice->GetLogicalDevice(), m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
    }

    void DoodleGraphicsPipeline::CreateGraphicsPipeline()
    {
        const VkDevice pDevice = m_pDevice->GetLogicalDevice();

        //着色器模块用局部变量管住寿命。vkCreateGraphicsPipelines 是拷贝语义：
        //它把编译结果存进管线对象，之后不再引用模块，所以函数返回前销毁即可。
        //这正是 DoodleShaderModule 做成静态工具类而不是一层的原因
        const VkShaderModule vertShaderModule = DoodleShaderModule::CreateShaderModule(pDevice, "Shaders/vert.spv");
        const VkShaderModule fragShaderModule = DoodleShaderModule::CreateShaderModule(pDevice, "Shaders/frag.spv");

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        const VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        //动态状态：视口与剪裁不烘焙进管线，改由录制命令时 vkCmdSetViewport 指定。
        //本层因此不消费交换链尺寸，也就不需要引用交换链
        const std::vector<VkDynamicState> dynamicStates =
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        //顶点输入：两份描述都从 Vertex 类型上取。管线只吃描述，不关心数据来自哪
        //
        //这两个是按值返回的局部量，取它们的地址喂给管线即可。
        //VkPipelineVertexInputStateCreateInfo 里存的是裸指针、不拷贝内容，
        //所以它们必须活到 vkCreateGraphicsPipelines 返回为止 —— 局部量正好满足：
        //创建调用就在本函数里，两个变量的寿命完整覆盖它。
        //（这也是为什么不能把描述做成返回临时对象引用的函数：出了那个函数就悬空了）
        const auto bindingDescription = Vertex::GetBindingDescription();
        const auto attributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        //输入装配：每 3 个顶点组成一个三角形
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        //视口与剪裁走动态状态，这里只填数量，pViewports / pScissors 留空
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        //光栅化。顶点数组本身仍按「左上 → 右上 → 右下 → 左下」排，
        //但顶点着色器用 GLM 投影时做了 Y 翻转（proj[1][1] *= -1）——
        //那一步把屏幕空间的绕序整体反了过来，所以这里的 frontFace 取逆时针。
        //frontFace、投影约定与顶点顺序三者必须一起看；只改其中一处，画面会整片消失
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        //多重采样：先关闭
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        //颜色混合：不透明，直接覆盖写入
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        //动态渲染：管线不再绑定渲染通道对象，改成在这里声明「我要往什么格式的图上画」。
        //
        //原来这条信息装在 VkRenderPass 里，由 pipelineInfo.renderPass 指过去，
        //兼容性规则在创建期就替你校验了附件格式对不对。现在没有那个对象可比，
        //约束退化成一句：这里声明的格式与数量，必须与录制时 vkCmdBeginRendering
        //给的附件视图一致。对不上校验层会在绘制时报出来，而不是在创建这里
        //
        //pColorAttachmentFormats 指向的数组只在本次创建调用期间被读取，
        //从交换链读出来的这个局部量活到函数结束，够用 —— 不必为它留成员
        VkPipelineRenderingCreateInfo renderingCreateInfo{};
        renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        const auto colorFormat = m_pSwapChain->GetImageFormat();
        renderingCreateInfo.colorAttachmentCount = 1;
        renderingCreateInfo.pColorAttachmentFormats = &colorFormat;

        //深度格式从深度图组取 —— 格式是那一组挑的，这里只消费结果。
        //与颜色格式一样，创建管线时读一次、编译进管线，之后不再读
        const auto depthFormat = m_pDepthImages->GetFormat();
        renderingCreateInfo.depthAttachmentFormat = depthFormat;

        //含模板分量时，stencil 那一栏必须一起填，且与深度相同。
        //只填 depth 不填 stencil，校验层会报两者不一致
        if (DoodleDepthImages::HasStencilComponent(depthFormat))
        {
            renderingCreateInfo.stencilAttachmentFormat = depthFormat;
        }

        //深度／模板状态。声明了 depthAttachmentFormat 之后，这一格必须是有效指针 ——
        //规范为 renderPass 为空的情形专门写了这条约束。两处必须同一次改完：
        //只填格式不给状态，管线建不出来；只给状态不填格式，校验层要到绘制时才报
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        //更小的深度 = 更近，与清屏值 1.0（最远）配对
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        //边界测试与模板都先不用。注意 depthBounds 是可选设备特性，
        //本项目的 VkPhysicalDeviceFeatures 全零，也就是没开它
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {};
        depthStencil.back = {};

        //上面这些状态结构体都是栈上的局部变量，pipelineInfo 只记录它们的地址。
        //所以创建调用必须留在本函数作用域内 —— 拆出去就是悬垂指针
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingCreateInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        //动态渲染的标志就是这两个字段一起留空：没有渲染通道对象可指，
        //子通道这个概念也随之消失。附件信息改由上一条 pNext 链承载
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        //第二参数暂传 VK_NULL_HANDLE，即不使用管线缓存。
        //等「管线缓存」那一章再接上，用于跨次运行复用编译结果
        const auto result = vkCreateGraphicsPipelines(pDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        //管线已经拿到编译结果，模块可以立即销毁
        vkDestroyShaderModule(pDevice, fragShaderModule, nullptr);
        vkDestroyShaderModule(pDevice, vertShaderModule, nullptr);
    }

    void DoodleGraphicsPipeline::DestroyGraphicsPipeline()
    {
        if (m_graphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_pDevice->GetLogicalDevice(), m_graphicsPipeline, nullptr);
            m_graphicsPipeline = VK_NULL_HANDLE;
        }
    }
}
