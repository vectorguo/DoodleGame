//
//  DoodleVertex.h
//  Doodle
//
//  Created by 郭智 on 2026/10/11.
//
#pragma once

#include <array>
#include <cstddef>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * 一个顶点：位置 + 颜色
     *
     * 布局是交织的（interleaved）—— 同一个顶点的属性在内存里挨着，顶点依次排下去，
     * 一条绑定的 stride 就是整个结构体：
     *
     *     [pos0][color0] [pos1][color1] [pos2][color2]
     *      ↑偏移 0 ↑偏移 8
     *      └──── sizeof(Vertex) = 20 字节 ────┘
     *
     * 成员用 glm::vec2 / glm::vec3 而不是裸的 float[2] / float[3]：位置与颜色本来
     * 就是向量，后面的 MVP 变换、法线、UV 也都会用 glm，一套词汇比两套好
     *
     * 代价是「stride = 20、颜色在偏移 8」这两条不再只由本文件决定 —— 它们依赖
     * glm 的 vec3 是 12 字节、且不带尾部填充。这条假设必须被钉住：GLM 有一个
     * GLM_FORCE_ALIGNED_GENTYPES（以及连带的 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES）
     * 会把它抬成 16 字节对齐，vec3 于是变成 16 字节。本项目没有定义那些宏，
     * 所以现在是对的；但「没人定义」这件事本身没有东西替我们守着 ——
     * 哪天有人为了别的原因加上，顶点布局会静默改变，画面上表现为整体错位，
     * 而校验层一句话都不会说
     *
     * 所以文件末尾有两行 static_assert。它们不是文档，是编译期的守门人：
     * 那条假设一旦破了，构建当场失败
     *
     * 两份描述做成 static 成员函数、而不是管线层的私有方法：描述是类型的属性，
     * 与字段同生共死 —— 往这里加一个字段，就必须同步加一行属性描述、并可能改 stride，
     * 两者写在同一个文件里，改一处时另一处就在眼前。它们不读任何成员
     * （描述的是类型本身，而不是某个对象），所以是 static
     */
    struct Vertex
    {
        /**
         * 位置，对应着色器里的 layout(location = 0) in vec2
         */
        glm::vec2 pos;

        /**
         * 颜色，对应着色器里的 layout(location = 1) in vec3
         */
        glm::vec3 color;

        /**
         * 绑定描述：一条顶点数据流有多宽、按什么节奏前进
         *
         * 说明的是「从内存里怎么遍历这些顶点」—— 相邻两条数据之间隔多少字节，
         * 以及读头在什么时候前进到下一条
         */
        [[nodiscard]] static VkVertexInputBindingDescription GetBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};

            //本结构体的全部属性打包在同一个数组里，所以只需要一条绑定，编号取 0。
            //这是个约定编号：下面属性描述里的 binding 必须指回同一个数字
            bindingDescription.binding = 0;

            //写 sizeof(Vertex) 而不是 20：前者是「关于结构体的事实」，往结构体里
            //加字段时它自动跟上；后者是一份「关于结构体的记忆」，会悄悄失效。
            //这条规矩在下面的 offset 上还要再贯彻一次
            bindingDescription.stride = sizeof(Vertex);

            //每个顶点之后前进到下一条。本项目不做实例化渲染，没有逐实例的数据流
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        /**
         * 属性描述：从上面那条数据流的每一条记录里，切出两个属性
         *
         * 这里的 location 必须与着色器里 layout(location = N) in 的编号对上 ——
         * 变量名可以完全不同，编号是两边唯一的对接口径
         *
         * 格式的通道数要与着色器分量的个数匹配：通道多了会被静默丢弃，
         * 少了则补 (0, 0, 1)。这一处配错了不会报错，只会画出奇怪的颜色
         */
        [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

            //位置：location 0，两个 32 位浮点分量（vec2）
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            //偏移用宏算，理由同上面的 sizeof：字段挪动位置时它自动跟上
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            //颜色：location 1，三个 32 位浮点分量（vec3）
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            return attributeDescriptions;
        }
    };

    //上面那段「依赖 glm vec3 是 12 字节」的假设，在这里变成编译期检查。
    //
    //为什么值得写：这两条一旦悄悄变了，症状是模型整体错位或颜色串位，
    //校验层查不到（它不知道你把什么字节当成了顶点），而着色器那边也查不到
    //（它按 location 读，读到的就是错的数据）。把失败挪到编译期成本最低
    static_assert(sizeof(Vertex) == 20, "Vertex 布局变了：glm::vec3 不再是 12 字节（是不是定义了 GLM_FORCE_ALIGNED_GENTYPES 之类？）");
    static_assert(offsetof(Vertex, color) == 8, "Vertex 布局变了：color 不再位于偏移 8");
}
