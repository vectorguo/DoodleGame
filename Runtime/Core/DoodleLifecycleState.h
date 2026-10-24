//
//  DoodleLifecycleState.h
//  Doodle
//
//  Created by 郭智 on 2026/10/23.
//

#pragma once

namespace Doodle
{
    /**
     * 对象的生命周期状态（单向轴）
     *
     * 只描述「活着 → 请求销毁 → 正在销毁 → 已销毁」这条不可逆的线：
     * - Alive           正常可用
     * - PendingDestroy  已请求销毁，等拥有者在安全点执行；不再更新、不能再创建子对象
     * - Destroying      正在跑销毁回调；重入的销毁请求在这里被挡下
     * - Destroyed       回调跑完、子容器已清空；内存还在，等拥有者释放
     *
     * enabled / active 是可逆的「模式」，与这条单向轴正交，不放进本枚举。
     */
    enum class ELifecycleState
    {
        Alive,
        PendingDestroy,
        Destroying,
        Destroyed,
    };
}
