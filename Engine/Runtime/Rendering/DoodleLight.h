//
//  DoodleLight.h
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "Core/DoodleComponent.h"

namespace Doodle
{
    /**
     * 方向光组件：颜色与强度存在这里，方向从宿主 Transform 现取
     *
     * 方向光的「位置」没有意义：它照亮的是一整片平行光，换个位置画出来完全
     * 一样。所以本组件不存位置，也不存方向 —— 方向就是宿主 Transform 的
     * +X 前向（世界 Z-up 约定，见 DoodleTransform::GetForward）。旋转对象
     * 就等于旋转太阳，和其它组件改 Transform 就能影响渲染是同一条规则。
     *
     * 方向的语义是**光线传播方向**（从光源射向场景），不是「指向光源」。
     * 着色器算 Lambert 时取它的反方向：`dot(normal, -lightDirection)`。
     * 渲染门面写 UBO 时把这一点原样带过去，两边的约定必须一致。
     *
     * 主光源的规则与相机相同：第一个启用状态的光源是本帧的主光源，由组件
     * 自己在登记 / 销毁 / 启停时维护，读取走 GetMainLight()，不再每帧遍历场景。
     * 没有光源不是错误 —— 本帧只剩环境项，画面偏暗但不崩，和「空绘制列表合法」
     * 是同一种取舍。
     *
     * 字段只有颜色和强度。环境光强度、多光源、阴影都还没有；等到需要时再加，
     * 不在第一版里留下没人读的开关。
     *
     * 组件不能脱离宿主使用：GetDirection 读 m_pGameObject；直接构造出来的
     * 实例没有宿主，只能用于 Debug 校验探针里的 Deserialize。
     */
    class DoodleLight final : public DoodleComponent
    {
    public:
        /**
         * 登记进光源注册表，并重算主光源
         *
         * 与相机同构：登记放在 OnAwake 而不是 OnEnable，停用的光源也留在
         * 注册表里，挑选时按 IsEnabled 过滤
         */
        void OnAwake() override;

        /**
         * 启用状态变了，主光源候选集合跟着变，重算
         */
        void OnEnable() override;

        /**
         * 停用后不再是候选，重算
         */
        void OnDisable() override;

        /**
         * 从注册表摘除自己并重算
         *
         * 同相机：不使用析构函数 —— Debug 校验探针是不会走 OnDestroy 的
         * 无宿主临时实例
         */
        void OnDestroy() override;

        /**
         * 当前主光源；没有启用的光源时返回 nullptr
         *
         * O(1) 读取，不遍历场景。返回值非拥有，只能当场使用，不要缓存成
         * 跨帧的成员 —— 光源可能被停用或销毁
         */
        [[nodiscard]] static const DoodleLight* GetMainLight()
        {
            return s_pMainLight;
        }

        /**
         * 把颜色与强度写回序列化配置
         *
         * 与 Deserialize 使用同一组 key；方向不在其中，它随 Transform 走。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从序列化配置读取颜色与强度
         *
         * 字段：color（3 个非负数字，默认白色）、intensity（非负数，默认 1）。
         * 不依赖宿主，Debug 校验探针也可以调用。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        /**
         * 光线传播方向：宿主 Transform 的 +X 前向，世界空间单位向量
         *
         * 宿主由 AddComponent 保证非空；脱离宿主调用是错误用法。
         * 零旋转时方向是 (1, 0, 0)。
         */
        [[nodiscard]] glm::vec3 GetDirection() const;

        /**
         * 光源颜色，各分量非负；可以超过 1（HDR）
         */
        [[nodiscard]] const glm::vec3& GetColor() const
        {
            return m_color;
        }

        /**
         * 设置颜色；运行时直接写成员，不做范围校验（与相机 setter 一致）
         */
        void SetColor(const glm::vec3& color)
        {
            m_color = color;
        }

        /**
         * 光照强度，非负
         */
        [[nodiscard]] float GetIntensity() const
        {
            return m_intensity;
        }

        /**
         * 设置强度；运行时直接写成员，不做范围校验
         */
        void SetIntensity(const float intensity)
        {
            m_intensity = intensity;
        }

    private:
        /**
         * 光源颜色，默认白色
         */
        glm::vec3 m_color{1.0f};

        /**
         * 光照强度，默认 1
         */
        float m_intensity = 1.0f;

        /**
         * 重算主光源
         *
         * 只在光源登记、销毁或启用状态变化时调用；每帧读取的是 s_pMainLight
         */
        static void RefreshMainLight();

        /**
         * 所有存活的光源组件，按登记（创建）顺序
         *
         * 由 OnAwake 登记、OnDestroy 摘除；停用的也留在其中，挑选时过滤。
         * 只在主线程使用，不做同步
         */
        static std::vector<DoodleLight*> s_lights;

        /**
         * 当前主光源，由 s_lights 重算得出；没有启用的光源时为空
         */
        static const DoodleLight* s_pMainLight;
    };
}
