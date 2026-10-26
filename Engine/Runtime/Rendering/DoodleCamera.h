//
//  DoodleCamera.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "Core/DoodleComponent.h"

namespace Doodle
{
    /**
     * 相机的投影方式
     *
     * 场景数据（DoodleSceneData）也用这个类型描述相机配置，所以它跟着
     * DoodleCamera 走，不单独成头文件
     */
    enum class ECameraProjectionType : std::uint8_t
    {
        /**
         * 透视投影：近大远小，由垂直 FOV 决定可见范围
         */
        Perspective,

        /**
         * 平行投影（正交投影）：没有近大远小，由正交半高决定可见范围
         */
        Orthographic,
    };

    /**
     * 相机组件：只存投影参数，位姿从宿主 Transform 现算
     *
     * 职责压缩成两件事：
     * - 观察：从宿主 Transform 的 position / forward / up 生成 view；
     * - 投影：按调用方给的宽高比生成 projection，并包含 Vulkan 的 Y 翻转。
     *   支持透视（由垂直 FOV 决定）与平行投影（由正交半高决定）两种方式。
     *
     * 不存 position、yaw/pitch，也不存窗口尺寸或输入状态。位姿的唯一来源是宿主
     * 的 DoodleTransform：任何改 Transform 的代码（行为组件、动画、脚本）都会
     * 自动反映到画面上，相机不需要知道是谁在驱动它。
     *
     * 组件不能脱离宿主使用：view 依赖宿主 Transform（m_pGameObject）。相机只能
     * 通过 GameObject::AddComponent<DoodleCamera>() 创建；直接构造后指望算 view
     * 会拿到空宿主，是错误用法。
     *
     * 交换链预旋转不在这里。投影只负责「显示方向 + Vulkan 裁剪空间」，
     * 预旋转是把算好的画面转到交换链图像朝向的最后一步，由渲染门面左乘
     * （见 DoodleVulkanManager::UpdateUniformBuffer）—— 两者顺序不能换。
     */
    class DoodleCamera final : public DoodleComponent
    {
    public:
        /**
         * 从宿主 Transform 生成 view 矩阵
         *
         * 用 Transform 自己的 up，而不是世界 +Z：相机挂在一个滚转的对象上时
         * 画面跟着滚，这是「相机只读载体姿态」的预期行为。自由相机的
         * 无滚转由控制它的行为组件负责，不在这里做。
         */
        [[nodiscard]] glm::mat4 GetViewMatrix() const;

        /**
         * 按显示方向的宽高比生成投影矩阵
         *
         * 只做投影 + Vulkan 的 Y 翻转，不含交换链预旋转。宽高比必须由调用方
         * 按**显示方向**算好（交换链 90/270 时与图像宽高的关系是对调的），
         * 见 DoodleVulkanUtils::GetDisplayAspect。
         *
         * 投影方式由 GetProjectionType 决定：
         * - Perspective：用垂直 FOV 生成透视投影；
         * - Orthographic：用正交半高生成平行投影，宽度按 aspect 展开，
         *   没有近大远小。
         *
         * @param aspect 显示方向的 width / height
         */
        [[nodiscard]] glm::mat4 GetProjectionMatrix(const float aspect) const;

        /**
         * 当前投影方式
         */
        [[nodiscard]] ECameraProjectionType GetProjectionType() const
        {
            return m_projectionType;
        }

        /**
         * 切换投影方式；切换只影响下一帧的投影矩阵，不改位姿
         */
        void SetProjectionType(const ECameraProjectionType type)
        {
            m_projectionType = type;
        }

        /**
         * 垂直视场角，单位是弧度
         *
         * 只在 Perspective 下参与投影；必须为 (0, π)
         */
        void SetFovY(const float fovY)
        {
            m_fovY = fovY;
        }

        /**
         * 垂直视场角（弧度）
         */
        [[nodiscard]] float GetFovY() const
        {
            return m_fovY;
        }

        /**
         * 近平面，必须为正数；透视与平行投影都用它做裁剪
         */
        void SetNearPlane(const float nearPlane)
        {
            m_near = nearPlane;
        }

        /**
         * 近平面
         */
        [[nodiscard]] float GetNearPlane() const
        {
            return m_near;
        }

        /**
         * 远平面，必须大于近平面；透视与平行投影都用它做裁剪
         */
        void SetFarPlane(const float farPlane)
        {
            m_far = farPlane;
        }

        /**
         * 远平面
         */
        [[nodiscard]] float GetFarPlane() const
        {
            return m_far;
        }

        /**
         * 正交投影的可见高度是 2 × halfHeight，单位与世界坐标一致
         *
         * 只在 Orthographic 下参与投影；必须为正数
         */
        void SetOrthoHalfHeight(const float halfHeight)
        {
            m_orthoHalfHeight = halfHeight;
        }

        /**
         * 正交投影的半高
         */
        [[nodiscard]] float GetOrthoHalfHeight() const
        {
            return m_orthoHalfHeight;
        }

    private:
        /**
         * 投影方式，默认透视
         */
        ECameraProjectionType m_projectionType = ECameraProjectionType::Perspective;

        /**
         * 垂直视场角（弧度）。GLM::perspective 收的是垂直角；只在透视下使用
         */
        float m_fovY = glm::radians(45.0f);

        /**
         * 正交投影的半高（可见高度 = 2 × 它），单位与世界坐标一致
         *
         * 2.0 大致把原点附近的立方体框进来；需要更精确的取景时用
         * SetOrthoHalfHeight 调整
         */
        float m_orthoHalfHeight = 2.0f;

        /**
         * 近平面
         */
        float m_near = 0.1f;

        /**
         * 远平面
         *
         * 从 A3 的 10 放宽到 100：自由相机飞远后立方体不被裁掉
         */
        float m_far = 100.0f;
    };
}
