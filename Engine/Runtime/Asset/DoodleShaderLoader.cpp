//
//  DoodleShaderLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/6.
//

#include "DoodleShaderLoader.h"

#include <algorithm>
#include <cstdint>
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
            config.ExpectOnlyKeys({"passes", "vertexFormat", "instanceFormat"});
#endif
            const EVertexFormat vertexFormat = ParseVertexFormat(config.GetString("vertexFormat", "posNormalUv"));
            const EInstanceFormat instanceFormat = ParseInstanceFormat(config.GetString("instanceFormat", "none"));

            //passes 是必须的对象，且至少一节（DoodleShader::Initialize 不再核对）；
            //每个 key 是 pass 名，value 是该 pass 的源文件
            const nlohmann::json& rootJson = config.GetJson();
            if (!rootJson.contains("passes") || !rootJson.at("passes").is_object())
            {
                throw std::runtime_error("'passes' must be an object with at least one pass");
            }
            const nlohmann::json& passesJson = rootJson.at("passes");
            if (passesJson.empty())
            {
                throw std::runtime_error("'passes' must contain at least one pass");
            }

            std::vector<DoodleShaderInitInfo::PassInitInfo> passInfos;
            passInfos.reserve(passesJson.size());
            for (const auto& [passName, passJson] : passesJson.items())
            {
                if (!passJson.is_object())
                {
                    throw std::runtime_error("pass '" + passName + "' must be an object");
                }

                //pass 名无条件解析：拼错的名字会让整节描述凭空消失，必须当场报错
                const EDoodleRenderPassType pass = ParseShaderPass(passName);

                //JSON 对象的键本身不重复，这里防的是两个名字解析成同一个 pass。
                //DoodleShader::Initialize 不再查重：重复会让后一节悄悄覆盖前一节的描述
                const bool duplicated = std::ranges::any_of(
                    passInfos,
                    [pass](const DoodleShaderInitInfo::PassInitInfo& existing)
                    {
                        return existing.passType == pass;
                    });
                if (duplicated)
                {
                    throw std::runtime_error("pass '" + passName + "' is declared more than once");
                }

                const DoodleComponentConfig passConfig(passJson);
#ifndef NDEBUG
                passConfig.ExpectOnlyKeys({"vertex", "fragment", "renderState"});
#endif
                const std::string vertexPath = passConfig.GetString("vertex", "");
                const std::string fragmentPath = passConfig.GetString("fragment", "");
                if (vertexPath.empty() || fragmentPath.empty())
                {
                    throw std::runtime_error("pass '" + passName + "': 'vertex' and 'fragment' must both name a SPIR-V file");
                }

                //renderState 整块可选：缺省就用渲染状态的默认值（剔除背面、深度测试与
                //写入都开、LESS、不透明、不偏移、三角形列表）
                DoodleRenderState renderState;
                if (passJson.contains("renderState"))
                {
                    const DoodleComponentConfig renderConfig(passJson.at("renderState"));
#ifndef NDEBUG
                    renderConfig.ExpectOnlyKeys({"cullMode", "depthTest", "depthWrite", "depthCompare",
                                                 "blendMode", "depthBiasEnabled", "depthBiasConstant",
                                                 "depthBiasSlope", "topology"});
#endif
                    renderState = ReadRenderState(renderConfig);
                }

                DoodleShaderInitInfo::PassInitInfo passInfo;
                passInfo.passType = pass;
                passInfo.vertexBytecode = ReadSpirvFile(GetFileSystem(), vertexPath);
                passInfo.fragmentBytecode = ReadSpirvFile(GetFileSystem(), fragmentPath);
                passInfo.passRenderState = renderState;
                passInfos.push_back(std::move(passInfo));
            }

            //DoodleShader 的构造只对本类开放（友元），make_unique 访问不到；裸 new 立刻交给 unique_ptr
            auto shader = std::unique_ptr<DoodleShader>(new DoodleShader(key));
            shader->Initialize(
            {
                .device = *m_pDevice,
                .vertexFormat = vertexFormat,
                .instanceFormat = instanceFormat,
                .passInitInfos = passInfos
            });
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

    std::vector<std::byte> DoodleShaderLoader::ReadSpirvFile(const DoodleFileSystem& fileSystem,
                                                            const std::string& path)
    {
        //SPIR-V 是 32 位字的序列，开头 5 个字是模块头（magic、版本、生成器、bound、schema）
        constexpr std::size_t kWordSize = sizeof(std::uint32_t);
        constexpr std::size_t kHeaderSize = 5 * kWordSize;

        std::vector<std::byte> bytecode = ReadShaderFile(fileSystem, path);
        if (bytecode.size() < kHeaderSize || bytecode.size() % kWordSize != 0)
        {
            throw std::runtime_error("SPIR-V file '" + path + "' is " + std::to_string(bytecode.size()) +
                                     " bytes; a module needs at least " + std::to_string(kHeaderSize) +
                                     " bytes (the 5-word header) and a multiple of " + std::to_string(kWordSize));
        }
        return bytecode;
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

    EDoodleRenderPassType DoodleShaderLoader::ParseShaderPass(const std::string& value)
    {
        if (value == "forward")
        {
            return EDoodleRenderPassType::Forward;
        }
        if (value == "shadow")
        {
            return EDoodleRenderPassType::Shadow;
        }
        throw std::runtime_error("unknown pass '" + value + "'");
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
