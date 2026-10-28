//
//  DoodleVirtualCamera.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <string>

#include "Core/DoodleComponent.h"
#include "DoodleCameraDirectorTypes.h"

namespace Doodle
{
    class DoodleComponentConfig;
    class DoodleGameObject;

    /**
     * 虚拟相机：描述“应该怎么看”，不直接拥有渲染相机
     *
     * 对应 CinemachineVirtualCamera 红框里的基础字段：
     * Priority / Follow / Look At / Standby Update / Lens。
     *
     * 它挂在场景对象上，由 DoodleCameraDirector（Brain）挑选并求值；
     * 真正的 DoodleCamera 在另一个对象上，由 Brain 把结果写过去。
     *
     * Follow / LookAt 在场景文件里用对象名字引用，OnAwake 时解析。
     * 名字不唯一，取第一个匹配；稳定对象 ID 以后再补。
     */
    class DoodleVirtualCamera final : public DoodleComponent
    {
    public:
        /**
         * 把基础字段写回配置
         *
         * follow / lookAt 写的是场景里的对象名字，不是 OnAwake 解析后的指针；
         * standbyUpdate 存整数（0 = Always，1 = Never，2 = RoundRobin）；
         * fov 用度，near / far 是相机距离。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从配置读取 Priority / Follow / LookAt / StandbyUpdate / Lens
         *
         * 只记录名字和数值；对象引用在 OnAwake 里解析。
         * standbyUpdate 接受 0 / 1 / 2。
         */
        void Deserialize(const DoodleComponentConfig& config) override;
        void OnAwake() override;

        [[nodiscard]] int GetPriority() const
        {
            return m_priority;
        }

        [[nodiscard]] const DoodleGameObject* GetFollowTarget() const
        {
            return m_pFollow;
        }

        [[nodiscard]] const DoodleGameObject* GetLookAtTarget() const
        {
            return m_pLookAt;
        }

        [[nodiscard]] ECameraStandbyUpdate GetStandbyUpdate() const
        {
            return m_standbyUpdate;
        }

        [[nodiscard]] float GetFovYDegrees() const
        {
            return m_fovYDegrees;
        }

        [[nodiscard]] float GetNearPlane() const
        {
            return m_nearPlane;
        }

        [[nodiscard]] float GetFarPlane() const
        {
            return m_farPlane;
        }

        [[nodiscard]] float GetDutchDegrees() const
        {
            return m_dutchDegrees;
        }

    private:
        int m_priority = 0;

        /**
         * 场景文件里的对象名；空表示没有目标
         */
        std::string m_followName;
        std::string m_lookAtName;

        ECameraStandbyUpdate m_standbyUpdate = ECameraStandbyUpdate::RoundRobin;

        float m_fovYDegrees = 60.0f;
        float m_nearPlane = 0.3f;
        float m_farPlane = 2500.0f;
        float m_dutchDegrees = 0.0f;

        /**
         * OnAwake 解析出来的非拥有引用
         */
        DoodleGameObject* m_pFollow = nullptr;
        DoodleGameObject* m_pLookAt = nullptr;
    };
}
