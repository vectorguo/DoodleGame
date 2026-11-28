//
//  DoodleMaterial.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include "Utility/DoodleStd.h"

#include "Asset/DoodleAsset.h"
#include "Math/DoodleColor.h"

namespace Doodle
{
    //只以指针出现，完整定义留给使用它的源文件
    class DoodleShader;
    class DoodleTexture;

    /**
     * 材质资产：基色 + 按贴图槽配的贴图 + 着色器
     *
     * 由 DoodleMaterialLoader 从 Materials/ 下的 JSON 文件加载，资产管理器按路径缓存；
     * 不同对象引用同一个材质资产时共享同一份配置。renderer 不复制材质，
     * 只按槽保存非拥有指针。贴图与着色器都是非拥有指针，寿命由资产管理器保证。
     *
     * 贴图按槽名配：槽是着色器 set 1 里声明的贴图（DoodleShader::GetMaterialTextureSlots），
     * 名字就是 GLSL 里的变量名。着色器声明了几张，材质就能配几张；没配的槽绘制时
     * 绑定 1×1 白图。
     *
     * 材质本身不持有 GPU 资源：描述符集由描述符管理器按材质创建（布局来自材质的
     * 着色器），管线由各个渲染 pass 的管线表持有。材质不记录管线 —— 管线是
     * 「着色器 × pass」的产物，录制时由当前 pass 按材质着色器现取；
     * 一个材质有几条管线由 pass 的数量决定，不由材质决定。加载完成后按只读使用：
     * 描述符管理器按材质缓存 set 1，第一次画过之后再改贴图不会生效。
     * renderer 的槽可以没有材质，没有材质的子网格不画，引擎不补一份默认材质。
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
         * 某个贴图槽配的贴图；nullptr 表示没有配置，绘制时绑定 1×1 白图
         *
         * 指针非拥有：贴图是资产管理器的资产，寿命覆盖材质
         * （场景先于资产管理器销毁）
         *
         * @param slotName 着色器 set 1 里贴图的变量名，如 "albedoTexture"
         */
        [[nodiscard]] const DoodleTexture* GetTexture(const std::string_view slotName) const
        {
            const auto it = m_textures.find(slotName);
            return it != m_textures.end() ? it->second : nullptr;
        }

        /**
         * 给一个贴图槽配贴图；传 nullptr 表示回到默认白图
         *
         * 只在加载时用：描述符管理器按材质缓存 set 1，第一次画过之后再改不会生效。
         * 槽名是否属于着色器由加载器核对，这里不查
         */
        void SetTexture(const std::string_view slotName, const DoodleTexture* pTexture)
        {
            m_textures.insert_or_assign(std::string(slotName), pTexture);
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
         * 贴图槽名 → 贴图，非拥有；不在表里（或为 nullptr）的槽绘制时绑定默认白图。
         * std::less<> 让 string_view 直接查表，不必先拼一个 std::string
         */
        std::map<std::string, const DoodleTexture*, std::less<>> m_textures;
    };
}
