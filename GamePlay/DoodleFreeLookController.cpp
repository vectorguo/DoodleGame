//
//  DoodleFreeLookController.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleFreeLookController.h"

#include <cmath>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/DoodleGameObject.h"
#include "Core/DoodleTransform.h"
#include "Platform/DoodleInput.h"
#include "Core/DoodleComponentConfig.h"

namespace Doodle
{
    void DoodleFreeLookController::Serialize(DoodleComponentConfig& config) const
    {
        //与 Deserialize 同一组 key；配置里保持角度制
        config.SetFloat("moveSpeed", m_moveSpeed);
        config.SetFloat("boostMultiplier", m_boostMultiplier);
        config.SetFloat("lookSensitivity", m_lookSensitivity);
        config.SetFloat("pitchLimit", m_pitchLimit);
    }

    void DoodleFreeLookController::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"moveSpeed", "boostMultiplier", "lookSensitivity", "pitchLimit"});
#endif

        m_moveSpeed = config.GetFloat("moveSpeed", 2.5f);
        m_boostMultiplier = config.GetFloat("boostMultiplier", 4.0f);
        m_lookSensitivity = config.GetFloat("lookSensitivity", 0.12f);
        m_pitchLimit = config.GetFloat("pitchLimit", 89.0f);

        if (!(m_moveSpeed > 0.0f))
        {
            throw std::runtime_error(config.GetPath() + ".moveSpeed: must be greater than 0");
        }

        if (!(m_boostMultiplier > 0.0f))
        {
            throw std::runtime_error(config.GetPath() +
                                     ".boostMultiplier: must be greater than 0");
        }

        if (!(m_lookSensitivity >= 0.0f))
        {
            throw std::runtime_error(config.GetPath() +
                                     ".lookSensitivity: must not be negative");
        }

        if (!(m_pitchLimit > 0.0f && m_pitchLimit < 90.0f))
        {
            throw std::runtime_error(config.GetPath() + ".pitchLimit: must be in (0, 90)");
        }
    }
    void DoodleFreeLookController::OnUpdate(const DoodleUpdateContext& context)
    {
        DoodleTransform& transform = GetGameObject().GetTransform();
        const float deltaSeconds = context.deltaSeconds;

        //环视：只在光标捕获（应用层按右键切换）且有位移时重建旋转。
        //没有位移时保持 Transform 原样，避免每帧把外部设置的旋转改掉
        if (DoodleInput::IsCursorCaptured())
        {
            const glm::vec2 mouseDelta = DoodleInput::GetMouseDelta();

            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
            {
                const glm::vec3 forward = transform.GetForward();

                //从当前姿态反解 yaw / pitch；自由相机没有滚转，这两个角足够
                const float yaw = std::atan2(forward.y, forward.x);
                const float pitch = std::asin(glm::clamp(forward.z, -1.0f, 1.0f));

                const float sensitivityRadians = glm::radians(m_lookSensitivity);
                const float pitchLimitRadians = glm::radians(m_pitchLimit);

                //屏幕 Y 向下：mouseDelta.y 为正表示鼠标向下，取负号后 pitch 增大
                //yaw 从 +X 逆时针起算：向右移动应减小 yaw
                const float newYaw = yaw - mouseDelta.x * sensitivityRadians;
                const float newPitch = glm::clamp(
                    pitch - mouseDelta.y * sensitivityRadians,
                    -pitchLimitRadians,
                    pitchLimitRadians);

                //与场景初始姿态同一套约定：先世界 +Z 的 yaw，再局部右轴 -Y 的 pitch
                transform.rotation =
                    glm::angleAxis(newYaw, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(newPitch, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        //移动：水平方向只看 yaw，抬头时按 W 不会飞起来；升降显式交给 Q/E
        const glm::vec3 forward = transform.GetForward();
        const float yaw = std::atan2(forward.y, forward.x);

        const glm::vec3 forwardOnPlane{std::cos(yaw), std::sin(yaw), 0.0f};
        const glm::vec3 rightOnPlane{std::sin(yaw), -std::cos(yaw), 0.0f};

        glm::vec3 direction{0.0f};

        if (DoodleInput::GetKey(DoodleKey::W))
        {
            direction += forwardOnPlane;
        }
        if (DoodleInput::GetKey(DoodleKey::S))
        {
            direction -= forwardOnPlane;
        }
        if (DoodleInput::GetKey(DoodleKey::D))
        {
            direction += rightOnPlane;
        }
        if (DoodleInput::GetKey(DoodleKey::A))
        {
            direction -= rightOnPlane;
        }
        if (DoodleInput::GetKey(DoodleKey::E))
        {
            direction.z += 1.0f;
        }
        if (DoodleInput::GetKey(DoodleKey::Q))
        {
            direction.z -= 1.0f;
        }

        //同时按两个方向时速度不应变成 √2 倍
        if (glm::dot(direction, direction) > 0.0f)
        {
            direction = glm::normalize(direction);
        }

        const float speed = m_moveSpeed *
                            (DoodleInput::GetKey(DoodleKey::LeftShift)
                                 ? m_boostMultiplier
                                 : 1.0f);

        transform.position += direction * speed * deltaSeconds;
    }
}
