//
//  DoodleTextureLoader.cpp
//  Doodle
//
//  由 DoodleTexture 资产化新增
//

#include "DoodleTextureLoader.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <stb/stb_image.h>

#include "Platform/DoodleFileSystem.h"
#include "Vulkan/DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleTextureLoader::DoodleTextureLoader(DoodleVulkanDevice& device,
                                             const DoodleFileSystem& fileSystem)
        : DoodleAssetLoader<DoodleTexture>(fileSystem), m_pDevice(&device)
    {
    }

    std::unique_ptr<DoodleTexture> DoodleTextureLoader::Create(const std::string_view key)
    {
        const std::vector<std::byte> bytes = GetFileSystem().ReadBinary(key);
        if (bytes.empty())
        {
            throw std::runtime_error("texture file is empty: " + std::string(key));
        }
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw std::runtime_error("texture file is too large: " + std::string(key));
        }

        int width = 0;
        int height = 0;

        //统一转成 RGBA8：DoodleTexture 目前只接受这一种格式。
        //stb 的返回缓冲由 stbi_image_free 释放，交给 unique_ptr 兜住 Initialize 的异常路径
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            nullptr,
            STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            const char* reason = stbi_failure_reason();
            throw std::runtime_error("failed to decode texture '" + std::string(key) + "': " +
                                     (reason != nullptr ? reason : "unknown error"));
        }

        const std::unique_ptr<stbi_uc, void (*)(stbi_uc*)> pixelData(
            pixels,
            [](stbi_uc* data)
            {
                stbi_image_free(data);
            });

        if (width <= 0 || height <= 0)
        {
            throw std::runtime_error("texture has invalid dimensions '" + std::string(key) + "': " +
                                     std::to_string(width) + "x" + std::to_string(height));
        }

        const auto pixelCount = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);
        const VkDeviceSize requiredSize = pixelCount * 4;

        auto texture = std::make_unique<DoodleTexture>(key);
        texture->Initialize(*m_pDevice,
                            static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height),
                            pixels,
                            requiredSize);
        return texture;
    }

    void DoodleTextureLoader::DestroyAsset(DoodleTexture& texture)
    {
        texture.Destroy();
    }
}
