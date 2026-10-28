//
//  DoodleGame.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

namespace Doodle
{
    /**
     * GamePlay 层的组合入口
     *
     * App/Platform 下的平台 main 在场景加载之前调用 RegisterComponents，把 GamePlay
     * 自己定义的场景组件类型登记到注册表。Engine/Package 的类型由引擎在
     * DoodleApplication::InitializeRuntime 里统一注册，不经过这里。
     *
     * 这里只注册类型，不创建/挂载任何对象；对象挂什么组件完全由场景文件决定。
     */
    class DoodleGame final
    {
    public:
        DoodleGame() = delete;

        static void RegisterComponents();
    };
}
