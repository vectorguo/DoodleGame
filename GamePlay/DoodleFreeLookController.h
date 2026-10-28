//
//  DoodleFreeLookController.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include "Core/DoodleComponent.h"

namespace Doodle
{
    class DoodleComponentConfig;

    /**
     * GamePlay 的自由视角控制器
     *
     * 挂在相机 rig 上，读 DoodleInput，修改宿主的 Transform：
     *   - 按住右键（由应用层捕获）后移动鼠标：yaw / pitch，pitch 钳位、无滚转；
     *   - W/S/A/D：沿水平朝向移动；Q/E：世界 Z 升降；
     *   - Left Shift：加速；
     *   - 斜向归一化，位移乘 deltaSeconds。
     *
     * 它不接触 DoodleCamera，也不接触窗口：CameraDirector 的虚拟相机组件
     * 和它挂在同一个 rig 上，Brain 读取 Transform 后驱动真正的相机。
     *
     * 初始位姿来自场景文件里的 Transform，没有 Reset()。
     */
    class DoodleFreeLookController final : public DoodleComponent
    {
    public:
        /**
         * 把移动 / 视角参数写回配置
         *
         * 和 Deserialize 使用同一组 key；lookSensitivity 是度 / 屏幕单位，
         * pitchLimit 是度。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从配置读取移动 / 视角参数
         *
         * moveSpeed 单位/秒；lookSensitivity 度/屏幕单位；pitchLimit 度。
         * 参数越界在这里报错。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        void OnUpdate(const DoodleUpdateContext& context) override;

    private:
        /**
         * 移动速度，单位 / 秒
         */
        float m_moveSpeed = 2.5f;

        /**
         * Left Shift 时的移动倍率
         */
        float m_boostMultiplier = 4.0f;

        /**
         * 视角灵敏度，度 / 屏幕单位。内部换算成弧度再乘鼠标位移
         */
        float m_lookSensitivity = 0.12f;

        /**
         * pitch 钳位，度；必须小于 90
         */
        float m_pitchLimit = 89.0f;
    };
}
