//
//  DoodleSceneLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleSceneLoader.h"

#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Platform/DoodleFileSystem.h"

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

        sceneData.gameObjects.reserve(gameObjectsIt->size());

        for (std::size_t i = 0; i < gameObjectsIt->size(); ++i)
        {
            const std::string objectPath = IndexedPath("gameObjects", i);
            const Json& objectJson = (*gameObjectsIt)[i];

            if (!objectJson.is_object())
            {
                ThrowFieldError(sourceName, objectPath, "expected an object");
            }

            CheckUnknownKeys(objectJson, {"name", "mesh", "position", "rotation", "scale"},
                             sourceName, objectPath);

            Doodle::DoodleSceneObjectData objectData;
            objectData.name = ReadOptionalNonEmptyString(objectJson, "name", "GameObject",
                                                         sourceName, objectPath);

            const auto meshIt = objectJson.find("mesh");
            if (meshIt == objectJson.end())
            {
                ThrowFieldError(sourceName, objectPath, "missing field 'mesh'");
            }

            if (!meshIt->is_string())
            {
                ThrowFieldError(sourceName, objectPath + ".mesh", "expected a string");
            }

            objectData.meshKey = meshIt->get<std::string>();
            if (objectData.meshKey.empty())
            {
                ThrowFieldError(sourceName, objectPath + ".mesh", "must not be empty");
            }

            objectData.position = ReadOptionalVec3(objectJson, "position", glm::vec3(0.0f),
                                                   sourceName, objectPath);
            objectData.rotationDegrees = ReadOptionalVec3(objectJson, "rotation", glm::vec3(0.0f),
                                                          sourceName, objectPath);
            objectData.scale = ReadOptionalVec3(objectJson, "scale", glm::vec3(1.0f),
                                                sourceName, objectPath);

            sceneData.gameObjects.push_back(std::move(objectData));
        }

        return sceneData;
    }
}

namespace Doodle
{
    const DoodleSceneData* DoodleSceneLoader::Load(const DoodleAssetLoadContext& context,
                                                   const std::string_view key)
    {
        const auto it = m_sceneData.find(key);
        if (it != m_sceneData.end())
        {
            return &it->second;
        }

        const std::vector<std::byte> bytes = context.fileSystem.ReadBinary(key);
        DoodleSceneData sceneData = Parse(bytes, key);

        // map 的节点地址稳定：这里返回的指针不会因为后续插入而失效
        const auto [insertedIt, inserted] = m_sceneData.emplace(std::string(key), std::move(sceneData));
        (void)inserted;
        return &insertedIt->second;
    }

    void DoodleSceneLoader::Destroy()
    {
        m_sceneData.clear();
    }

    DoodleSceneData DoodleSceneLoader::Parse(const std::span<const std::byte> bytes,
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
            throw std::runtime_error(std::string(sourceName) + ": JSON parse error at byte " +
                                     std::to_string(error.byte) + ": " + error.what());
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string(sourceName) + ": JSON parse error: " + error.what());
        }

        return ParseRoot(root, sourceName);
    }
}
