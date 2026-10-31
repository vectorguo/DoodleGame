//
//  DoodleMaterial.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <glm/vec4.hpp>

namespace Doodle
{
    /**
     * 一个材质实例的 CPU 侧配置：当前只有基色
     *
     * 它还不是资产：没有 loader / 缓存键，也不持有 Vulkan 资源。现在由
     * DoodleMeshRenderer 按自己的序列化配置创建并持有，一个材质槽一个实例；
     * 同一份网格被多个对象使用时，各自的 renderer 可以有不同材质。
     *
     * 做成 class 而不是 struct：后续会在这里长出纹理、采样器、双面、透明度等
     * 行为，setter / getter 留出校验与转换的位置，调用点不必跟着成员布局改。
     * 真正的材质资产化（共享、引用计数、显存资源）是后面的事，届时它的所有权
     * 会从 renderer 移到资产管理器，而这里的方法表面不用推翻。
     *
     * 颜色用 vec4 而不是 vec3：它要进 push constant，std430 下 vec3 占 16 字节，
     * 而 glm::vec3 只有 12 字节，会让后面的成员整体错位；第四个分量现在不参与
     * 混合，固定为 1。
     */
    class DoodleMaterial
    {
    public:
        DoodleMaterial() = default;

        /**
         * 基色；着色器把它当线性值直接乘进环境项与漫反射项
         *
         * 当前不做 sRGB → linear 转换：源文件 / 配置里怎么存就怎么用，
         * 等贴图与材质系统落地时统一处理色彩空间
         */
        [[nodiscard]] const glm::vec4& GetBaseColor() const
        {
            return m_baseColor;
        }

        /**
         * 设置基色；运行时直接写成员，不做范围校验（与光源 setter 一致）
         */
        void SetBaseColor(const glm::vec4& baseColor)
        {
            m_baseColor = baseColor;
        }

    private:
        /**
         * 默认 0.82 灰：renderer 没配材质时保持加材质前的外观
         */
        glm::vec4 m_baseColor{0.82f, 0.82f, 0.82f, 1.0f};
    };
}
