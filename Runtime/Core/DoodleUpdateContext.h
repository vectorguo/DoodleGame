//
//  DoodleUpdateContext.h
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#pragma once

namespace Doodle
{
    /**
     * 每帧交给组件的上下文
     *
     * 组件不直接摸窗口、时钟或输入库：本帧需要的事实由应用层算好，装进这个
     * 结构按 const& 传下来。这样组件不依赖 GLFW / Vulkan，也能在测试里手工
     * 构造一份上下文。
     *
     * 当前只有 deltaSeconds。输入快照（键位、鼠标位移）会随输入层一起加入，
     * 字段和类型按 A4 方案 §5.2 的 DoodleInputState 对齐；加字段只改变
     * 构造上下文的地方，不动组件接口。
     *
     * deltaSeconds 由主循环用 steady_clock 算两帧间隔并钳上限（0.1s），
     * 防止断点、拖窗口或系统卡顿后单帧过大。组件拿到的永远是处理过的值。
     */
    struct DoodleUpdateContext
    {
        float deltaSeconds = 0.0f;
    };
}
