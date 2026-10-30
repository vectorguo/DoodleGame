//
//  DoodleCameraDirector.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleCameraDirector.h"

#include <cmath>
#include <cstddef>

#include <glm/gtc/quaternion.hpp>

#include "Core/DoodleGameObject.h"
#include "DoodleVirtualCamera.h"
#include "DoodleCameraOffset.h"
#include "Rendering/DoodleCamera.h"
#include "Core/DoodleComponentConfig.h"
#include "Scene/DoodleScene.h"

namespace Doodle
{
    void DoodleCameraDirector::Serialize(DoodleComponentConfig& /*config*/) const
    {
        //Brain 没有参数
    }

    void DoodleCameraDirector::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        //基础版 Brain 没有参数；仍然拒绝未知字段，避免拼错后被静默忽略
        config.ExpectOnlyKeys({});
#else
        (void)config;
#endif
    }
    void DoodleCameraDirector::OnUpdate()
    {
        const DoodleVirtualCamera* pActiveCamera = FindActiveVirtualCamera();
        if (pActiveCamera == nullptr)
        {
            return;
        }

        const DoodleGameObject& virtualCameraObject = pActiveCamera->GetGameObject();
        const DoodleTransform& virtualCameraTransform = virtualCameraObject.GetTransform();

        //位置：有 Follow 就跟目标，没有就用虚拟相机自己的位置
        glm::vec3 position = virtualCameraTransform.position;
        if (const DoodleGameObject* pFollow = pActiveCamera->GetFollowTarget())
        {
            position = pFollow->GetTransform().position;
        }

        //朝向：有 LookAt 就瞄准目标，否则沿用虚拟相机自己的旋转。
        //世界 +Z 作为参考上方向；视线接近竖直时 MakeLookRotation 会换一个退化方向
        const DoodleGameObject* pLookAt = pActiveCamera->GetLookAtTarget();
        const bool hasLookAt = pLookAt != nullptr;
        const glm::vec3 lookAtPosition =
            hasLookAt ? pLookAt->GetTransform().position : glm::vec3(0.0f);
        const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);

        glm::quat orientation = hasLookAt
                                    ? MakeLookRotation(position, lookAtPosition, worldUp)
                                    : virtualCameraTransform.rotation;

        const DoodleCameraOffset* pOffset =
            virtualCameraObject.GetComponent<DoodleCameraOffset>();

        //Apply After = Body：先偏移位置，再瞄准
        if (pOffset != nullptr &&
            pOffset->GetApplyAfter() == ECameraOffsetApplyAfter::Body)
        {
            position += virtualCameraTransform.rotation * pOffset->GetOffset();
            orientation = hasLookAt
                              ? MakeLookRotation(position, lookAtPosition, worldUp)
                              : virtualCameraTransform.rotation;
        }

        //Dutch 是绕相机自身视线（局部 +X）的滚转
        const glm::quat dutchRotation = glm::angleAxis(
            glm::radians(pActiveCamera->GetDutchDegrees()), glm::vec3(1.0f, 0.0f, 0.0f));
        orientation *= dutchRotation;

        //Apply After = Aim：瞄准之后再做局部偏移。
        //没有 Preserve Composition 时朝向保持不变，目标会在画面里移动；
        //打开后会从新位置重新瞄准 LookAt 目标，目标留在原来的构图位置
        if (pOffset != nullptr &&
            pOffset->GetApplyAfter() == ECameraOffsetApplyAfter::Aim)
        {
            position += orientation * pOffset->GetOffset();

            if (pOffset->GetPreserveComposition() && hasLookAt)
            {
                orientation = MakeLookRotation(position, lookAtPosition, worldUp) * dutchRotation;
            }
        }

        DoodleTransform& cameraTransform = GetGameObject().GetTransform();
        cameraTransform.position = position;
        cameraTransform.rotation = orientation;

        //把虚拟相机的 Lens 写给真正的相机组件。基础版按透视相机处理
        if (DoodleCamera* pCamera = GetGameObject().GetComponent<DoodleCamera>())
        {
            pCamera->SetProjectionType(ECameraProjectionType::Perspective);
            pCamera->SetFovY(glm::radians(pActiveCamera->GetFovYDegrees()));
            pCamera->SetNearPlane(pActiveCamera->GetNearPlane());
            pCamera->SetFarPlane(pActiveCamera->GetFarPlane());
        }
    }

    const DoodleVirtualCamera* DoodleCameraDirector::FindActiveVirtualCamera()
    {
        //基础版每帧扫描本场景：场景很小，不做缓存/注册中心。
        //以后虚拟相机数量多到扫描成为热点，再引入场景级缓存
        const DoodleScene& scene = GetGameObject().GetScene();
        scene.GetGameObjects(m_reusableGameObjects);

        const DoodleVirtualCamera* pBest = nullptr;
        int bestPriority = 0;

        for (DoodleGameObject* pGameObject : m_reusableGameObjects)
        {
            const DoodleVirtualCamera* pVirtualCamera =
                pGameObject->GetComponent<DoodleVirtualCamera>();

            if (pVirtualCamera == nullptr || !pVirtualCamera->IsEnabled())
            {
                continue;
            }

            //同优先级保持场景顺序里靠前的那个
            if (pBest == nullptr || pVirtualCamera->GetPriority() > bestPriority)
            {
                pBest = pVirtualCamera;
                bestPriority = pVirtualCamera->GetPriority();
            }
        }

        return pBest;
    }

    glm::quat DoodleCameraDirector::MakeLookRotation(const glm::vec3& eye,
                                                     const glm::vec3& target,
                                                     const glm::vec3& up)
    {
        glm::vec3 forward = target - eye;
        if (glm::dot(forward, forward) < 1e-12f)
        {
            //目标与相机重合：没有合法朝向，退回单位旋转
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        forward = glm::normalize(forward);

        glm::vec3 safeUp = up;
        if (std::abs(glm::dot(forward, safeUp)) > 0.999f)
        {
            //视线与参考上方向几乎平行，换一个不共线的方向
            safeUp = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, safeUp)) > 0.999f)
            {
                safeUp = glm::vec3(1.0f, 0.0f, 0.0f);
            }
        }

        safeUp = glm::normalize(safeUp - forward * glm::dot(safeUp, forward));

        //Transform 约定：局部 +X 前、+Y 左、+Z 上；右 = -Y
        const glm::vec3 left = glm::cross(safeUp, forward);
        return glm::quat_cast(glm::mat3(forward, left, safeUp));
    }
}
