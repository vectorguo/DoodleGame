//
//  DoodleDrawContext.h
//  Doodle
//
//  Created by 郭智 on 2026/10/30.
//

#pragma once

#include <vector>

#include "DoodleDrawItem.h"

namespace Doodle
{
    //只以引用 / 指针出现，完整定义留给使用它们的源文件
    class DoodleCamera;
    class DoodleLight;

    /**
     * 一次 DrawFrame（一帧）的输入：相机、主光源与绘制项
     *
     * 名字里的 Draw 指「这次绘制任务」，不是单个 draw call：逐对象的数据是
     * DoodleDrawItem，本结构是整帧一份的聚合。它在 DrawFrame 开头由调用方
     * 构造，返回即失效。
     *
     * 这是「应用层收集」与「渲染门面录制」之间的数据形状。放在 Rendering
     * 而不是 Vulkan：它描述的是渲染域的一帧输入，不碰任何 Vulkan 对象，
     * 换成别的后端也还是同一份东西；Vulkan 门面只是消费它的一方。
     *
     * 门面不再按参数逐个接它们：帧级输入以后还会长（时间、曝光、阴影参数……），
     * 每加一样就改一次 DrawFrame 的签名，所有调用点都跟着动；收进一个结构
     * 之后，签名只认「一帧的输入」，调用点按字段名填，加字段不会静默错位。
     *
     * 成员都是非拥有引用 / 指针，只在本次 DrawFrame 期间有效：
     * - camera 必须有效：view / proj 由渲染门面从它现算；
     * - pLight 可为空：没有启用的光源不是错误，着色器只剩环境项；
     * - drawItems 为空合法：本帧只清屏。
     *
     * camera 与 drawItems 用引用、pLight 用指针，是为了让「必须存在」和
     * 「可能缺席」在类型上分开。相机和绘制列表每帧都有，让它们可空只会给
     * 每个使用点加一句永远为真的判断；光源是唯一真会缺席的那一个。
     *
     * 这个结构是调用点的栈上临时量，不保存、不跨帧复用 ——
     * 它只是一组「本帧借用什么」的引用，没有自己的生命周期。
     */
    struct DoodleDrawContext
    {
        /**
         * 本帧的相机
         */
        const DoodleCamera& camera;

        /**
         * 本帧的主光源；为空表示没有启用的光源
         */
        const DoodleLight* pLight = nullptr;

        /**
         * 本帧要画的网格与模型矩阵；数组顺序就是绘制顺序
         */
        const std::vector<DoodleDrawItem>& drawItems;
    };
}
