//
//  DoodleSceneImporter.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#include "DoodleSceneImporter.h"

#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
    using Json = nlohmann::json;

    /**
     * 带「文件 + 字段路径」的错误
     *
     * 场景文件是手改的，错误消息必须直接指出是哪个文件、哪个字段，否则
     * 只能看到一个泛泛的 JSON 异常。sourceName 通常是 "Scenes/main.json"。
     */
    [[noreturn]] void ThrowFieldError(const std::string_view sourceName,
                                      const std::string_view path,
                                      const std::string& message)
    {
        throw std::runtime_error(std::string(sourceName) + ": " + std::string(path) + ": " + message);
    }

    std::string IndexedPath(const std::string_view path, const std::size_t index)
    {
        return std::string(path) + "[" + std::to_string(index) + "]";
    }

    /**
     * 拒绝未知字段
     *
     * 拼错字段名（"postion"）如果被静默忽略，画面不会变、也没有任何提示；
     * 对一份手改的配置文件来说，当场报错比宽容更有用。
     */
    void CheckUnknownKeys(const Json& object,
                          const std::initializer_list<std::string_view> allowedKeys,
                          const std::string_view sourceName,
                          const std::string_view path)
    {
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            const std::string_view key(it.key());
            bool known = false;
            for (const std::string_view allowed : allowedKeys)
            {
                if (key == allowed)
                {
                    known = true;
                    break;
                }
            }

            if (!known)
            {
                ThrowFieldError(sourceName, path, "unknown field '" + std::string(key) + "'");
            }
        }
    }

    glm::vec3 ReadVec3(const Json& value, const std::string_view sourceName, const std::string_view path)
    {
        if (!value.is_array() || value.size() != 3)
        {
            ThrowFieldError(sourceName, path, "expected an array of 3 numbers");
        }

        glm::vec3 result{0.0f};
        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::string elementPath = IndexedPath(path, i);
            const Json& element = value[i];

            if (!element.is_number())
            {
                ThrowFieldError(sourceName, elementPath, "expected a number");
            }

            const double number = element.get<double>();
            if (!std::isfinite(number))
            {
                ThrowFieldError(sourceName, elementPath, "must be a finite number");
            }

            const float converted = static_cast<float>(number);
            if (!std::isfinite(converted))
            {
                // double 合法但转 float 溢出（例如 1e300）
                ThrowFieldError(sourceName, elementPath, "is out of range for float");
            }

            result[i] = converted;
        }

        return result;
    }

    glm::vec3 ReadOptionalVec3(const Json& object,
                               const char* field,
                               const glm::vec3& defaultValue,
                               const std::string_view sourceName,
                               const std::string_view path)
    {
        const auto it = object.find(field);
        if (it == object.end())
        {
            return defaultValue;
        }

        return ReadVec3(*it, sourceName, std::string(path) + "." + field);
    }

    std::string ReadOptionalNonEmptyString(const Json& object,
                                           const char* field,
                                           std::string defaultValue,
                                           const std::string_view sourceName,
                                           const std::string_view path)
    {
        const auto it = object.find(field);
        if (it == object.end())
        {
            return defaultValue;
        }

        const std::string fieldPath = std::string(path) + "." + field;
        if (!it->is_string())
        {
            ThrowFieldError(sourceName, fieldPath, "expected a string");
        }

        std::string value = it->get<std::string>();
        if (value.empty())
        {
            ThrowFieldError(sourceName, fieldPath, "must not be empty");
        }

        return value;
    }

    Doodle::DoodleComponentData ParseComponent(const Json& componentJson,
                                               const std::string_view sourceName,
                                               const std::string& path)
    {
        if (!componentJson.is_object())
        {
            ThrowFieldError(sourceName, path, "expected an object");
        }

        // type / settings 是组件条目的全部顶层字段；参数内部由组件自己校验
        CheckUnknownKeys(componentJson, {"type", "settings"}, sourceName, path);

        const auto typeIt = componentJson.find("type");
        if (typeIt == componentJson.end())
        {
            ThrowFieldError(sourceName, path, "missing field 'type'");
        }

        if (!typeIt->is_string())
        {
            ThrowFieldError(sourceName, path + ".type", "expected a string");
        }

        std::string type = typeIt->get<std::string>();
        if (type.empty())
        {
            ThrowFieldError(sourceName, path + ".type", "must not be empty");
        }

        Json settingsJson = Json::object();
        const auto settingsIt = componentJson.find("settings");
        if (settingsIt != componentJson.end())
        {
            if (!settingsIt->is_object())
            {
                ThrowFieldError(sourceName, path + ".settings", "expected an object");
            }
            settingsJson = *settingsIt;
        }

        //路径带上文件名：这些路径会被 DoodleSceneData 带到装配阶段，
        //组件参数错误 / 未知类型的报错必须能指回是哪个场景文件
        const std::string sourcePath = std::string(sourceName) + ": " + path;

        Doodle::DoodleComponentData componentData;
        componentData.typeName = std::move(type);
        componentData.path = sourcePath;
        componentData.config = Doodle::DoodleComponentConfig(
            std::move(settingsJson), sourcePath + ".settings");
        return componentData;
    }

    Doodle::DoodleSceneData ParseRoot(const Json& root, const std::string_view sourceName)
    {
        if (!root.is_object())
        {
            ThrowFieldError(sourceName, "$", "root must be an object");
        }

        CheckUnknownKeys(root, {"name", "gameObjects"}, sourceName, "$");

        Doodle::DoodleSceneData sceneData;
        sceneData.name = ReadOptionalNonEmptyString(root, "name", "Scene", sourceName, "$");

        const auto gameObjectsIt = root.find("gameObjects");
        if (gameObjectsIt == root.end())
        {
            ThrowFieldError(sourceName, "$", "missing field 'gameObjects'");
        }

        if (!gameObjectsIt->is_array())
        {
            ThrowFieldError(sourceName, "gameObjects", "expected an array");
        }

        sceneData.gameObjectDataList.reserve(gameObjectsIt->size());

        for (std::size_t i = 0; i < gameObjectsIt->size(); ++i)
        {
            const std::string objectPath = IndexedPath("gameObjects", i);
            const Json& objectJson = (*gameObjectsIt)[i];

            if (!objectJson.is_object())
            {
                ThrowFieldError(sourceName, objectPath, "expected an object");
            }

            CheckUnknownKeys(objectJson,
                             {"name", "position", "rotation", "scale", "components"},
                             sourceName, objectPath);

            Doodle::DoodleSceneObjectData objectData;
            objectData.name = ReadOptionalNonEmptyString(objectJson, "name", "GameObject",
                                                         sourceName, objectPath);

            // 组件列表是对象内容的唯一来源：mesh、camera 等引擎组件也走这里。
            // 只校验条目的形状，具体字段留给组件自己的 Deserialize；type 是否注册、
            // 参数是否合法由 DoodleScene::InitializeFromData 在创建对象之前检查
            const auto componentsIt = objectJson.find("components");
            if (componentsIt != objectJson.end())
            {
                if (!componentsIt->is_array())
                {
                    ThrowFieldError(sourceName, objectPath + ".components", "expected an array");
                }

                objectData.componentDataList.reserve(componentsIt->size());
                for (std::size_t j = 0; j < componentsIt->size(); ++j)
                {
                    const std::string componentPath =
                        IndexedPath(objectPath + ".components", j);
                    objectData.componentDataList.push_back(
                        ParseComponent((*componentsIt)[j], sourceName, componentPath));
                }
            }

            // 没有组件的对象在当前模型里什么也不做：没有层级，它连
            // 「只提供变换的父节点」都当不了。多半是漏了字段，当场报错
            if (objectData.componentDataList.empty())
            {
                ThrowFieldError(sourceName, objectPath, "must have at least one component");
            }

            objectData.position = ReadOptionalVec3(objectJson, "position", glm::vec3(0.0f),
                                                   sourceName, objectPath);
            objectData.rotationDegrees = ReadOptionalVec3(objectJson, "rotation", glm::vec3(0.0f),
                                                          sourceName, objectPath);
            objectData.scale = ReadOptionalVec3(objectJson, "scale", glm::vec3(1.0f),
                                                sourceName, objectPath);

            sceneData.gameObjectDataList.push_back(std::move(objectData));
        }

        return sceneData;
    }
}

namespace Doodle
{
    DoodleSceneData DoodleSceneImporter::Import(const std::span<const std::byte> bytes,
                                                const std::string_view sourceName)
    {
        if (bytes.empty())
        {
            throw std::runtime_error(std::string(sourceName) + ": scene file is empty");
        }

        const auto* pBegin = reinterpret_cast<const char*>(bytes.data());
        const auto* pEnd = pBegin + bytes.size();

        Json root;
        try
        {
            root = Json::parse(pBegin, pEnd);
        }
        catch (const nlohmann::json::parse_error& error)
        {
            throw std::runtime_error(std::string(sourceName) + ": JSON parse error at byte " + std::to_string(error.byte) + ": " + error.what());
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string(sourceName) + ": JSON parse error: " + error.what());
        }

        return ParseRoot(root, sourceName);
    }
}
