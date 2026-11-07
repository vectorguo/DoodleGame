//
//  DoodleRenderState.h
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#pragma once

#include <cstddef>

#include "Utility/DoodleHash.h"

namespace Doodle
{
    /**
     * 剔除哪一面
     *
     * 哪一面算正面不在这里选：frontFace 和投影的 Y 翻转、顶点顺序绑在一起，
     * 是全局约定（逆时针为正面，见 DoodleGraphicsPipeline 里光栅化那段）
     */
    enum class ECullMode : std::uint8_t
    {
        /**
         * 两面都画：树叶、草这类单层面片
         */
        None = 0,

        /**
         * 剔除背面：封闭的不透明网格，默认值
         */
        Back = 1,

        /**
         * 剔除正面：从内部看的天空盒、反向外扩的描边
         */
        Front = 2,
    };

    /**
     * 比较方式：新片元的值与附件里已有的值怎么比，通过才继续
     *
     * 现在只有深度测试用它。取值与 Vulkan 的 VkCompareOp 一一对应，
     * 转换在 DoodleGraphicsPipeline 里
     */
    enum class ECompareOp : std::uint8_t
    {
        Never = 0,
        Less = 1,
        Equal = 2,
        LessOrEqual = 3,
        Greater = 4,
        NotEqual = 5,
        GreaterOrEqual = 6,
        Always = 7,
    };

    /**
     * 混合方式：片元颜色怎么和颜色附件里已有的颜色合成
     *
     * 只给常用的四种效果，不开放 Vulkan 的全部混合因子：材质作者选的是效果，
     * 不是一组因子。每种对应的因子与 Unity URP 的同名模式一致，见
     * DoodleGraphicsPipeline::MakeColorBlendAttachment
     */
    enum class EBlendMode : std::uint8_t
    {
        /**
         * 不混合，直接覆盖：不透明物体，默认值
         */
        Opaque = 0,

        /**
         * 按片元 alpha 与背景插值：玻璃、半透明特效
         */
        AlphaBlend = 1,

        /**
         * 按片元 alpha 加到背景上：火焰、光效粒子
         */
        Additive = 2,

        /**
         * 颜色已经预乘过 alpha：避免半透明边缘出现黑边
         */
        PremultipliedAlpha = 3,
    };

    /**
     * 图元拓扑：顶点按什么方式连成图元
     */
    enum class EPrimitiveTopology : std::uint8_t
    {
        /**
         * 每三个顶点一个三角形：所有网格，默认值
         */
        TriangleList = 0,

        /**
         * 每两个顶点一条线：调试绘制（包围盒、射线、网格线）
         */
        LineList = 1,
    };

    /**
     * 渲染状态：一条管线里由着色器决定的那部分固定功能状态
     *
     * 放在 Rendering：它描述「这个着色器怎么画」，是着色器资产的一部分
     * （由 Shaders/ 下的 *.shader.json 的 renderState 写出来），不碰任何 Vulkan 类型；
     * 翻译成 Vk* 结构是 DoodleGraphicsPipeline 的事。
     *
     * 全部烘焙进管线，不走 Vulkan 1.3 的扩展动态状态：MoltenVK 与 Android
     * 驱动上烘焙更稳，而且管线数只取决于「着色器 × 渲染状态 × pass」，
     * 与世界有多大无关 —— 组合数是有界的。视口与剪裁仍是动态状态（与窗口尺寸有关）。
     *
     * 刻意不放进来的三样：
     * - frontFace：全局约定，理由见 ECullMode；
     * - polygonMode：线框要开设备特性 fillModeNonSolid，很多 Android GPU 不支持；
     * - 模板测试：当前的深度格式不一定带模板分量，路线里也还没有用到模板的效果。
     *
     * 默认值就是第 1 步之前管线里写死的那一组：剔除背面、深度测试与写入都开、
     * LESS、不透明、不偏移、三角形列表。
     *
     * 它是管线缓存查找键的一部分，所以要能判等、能算哈希（见 GetHashValue）。
     * 判等用默认的逐成员比较：以后加字段时自动参与比较，
     * 不会出现「两份不同的状态被当成同一条管线」。两个浮点因子必须是有限值 ——
     * NaN 不等于自己，会让同一份状态每次都查不到；DoodleGraphicsPipeline
     * 建管线时会拦下来
     */
    struct DoodleRenderState
    {
        /**
         * 剔除哪一面
         */
        ECullMode cullMode = ECullMode::Back;

        /**
         * 是否做深度测试。pass 没有深度附件时整组深度设置都不生效
         */
        bool depthTestEnabled = true;

        /**
         * 测试通过后是否写回深度。半透明物体通常只测不写
         */
        bool depthWriteEnabled = true;

        /**
         * 深度比较方式。更小 = 更近，与清屏值 1.0（最远）配对，所以默认 LESS
         */
        ECompareOp depthCompareOp = ECompareOp::Less;

        /**
         * 混合方式；作用于 pass 的每一个颜色附件
         */
        EBlendMode blendMode = EBlendMode::Opaque;

        /**
         * 是否做深度偏移：贴花压在墙面上不闪、阴影投射避免自阴影粉刺
         */
        bool depthBiasEnabled = false;

        /**
         * 深度偏移的常量项，单位是深度格式的最小可分辨量
         */
        float depthBiasConstantFactor = 0.0f;

        /**
         * 深度偏移的斜率项：随多边形相对视线的倾斜程度放大
         */
        float depthBiasSlopeFactor = 0.0f;

        /**
         * 图元拓扑
         */
        EPrimitiveTopology topology = EPrimitiveTopology::TriangleList;

        [[nodiscard]] bool operator==(const DoodleRenderState&) const = default;

        /**
         * 这个状态的哈希值：逐成员混进同一个种子
         *
         * 与 operator== 必须同口径：判等的成员都要进哈希。漏一个只会多些碰撞，
         * 不会出错（命中后还有逐项比对）；反过来哈希多算一个判等不看的成员就错了。
         * 浮点走 DoodleHash::FloatToHashValue：+0 与 -0 的归一在那边（与 == 一致）
         *
         * 做成成员而不是 std::hash 特化：哈希与 == 挨在一起，加字段时两个都在眼前。
         * 代价是拿它当标准容器的键时要另补一个转发特化 —— 现在没人这么用
         */
        [[nodiscard]] std::size_t GetHashValue() const noexcept
        {
            std::size_t seed = 0;
            DoodleHash::Combine(seed, static_cast<std::size_t>(cullMode));
            DoodleHash::Combine(seed, static_cast<std::size_t>(depthTestEnabled));
            DoodleHash::Combine(seed, static_cast<std::size_t>(depthWriteEnabled));
            DoodleHash::Combine(seed, static_cast<std::size_t>(depthCompareOp));
            DoodleHash::Combine(seed, static_cast<std::size_t>(blendMode));
            DoodleHash::Combine(seed, static_cast<std::size_t>(depthBiasEnabled));
            DoodleHash::Combine(seed, DoodleHash::FloatToHashValue(depthBiasConstantFactor));
            DoodleHash::Combine(seed, DoodleHash::FloatToHashValue(depthBiasSlopeFactor));
            DoodleHash::Combine(seed, static_cast<std::size_t>(topology));
            return seed;
        }
    };
}
