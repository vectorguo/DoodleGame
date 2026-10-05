//
// Created by 郭智 on 2026/10/5.
//

#include "DoodleShaderModule.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif


namespace Doodle
{
    VkShaderModule DoodleShaderModule::CreateShaderModule(VkDevice pDevice, const std::string& shaderPath)
    {
        const auto code = ReadFile(shaderPath);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

        // SPIR-V 是 32 位字流：codeSize 以「字节」计，pCode 却是 uint32_t*。
        // 单位不一致是这一处的常见困惑点，推论有两条：
        //   1. codeSize 必须是 4 的倍数（合法的 glslc 产物天然满足，
        //      手动补过 '\0' 就会破坏）
        //   2. pCode 须满足 uint32_t 对齐 —— std::vector 的默认分配器
        //      保证最坏情况对齐，所以下面的转换是安全的
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(pDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module: " + shaderPath);
        }
        return shaderModule;
    }

    std::string DoodleShaderModule::GetExecutableDirectory()
    {
#ifdef __APPLE__
        // 先给一个足够大的缓冲；不够时 _NSGetExecutablePath 会把所需
        // 大小写回 size，再试一次即可
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

    std::vector<char> DoodleShaderModule::ReadFile(const std::string& filename)
    {
        // 候选路径，顺序即优先级
        std::vector<std::string> candidates = {filename};

        const auto executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty())
        {
            candidates.push_back((std::filesystem::path(executableDirectory) / ".." / "Resources" / filename)
                                 .lexically_normal()
                                 .string());
        }

        std::string triedPaths;
        for (const auto& candidate : candidates)
        {
            // ate    ：打开后直接定位到文件末尾，随后用 tellg 就能取到文件大小，
            //          比「循环读到 EOF 再 push_back」少一次搬运
            // binary ：以二进制方式读取。在 Windows 上文本模式会把 \r\n 转成 \n，
            //          字节码被改动一个字节就整体错位，必须显式关掉
            std::ifstream file(candidate, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                // 报错时给出绝对路径，方便判断「工作目录到底对不对」
                std::error_code errorCode;
                const auto absolutePath = std::filesystem::absolute(candidate, errorCode).string();
                triedPaths += "\n  tried: " + (errorCode ? candidate : absolutePath);
                continue;
            }

            const auto fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);

            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
            file.close();
            return buffer;
        }

        throw std::runtime_error("failed to open file: " + filename + triedPaths);
    }
}
