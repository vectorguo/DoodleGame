//
//  DoodleMaterialLoader.cpp
//  Doodle
//
//  由 DoodleMaterial 资产化新增
//

#include "DoodleMaterialLoader.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "Core/DoodleComponentConfig.h"
#include "DoodleAssetManager.h"
#include "Platform/DoodleFileSystem.h"
#include "Rendering/DoodleShader.h"
#include "Rendering/DoodleTexture.h"

namespace Doodle
{
    DoodleMaterialLoader::DoodleMaterialLoader(const DoodleFileSystem& fileSystem)
        : DoodleAssetLoader(fileSystem)
    {
    }

    std::unique_ptr<DoodleMaterial> DoodleMaterialLoader::Create(const std::string_view key)
    {
        nlohmann::json root;
        try
        {
            root = nlohmann::json::parse(GetFileSystem().ReadText(key));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("material '" + std::string(key) + "': " + error.what());
        }

        if (!root.is_object())
        {
            throw std::runtime_error("material '" + std::string(key) + "': expected a JSON object");
        }

        try
        {
            //材质文件与组件 settings 是同一种 key-value 形状，复用配置的
            //类型化读取与错误口径；字段白名单与场景组件一样只在 Debug 检查
            const DoodleComponentConfig config(std::move(root));

#ifndef NDEBUG
            config.ExpectOnlyKeys({"baseColor", "texture", "shader"});
#endif
            auto material = std::make_unique<DoodleMaterial>(key);
            material->SetBaseColor(config.GetVec4("baseColor", material->GetBaseColor().ToVec4()));

            const std::string texturePath = config.GetString("texture", "");
            if (!texturePath.empty())
            {
                //纹理是共享资产：同一路径在多个材质之间由资产管理器去重
                const auto* pTexture = DoodleAssetManager::GetInstance().Load<DoodleTexture>(texturePath);
                material->SetTexture(pTexture);
            }

            //着色器省略时用 lit：绝大多数材质只改颜色 / 贴图，不换着色器。
            //着色器是共享资产，同一路径的多个材质拿到同一个 DoodleShader
            const std::string shaderPath = config.GetString("shader", "Shaders/lit.shader.json");
            const auto* pShader = DoodleAssetManager::GetInstance().Load<DoodleShader>(shaderPath);
            if (pShader == nullptr)
            {
                // Loader 的契约是不返回空指针；这里只做防御
                throw std::runtime_error("shader: shader '" + shaderPath + "' is missing");
            }
            material->SetShader(pShader);

            //管线不在这里解析：材质只记着色器，由录制侧的各个 pass 按自己的
            //着色器侧描述向自己的管线表要 —— 加载器不知道有几个 pass，也不该知道

            return material;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("material '" + std::string(key) + "': " + error.what());
        }
    }
}
