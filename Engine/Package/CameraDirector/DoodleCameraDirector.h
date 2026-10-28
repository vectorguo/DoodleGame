//
//  DoodleCameraDirector.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Core/DoodleComponent.h"

namespace Doodle
{
    class DoodleComponentConfig;
    class DoodleGameObject;
    class DoodleVirtualCamera;

    /**
     * 相机导演（Brain）：挂在真正的相机对象上，驱动 DoodleCamera
     *
     * 每帧在场景里挑选优先级最高（同优先级取场景顺序靠前）的启用虚拟相机，
     * 计算它的位置 / 朝向 / 镜头，写到宿主 Transform 与 DoodleCamera。
     *
     * 基础版没有混合 / 转场；Standby Update 只保存不使用。
     *
     * 更新顺序：相机对象应当排在相机 rig / 跟随目标之后（场景文件靠后），
     * 否则 Brain 读到的是它们上一帧的 Transform。
     */
    class DoodleCameraDirector final : public DoodleComponent
    {
    public:
        /**
         * Brain 没有参数，写一个空配置
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * Brain 没有参数；仍然拒绝未知字段，避免拼错被静默忽略
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        void OnUpdate(const DoodleUpdateContext& context) override;

    private:
        [[nodiscard]] const DoodleVirtualCamera* FindActiveVirtualCamera();

        /**
         * 构造一个「局部 +X 朝 forward、+Z 尽量贴近 up」的旋转
         */
        [[nodiscard]] static glm::quat MakeLookRotation(const glm::vec3& eye,
                                                        const glm::vec3& target,
                                                        const glm::vec3& up);

    private:
        /**
         * 每帧扫描场景时复用的容器，稳态下不再分配
         */
        std::vector<DoodleGameObject*> m_reusableGameObjects;
    };
}
