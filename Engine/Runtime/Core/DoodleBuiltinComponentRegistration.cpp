//
//  DoodleBuiltinComponentRegistration.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleBuiltinComponentRegistration.h"

#include "DoodleComponentRegistry.h"
#include "Rendering/DoodleCamera.h"
#include "Rendering/DoodleLight.h"
#include "Rendering/DoodleMeshRenderer.h"

namespace Doodle
{
    void RegisterBuiltinComponents()
    {
        DoodleComponentRegistry::RegisterComponent<DoodleMeshRenderer>("Engine.MeshRenderer");
        DoodleComponentRegistry::RegisterComponent<DoodleCamera>("Engine.Camera");
        DoodleComponentRegistry::RegisterComponent<DoodleLight>("Engine.Light");
    }
}
