//
//  DoodlePackageRegistration.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

namespace Doodle
{
    /**
     * 注册 Engine/Package 下所有包的场景组件类型
     *
     * 由 DoodleApplication::InitializeRuntime 在加载启动场景之前调用：
     * 包属于引擎，不需要平台入口或 GamePlay 关心。新加一个包时，只需在
     * DoodlePackageRegistration.cpp 里补上它的组件注册。
     *
     * 每个包不再各自维护 Registration 文件 —— 包的注册集中在这里，
     * 依赖顺序和启用的包都在一处可见。
     */
    void RegisterPackageComponents();
}
