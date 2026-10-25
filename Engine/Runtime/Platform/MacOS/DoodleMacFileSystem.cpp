//
//  DoodleMacFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleMacFileSystem.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace
{
    /**
     * 取可执行文件所在目录的绝对路径；取不到返回空串
     *
     * 与 DoodleShaderModule::GetExecutableDirectory 是同一段逻辑。暂时各留一份：
     * 着色器创建是静态工具、不持有文件来源对象，硬把两者并到一起要改图形管线的
     * 初始化链；等文件来源真的接管着色器读取时再统一。
     */
    std::string GetExecutableDirectory()
    {
#ifdef __APPLE__
        // 先给一个足够大的缓冲；不够时 _NSGetExecutablePath 会把所需大小写回 size
        uint32_t size = 4096;
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0)
        {
            path.assign(size, '\0');
            if (_NSGetExecutablePath(path.data(), &size) != 0)
            {
                return {};
            }
        }

        // 返回值可能含符号链接，规范化之后再取父目录
        std::error_code errorCode;
        const auto canonical = std::filesystem::canonical(path.c_str(), errorCode);
        if (errorCode)
        {
            return {};
        }
        return canonical.parent_path().string();
#else
        return {};
#endif
    }

    /**
     * 按优先级给出候选路径
     *
     * 两个候选都是字符串而不是 filesystem::path：调用方给的就是字符串，
     * 报错时原样带出去，避免路径在转换过程中被规范化成另一种样子。
     */
    std::vector<std::string> BuildCandidates(const std::string_view path)
    {
        std::vector<std::string> candidates{std::string(path)};

        const std::string executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty())
        {
            candidates.push_back(
                (std::filesystem::path(executableDirectory) / ".." / "Resources" / path)
                .lexically_normal()
                .string());
        }

        return candidates;
    }
}

namespace Doodle
{
    bool DoodleMacFileSystem::Exists(const std::string_view path) const
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

    std::vector<std::byte> DoodleMacFileSystem::ReadBinary(const std::string_view path) const
    {
        std::string triedPaths;

        for (const auto& candidate : BuildCandidates(path))
        {
            // ate    ：打开后直接定位到末尾，tellg 一次拿到大小
            // binary ：不转换换行。将来读 FBX / 图片时这是必须的
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
