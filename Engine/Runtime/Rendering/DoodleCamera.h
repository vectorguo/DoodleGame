//
//  DoodleCamera.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Core/DoodleComponent.h"

namespace Doodle
{
    /**
     * 相机的投影方式
     *
     * 场景数据（DoodleSceneData）也用这个类型描述相机配置，所以它跟着
     * DoodleCamera 走，不单独成头文件。
     *
     * 枚举值直接作为序列化值：0 = 透视，1 = 正交。
     */
    enum class ECameraProjectionType : std::uint8_t
    {
        /**
         * 透视投影：近大远小，由垂直 FOV 决定可见范围
         */
        Perspective = 0,

        /**
         * 平行投影（正交投影）：没有近大远小，由正交半高决定可见范围
         */
        Orthographic = 1,
    };

    /**
     * 相机的渲染角色
     *
     * Base 参与主相机的挑选，本帧的 view / proj 由选中的那台提供；Overlay
     * 叠在主相机之上绘制（等做 UI 时实现），现在只保存类型、不参与挑选。
     *
     * 枚举值直接作为序列化值：0 = Base，1 = Overlay。
     */
    enum class ECameraRenderType : std::uint8_t
    {
        /**
         * 主相机：参与主相机挑选，优先级最高的那台生效
         */
        Base = 0,

        /**
         * 叠加相机：叠在主相机之上绘制；现在只保存类型，不参与挑选
         */
        Overlay = 1,
    };

    /**
     * 相机组件：投影参数 + 渲染角色，位姿从宿主 Transform 现算
     *
     * 职责是三件事：
     * - 观察：从宿主 Transform 的 position / forward / up 生成 view；
     * - 投影：按调用方给的宽高比生成 projection，并包含 Vulkan 的 Y 翻转。
     *   支持透视（由垂直 FOV 决定）与平行投影（由正交半高决定）两种方式；
     * - 角色：Base / Overlay 与优先级。组件自己登记自己，Base 相机里优先级
     *   最高的一台就是本帧的主相机，由 GetMainCamera() 直接取用 —— 调用方
     *   不再遍历场景找相机（见下）。
     *
     * 主相机由本类内部的注册表维护，不是每帧现算：OnAwake 登记、OnDestroy
     * 摘除，启用状态或选择参数变化时立刻重算。所以 GetMainCamera() 的返回值
     * 在任何时刻都是有效的（或为空），但它是非拥有视图，不要缓存成跨帧的
     * 成员 —— 相机会被停用 / 销毁，跨帧指针会悬空。
     *
     * 不存 position、yaw/pitch，也不存窗口尺寸或输入状态。位姿的唯一来源是宿主
     * 的 DoodleTransform：任何改 Transform 的代码（行为组件、动画、脚本）都会
     * 自动反映到画面上，相机不需要知道是谁在驱动它。
     *
     * 组件不能脱离宿主使用：view 依赖宿主 Transform（m_pGameObject）。相机只能
     * 通过 GameObject::AddComponent<DoodleCamera>() 创建；直接构造后指望算 view
     * 会拿到空宿主，是错误用法。
     *
     * 交换链预旋转不在这里。投影只负责「显示方向 + Vulkan 裁剪空间」，
     * 预旋转是把算好的画面转到交换链图像朝向的最后一步，由渲染门面左乘
     * （见 DoodleVulkanManager::UpdateUniformBuffer）—— 两者顺序不能换。
     */
    class DoodleCamera final : public DoodleComponent
    {
    public:
        /**
         * 登记进相机注册表，按优先级排好并重算主相机
         *
         * 登记放在 OnAwake 而不是 OnEnable：停用的相机也留在注册表里，
         * 挑选时按 IsEnabled 过滤，启停只需要重算、不需要增删注册项
         */
        void OnAwake() override;

        /**
         * 启用状态变了，主相机候选集合跟着变，重算
         */
        void OnEnable() override;

        /**
         * 停用后不再是候选，重算
         */
        void OnDisable() override;

        /**
         * 从注册表摘除自己并重算
         *
         * 先摘除再重算：重算结果不可能再落回一个正在销毁的组件。
         * 不使用析构函数：Debug 校验探针是一个没有宿主的栈上临时实例，
         * 它不会走 OnDestroy，用生命周期钩子就不会污染注册表
         */
        void OnDestroy() override;

        /**
         * 当前主相机；没有可用的 Base 相机时返回 nullptr
         *
         * O(1) 读取，不遍历场景：注册表在相机登记、销毁、启停与选择参数变化时
         * 已经重算好。返回值非拥有，只能当场使用，不要缓存成跨帧的成员
         */
        [[nodiscard]] static const DoodleCamera* GetMainCamera()
        {
            return s_pMainCamera;
        }

        /**
         * 把投影参数写回序列化配置
         *
         * projection / renderType 存枚举整数；priority 原样写；fov 内部存弧度，
         * 写回时转成度；near / far / orthoHalfHeight 原样写。
         */
        void Serialize(DoodleComponentConfig& config) const override;

        /**
         * 从序列化配置读取投影参数与渲染角色
         *
         * 字段：projection（0 = perspective，1 = orthographic）、
         * renderType（0 = base，1 = overlay）、priority、fov（度）、
         * near、far、orthoHalfHeight。不依赖宿主，Debug 校验探针也可以调用。
         */
        void Deserialize(const DoodleComponentConfig& config) override;

        /**
         * 渲染角色：Base 参与主相机挑选，Overlay 只保存、不参与
         */
        [[nodiscard]] ECameraRenderType GetRenderType() const
        {
            return m_renderType;
        }

        /**
         * 切换渲染角色；运行中改会立即重算主相机
         */
        void SetRenderType(const ECameraRenderType type);

        /**
         * 挑选主相机时的优先级，只对 Base 类型有效；高者胜
         *
         * 同优先级的先后不作承诺（列表里靠地址定序）。注意它和
         * DoodleVirtualCamera 的 priority 不是一回事：那里挑的是驱动相机的
         * 虚拟相机，这里挑的是实际渲染用的物理相机
         */
        [[nodiscard]] int GetPriority() const
        {
            return m_priority;
        }

        /**
         * 设置优先级；运行中改会立即重排并重算主相机
         */
        void SetPriority(const int priority);

        /**
         * 从宿主 Transform 生成 view 矩阵
         *
         * 用 Transform 自己的 up，而不是世界 +Z：相机挂在一个滚转的对象上时
         * 画面跟着滚，这是「相机只读载体姿态」的预期行为。自由相机的
         * 无滚转由控制它的行为组件负责，不在这里做。
         */
        [[nodiscard]] glm::mat4 GetViewMatrix() const;

        /**
         * 按显示方向的宽高比生成投影矩阵
         *
         * 只做投影 + Vulkan 的 Y 翻转，不含交换链预旋转。宽高比必须由调用方
         * 按**显示方向**算好（交换链 90/270 时与图像宽高的关系是对调的），
         * 见 DoodleVulkanUtils::GetDisplayAspect。
         *
         * 投影方式由 GetProjectionType 决定：
         * - Perspective：用垂直 FOV 生成透视投影；
         * - Orthographic：用正交半高生成平行投影，宽度按 aspect 展开，
         *   没有近大远小。
         *
         * @param aspect 显示方向的 width / height
         */
        [[nodiscard]] glm::mat4 GetProjectionMatrix(const float aspect) const;

        /**
         * 当前投影方式
         */
        [[nodiscard]] ECameraProjectionType GetProjectionType() const
        {
            return m_projectionType;
        }

        /**
         * 切换投影方式；切换只影响下一帧的投影矩阵，不改位姿
         */
        void SetProjectionType(const ECameraProjectionType type)
        {
            m_projectionType = type;
        }

        /**
         * 垂直视场角，单位是弧度
         *
         * 只在 Perspective 下参与投影；必须为 (0, π)
         */
        void SetFovY(const float fovY)
        {
            m_fovY = fovY;
        }

        /**
         * 垂直视场角（弧度）
         */
        [[nodiscard]] float GetFovY() const
        {
            return m_fovY;
        }

        /**
         * 近平面，必须为正数；透视与平行投影都用它做裁剪
         */
        void SetNearPlane(const float nearPlane)
        {
            m_near = nearPlane;
        }

        /**
         * 近平面
         */
        [[nodiscard]] float GetNearPlane() const
        {
            return m_near;
        }

        /**
         * 远平面，必须大于近平面；透视与平行投影都用它做裁剪
         */
        void SetFarPlane(const float farPlane)
        {
            m_far = farPlane;
        }

        /**
         * 远平面
         */
        [[nodiscard]] float GetFarPlane() const
        {
            return m_far;
        }

        /**
         * 正交投影的可见高度是 2 × halfHeight，单位与世界坐标一致
         *
         * 只在 Orthographic 下参与投影；必须为正数
         */
        void SetOrthoHalfHeight(const float halfHeight)
        {
            m_orthoHalfHeight = halfHeight;
        }

        /**
         * 正交投影的半高
         */
        [[nodiscard]] float GetOrthoHalfHeight() const
        {
            return m_orthoHalfHeight;
        }

    private:
        /**
         * 重算主相机
         *
         * s_cameras 已按优先级降序排好，这里只取第一个启用的 Base 相机，
         * 不再逐个比较优先级
         */
        static void RefreshMainCamera();

        /**
         * 按「优先级降序 → 地址升序」重排 s_cameras
         *
         * 优先级是排序键，所以登记与改优先级之后都要调用；启用状态与渲染
         * 角色不参与排序，启停、切换 Base / Overlay 都不需要重排。
         *
         * 同优先级用地址定序：只为让排序成为严格全序、结果确定可复现，
         * 不承诺同优先级之间谁先谁后（需要「先创建者胜」时再加创建序号）
         */
        static void SortCameras();

    private:
        /**
         * 所有存活的相机组件，按「优先级降序 → 地址升序」排列
         *
         * 由 OnAwake 登记、OnDestroy 摘除。停用的相机与 Overlay 也留在其中，
         * 刷新主相机时按 IsEnabled / renderType 过滤。只在主线程使用，不做同步
         */
        static std::vector<DoodleCamera*> s_cameras;

        /**
         * 当前主相机，由 s_cameras 重算得出；没有可用相机时为空
         */
        static const DoodleCamera* s_pMainCamera;

    private:
        /**
         * 渲染角色，默认 Base
         */
        ECameraRenderType m_renderType = ECameraRenderType::Base;

        /**
         * 主相机挑选优先级，默认 0；只对 Base 有效
         */
        int m_priority = 0;

        /**
         * 投影方式，默认透视
         */
        ECameraProjectionType m_projectionType = ECameraProjectionType::Perspective;

        /**
         * 垂直视场角（弧度）。GLM::perspective 收的是垂直角；只在透视下使用
         */
        float m_fovY = glm::radians(45.0f);

        /**
         * 正交投影的半高（可见高度 = 2 × 它），单位与世界坐标一致
         *
         * 2.0 大致把原点附近的立方体框进来；需要更精确的取景时用
         * SetOrthoHalfHeight 调整
         */
        float m_orthoHalfHeight = 2.0f;

        /**
         * 近平面
         */
        float m_near = 0.1f;

        /**
         * 远平面
         *
         * 从 A3 的 10 放宽到 100：自由相机飞远后立方体不被裁掉
         */
        float m_far = 100.0f;
    };
}
