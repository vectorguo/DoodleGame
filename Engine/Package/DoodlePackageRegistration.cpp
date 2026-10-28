//
//  DoodlePackageRegistration.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodlePackageRegistration.h"

#include "CameraDirector/DoodleCameraDirector.h"
#include "CameraDirector/DoodleCameraOffset.h"
#include "CameraDirector/DoodleVirtualCamera.h"
#include "Core/DoodleComponentRegistry.h"

namespace Doodle
{
    void RegisterPackageComponents()
    {
        // CameraDirector
        DoodleComponentRegistry::RegisterComponent<DoodleCameraDirector>("CameraDirector.Brain");
        DoodleComponentRegistry::RegisterComponent<DoodleVirtualCamera>("CameraDirector.VirtualCamera");
        DoodleComponentRegistry::RegisterComponent<DoodleCameraOffset>("CameraDirector.CameraOffset");
    }
}
