//
//  DoodleShaderPass.h
//  Doodle
//
//  Created by 郭智 on 2026/11/22.
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
     * 着色器的一个 pass：两段 SPIR-V 与这个 pass 的管线输入
     *
     * 由 DoodleShader 按 pass 各持有一份；建管线的一方按值拷一份作为重建快照，
     * 所以这份数据必须能独立于着色器资产存在 —— 资产先于渲染器销毁，交换链重建
     * 时只剩快照可用。
     *
     * 与 DoodlePassShaderInterface 的分工：那个是 pass 侧的契约（set 0 与
     * push constant 的约定，加载时按它核对）；本类是着色器侧的内容。
     */
    struct DoodleShaderPass
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
         * set 1 的描述符集布局：这个着色器的材质贴图；不持有
         *
         * 由 DoodleShader 按各 pass 反射出的 set 1 合并建出，整个着色器一份，
         * 每个 pass 的数据里放的都是同一个句柄。可以为 VK_NULL_HANDLE：不用贴图的
         * 着色器只有 set 0，与其它管线对 set 0 仍然兼容。DoodleDescriptorManager
         * 按同一个布局给用这个着色器的材质分配 set 1，所以材质集合在每个 pass 的
         * 管线上都能绑
         *
         * 只在建管线布局时被读取一次：Vulkan 规定作为参数传给另一个对象创建的
         * set 布局，创建返回之后就不再被访问，所以这个布局先于管线布局销毁是合法的
         */
        VkDescriptorSetLayout materialDescriptorSetLayout = VK_NULL_HANDLE;

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
