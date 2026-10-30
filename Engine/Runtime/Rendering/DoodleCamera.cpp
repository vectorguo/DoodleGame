//
//  DoodleCamera.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleCamera.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

#include "Core/DoodleComponentConfig.h"
#include "Core/DoodleGameObject.h"
#include "Core/DoodleTransform.h"

namespace Doodle
{
    std::vector<DoodleCamera*> DoodleCamera::s_cameras;
    const DoodleCamera* DoodleCamera::s_pMainCamera = nullptr;

    void DoodleCamera::OnAwake()
    {
        s_cameras.push_back(this);
        SortCameras();
        RefreshMainCamera();
    }

    void DoodleCamera::OnEnable()
    {
        RefreshMainCamera();
    }

    void DoodleCamera::OnDisable()
    {
        RefreshMainCamera();
    }

    void DoodleCamera::OnDestroy()
    {
        //先摘除再重算：重算结果不可能再落回一个正在销毁的组件
        std::erase(s_cameras, this);
        RefreshMainCamera();
    }

    void DoodleCamera::Serialize(DoodleComponentConfig& config) const
    {
        //与 Deserialize 同一组 key；projection / renderType 存枚举整数，
        //fov 写回角度制
        config.SetInt("projection", static_cast<int>(m_projectionType));
        config.SetInt("renderType", static_cast<int>(m_renderType));
        config.SetInt("priority", m_priority);
        config.SetFloat("fov", glm::degrees(m_fovY));
        config.SetFloat("near", m_near);
        config.SetFloat("far", m_far);
        config.SetFloat("orthoHalfHeight", m_orthoHalfHeight);
    }

    void DoodleCamera::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"projection", "renderType", "priority", "fov", "near", "far", "orthoHalfHeight"});
#endif

        const int projection = config.GetInt("projection", static_cast<int>(ECameraProjectionType::Perspective));
        if (projection == static_cast<int>(ECameraProjectionType::Perspective))
        {
            m_projectionType = ECameraProjectionType::Perspective;
        }
        else if (projection == static_cast<int>(ECameraProjectionType::Orthographic))
        {
            m_projectionType = ECameraProjectionType::Orthographic;
        }
        else
        {
            throw std::runtime_error(config.GetPath() + ".projection: expected 0 (perspective) or 1 (orthographic)");
        }

        const int renderType = config.GetInt("renderType", static_cast<int>(ECameraRenderType::Base));
        if (renderType == static_cast<int>(ECameraRenderType::Base))
        {
            m_renderType = ECameraRenderType::Base;
        }
        else if (renderType == static_cast<int>(ECameraRenderType::Overlay))
        {
            m_renderType = ECameraRenderType::Overlay;
        }
        else
        {
            throw std::runtime_error(config.GetPath() + ".renderType: expected 0 (base) or 1 (overlay)");
        }

        //优先级任意整数，不分范围：只有相对高低有意义
        m_priority = config.GetInt("priority", 0);

        m_fovY = glm::radians(config.GetFloat("fov", 45.0f));
        m_near = config.GetFloat("near", 0.1f);
        m_far = config.GetFloat("far", 100.0f);
        m_orthoHalfHeight = config.GetFloat("orthoHalfHeight", 2.0f);

        if (!(m_fovY > 0.0f && m_fovY < glm::radians(180.0f)))
        {
            throw std::runtime_error(config.GetPath() + ".fov: must be in (0, 180)");
        }

        if (!(m_near > 0.0f))
        {
            throw std::runtime_error(config.GetPath() + ".near: must be greater than 0");
        }

        if (!(m_far > m_near))
        {
            throw std::runtime_error(config.GetPath() + ".far: must be greater than near");
        }

        if (!(m_orthoHalfHeight > 0.0f))
        {
            throw std::runtime_error(config.GetPath() + ".orthoHalfHeight: must be greater than 0");
        }
    }

    void DoodleCamera::SetRenderType(const ECameraRenderType type)
    {
        m_renderType = type;
        RefreshMainCamera();
    }

    void DoodleCamera::SetPriority(const int priority)
    {
        m_priority = priority;

        //优先级是排序键，改完必须恢复有序，否则列表头不再是最高的那台
        SortCameras();
        RefreshMainCamera();
    }

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

    void DoodleCamera::SortCameras()
    {
        //优先级降序 → 地址升序：列表头就是优先级最高的 Base 相机候选。
        //同优先级用 std::less 比较地址，只为凑出一个确定的全序；它不表达
        //「谁先创建」——同优先级之间谁排前面不作承诺
        std::ranges::sort(s_cameras,
                          [](const DoodleCamera* pLeft, const DoodleCamera* pRight)
                          {
                              if (pLeft->m_priority != pRight->m_priority)
                              {
                                  return pLeft->m_priority > pRight->m_priority;
                              }

                              return std::less<const DoodleCamera*>()(pLeft, pRight);
                          });
    }

    void DoodleCamera::RefreshMainCamera()
    {
        //列表已经按优先级排好，第一个启用的 Base 就是主相机；这里只需要
        //跳过停用的和 Overlay，不再比较优先级
        s_pMainCamera = nullptr;

        for (const DoodleCamera* pCamera : s_cameras)
        {
            if (pCamera->IsEnabled() && pCamera->m_renderType == ECameraRenderType::Base)
            {
                s_pMainCamera = pCamera;
                break;
            }
        }
    }
}
