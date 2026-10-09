//
//  DoodleMacApplication.h
//  Doodle
//
//  Created by 郭智 on 2026/10/9.
//

#pragma once
#include "../DoodleApplication.h"

namespace Doodle
{
    /**
     * macOS 的驱动层实现：窗口用 GLFW 建
     *
     * 本类只回答「窗口从哪来」这一件事，建出来的窗口立刻交给基类持有，
     * 什么时候释放、按什么顺序释放都在那边
     *
     * 初始尺寸和标题写在这里而不是入口：它们是「这次启动要开个多大的窗」的参数，
     * 与帧循环无关。以后要改成从配置里读，动的地方也只有这里
     */
    class DoodleMacApplication final : public DoodleApplication
    {
    public:
        DoodleMacApplication() = default;
        ~DoodleMacApplication() override = default;

    protected:
        /** 建出 GLFW 窗口，并把尺寸变化回调接到基类上 */
        std::unique_ptr<DoodleWindow> CreateWindow() override;
    };
} // Doodle
