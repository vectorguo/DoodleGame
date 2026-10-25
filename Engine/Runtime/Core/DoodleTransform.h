//
//  DoodleTransform.h
//  Doodle
//
//  Created by 郭智 on 2026/10/20.
//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Doodle
{
    /**
     * 对象的位姿：位置 + 旋转 + 缩放，GameObject 唯一的位姿来源
     *
     * 世界约定为 Z-up：局部 +X 是前，局部 +Z 是上，局部 -Y 是右。
     * 这个约定与现有 lookAt 的 up = (0, 0, 1) 一致，也是初始相机四元数
     * 的算法前提。
     *
     * 存四元数而不是 yaw/pitch：Transform 是通用对象的状态，任何对象都可能
     * 绕任意轴旋转；yaw/pitch 只是自由相机的操作模型，不是 Transform 的
     * 通用表示。行为组件在需要时可以临时把四元数解释成 yaw/pitch，算完写回。
     *
     * 不缓存矩阵：位置和旋转每帧都可能被行为组件改写，矩阵现算足够便宜；
     * 缓存会引入「脏标记」这套本来不存在的状态。等 profiler 证明它是热点再谈。
     */
    class DoodleTransform
    {
    public:
        /**
         * 局部 +X：前
         */
        [[nodiscard]] glm::vec3 GetForward() const
        {
            return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        }

        /**
         * 局部 +Z：上
         */
        [[nodiscard]] glm::vec3 GetUp() const
        {
            return rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        }

        /**
         * 局部 -Y：右
         *
         * 右手系下 X × Y = Z，所以 +Y 是左，右是 -Y
         */
        [[nodiscard]] glm::vec3 GetRight() const
        {
            return rotation * glm::vec3(0.0f, -1.0f, 0.0f);
        }

        /**
         * 局部到世界的变换矩阵
         *
         * 按 T * R * S 组合：先缩放、再旋转、最后平移。缩放因此作用在局部
         * 轴上，随对象一起旋转；这与「模型的本地坐标先被拉伸，再摆进世界」
         * 的直觉一致，也是 Unity 等引擎的 TRS 顺序。
         */
        [[nodiscard]] glm::mat4 GetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), position)
                 * glm::mat4_cast(rotation)
                 * glm::scale(glm::mat4(1.0f), scale);
        }

    public:
        /**
         * 世界空间位置
         */
        glm::vec3 position{0.0f};

        /**
         * 局部到世界的旋转；恒等旋转表示「前 +X、上 +Z」
         */
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   // w, x, y, z

        /**
         * 局部逐轴缩放，没有错切
         *
         * 只影响 GetMatrix；GetForward / GetUp / GetRight 仍返回单位方向向量，
         * 不因为缩放变长或变短。默认 1 表示不缩放。
         */
        glm::vec3 scale{1.0f};
    };
}
