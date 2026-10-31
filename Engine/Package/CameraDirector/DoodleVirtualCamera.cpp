//
//  DoodleVirtualCamera.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleVirtualCamera.h"

#include <stdexcept>

#include "Core/DoodleGameObject.h"
#include "Core/DoodleComponentConfig.h"
#include "Scene/DoodleScene.h"

namespace Doodle
{
    void DoodleVirtualCamera::Serialize(DoodleComponentConfig& config) const
    {
        //follow / lookAt 写名字而不是解析后的指针；standbyUpdate 存枚举整数
        config.SetInt("priority", m_priority);
        config.SetString("follow", m_followName);
        config.SetString("lookAt", m_lookAtName);
        config.SetInt("standbyUpdate", static_cast<int>(m_standbyUpdate));
        config.SetFloat("fov", m_fovYDegrees);
        config.SetFloat("near", m_nearPlane);
        config.SetFloat("far", m_farPlane);
        config.SetFloat("dutch", m_dutchDegrees);
    }

    void DoodleVirtualCamera::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys(
            {"priority", "follow", "lookAt", "standbyUpdate", "fov", "near", "far", "dutch"});
#endif

        m_priority = config.GetInt("priority", 0);
        m_followName = config.GetString("follow", "");
        m_lookAtName = config.GetString("lookAt", "");

        const int standbyUpdate = config.GetInt(
            "standbyUpdate", static_cast<int>(ECameraStandbyUpdate::RoundRobin));
        if (standbyUpdate == static_cast<int>(ECameraStandbyUpdate::Always))
        {
            m_standbyUpdate = ECameraStandbyUpdate::Always;
        }
        else if (standbyUpdate == static_cast<int>(ECameraStandbyUpdate::Never))
        {
            m_standbyUpdate = ECameraStandbyUpdate::Never;
        }
        else if (standbyUpdate == static_cast<int>(ECameraStandbyUpdate::RoundRobin))
        {
            m_standbyUpdate = ECameraStandbyUpdate::RoundRobin;
        }
        else
        {
            throw std::runtime_error("standbyUpdate: expected 0 (always), 1 (never), "
                                     "or 2 (roundRobin)");
        }

        m_fovYDegrees = config.GetFloat("fov", 60.0f);
        m_nearPlane = config.GetFloat("near", 0.3f);
        m_farPlane = config.GetFloat("far", 2500.0f);
        m_dutchDegrees = config.GetFloat("dutch", 0.0f);

        if (!(m_fovYDegrees > 0.0f && m_fovYDegrees < 180.0f))
        {
            throw std::runtime_error("fov: must be in (0, 180)");
        }

        if (!(m_nearPlane > 0.0f))
        {
            throw std::runtime_error("near: must be greater than 0");
        }

        if (!(m_farPlane > m_nearPlane))
        {
            throw std::runtime_error("far: must be greater than near");
        }
    }
    void DoodleVirtualCamera::OnAwake()
    {
        // Deserialize 只记录名字；这里所有 GameObject 都已经建好，按名字解析引用。
        // 名字写错属于内容错误，在启动阶段直接报出来
        const DoodleScene& scene = GetGameObject().GetScene();

        if (!m_followName.empty())
        {
            m_pFollow = scene.FindGameObject(m_followName);
            if (m_pFollow == nullptr)
            {
                throw std::runtime_error("DoodleVirtualCamera: follow target '" +
                                         m_followName + "' was not found");
            }
        }

        if (!m_lookAtName.empty())
        {
            m_pLookAt = scene.FindGameObject(m_lookAtName);
            if (m_pLookAt == nullptr)
            {
                throw std::runtime_error("DoodleVirtualCamera: lookAt target '" +
                                         m_lookAtName + "' was not found");
            }
        }
    }
}
