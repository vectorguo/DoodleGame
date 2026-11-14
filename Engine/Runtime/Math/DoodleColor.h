//
//  DoodleColor.h
//  Doodle
//
//  Created by 郭智 on 2026/11/14.
//

#pragma once

#include <cmath>
#include <type_traits>

#include <glm/vec4.hpp>

namespace Doodle
{
    /**
     * 颜色值：RGBA 四个分量，不携带色彩空间
     *
     * 与 Unity 的 Color 是同一个定位：类型只存数，sRGB 还是线性由调用点显式
     * 选择解释方式，转换见 SRGBToLinear / LinearToSRGB。不把「这是 sRGB」写进
     * 类型本身 —— 场景里同时存在两种解释：材质基色由作者按 sRGB 书写，光源
     * 颜色则是线性 HDR（分量可以超过 1）。类型一旦承诺一种，另一边的值就会
     * 被多余的转换转坏。
     *
     * 四个 float 占 16 字节，与 glm::vec4 同尺寸：进 push constant / UBO 的
     * 结构体时，后面成员不会像 glm::vec3（12 字节）那样整体错位。GPU 边界的
     * 字节契约仍写成 glm::vec4，用 ToVec4() 显式跨过去。
     *
     * 值类型：没有生命周期，不碰任何引擎状态。
     */
    struct DoodleColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        DoodleColor() = default;

        DoodleColor(const float red, const float green, const float blue, const float alpha = 1.0f)
            : r(red), g(green), b(blue), a(alpha)
        {
        }

        /**
         * 从配置 / JSON 读出的 glm::vec4 直接构造
         */
        DoodleColor(const glm::vec4& value)
            : r(value.r), g(value.g), b(value.b), a(value.a)
        {
        }

        /**
         * 转成 GPU 边界使用的 glm::vec4
         *
         * 显式写出：每处调用都在跨 CPU / GLSL 的字节契约，布局约束见
         * DoodleShaderData.h 里的 static_assert
         */
        [[nodiscard]] glm::vec4 ToVec4() const
        {
            return {r, g, b, a};
        }

        /**
         * sRGB → 线性：IEC 61966-2-1 的精确传递函数，不是 pow(2.2)
         *
         * alpha 不参与色彩空间转换，原样保留。调用方要先确认手里这份颜色
         * 确实按 sRGB 书写（材质基色是，光源颜色不是）。
         */
        [[nodiscard]] DoodleColor SRGBToLinear() const
        {
            return {
                ChannelSRGBToLinear(r),
                ChannelSRGBToLinear(g),
                ChannelSRGBToLinear(b),
                a
            };
        }

        /**
         * 线性 → sRGB，SRGBToLinear 的精确反变换
         *
         * 用于把线性值显示给人看（调色、调试输出）。超过 1 的分量会原样得到
         * 大于 1 的结果，不钳位 —— 显示端要不要夹取由它自己决定。
         */
        [[nodiscard]] DoodleColor LinearToSRGB() const
        {
            return {
                ChannelLinearToSRGB(r),
                ChannelLinearToSRGB(g),
                ChannelLinearToSRGB(b),
                a
            };
        }

        /**
         * 逐分量插值；t 不钳位，允许外插（与 DoodleMath::LerpAngle 一致）
         */
        [[nodiscard]] static DoodleColor Lerp(const DoodleColor& from, const DoodleColor& to, const float t)
        {
            return {
                from.r + (to.r - from.r) * t,
                from.g + (to.g - from.g) * t,
                from.b + (to.b - from.b) * t,
                from.a + (to.a - from.a) * t
            };
        }

        [[nodiscard]] DoodleColor operator+(const DoodleColor& other) const
        {
            return {r + other.r, g + other.g, b + other.b, a + other.a};
        }

        [[nodiscard]] DoodleColor operator-(const DoodleColor& other) const
        {
            return {r - other.r, g - other.g, b - other.b, a - other.a};
        }

        [[nodiscard]] DoodleColor operator*(const float scalar) const
        {
            return {r * scalar, g * scalar, b * scalar, a * scalar};
        }

        [[nodiscard]] bool operator==(const DoodleColor& other) const = default;

    private:
        /**
         * 单分量 sRGB → 线性；两个方向共用同一套 IEC 分段函数
         */
        [[nodiscard]] static float ChannelSRGBToLinear(const float value)
        {
            return value <= 0.04045f
                       ? value / 12.92f
                       : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        [[nodiscard]] static float ChannelLinearToSRGB(const float value)
        {
            return value <= 0.0031308f
                       ? value * 12.92f
                       : std::pow(value, 1.0f / 2.4f) * 1.055f - 0.055f;
        }
    };

    static_assert(sizeof(DoodleColor) == sizeof(glm::vec4),
                  "DoodleColor 必须是 4 个 float：进 push constant / UBO 时按 16 字节布局");
    static_assert(std::is_trivially_copyable_v<DoodleColor>,
                  "DoodleColor 是值类型，不能引入虚函数或非平凡拷贝");
}
