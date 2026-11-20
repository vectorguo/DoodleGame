//
//  DoodleWindowsFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleWindowsFileSystem.h"
#include "DoodleWindowsPaths.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    /**
     * 按优先级给出候选路径
     *
     * 两个候选都是字符串而不是 filesystem::path：调用方给的就是字符串，
     * 报错时原样带出去，避免路径在转换过程中被规范化成另一种样子。
     */
    std::vector<std::string> BuildCandidates(const std::string_view path)
    {
        std::vector<std::string> candidates{std::string(path)};

        const std::string executableDirectory = Doodle::GetExecutableDirectory();
        if (!executableDirectory.empty())
        {
            candidates.push_back(
                (std::filesystem::path(executableDirectory) / path)
                .lexically_normal()
                .string());
        }

        return candidates;
    }
}

namespace Doodle
{
    bool DoodleWindowsFileSystem::Exists(const std::string_view path) const
    {
        for (const auto& candidate : BuildCandidates(path))
        {
            std::error_code errorCode;
            if (std::filesystem::is_regular_file(candidate, errorCode))
            {
                return true;
            }
        }

        return false;
    }

    std::vector<std::byte> DoodleWindowsFileSystem::ReadBinary(const std::string_view path) const
    {
        std::string triedPaths;

        for (const auto& candidate : BuildCandidates(path))
        {
            // ate    ：打开后直接定位到末尾，tellg 一次拿到大小
            // binary ：不转换换行。Windows 上文本模式会把 \r\n 换成 \n，读二进制资产时是必须的
            std::ifstream file(candidate, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                // 报错给绝对路径，方便判断「工作目录到底对不对」
                std::error_code errorCode;
                const auto absolutePath = std::filesystem::absolute(candidate, errorCode).string();
                triedPaths += "\n  tried: " + (errorCode ? candidate : absolutePath);
                continue;
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
                throw std::runtime_error("failed to read file: " + candidate);
            }

            return buffer;
        }

        throw std::runtime_error("failed to open file: " + std::string(path) + triedPaths);
    }
}
