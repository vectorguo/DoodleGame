//
//  DoodleGame.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleGame.h"

#include "DoodleFreeLookController.h"
#include "Core/DoodleComponentRegistry.h"

namespace Doodle
{
    void DoodleGame::RegisterComponents()
    {
        //Engine/Package 的类型由 DoodleApplication::InitializeRuntime 统一注册；
        //这里只登记 GamePlay 自己定义的场景组件
        DoodleComponentRegistry::RegisterComponent<DoodleFreeLookController>("GamePlay.FreeLookController");
    }
}
