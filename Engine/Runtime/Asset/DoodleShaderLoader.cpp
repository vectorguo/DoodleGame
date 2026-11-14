//
//  DoodleShaderLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#include "DoodleShaderLoader.h"

#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "Core/DoodleComponentConfig.h"
#include "Platform/DoodleFileSystem.h"
#include "Vulkan/DoodleShaderModule.h"
#include "Vulkan/DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleShaderLoader::DoodleShaderLoader(DoodleVulkanDevice& device,
                                           const DoodleFileSystem& fileSystem)
        : DoodleAssetLoader<DoodleShader>(fileSystem), m_pDevice(&device)
    {
    }

    std::unique_ptr<DoodleShader> DoodleShaderLoader::Create(const std::string_view key)
    {
        const std::string shaderPath(key);

        nlohmann::json root;
        try
        {
            const std::vector<std::byte> descriptionBytes = ReadShaderFile(GetFileSystem(), shaderPath);
            const std::string descriptionText(reinterpret_cast<const char*>(descriptionBytes.data()),
                                              descriptionBytes.size());
            root = nlohmann::json::parse(descriptionText);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("shader '" + shaderPath + "': " + error.what());
        }

        try
        {
            const DoodleComponentConfig config(std::move(root));

#ifndef NDEBUG
            config.ExpectOnlyKeys({"vertex", "fragment", "vertexFormat", "instanceFormat", "renderState"});
#endif
            const std::string vertexPath = config.GetString("vertex", "");
            const std::string fragmentPath = config.GetString("fragment", "");
            if (vertexPath.empty() || fragmentPath.empty())
            {
                throw std::runtime_error("'vertex' and 'fragment' must both name a SPIR-V file");
            }

            const EVertexFormat vertexFormat = ParseVertexFormat(config.GetString("vertexFormat", "posNormalUv"));
            const EInstanceFormat instanceFormat = ParseInstanceFormat(config.GetString("instanceFormat", "none"));

            //renderState 整块可选：整个缺省就用渲染状态的默认值（剔除背面、深度测试与
            //写入都开、LESS、不透明、不偏移、三角形列表）
            DoodleRenderState renderState;
            const nlohmann::json& rootJson = config.GetJson();
            if (rootJson.contains("renderState"))
            {
                const DoodleComponentConfig renderConfig(rootJson.at("renderState"));
#ifndef NDEBUG
                renderConfig.ExpectOnlyKeys({"cullMode", "depthTest", "depthWrite", "depthCompare",
                                             "blendMode", "depthBiasEnabled", "depthBiasConstant",
                                             "depthBiasSlope", "topology"});
#endif
                renderState = ReadRenderState(renderConfig);
            }

            const std::vector<std::byte> vertexBytecode = ReadShaderFile(GetFileSystem(), vertexPath);
            const std::vector<std::byte> fragmentBytecode = ReadShaderFile(GetFileSystem(), fragmentPath);

            auto shader = std::make_unique<DoodleShader>(key);
            shader->Initialize(*m_pDevice, vertexBytecode, fragmentBytecode, renderState, vertexFormat, instanceFormat);
            return shader;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("shader '" + shaderPath + "': " + error.what());
        }
    }

    void DoodleShaderLoader::DestroyAsset(DoodleShader& shader)
    {
        shader.Destroy();
    }

    std::vector<std::byte> DoodleShaderLoader::ReadShaderFile(const DoodleFileSystem& fileSystem,
                                                             const std::string& path)
    {
        try
        {
            return fileSystem.ReadBinary(path);
        }
        catch (const std::runtime_error&)
        {
            //文件来源读不到：Android 的 APK assets 里没有 Shaders/，桌面上从别处启动时
            //工作目录也可能不对。内嵌副本与运行时文件出自同一次构建，退到它
            return DoodleShaderModule::ReadFile(path);
        }
    }

    DoodleRenderState DoodleShaderLoader::ReadRenderState(const DoodleComponentConfig& config)
    {
        DoodleRenderState renderState;

        renderState.cullMode = ParseCullMode(config.GetString("cullMode", "back"));
        renderState.depthTestEnabled = config.GetBool("depthTest", renderState.depthTestEnabled);
        renderState.depthWriteEnabled = config.GetBool("depthWrite", renderState.depthWriteEnabled);
        renderState.depthCompareOp = ParseCompareOp(config.GetString("depthCompare", "less"));
        renderState.blendMode = ParseBlendMode(config.GetString("blendMode", "opaque"));
        renderState.depthBiasEnabled = config.GetBool("depthBiasEnabled", renderState.depthBiasEnabled);
        renderState.depthBiasConstantFactor = config.GetFloat("depthBiasConstant", renderState.depthBiasConstantFactor);
        renderState.depthBiasSlopeFactor = config.GetFloat("depthBiasSlope", renderState.depthBiasSlopeFactor);
        renderState.topology = ParseTopology(config.GetString("topology", "triangleList"));
        return renderState;
    }

    EVertexFormat DoodleShaderLoader::ParseVertexFormat(const std::string& value)
    {
        if (value == "posNormalUv")
        {
            return EVertexFormat::PosNormalUv;
        }
        throw std::runtime_error("vertexFormat: unknown value '" + value + "'");
    }

    EInstanceFormat DoodleShaderLoader::ParseInstanceFormat(const std::string& value)
    {
        if (value == "none")
        {
            return EInstanceFormat::None;
        }
        throw std::runtime_error("instanceFormat: unknown value '" + value + "'");
    }

    ECullMode DoodleShaderLoader::ParseCullMode(const std::string& value)
    {
        if (value == "none")
        {
            return ECullMode::None;
        }
        if (value == "back")
        {
            return ECullMode::Back;
        }
        if (value == "front")
        {
            return ECullMode::Front;
        }
        throw std::runtime_error("renderState.cullMode: unknown value '" + value + "'");
    }

    ECompareOp DoodleShaderLoader::ParseCompareOp(const std::string& value)
    {
        if (value == "never")
        {
            return ECompareOp::Never;
        }
        if (value == "less")
        {
            return ECompareOp::Less;
        }
        if (value == "equal")
        {
            return ECompareOp::Equal;
        }
        if (value == "lessOrEqual")
        {
            return ECompareOp::LessOrEqual;
        }
        if (value == "greater")
        {
            return ECompareOp::Greater;
        }
        if (value == "notEqual")
        {
            return ECompareOp::NotEqual;
        }
        if (value == "greaterOrEqual")
        {
            return ECompareOp::GreaterOrEqual;
        }
        if (value == "always")
        {
            return ECompareOp::Always;
        }
        throw std::runtime_error("renderState.depthCompare: unknown value '" + value + "'");
    }

    EBlendMode DoodleShaderLoader::ParseBlendMode(const std::string& value)
    {
        if (value == "opaque")
        {
            return EBlendMode::Opaque;
        }
        if (value == "alphaBlend")
        {
            return EBlendMode::AlphaBlend;
        }
        if (value == "additive")
        {
            return EBlendMode::Additive;
        }
        if (value == "premultipliedAlpha")
        {
            return EBlendMode::PremultipliedAlpha;
        }
        throw std::runtime_error("renderState.blendMode: unknown value '" + value + "'");
    }

    EPrimitiveTopology DoodleShaderLoader::ParseTopology(const std::string& value)
    {
        if (value == "triangleList")
        {
            return EPrimitiveTopology::TriangleList;
        }
        if (value == "lineList")
        {
            return EPrimitiveTopology::LineList;
        }
        throw std::runtime_error("renderState.topology: unknown value '" + value + "'");
    }
}
