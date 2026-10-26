//
//  DoodleWindowsFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include "DoodleWindowsFileSystem.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

//<windows.h> 排在本工程的头文件之后，理由见 DoodleWindowsWindow.cpp。这里只为 GetModuleFileNameW
#include <windows.h>

namespace
{
    /**
     * 取可执行文件所在目录的绝对路径；取不到返回空串
     *
     * 与 DoodleMacFileSystem 里那份是同一个角色，换成了 GetModuleFileNameW。
     * 取宽字符版本再转回窄字符串：活动代码页是 UTF-8（见头文件），转出来的就是 UTF-8
     */
    std::string GetExecutableDirectory()
    {
        //开了长路径支持的系统上路径可以超过 MAX_PATH。缓冲不够时 GetModuleFileNameW
        //只会截断并返回缓冲长度，不告诉你要多大，所以只能加倍重试。
        //32768 是 Win32 路径长度的上限，到那儿还不够就不再试了
        std::wstring buffer(MAX_PATH, L'\0');
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            if (buffer.size() >= 32768)
            {
                return {};
            }
            buffer.resize(buffer.size() * 2);
        }

        //不像 macOS 那份先 canonical：那边是为了解开 _NSGetExecutablePath 可能带的符号链接，
        //GetModuleFileNameW 给的已经是加载时用的完整路径。在 Windows 上 canonical
        //还会把映射盘符换成 \\server\share 那种形式，报错时反而对不上用户看到的路径
        return std::filesystem::path(buffer).parent_path().string();
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
