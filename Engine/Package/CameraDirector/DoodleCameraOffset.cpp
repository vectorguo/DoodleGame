//
//  DoodleCameraOffset.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleCameraOffset.h"

#include <stdexcept>

#include "Core/DoodleComponentConfig.h"

namespace Doodle
{
    void DoodleCameraOffset::Serialize(DoodleComponentConfig& config) const
    {
        //applyAfter 存枚举整数：0 = Body，1 = Aim
        config.SetVec3("offset", m_offset);
        config.SetInt("applyAfter", static_cast<int>(m_applyAfter));
        config.SetBool("preserveComposition", m_preserveComposition);
    }

    void DoodleCameraOffset::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"offset", "applyAfter", "preserveComposition"});
#endif

        m_offset = config.GetVec3("offset", glm::vec3(0.0f));
        m_preserveComposition = config.GetBool("preserveComposition", false);

        const int applyAfter = config.GetInt(
            "applyAfter", static_cast<int>(ECameraOffsetApplyAfter::Aim));
        if (applyAfter == static_cast<int>(ECameraOffsetApplyAfter::Body))
        {
            m_applyAfter = ECameraOffsetApplyAfter::Body;
        }
        else if (applyAfter == static_cast<int>(ECameraOffsetApplyAfter::Aim))
        {
            m_applyAfter = ECameraOffsetApplyAfter::Aim;
        }
        else
        {
            throw std::runtime_error(config.GetPath() +
                                     ".applyAfter: expected 0 (body) or 1 (aim)");
        }
    }
}
