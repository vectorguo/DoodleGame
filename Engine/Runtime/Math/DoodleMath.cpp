//
//  DoodleMath.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleMath.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace
{
    constexpr float Pi = glm::pi<float>();
    constexpr float TwoPi = glm::two_pi<float>();
}

namespace Doodle
{
    float DoodleMath::InverseLerp(const float from, const float to, const float value)
    {
        // 区间长度为 0 时没有反函数。返回 0 而不是除零，让 Remap 落到 outMin
        if (from == to)
        {
            return 0.0f;
        }

        return (value - from) / (to - from);
    }

    float DoodleMath::Remap(const float value,
                            const float inMin, const float inMax,
                            const float outMin, const float outMax)
    {
        const float t = InverseLerp(inMin, inMax, value);
        return outMin + (outMax - outMin) * t;
    }

    float DoodleMath::MoveTowards(const float current, const float target, const float maxDelta)
    {
        const float difference = target - current;

        // 剩余距离在一步之内：直接吸附到目标，避免在目标附近来回抖
        if (std::abs(difference) <= maxDelta)
        {
            return target;
        }

        // copysign 让「朝目标走」这件事只由差值的符号决定，maxDelta 只管走多远
        return current + std::copysign(maxDelta, difference);
    }

    float DoodleMath::Damp(const float current, const float target, const float lambda, const float deltaSeconds)
    {
        // 解析解：剩余差值每帧乘 e^(-lambda * dt)。
        // 两步 dt 与一步 2*dt 的结果相同，因此与帧率无关
        return target + (current - target) * std::exp(-lambda * deltaSeconds);
    }

    bool DoodleMath::ApproximatelyEqual(const float a, const float b, const float tolerance)
    {
        // 尺度下限取 1：接近 0 时按绝对误差比较，大数值时按相对误差比较
        const float scale = std::max(1.0f, std::max(std::abs(a), std::abs(b)));
        return std::abs(a - b) <= tolerance * scale;
    }

    float DoodleMath::NormalizeAngle(const float radians)
    {
        // fmod 保留符号，先落到 [0, 2π) 再平移到 [-π, π)。
        // 不用 atan2(sin, cos)：角度很大时三角函数会先丢精度
        float wrapped = std::fmod(radians + Pi, TwoPi);
        if (wrapped < 0.0f)
        {
            wrapped += TwoPi;
        }

        return wrapped - Pi;
    }

    float DoodleMath::DeltaAngle(const float fromRadians, const float toRadians)
    {
        return NormalizeAngle(toRadians - fromRadians);
    }

    float DoodleMath::LerpAngle(const float fromRadians, const float toRadians, const float t)
    {
        return NormalizeAngle(fromRadians + DeltaAngle(fromRadians, toRadians) * t);
    }

    glm::vec3 DoodleMath::MoveTowards(const glm::vec3& current, const glm::vec3& target, const float maxDistance)
    {
        const glm::vec3 difference = target - current;
        const float distance = glm::length(difference);

        // 一步之内（含两点重合）：直接吸附，同时也避开下面的除零
        if (distance <= maxDistance || distance == 0.0f)
        {
            return target;
        }

        return current + difference * (maxDistance / distance);
    }

    glm::vec3 DoodleMath::SafeNormalize(const glm::vec3& vector)
    {
        const float lengthSquared = glm::dot(vector, vector);
        if (lengthSquared == 0.0f)
        {
            return glm::vec3(0.0f);
        }

        // 平方长度已经在手，inversesqrt 省掉一次 sqrt + 除法
        return vector * glm::inversesqrt(lengthSquared);
    }

    glm::vec3 DoodleMath::ProjectOnPlane(const glm::vec3& vector, const glm::vec3& planeNormal)
    {
        return vector - planeNormal * glm::dot(vector, planeNormal);
    }

    glm::quat DoodleMath::EulerDegreesToQuaternion(const glm::vec3& degrees)
    {
        // 逐个 angleAxis 相乘，保持写死的 Rz * Ry * Rx 顺序
        const glm::vec3 radians = glm::radians(degrees);
        return glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f))
             * glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f))
             * glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::vec3 DoodleMath::QuaternionToEulerDegrees(const glm::quat& rotation)
    {
        // 对 R = Rz * Ry * Rx 反解。glm::mat3 的下标是 [列][行]，
        // 下面每个下标都按「R[行][列]」写出对应关系，避免行列看反
        const glm::mat3 matrix = glm::mat3_cast(rotation);

        // R[2][0] = -sin(y)。钳位只为吸收非单位四元数带来的数值越界
        const float sinY = glm::clamp(-matrix[0][2], -1.0f, 1.0f);
        const float y = std::asin(sinY);

        float x = 0.0f;
        float z = 0.0f;

        // cos(y) 足够大：x 与 z 各自由两个矩阵元素唯一确定
        if (std::abs(sinY) < 1.0f - 1e-6f)
        {
            // x = atan2(R[2][1], R[2][2])
            x = std::atan2(matrix[1][2], matrix[2][2]);
            // z = atan2(R[1][0], R[0][0])
            z = std::atan2(matrix[0][1], matrix[0][0]);
        }
        else
        {
            // 万向锁：y = ±90° 时 x 与 z 只剩组合 x±z 可辨。
            // 固定 z = 0，把可辨识的那部分全部记到 x 上；
            // x = atan2(-R[1][2], R[1][1]) 在两个分支上同样成立
            x = std::atan2(-matrix[2][1], matrix[1][1]);
            z = 0.0f;
        }

        return glm::degrees(glm::vec3(x, y, z));
    }

    glm::quat DoodleMath::YawPitchToQuaternion(const float yawRadians, const float pitchRadians)
    {
        // 先绕世界 +Z 转 yaw，再绕旋转后的局部右轴 -Y 转 pitch。
        // 右轴用 -Y：右手系下 X × Y = Z，+Y 是左
        return glm::angleAxis(yawRadians, glm::vec3(0.0f, 0.0f, 1.0f))
             * glm::angleAxis(pitchRadians, glm::vec3(0.0f, -1.0f, 0.0f));
    }

    DoodleYawPitch DoodleMath::DirectionToYawPitch(const glm::vec3& direction)
    {
        // 零向量经 SafeNormalize 仍是零，atan2 / asin 得到 (0, 0)，
        // 返回确定值而不是 NaN
        const glm::vec3 unit = SafeNormalize(direction);

        DoodleYawPitch result;
        result.yaw = std::atan2(unit.y, unit.x);
        result.pitch = std::asin(glm::clamp(unit.z, -1.0f, 1.0f));
        return result;
    }
}
