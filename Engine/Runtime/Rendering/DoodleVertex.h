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
     * 一个顶点：位置 + 法线 + 第一套 UV
     *
     * 放在 Rendering：它是网格资产的数据格式。文件末尾那两个 VkVertexInput*
     * 描述是随类型走的管线契约，和字段写在同一处，改字段时不会漏改描述。
     *
     * 布局是交织的（interleaved）—— 同一个顶点的属性在内存里挨着，顶点依次排下去，
     * 一条绑定的 stride 就是整个结构体：
     *
     *     [pos0][normal0][uv0] [pos1][normal1][uv1] [pos2][normal2][uv2]
     *      ↑0    ↑12      ↑24
     *      └──────────── sizeof(Vertex) = 32 字节 ────────────┘
     *
     * 法线在模型空间（资产空间）：DoodleFbxImporter 把节点世界变换烘焙进位置时，
     * 也把它的逆转置作用在法线上，两者因此处在同一个空间。渲染时顶点着色器
     * 再用 GameObject 的 localToWorld 逆转置把法线变到世界空间 —— 三步用的是同一条
     * 「位置用矩阵、法线用矩阵的逆转置」规则。
     *
     * UV 只保留源文件的第一套。贴图还没接进来，这一路先跟着顶点格式走：
     * 等材质系统落地时，片元着色器直接从这里取坐标，不需要再改一次顶点布局。
     *
     * 成员用 glm 的向量类型而不是裸 float[2] / float[3]：位置、法线、UV 本来就是
     * 向量，后面的光照计算也都会用 glm，一套词汇比两套好
     *
     * 代价是「stride = 32、法线在偏移 12、UV 在偏移 24」这几条不再只由本文件
     * 决定 —— 它们依赖 glm 的 vec3 是 12 字节、vec2 是 8 字节、且都不带尾部
     * 填充。这条假设必须被钉住：GLM 有一个 GLM_FORCE_ALIGNED_GENTYPES
     * （以及连带的 GLM_FORCE_DEFAULT_ALIGNED_GENTYPES）会把 vec3 抬成 16 字节。
     * 本项目没有定义那些宏，所以现在是对的；但「没人定义」这件事本身没有
     * 东西替我们守着 —— 哪天有人为了别的原因加上，顶点布局会静默改变，
     * 画面上表现为整体错位或法线方向全错，而校验层一句话都不会说
     *
     * 所以文件末尾有三行 static_assert。它们不是文档，是编译期的守门人：
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
         * 位置，对应着色器里的 layout(location = 0) in vec3
         *
         * 第三个分量是模型空间深度：顶点着色器会乘 MVP 把它送进裁剪空间。
         * 正方体的六个面在空间里分开，透视投影与深度测试会正确区分前后。
         */
        glm::vec3 pos;

        /**
         * 单位法线，对应着色器里的 layout(location = 1) in vec3
         *
         * 模型空间，与 pos 同空间。顶点着色器乘 localToWorld 的逆转置后送进片元，
         * 方向光的明暗就靠它区分朝向
         */
        glm::vec3 normal;

        /**
         * 第一套纹理坐标，对应着色器里的 layout(location = 2) in vec2
         *
         * 贴图还没接进来，当前管线只把它放进顶点格式；片元着色器暂不声明它。
         */
        glm::vec2 uv;

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

            //写 sizeof(Vertex) 而不是 32：前者是「关于结构体的事实」，往结构体里
            //加字段时它自动跟上；后者是一份「关于结构体的记忆」，会悄悄失效。
            //这条规矩在下面的 offset 上还要再贯彻一次
            bindingDescription.stride = sizeof(Vertex);

            //每个顶点之后前进到下一条。本项目不做实例化渲染，没有逐实例的数据流
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        /**
         * 属性描述：从上面那条数据流的每一条记录里，切出三个属性
         *
         * 这里的 location 必须与着色器里 layout(location = N) in 的编号对上 ——
         * 变量名可以完全不同，编号是两边唯一的对接口径
         *
         * 格式的通道数要与着色器分量的个数匹配：通道多了会被静默丢弃，
         * 少了则补 (0, 0, 1)。这一处配错了不会报错，只会画出错误的明暗或贴图
         */
        [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            //位置：location 0，三个 32 位浮点分量（vec3）
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            //偏移用宏算，理由同上面的 sizeof：字段挪动位置时它自动跟上
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            //法线：location 1，三个 32 位浮点分量（vec3）
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            //UV：location 2，两个 32 位浮点分量（vec2）
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, uv);

            return attributeDescriptions;
        }
    };

    //上面那段「依赖 glm vec3 = 12 字节、vec2 = 8 字节」的假设，在这里变成编译期检查。
    //
    //为什么值得写：这些一旦悄悄变了，症状是模型整体错位或法线方向全错，
    //校验层查不到（它不知道你把什么字节当成了顶点），而着色器那边也查不到
    //（它按 location 读，读到的就是错的数据）。把失败挪到编译期成本最低
    static_assert(sizeof(Vertex) == 32, "Vertex 布局变了：glm::vec3 / vec2 不再是 12 / 8 字节（是不是定义了 GLM_FORCE_ALIGNED_GENTYPES 之类？）");
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 布局变了：normal 不再位于偏移 12");
    static_assert(offsetof(Vertex, uv) == 24, "Vertex 布局变了：uv 不再位于偏移 24");
}
