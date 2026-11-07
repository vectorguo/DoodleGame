//
//  DoodleVertex.h
//  Doodle
//
//  Created by 郭智 on 2026/10/11.
//
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * 顶点格式：逐顶点数据流的布局，管线按它决定从顶点缓冲里怎么读数据
     *
     * 现在只有 PosNormalUv（下面的 PosNormalUvVertex）。大世界一定还会有别的
     * 格式 —— 蒙皮网格要骨骼索引与权重、栅格地形可能是另一套属性。
     * 每加一种是三处：一个枚举值、一个顶点结构体、DoodleVertexInput::Describe
     * 里的一个分支。管线缓存把它当查找键的一维：同一份着色器配不同顶点格式是两条管线
     *
     * 它只管逐顶点的数据流。逐实例的数据流是正交的另一轴，见 EInstanceFormat ——
     * 草叶的顶点数据就是标准顶点，多出来的是每实例一条数据流，不归这里管
     *
     * 每个顶点结构体都带一个 kVertexFormat，把「类型」和「枚举值」绑在一起：
     * DoodleMesh::Initialize 靠它给网格打格式标签，也靠它把随便一个类型
     * 挡在编译期。枚举值定义在这里而不是结构体后面，是因为结构体的静态成员
     * 要用它
     */
    enum class EVertexFormat : std::uint8_t
    {
        /**
         * 位置 + 法线 + 第一套 UV，一条交织的数据流，即 PosNormalUvVertex
         */
        PosNormalUv = 0,
    };

    /**
     * 实例格式：逐实例数据流的布局，与 EVertexFormat 正交
     *
     * 顶点格式描述「一个顶点长什么样」，它描述「一个实例长什么样」。
     * 分开枚举而不是把「实例化的某某」塞进 EVertexFormat：否则每加一种顶点
     * 格式都要再乘一遍实例化版本，Describe 里的顶点属性块还要每个分支抄一遍
     *
     * 现在只有 None —— 还没做实例化。第一种实例布局（草的变换、地形块的
     * 原点 / 尺寸 / LOD）到来时，是同样三处：一个枚举值、一个逐实例结构体、
     * Describe 里产出 binding 1 的那一段
     */
    enum class EInstanceFormat : std::uint8_t
    {
        /**
         * 没有逐实例数据流：一个网格画一份，binding 1 不存在
         */
        None = 0,
    };

    /**
     * 标准顶点：位置 + 法线 + 第一套 UV
     *
     * 名字按属性集取，与枚举值 EVertexFormat::PosNormalUv 一一对应 ——
     * 类型名说的是「里面有哪些属性」，不是「谁在用它」。以后加蒙皮
     * （骨骼索引与权重）、地形栅格时，各自的结构体按同一条规则命名
     *
     * 放在 Rendering：它是网格资产的数据格式。文件后半那份 VkVertexInput*
     * 描述（DoodleVertexInput::Describe）是随类型走的管线契约，和字段写在同一处，
     * 改字段时不会漏改描述。
     *
     * 布局是交织的（interleaved）—— 同一个顶点的属性在内存里挨着，顶点依次排下去，
     * 一条绑定的 stride 就是整个结构体：
     *
     *     [pos0][normal0][uv0] [pos1][normal1][uv1] [pos2][normal2][uv2]
     *      ↑0    ↑12      ↑24
     *      └──────────── sizeof(PosNormalUvVertex) = 32 字节 ────────────┘
     *
     * 法线在模型空间（资产空间）：DoodleFbxImporter 把节点世界变换烘焙进位置时，
     * 也把它的逆转置作用在法线上，两者因此处在同一个空间。渲染时顶点着色器
     * 再用 GameObject 的 localToWorld 逆转置把法线变到世界空间 —— 三步用的是同一条
     * 「位置用矩阵、法线用矩阵的逆转置」规则。
     *
     * UV 只保留源文件的第一套：顶点着色器把它原样传给片元，片元用它采样
     * 材质贴图。源文件有多套 UV 时，其余几套暂时丢弃。
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
     * 所以结构体后面有三行 static_assert。它们不是文档，是编译期的守门人：
     * 那条假设一旦破了，构建当场失败
     *
     * 管线的输入描述不在管线层写死成具体类型，而是按顶点格式与实例格式
     * （EVertexFormat / EInstanceFormat）由 DoodleVertexInput::Describe 给出，
     * 写在本文件后半：往这里加一个字段，就必须同步加一行属性描述、并可能改
     * stride，两者在同一个文件里，改一处时另一处就在眼前
     */
    struct PosNormalUvVertex
    {
        /**
         * 本结构体对应的顶点格式
         *
         * 静态成员：不占结构体空间、不进 sizeof 与 offsetof。
         * DoodleMesh::Initialize 用它给网格打格式标签，同时把「没声明这个成员
         * 的类型」挡在编译期 —— 误传一个 vector<uint32_t> 会直接编译失败
         */
        static constexpr EVertexFormat kVertexFormat = EVertexFormat::PosNormalUv;

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
         * 顶点着色器把它传给片元，片元用它采样材质纹理。
         * FBX 源文件有多套 UV 时只保留第一套。
         */
        glm::vec2 uv;
    };

    //上面那段「依赖 glm vec3 = 12 字节、vec2 = 8 字节」的假设，在这里变成编译期检查。
    //
    //为什么值得写：这些一旦悄悄变了，症状是模型整体错位或法线方向全错，
    //校验层查不到（它不知道你把什么字节当成了顶点），而着色器那边也查不到
    //（它按 location 读，读到的就是错的数据）。把失败挪到编译期成本最低
    static_assert(sizeof(PosNormalUvVertex) == 32, "PosNormalUvVertex 布局变了：glm::vec3 / vec2 不再是 12 / 8 字节（是不是定义了 GLM_FORCE_ALIGNED_GENTYPES 之类？）");
    static_assert(offsetof(PosNormalUvVertex, normal) == 12, "PosNormalUvVertex 布局变了：normal 不再位于偏移 12");
    static_assert(offsetof(PosNormalUvVertex, uv) == 24, "PosNormalUvVertex 布局变了：uv 不再位于偏移 24");

    /**
     * 一种（顶点格式 × 实例格式）在管线里的完整输入描述：有哪几条绑定（数据流），
     * 每个属性从哪条流、哪个偏移、按什么格式取
     *
     * 两份数组由同一个函数一起给出：绑定编号是两者之间唯一的对接口径，
     * 分开给就可能出现「属性指向一条不存在的绑定」。用 vector 不用定长数组：
     * 不同格式的绑定数与属性数不同（实例化是两条流，蒙皮多两个属性），
     * 而这份描述只在建管线时取一次
     *
     * 两条编号约定，两轴共用：
     * - binding 0 = 逐顶点的流，binding 1 = 逐实例的流；没有实例数据时后者不出现
     * - 属性 location 0–7 留给顶点，8 起留给实例。Vulkan 的 location 是顶点流
     *   与实例流共用的一个平面，固定分界后，以后给顶点加切线 / 顶点色也不会
     *   把实例属性的编号挤走
     */
    struct DoodleVertexInput
    {
        /**
         * 绑定描述：每条数据流有多宽、按什么节奏前进
         */
        std::vector<VkVertexInputBindingDescription> bindings;

        /**
         * 属性描述：从数据流的每一条记录里切出哪些属性
         */
        std::vector<VkVertexInputAttributeDescription> attributes;

        /**
         * 给出一种（顶点格式 × 实例格式）的输入描述
         *
         * 两个参数各管一条数据流：vertexFormat 产出 binding 0，
         * instanceFormat 产出 binding 1（None 就不产出）
         *
         * 按值返回：VkPipelineVertexInputStateCreateInfo 只记数组的地址、不拷贝内容，
         * 调用方要让返回值活到 vkCreateGraphicsPipelines 返回为止 —— 存成创建函数里的
         * 局部量正好满足（这也是为什么不能返回临时对象的引用：出了函数就悬空了）
         *
         * @throw std::invalid_argument 不认识的格式（只会出现在强转出来的非法枚举值上）
         */
        [[nodiscard]] static DoodleVertexInput Describe(const EVertexFormat vertexFormat, const EInstanceFormat instanceFormat)
        {
            //两个轴分开取：binding 0 由顶点格式给出，binding 1 由实例格式给出。
            //先攒进同一个返回值，再一起交给管线
            DoodleVertexInput input;

            switch (vertexFormat)
            {
            case EVertexFormat::PosNormalUv:
                input.bindings =
                {
                    {
                        //本结构体的全部属性打包在同一个数组里，所以只需要一条绑定，编号取 0。
                        //这是个约定编号：下面属性描述里的 binding 必须指回同一个数字
                        .binding = 0,
                        //写 sizeof(PosNormalUvVertex) 而不是 32：前者是「关于结构体的事实」，往结构体里
                        //加字段时它自动跟上；后者是一份「关于结构体的记忆」，会悄悄失效。
                        //这条规矩在下面的 offset 上还要再贯彻一次
                        .stride = sizeof(PosNormalUvVertex),
                        //每个顶点之后前进到下一条。顶点流里没有逐实例的数据
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                    }
                };
                //这里的 location 必须与着色器里 layout(location = N) in 的编号对上 ——
                //变量名可以完全不同，编号是两边唯一的对接口径。
                //
                //格式的通道数要与着色器分量的个数匹配：通道多了会被静默丢弃，
                //少了则补 (0, 0, 1)。这一处配错了不会报错，只会画出错误的明暗或贴图
                input.attributes =
                {
                    //位置：location 0，三个 32 位浮点分量（vec3）
                    {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        //偏移用宏算，理由同上面的 sizeof：字段挪动位置时它自动跟上
                        .offset = offsetof(PosNormalUvVertex, pos)
                    },
                    //法线：location 1，三个 32 位浮点分量（vec3）
                    {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(PosNormalUvVertex, normal)
                    },
                    //UV：location 2，两个 32 位浮点分量（vec2）
                    {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(PosNormalUvVertex, uv)
                    }
                };
                break;
            }

            //实例轴：现在只有 None，不产出 binding 1。
            //加第一种实例布局时，在这里追加 binding = 1、
            //inputRate = VK_VERTEX_INPUT_RATE_INSTANCE 的条目，
            //属性 location 从 8 起（见上面的编号约定）
            if (instanceFormat != EInstanceFormat::None)
            {
                throw std::invalid_argument("unknown instance format: " + std::to_string(static_cast<int>(instanceFormat)));
            }

            //switch 故意不写 default：以后加枚举值却漏了分支时，-Wswitch 还有机会开口。
            //现在走到这里的只可能是强转出来的非法值
            if (input.bindings.empty())
            {
                throw std::invalid_argument("unknown vertex format: " + std::to_string(static_cast<int>(vertexFormat)));
            }

            return input;
        }
    };
}
