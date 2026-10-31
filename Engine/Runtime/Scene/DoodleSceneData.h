//
//  DoodleSceneData.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/DoodleComponentConfig.h"

namespace Doodle
{
    /**
     * 场景文件里的一个组件条目
     *
     * typeName 是注册表里的稳定类型名（例如 "Engine.Camera"）；config 是
     * 组件自己的配置，由组件解释。引擎不展开具体字段，也不认识 typeName
     * 对应的 C++ 类型。
     */
    struct DoodleComponentData
    {
        /**
         * 组件类型名，对应 DoodleComponentRegistry 的 name
         */
        std::string typeName;

        /**
         * 错误消息用的路径，例如
         * "Scenes/main.json: gameObjects[2].components[0]"
         */
        std::string path;

        /**
         * 组件配置；配置本身不带来源路径，装配层用 path 补错误前缀
         */
        DoodleComponentConfig config;
    };

    /**
     * 场景文件里的一个对象实例
     *
     * 只存实例数据：名字、位姿与组件列表。顶点、材质等属于组件 / 资产，
     * 不在这里展开；对象挂什么组件完全由 componentDataList 决定。
     */
    struct DoodleSceneObjectData
    {
        /**
         * 对象名，只用于查找与日志；允许重复
         */
        std::string name{"GameObject"};

        /**
         * 场景里的组件数据列表，按文件里的顺序创建
         */
        std::vector<DoodleComponentData> componentDataList;

        /**
         * 世界空间位置
         */
        glm::vec3 position{0.0f};

        /**
         * 欧拉角，角度制
         *
         * 只存在于文件边界：DoodleTransform 内部一律用四元数。转换顺序是
         * q = Rz * Ry * Rx（先绕 X、再绕 Y、最后绕 Z，世界轴），由
         * DoodleMath::EulerDegreesToQuaternion 完成（在
         * DoodleScene::InitializeFromData 里调用）。这样手改场景文件时不用手算四元数。
         */
        glm::vec3 rotationDegrees{0.0f};

        /**
         * 逐轴缩放
         */
        glm::vec3 scale{1.0f};
    };

    /**
     * 一个场景文件的完整内容
     *
     * CPU 侧纯数据，不持有 Vulkan 句柄，也不引用任何运行时对象。
     * 文件解析（DoodleSceneLoader）与实例化（DoodleScene::InitializeFromData）
     * 以它为界：
     * 解析可以独立测试，实例化失败也不会留下解析到一半的状态。
     */
    struct DoodleSceneData
    {
        /**
         * 场景名，允许重复
         */
        std::string name{"Scene"};

        /**
         * 场景里的对象数据列表，按文件里的顺序创建；可以为空
         */
        std::vector<DoodleSceneObjectData> gameObjectDataList;
    };
}
