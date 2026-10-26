//
//  DoodleCamera.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include "Core/DoodleGameObject.h"
#include "Core/DoodleTransform.h"

namespace Doodle
{
    glm::mat4 DoodleCamera::GetViewMatrix() const
    {
        // 宿主由 AddComponent 保证非空；脱离宿主构造的相机是错误用法
        const DoodleTransform& transform = m_pGameObject->GetTransform();

        // forward / up 来自同一个旋转基，两者恒正交，lookAt 不会因为
        // 「forward 与世界 +Z 平行」而退化
        return glm::lookAt(
            transform.position,
            transform.position + transform.GetForward(),
            transform.GetUp());
    }

    glm::mat4 DoodleCamera::GetProjectionMatrix(const float aspect) const
    {
        glm::mat4 proj(1.0f);

        switch (m_projectionType)
        {
        case ECameraProjectionType::Perspective:
            proj = glm::perspective(m_fovY, aspect, m_near, m_far);
            break;

        case ECameraProjectionType::Orthographic:
            {
                // 平行投影没有近大远小：可见高度固定为 2×半高，宽度按宽高比展开。
                // 近远平面仍然参与裁剪，只是不改变物体在画面上的大小
                const float halfWidth = m_orthoHalfHeight * aspect;
                proj = glm::ortho(
                    -halfWidth,
                    halfWidth,
                    -m_orthoHalfHeight,
                    m_orthoHalfHeight,
                    m_near,
                    m_far);
                break;
            }
        }

        // GLM 默认按 OpenGL 的裁剪空间生成投影；Vulkan 的 Y 轴方向相反。
        // Z 轴方向已经由 CMake 里的 GLM_FORCE_DEPTH_ZERO_TO_ONE 处理，
        // 这里只剩 Y 需要翻转。两种投影都必须走这一步。
        //
        // 这一步必须排在交换链预旋转之前：它负责「换成 Vulkan 的 NDC」，
        // 而预旋转是在 Vulkan 的 NDC 里做的。预旋转不在这里 —— 它属于渲染
        // 门面（见 DoodleVulkanManager::UpdateUniformBuffer）
        proj[1][1] *= -1.0f;

        return proj;
    }
}
