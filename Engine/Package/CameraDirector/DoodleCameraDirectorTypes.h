//
//  DoodleCameraDirectorTypes.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <cstdint>

namespace Doodle
{
    /**
     * 非活动虚拟相机的更新策略
     *
     * 对应截图里的 Standby Update。基础版没有混合/转场，非活动相机本来就不会
     * 被求值，所以这个字段目前只做保存和序列化；等有混合时再让它生效。
     *
     * 枚举值直接作为序列化值：0 = Always，1 = Never，2 = RoundRobin。
     */
    enum class ECameraStandbyUpdate : std::uint8_t
    {
        Always = 0,
        Never = 1,
        RoundRobin = 2,
    };

    /**
     * Camera Offset 在哪个阶段之后生效
     *
     * 枚举值直接作为序列化值：0 = Body，1 = Aim。
     */
    enum class ECameraOffsetApplyAfter : std::uint8_t
    {
        /**
         * 位置计算之后、瞄准之前：后续的 Aim 会自然重新瞄准
         */
        Body = 0,

        /**
         * 瞄准之后：位置偏移不影响朝向，除非 Preserve Composition
         */
        Aim = 1,
    };
}
