//
//  DoodleLight.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#include "DoodleLight.h"

#include <stdexcept>

#include "Core/DoodleComponentConfig.h"
#include "Core/DoodleGameObject.h"
#include "Core/DoodleTransform.h"
#include "Math/DoodleMath.h"

namespace Doodle
{
    std::vector<DoodleLight*> DoodleLight::s_lights;
    const DoodleLight* DoodleLight::s_pMainLight = nullptr;

    void DoodleLight::OnAwake()
    {
        //登记顺序就是创建顺序，主光源取第一个启用的那个，与改动前的
        //「遍历顺序里第一个启用的」一致
        s_lights.push_back(this);
        RefreshMainLight();
    }

    void DoodleLight::OnEnable()
    {
        RefreshMainLight();
    }

    void DoodleLight::OnDisable()
    {
        RefreshMainLight();
    }

    void DoodleLight::OnDestroy()
    {
        //先摘除再重算：结果不可能再落回一个正在销毁的组件
        std::erase(s_lights, this);
        RefreshMainLight();
    }

    void DoodleLight::RefreshMainLight()
    {
        s_pMainLight = nullptr;

        for (const DoodleLight* pLight : s_lights)
        {
            if (pLight->IsEnabled())
            {
                s_pMainLight = pLight;
                break;
            }
        }
    }

    void DoodleLight::Serialize(DoodleComponentConfig& config) const
    {
        //与 Deserialize 同一组 key；方向随宿主 Transform，不写进配置
        config.SetVec3("color", m_color);
        config.SetFloat("intensity", m_intensity);
    }

    void DoodleLight::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"color", "intensity"});
#endif

        m_color = config.GetVec3("color", glm::vec3(1.0f));
        m_intensity = config.GetFloat("intensity", 1.0f);

        // 负颜色会让光照结果从基色里「减掉」亮度，含义不明；零颜色是合法的
        //（当作关灯，只剩环境项）。GetVec3 已经保证各分量是有限数
        if (m_color.x < 0.0f || m_color.y < 0.0f || m_color.z < 0.0f)
        {
            throw std::runtime_error(config.GetPath() + ".color: components must be non-negative");
        }

        // GetFloat 已经拒绝 NaN / 无穷，这里只需要挡住负数
        if (m_intensity < 0.0f)
        {
            throw std::runtime_error(config.GetPath() + ".intensity: must be non-negative");
        }
    }

    glm::vec3 DoodleLight::GetDirection() const
    {
        // 宿主由 AddComponent 保证非空；脱离宿主构造的灯是错误用法
        const DoodleTransform& transform = m_pGameObject->GetTransform();

        // SafeNormalize 兜住「旋转四元数被外部写成非单位长度」的情况：
        // 光照方向必须是单位向量，否则 diffuse 的点乘结果会跟着缩放
        return DoodleMath::SafeNormalize(transform.GetForward());
    }
}
