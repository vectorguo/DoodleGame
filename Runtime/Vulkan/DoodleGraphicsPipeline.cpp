//
// Created by 郭智 on 2026/10/6.
//

#include "DoodleGraphicsPipeline.h"

#include <stdexcept>
#include <vector>

#include "DoodleShaderModule.h"

namespace Doodle
{
    void DoodleGraphicsPipeline::Initialize(const DoodleVulkanDevice& device, const DoodleSwapChain& swapChain)
    {
        m_pDevice = &device;
        m_pSwapChain = &swapChain;

        //管线布局是管线的前置条件，先创建
        CreatePipelineLayout();
        CreateGraphicsPipeline();
    }

    void DoodleGraphicsPipeline::Destroy()
    {
        //管线在创建时引用了布局，先销毁管线
        DestroyGraphicsPipeline();
        DestroyPipelineLayout();
    }

    void DoodleGraphicsPipeline::CreatePipelineLayout()
    {
        //本阶段还没有 uniform 与 push constant，先建一个空布局占位。
        //管线必须有一个布局，哪怕它是空的
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = 0;
        createInfo.pSetLayouts = nullptr;
        createInfo.pushConstantRangeCount = 0;
        createInfo.pPushConstantRanges = nullptr;

        const auto result = vkCreatePipelineLayout(m_pDevice->GetLogicalDevice(), &createInfo, nullptr, &m_pPipelineLayout);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void DoodleGraphicsPipeline::DestroyPipelineLayout()
    {
        if (m_pPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_pDevice->GetLogicalDevice(), m_pPipelineLayout, nullptr);
            m_pPipelineLayout = VK_NULL_HANDLE;
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

        //顶点输入：目前顶点数据硬编码在顶点着色器里，所以这里是空的。
        //等「顶点缓冲」那一章再补上绑定与属性描述
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;

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

        //光栅化。frontFace 取顺时针，与着色器里给出的顶点顺序（左上、右下、左下）一致
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
        //深度与模板附件本阶段还没有，留零即「不声明」。
        //等「深度缓冲」那一章，这里补 depthAttachmentFormat 即可，写法与颜色对称

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
        pipelineInfo.pDepthStencilState = nullptr;   //本阶段还没有深度缓冲
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pPipelineLayout;
        //动态渲染的标志就是这两个字段一起留空：没有渲染通道对象可指，
        //子通道这个概念也随之消失。附件信息改由上一条 pNext 链承载
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        //第二参数暂传 VK_NULL_HANDLE，即不使用管线缓存。
        //等「管线缓存」那一章再接上，用于跨次运行复用编译结果
        const auto result = vkCreateGraphicsPipelines(pDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pGraphicsPipeline);
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
        if (m_pGraphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_pDevice->GetLogicalDevice(), m_pGraphicsPipeline, nullptr);
            m_pGraphicsPipeline = VK_NULL_HANDLE;
        }
    }
}
