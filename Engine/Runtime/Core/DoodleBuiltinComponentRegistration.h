//
//  DoodleBuiltinComponentRegistration.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

namespace Doodle
{
    /**
     * 注册引擎内置的可序列化组件
     *
     * MeshRenderer、Camera 和 Light 不再走场景的专用字段，而是和 GamePlay /
     * Package 组件一样通过类型 ID 进入注册表。由
     * DoodleApplication::InitializeRuntime 在内容加载之前调用。
     */
    void RegisterBuiltinComponents();
}
