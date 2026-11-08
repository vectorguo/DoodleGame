//
// Created by 郭智 on 2026/11/8.
//

#pragma once

#include <cstddef>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "Rendering/DoodleRenderState.h"
#include "Rendering/DoodleVertex.h"

namespace Doodle
{
    /**
     * 管线创建的输入描述：向管线缓存要一条管线时用的词汇表
     *
     * 与 DoodleGraphicsPipeline 这个具体管线对象分开放，是因为要用它的不只是管线本身：
     * 着色器资产（DoodleShader）按值持有一份 DoodlePipelineShaderDescription，
     * 内容层（Rendering）的公开头文件因此只需要这份描述，不必依赖管线类的完整定义
     * （连私有成员布局一起）。管线类与管线缓存都按值/按引用接收这两样，谁也不独占
     */

    /**
     * pass 侧配置：这一批绘制往什么样的附件上画
     *
     * 动态渲染之后管线不再绑某个渲染通道对象，而是在创建时声明附件的格式与采样数；
     * 录制时 vkCmdBeginRendering 给的附件必须与这里一致。同一种 pass 配置下的
     * 管线由同一个管线缓存实例持有（见 DoodlePipelineCache）
     *
     * 判等用默认的逐成员比较：交换链重建后门面拿新旧两份一比，就知道管线要不要重建
     */
    struct DoodlePipelinePassConfig
    {
        /**
         * 每个颜色附件的格式，按 location 排列
         *
         * 现在只有一个（交换链图像）。做成数组是给多渲染目标留的：延迟渲染的
         * G-buffer、离屏的多个输出；深度预渲染 / 阴影这类只写深度的 pass 是空数组
         */
        std::vector<VkFormat> colorFormats;

        /**
         * 深度附件格式；VK_FORMAT_UNDEFINED 表示这个 pass 没有深度附件
         * （后处理、UI 这类全屏 pass），此时渲染状态里的深度设置不生效
         */
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        /**
         * 光栅化采样数。现在是 1，留给 MSAA：采样数是管线的一部分，
         * 开 MSAA 的 pass 要另一套管线
         */
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        [[nodiscard]] bool operator==(const DoodlePipelinePassConfig&) const = default;
    };

    /**
     * 着色器侧描述：一条管线里由着色器决定的全部输入
     *
     * 每次向管线缓存要管线时传入。以后由着色器资产（DoodleShader）按描述文件与
     * SPIR-V 反射结果拼出来；材质加载时拿它向管线缓存要管线。现在由门面按
     * lit 的两份字节码拼出来
     *
     * 字节码按值持有（std::vector<std::byte>，与 DoodleFileSystem::ReadBinary 同一个类型）：
     * 管线缓存的条目要存一份副本，格式变了原地重建时再用
     */
    struct DoodlePipelineShaderDescription
    {
        /**
         * 顶点着色器的 SPIR-V
         */
        std::vector<std::byte> vertexBytecode;

        /**
         * 片元着色器的 SPIR-V
         */
        std::vector<std::byte> fragmentBytecode;

        /**
         * set 1 的描述符集布局：这个着色器的材质资源（贴图……）；不持有
         *
         * 由 DoodleShader 按 SPIR-V 反射结果建（一个着色器一份）。可以为
         * VK_NULL_HANDLE：不需要材质资源的着色器（调试线框）只有 set 0，
         * 与其它管线对 set 0 仍然兼容。第 3 步之前所有着色器的 set 1 定义相同，
         * 所以描述符管理器按纹理分配的集合能绑到任一着色器的管线上
         *
         * 只在建管线布局时被读取一次：Vulkan 规定作为参数传给另一个对象创建的
         * set 布局，创建返回之后就不再被访问，所以这个布局先于管线布局销毁是合法的
         */
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;

        /**
         * 渲染状态：剔除、深度、混合、深度偏移、拓扑
         */
        DoodleRenderState renderState;

        /**
         * 顶点格式：从顶点缓冲里怎么读数据
         */
        EVertexFormat vertexFormat = EVertexFormat::PosNormalUv;

        /**
         * 实例格式：逐实例数据流怎么读；None 表示没有第二条流
         */
        EInstanceFormat instanceFormat = EInstanceFormat::None;
    };
}
