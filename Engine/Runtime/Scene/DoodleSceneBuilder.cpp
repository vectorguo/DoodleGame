//
//  DoodleSceneBuilder.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleSceneBuilder.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "Asset/DoodleAssetManager.h"
#include "Core/DoodleGameObject.h"
#include "DoodleScene.h"
#include "DoodleSceneData.h"
#include "DoodleSceneManager.h"
#include "Rendering/DoodleMeshRenderer.h"
#include "Vulkan/DoodleMesh.h"

namespace
{
    /**
     * 文件里的欧拉角 → Transform 用的四元数
     *
     * 约定 q = Rz * Ry * Rx：先绕 X、再绕 Y、最后绕 Z，三个轴都是世界轴。
     * 单轴测试（例如 [0, 0, 45]）与旋转顺序无关，但多轴组合必须有一份
     * 写死的约定，否则同一份文件在不同实现里会得到不同的朝向。
     *
     * 不用 glm::quat(vec3)：它的分量组合顺序没有写在这边的代码里，
     * 换一个人读时还得去翻 GLM 实现。这里逐个 angleAxis 乘，顺序一目了然。
     */
    glm::quat EulerDegreesToQuaternion(const glm::vec3& degrees)
    {
        const glm::vec3 radians = glm::radians(degrees);
        return glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f))
             * glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f))
             * glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    }
}

namespace Doodle
{
    DoodleScene& DoodleSceneBuilder::Build(DoodleSceneManager& sceneManager,
                                           DoodleAssetManager& assetManager,
                                           const DoodleSceneData& sceneData)
    {
        // 阶段一：把全部网格解析完再动场景。
        //
        // 网格名写错是最常见的内容错误；如果在循环里边查边建，前面几个对象
        // 已经进了场景，错误却抛出在第三个。这里的 sceneManager.CreateScene
        // 一旦调用，就没有「撤销」的公开入口，所以校验必须排在它前面。
        std::vector<const DoodleMesh*> meshes;
        meshes.reserve(sceneData.gameObjects.size());

        for (const DoodleSceneObjectData& objectData : sceneData.gameObjects)
        {
            const DoodleMesh* pMesh = assetManager.Load<DoodleMesh>(objectData.meshKey);
            if (pMesh == nullptr)
            {
                // Loader 的契约是不返回空指针；这里只是防御，报错信息带上对象名
                throw std::runtime_error("DoodleSceneBuilder: mesh '" + objectData.meshKey +
                                         "' for GameObject '" + objectData.name + "' is missing");
            }

            meshes.push_back(pMesh);
        }

        // 阶段二：数据校验通过，开始建场景
        DoodleScene& scene = sceneManager.CreateScene(sceneData.name);

        for (std::size_t i = 0; i < sceneData.gameObjects.size(); ++i)
        {
            const DoodleSceneObjectData& objectData = sceneData.gameObjects[i];

            DoodleGameObject& gameObject = scene.CreateGameObject(objectData.name);

            DoodleTransform& transform = gameObject.GetTransform();
            transform.position = objectData.position;
            transform.rotation = EulerDegreesToQuaternion(objectData.rotationDegrees);
            transform.scale = objectData.scale;

            gameObject.AddComponent<DoodleMeshRenderer>().SetMesh(meshes[i]);
        }

        return scene;
    }
}
