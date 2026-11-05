//
//  DoodleMaterial.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <cmath>
#include <string_view>

#include <glm/vec4.hpp>

#include "Asset/DoodleAsset.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleTexture;

    /**
     * 材质资产：基色 + 一张可选纹理
     *
     * 由 DoodleMaterialLoader 从 Materials/ 下的 JSON 文件加载，资产管理器按路径缓存；
     * 不同对象引用同一个材质资产时共享同一份配置。renderer 不复制材质，
     * 只按槽保存非拥有指针。纹理也是非拥有指针，寿命由资产管理器保证。
     *
     * 材质本身不持有 GPU 资源：描述符集由渲染门面按纹理创建，材质只提供参数。
     * 加载完成后按只读使用；默认材质（没有配置材质的槽）由 renderer 自己持有一个实例。
     *
     * 颜色用 vec4 而不是 vec3：它要进 push constant，std430 下 vec3 占 16 字节，
     * 而 glm::vec3 只有 12 字节，会让后面的成员整体错位；第四个分量现在不参与
     * 混合，固定为 1。
     */
    class DoodleMaterial : public DoodleAsset
    {
    public:
        DoodleMaterial() = default;

        /**
         * 文件资产：路径交给 DoodleAsset 保存并校验非空
         */
        explicit DoodleMaterial(std::string_view assetPath)
            : DoodleAsset(assetPath)
        {
        }

        /**
         * 配置里的基色，按 sRGB 书写
         *
         * 着色器要的是线性值，推送前用 GetLinearBaseColor。这里保留作者写的
         * sRGB 值，序列化往返不会改变配置文件里的数字
         */
        [[nodiscard]] const glm::vec4& GetBaseColor() const
        {
            return m_baseColor;
        }

        /**
         * 转成线性空间的基色，供 push constant 使用
         *
         * 纹理按 SRGB 格式上传，采样结果已经是线性的；基色同样转成线性，
         * 两者相乘才在同一空间里。第四分量（透明度）不做色彩空间转换
         */
        [[nodiscard]] glm::vec4 GetLinearBaseColor() const
        {
            return {
                SRGBToLinear(m_baseColor.r),
                SRGBToLinear(m_baseColor.g),
                SRGBToLinear(m_baseColor.b),
                m_baseColor.a
            };
        }

        /**
         * 设置基色；运行时直接写成员，不做范围校验（与光源 setter 一致）
         */
        void SetBaseColor(const glm::vec4& baseColor)
        {
            m_baseColor = baseColor;
        }

        /**
         * 材质使用的纹理；nullptr 表示没有配置，绘制时绑定 1×1 白图
         *
         * 指针非拥有：纹理是资产管理器的资产，寿命覆盖材质
         * （场景先于资产管理器销毁）
         */
        [[nodiscard]] const DoodleTexture* GetTexture() const
        {
            return m_pTexture;
        }

        /**
         * 设置纹理；传 nullptr 表示回到默认白图
         */
        void SetTexture(const DoodleTexture* pTexture)
        {
            m_pTexture = pTexture;
        }

    private:
        /**
         * sRGB → 线性：IEC 61966-2-1 的精确传递函数，不是 pow(2.2)
         */
        [[nodiscard]] static float SRGBToLinear(const float value)
        {
            return value <= 0.04045f
                       ? value / 12.92f
                       : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

    private:
        /**
         * 配置里的基色，按 sRGB 书写；送着色器前经 GetLinearBaseColor 转换
         */
        glm::vec4 m_baseColor{0.82f, 0.82f, 0.82f, 1.0f};

        /**
         * 材质纹理，非拥有；nullptr 时绘制侧绑定默认白图
         */
        const DoodleTexture* m_pTexture = nullptr;
    };
}
