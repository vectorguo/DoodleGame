//
//  DoodleMath.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Doodle
{
    /**
     * 朝向的 yaw / pitch 表示：yaw 绕世界 +Z，pitch 是相对 XY 平面的仰角
     *
     * 只描述朝向，不含滚转。Z-up 世界里的自由视角、瞄准、水平移动都只需要
     * 这两个角：
     * - yaw 从 +X 起算，绕 +Z 逆时针为正；
     * - pitch 在 XY 平面内为 0，抬头为正。
     *
     * 和场景文件的 rotationDegrees 不是同一套：那里是三轴欧拉角，允许滚转，
     * 组合顺序见 EulerDegreesToQuaternion。两者只在 x = 0 时重合。
     */
    struct DoodleYawPitch
    {
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    /**
     * 引擎通用的纯数学工具
     *
     * 底层是 GLM，但不转发它已经提供的功能：向量 / 矩阵 / 四元数运算、mix、
     * clamp、smoothstep 等都直接调 GLM，多包一层只会多一处需要同步维护的语义。
     * 收进来的只有三类：
     *
     *   1. GLM 没有、又通用到不该塞进某个业务类的计算：最短角差、MoveTowards、
     *      帧率无关的阻尼、浮点约等、零向量保护的单位化
     *   2. 本引擎的坐标与旋转约定。这类约定一旦在调用点各写一份，迟早会出现
     *      两套「看起来都对」的实现：Z-up、欧拉角顺序、yaw/pitch 的定义
     *   3. 必须成对出现的正反变换：欧拉角 ↔ 四元数、yaw/pitch ↔ 朝向
     *
     * 准入门槛沿用 DoodleVulkanUtils：不碰任何引擎状态（设备、场景、组件），
     * 纯函数，同样的输入永远同样的输出。工具没有生命周期，实例化在编译期禁止。
     *
     * 角度一律用弧度；只有名字里带 Degrees 的函数用角度制。这个后缀是约定，
     * 不要在调用点省掉。
     */
    class DoodleMath final
    {
    public:
        DoodleMath() = delete;

        // ---- 标量 ----

        /**
         * 反插值：value 在 [from, to] 中的相对位置
         *
         * 不钳位：value 在区间外时返回 [0, 1] 之外的值，要不要夹取由调用方决定。
         * from == to 时区间没有反函数，返回 0。
         */
        [[nodiscard]] static float InverseLerp(const float from, const float to, const float value);

        /**
         * 线性重映射：[inMin, inMax] 中的 value → [outMin, outMax] 中的对应值
         *
         * 不钳位，与 InverseLerp 一致。输入区间长度为 0 时返回 outMin。
         */
        [[nodiscard]] static float Remap(const float value,
                                         const float inMin, const float inMax,
                                         const float outMin, const float outMax);

        /**
         * 从 current 向 target 移动，最多走 maxDelta
         *
         * 距离不超过 maxDelta 时直接落到 target，不会越过。
         * maxDelta 必须非负；负数会朝反方向走，属于错误用法。
         */
        [[nodiscard]] static float MoveTowards(const float current, const float target, const float maxDelta);

        /**
         * 帧率无关的指数阻尼：每秒钟把「与目标的差」衰减 e^(-lambda) 倍
         *
         * 公式：target + (current - target) * exp(-lambda * deltaSeconds)。
         * 不用 lerp(current, target, k) 那种写法 —— 后者的实际速度取决于调用
         * 频率，60 帧与 120 帧的手感会不一样。
         *
         * @param lambda 阻尼速率（1/秒），越大追得越快；必须非负
         * @param deltaSeconds 本帧时长（秒）；为 0 时返回 current
         */
        [[nodiscard]] static float Damp(const float current, const float target,
                                        const float lambda, const float deltaSeconds);

        /**
         * 浮点约等
         *
         * 纯绝对误差在大数值上太严（1e9 附近 float 的表示精度本来就有几百），
         * 纯相对误差在 0 附近又太松。这里用 max(1, |a|, |b|) 做尺度：接近 0
         * 时退化为绝对误差，大数值时是相对误差。
         *
         * @param tolerance 相对容差，非负；默认 1e-5 大约是 float 精度两个数量级
         */
        [[nodiscard]] static bool ApproximatelyEqual(const float a, const float b,
                                                     const float tolerance = 1e-5f);

        // ---- 角度（弧度）----

        /**
         * 把角度归一到 [-π, π)
         *
         * 用 fmod 而不是 atan2(sin, cos)：大角度不会因为三角函数丢精度。
         * -π 与 π 表示同一个方向，端点归到 -π，区间取左闭右开。
         */
        [[nodiscard]] static float NormalizeAngle(const float radians);

        /**
         * 从 from 转到 to 的最短有符号角差，结果在 [-π, π)
         *
         * 正数表示逆时针（绕 +Z）更近，负数表示顺时针更近。
         * 例：DeltaAngle(350°, 10°) 返回 +20°，而不是 -340°。
         */
        [[nodiscard]] static float DeltaAngle(const float fromRadians, const float toRadians);

        /**
         * 沿最短弧插值
         *
         * 与 DeltaAngle 同一套方向判断，绕过 ±180° 时不会倒着转一圈。
         * t 不钳位，允许外插；返回值总是归一化到 [-π, π)。
         */
        [[nodiscard]] static float LerpAngle(const float fromRadians, const float toRadians, const float t);

        // ---- 向量 ----

        /**
         * 沿直线从 current 向 target 移动，最多走 maxDistance
         *
         * 距离不超过 maxDistance（或两点重合）时直接落到 target，不会越过。
         * maxDistance 必须非负。
         */
        [[nodiscard]] static glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target,
                                                  const float maxDistance);

        /**
         * 单位化；零向量返回零向量
         *
         * glm::normalize 对零向量会得到 NaN。方向类计算里经常遇到「没有输入」的
         * 情况，这里把它定义成「没有方向」，调用方不需要每处都先判断长度。
         */
        [[nodiscard]] static glm::vec3 SafeNormalize(const glm::vec3& vector);

        /**
         * 把向量投影到平面上（去掉沿法线的分量）
         *
         * planeNormal 必须是单位向量。本函数不替调用方归一化 —— 那会隐藏一次
         * 错误传入（长度不为 1 时结果本身就是错的），也多一次没必要的 sqrt。
         * 例：Z-up 世界里取水平方向 = ProjectOnPlane(direction, {0, 0, 1})。
         */
        [[nodiscard]] static glm::vec3 ProjectOnPlane(const glm::vec3& vector, const glm::vec3& planeNormal);

        // ---- 旋转 ----

        /**
         * 欧拉角（角度制）→ 四元数，场景文件使用的三轴欧拉约定
         *
         * 约定 q = Rz * Ry * Rx：先绕 X、再绕 Y、最后绕 Z，三个轴都是世界轴。
         * 单轴测试（例如 [0, 0, 45]）与旋转顺序无关，但多轴组合必须有一份写死的
         * 约定，否则同一份文件在不同实现里会得到不同的朝向。
         *
         * 不用 glm::quat(vec3)：它的分量组合顺序没有写在这边的代码里，换一个人
         * 读时还得去翻 GLM 实现。这里逐个 angleAxis 乘，顺序一目了然。
         *
         * 与 QuaternionToEulerDegrees 互逆（万向锁附近见那边的说明）。
         */
        [[nodiscard]] static glm::quat EulerDegreesToQuaternion(const glm::vec3& degrees);

        /**
         * EulerDegreesToQuaternion 的逆，返回同一约定下的三轴角（角度制）
         *
         * 万向锁（y = ±90°）附近 x 与 z 无法分开：这里固定 z = 0，把可辨识的
         * 那部分全部记到 x 上。反解结果重建出的旋转与原旋转一致，但分量本身
         * 不唯一 —— 往返比较请比四元数，不要逐分量比角度。
         *
         * rotation 必须是单位四元数（DoodleTransform 的 rotation 满足这一点）。
         */
        [[nodiscard]] static glm::vec3 QuaternionToEulerDegrees(const glm::quat& rotation);

        /**
         * yaw / pitch（弧度）→ 无滚转的四元数
         *
         * 先绕世界 +Z 转 yaw，再绕旋转后的局部右轴 -Y 转 pitch：
         * q = Rz(yaw) * R(-Y)(pitch)。+Y 是左、-Y 是右，与 DoodleTransform
         * 的 GetRight 是同一个约定。结果等价于 x = 0 的三轴欧拉角。
         */
        [[nodiscard]] static glm::quat YawPitchToQuaternion(const float yawRadians, const float pitchRadians);

        /**
         * 朝向向量 → yaw / pitch（弧度）
         *
         * yaw = atan2(y, x)，pitch = asin(z)；输入长度不限，内部先单位化。
         * 零向量没有方向，返回 {0, 0}。与 YawPitchToQuaternion 互逆：
         * 把算出来的四元数作用到 (1, 0, 0) 再传回来，得到同一组角。
         */
        [[nodiscard]] static DoodleYawPitch DirectionToYawPitch(const glm::vec3& direction);
    };
}
