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

#include "Rendering/DoodleCamera.h"

namespace Doodle
{
    /**
     * 场景文件里的一个对象实例
     *
     * 只存实例数据：名字、网格资产键、位姿。顶点数据属于 DoodleMesh，
     * 材质 / 贴图将来同样是按名字引用，不在这里展开。
     */
    struct DoodleSceneObjectData
    {
        /**
         * 对象名，只用于查找与日志；允许重复
         */
        std::string name{"GameObject"};

        /**
         * 网格资产的键，交给 DoodleAssetManager::Load<DoodleMesh>
         *
         * 现在是内建名（"builtin:cube"）；将来读外部网格时这里就是相对路径。
         * 可以为空：对象可以只挂相机，不带网格
         */
        std::string meshKey;

        /**
         * 是否挂 DoodleCamera 组件
         *
         * 相机是普通组件，位姿与其它对象一样来自本结构的 position / rotation。
         * 一个对象可以既有网格又有相机，也可以只有其中之一
         */
        bool hasCamera = false;

        /**
         * 相机的投影方式；只有 hasCamera 为 true 时有意义
         *
         * 默认透视。场景文件里通过 "camera" 对象的 "type" 字段选择
         */
        ECameraProjectionType cameraProjection = ECameraProjectionType::Perspective;

        /**
         * 世界空间位置
         */
        glm::vec3 position{0.0f};

        /**
         * 欧拉角，角度制
         *
         * 只存在于文件边界：DoodleTransform 内部一律用四元数。转换顺序是
         * q = Rz * Ry * Rx（先绕 X、再绕 Y、最后绕 Z，世界轴），由
         * DoodleScene::InitializeFromData 完成。这样手改场景文件时不用手算四元数。
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
         * 对象实例，按文件里的顺序创建；可以为空
         */
        std::vector<DoodleSceneObjectData> gameObjects;
    };
}
