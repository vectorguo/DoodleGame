//
//  DoodleMaterialLoader.cpp
//  Doodle
//
//  由 DoodleMaterial 资产化新增
//

#include "DoodleMaterialLoader.h"

#include <algorithm>
#include <span>
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
    namespace
    {
        /**
         * 把着色器的贴图槽名列成一句话，只用于报错：写错槽名时直接告诉作者有哪些可选
         */
        std::string DescribeTextureSlots(const std::span<const DoodleMaterialTextureSlot> slots)
        {
            if (slots.empty())
            {
                return "it declares no textures";
            }

            std::string description = "slots:";
            for (const DoodleMaterialTextureSlot& slot : slots)
            {
                description += " " + slot.name;
            }
            return description;
        }
    }

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
            config.ExpectOnlyKeys({"baseColor", "textures", "shader"});
#endif
            auto material = std::make_unique<DoodleMaterial>(key);
            material->SetBaseColor(config.GetVec4("baseColor", material->GetBaseColor().ToVec4()));

            //着色器先加载：贴图按着色器声明的槽来配，得先知道有哪些槽。
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

            //textures 可省略；写了就必须是对象：键是贴图槽名（着色器 set 1 里贴图的
            //变量名），值是贴图路径。读嵌套对象的写法与着色器加载器读 passes 相同
            const nlohmann::json& rootJson = config.GetJson();
            if (rootJson.contains("textures"))
            {
                const nlohmann::json& texturesJson = rootJson.at("textures");
                if (!texturesJson.is_object())
                {
                    throw std::runtime_error("'textures' must be an object mapping texture slot names to texture paths");
                }

                const std::span<const DoodleMaterialTextureSlot> slots = pShader->GetMaterialTextureSlots();
                for (const auto& [slotName, pathJson] : texturesJson.items())
                {
                    //槽名无条件核对（不只 Debug）：拼错的名字会让这张贴图悄悄变成白图，
                    //必须当场报错
                    const bool isKnownSlot = std::ranges::any_of(
                        slots,
                        [&slotName](const DoodleMaterialTextureSlot& slot)
                        {
                            return slot.name == slotName;
                        });
                    if (!isKnownSlot)
                    {
                        throw std::runtime_error("textures: shader '" + shaderPath + "' has no texture slot '" +
                                                 slotName + "' (" + DescribeTextureSlots(slots) + ")");
                    }

                    if (!pathJson.is_string())
                    {
                        throw std::runtime_error("textures." + slotName + ": expected a string");
                    }

                    //空串表示这个槽不配贴图，绘制侧绑定默认白图
                    const std::string texturePath = pathJson.get<std::string>();
                    if (texturePath.empty())
                    {
                        continue;
                    }

                    //贴图是共享资产：同一路径在多个材质之间由资产管理器去重
                    const auto* pTexture = DoodleAssetManager::GetInstance().Load<DoodleTexture>(texturePath);
                    material->SetTexture(slotName, pTexture);
                }
            }

            //管线不在这里解析：材质只记着色器，由录制侧的各个 pass 按材质着色器
            //向自己的管线表要 —— 加载器不知道有几个 pass，也不该知道

            return material;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("material '" + std::string(key) + "': " + error.what());
        }
    }
}
