//
//  DoodleFrameContext.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/21.
//

#include "DoodleFrameContext.h"
#include "DoodleCamera.h"
#include "DoodleLight.h"

namespace Doodle
{
    DoodleFrameContext::DoodleFrameContext(const DoodleCamera* pCamera, const std::vector<DoodleDrawItem>& drawItems)
        : camera(*pCamera), drawItems(drawItems)
    {
        pLight = DoodleLight::GetMainLight();
    }
}
