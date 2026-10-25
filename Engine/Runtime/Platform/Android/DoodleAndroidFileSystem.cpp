//
//  DoodleAndroidFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleAndroidFileSystem.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Doodle
{
    bool DoodleAndroidFileSystem::Exists(const std::string_view path) const
    {
        std::error_code errorCode;
        return std::filesystem::is_regular_file(std::filesystem::path(path), errorCode);
    }

    std::vector<std::byte> DoodleAndroidFileSystem::ReadBinary(const std::string_view path) const
    {
        // 第 5 步：APK assets 走 AAssetManager，这条文件系统分支保留给外部文件。
        // 现在没有 assets 来源，读不到时给出明确的错误，而不是静默返回空
        std::ifstream file(std::string(path), std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error(
                "failed to open file: " + std::string(path) +
                " (APK assets source is not implemented yet)");
        }

        const auto fileSize = static_cast<std::size_t>(file.tellg());
        std::vector<std::byte> buffer(fileSize);

        file.seekg(0);
        if (fileSize > 0)
        {
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
        }

        if (!file)
        {
            throw std::runtime_error("failed to read file: " + std::string(path));
        }

        return buffer;
    }
}
