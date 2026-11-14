//
//  DoodleMaterial.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <string_view>

#include "Asset/DoodleAsset.h"
#include "Math/DoodleColor.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleShader;
    class DoodleTexture;

    /**
     * 材质资产：基色 + 一张可选纹理 + 着色器
     *
     * 由 DoodleMaterialLoader 从 Materials/ 下的 JSON 文件加载，资产管理器按路径缓存；
     * 不同对象引用同一个材质资产时共享同一份配置。renderer 不复制材质，
     * 只按槽保存非拥有指针。纹理与着色器都是非拥有指针，寿命由资产管理器保证。
     *
     * 材质本身不持有 GPU 资源：描述符集由描述符管理器按纹理创建，管线由各个渲染
     * pass 的管线表持有。材质不记录管线 —— 管线是「着色器 × 渲染状态 × pass」的
     * 产物，录制时由当前 pass 按材质着色器现取；一个材质有几条管线由 pass 的数量
     * 决定，不由材质决定。加载完成后按只读使用；renderer 的槽可以没有材质，
     * 没有材质的子网格不画，引擎不补一份默认材质。
     *
     * 基色用 DoodleColor：4 个 float、16 字节，进 push constant 时与 glm::vec4
     * 同布局；第四个分量现在不参与混合，固定为 1。
     */
    class DoodleMaterial : public DoodleAsset
    {
    public:
        DoodleMaterial() = default;

        /**
         * 文件资产：路径交给 DoodleAsset 保存并校验非空
         */
        explicit DoodleMaterial(const std::string_view assetPath)
            : DoodleAsset(assetPath)
        {
        }

        /**
         * 材质的着色器资产；非拥有，寿命由资产管理器保证
         */
        [[nodiscard]] const DoodleShader* GetShader() const
        {
            return m_pShader;
        }

        /**
         * 设置着色器；加载器在 Create 里设置，运行时一般不改
         */
        void SetShader(const DoodleShader* pShader)
        {
            m_pShader = pShader;
        }

        /**
         * 配置里的基色，按 sRGB 书写
         *
         * 着色器要的是线性值，推送前由调用侧转（基色本身不持有色彩空间，
         * 见 DoodleColor）。这里保留作者写的 sRGB 值，序列化往返不会改变
         * 配置文件里的数字
         */
        [[nodiscard]] const DoodleColor& GetBaseColor() const
        {
            return m_baseColor;
        }

        /**
         * 设置基色；运行时直接写成员，不做范围校验（与光源 setter 一致）
         */
        void SetBaseColor(const DoodleColor& baseColor)
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
         * 材质着色器，非拥有；由加载器设置
         */
        const DoodleShader* m_pShader = nullptr;

        /**
         * 配置里的基色，按 sRGB 书写；送着色器前由调用侧转成线性
         */
        DoodleColor m_baseColor{0.82f, 0.82f, 0.82f, 1.0f};

        /**
         * 材质纹理，非拥有；nullptr 时绘制侧绑定默认白图
         */
        const DoodleTexture* m_pTexture = nullptr;
    };
}
