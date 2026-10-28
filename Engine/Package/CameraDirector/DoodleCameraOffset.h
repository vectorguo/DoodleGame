//
//  DoodleCameraOffset.h
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#pragma once

#include <glm/glm.hpp>

#include "Core/DoodleComponent.h"
#include "DoodleCameraDirectorTypes.h"

namespace Doodle
{
    class DoodleComponentConfig;

    /**
     * 相机偏移扩展：挂在虚拟相机同一个对象上
     *
     * 对应截图里的 Cinemachine Camera Offset：
     * Offset / Apply After / Preserve Composition。
     *
     * Offset 是相机局部空间里的位移；Apply After 决定它出现在位置计算之后
     * 还是瞄准之后；Preserve Composition 只在 Aim 阶段有意义 —— 偏移后重新
     * 瞄准 LookAt 目标，让目标留在画面原来的位置。
     */
    class DoodleCameraOffset final : public DoodleComponent
    {
    public:
        /**
         * 把 Offset / ApplyAfter / PreserveComposition 写回配置
         *
         * applyAfter 存整数：0 = Body，1 = Aim。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从配置读取 Offset / ApplyAfter / PreserveComposition
         *
         * applyAfter 接受 0（body）/ 1（aim）；offset 是相机局部空间位移。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        [[nodiscard]] const glm::vec3& GetOffset() const
        {
            return m_offset;
        }

        [[nodiscard]] ECameraOffsetApplyAfter GetApplyAfter() const
        {
            return m_applyAfter;
        }

        [[nodiscard]] bool GetPreserveComposition() const
        {
            return m_preserveComposition;
        }

    private:
        glm::vec3 m_offset{0.0f};
        ECameraOffsetApplyAfter m_applyAfter = ECameraOffsetApplyAfter::Aim;
        bool m_preserveComposition = false;
    };
}
