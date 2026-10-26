//
//  DoodleAndroidFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleAndroidFileSystem.h"

#include <android/asset_manager.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    /**
     * AAsset 的析构器
     *
     * 打开之后无论是读成功、读失败，还是 vector 分配抛了异常，离开作用域都会
     * 关掉。AAsset_close 只出现在这一处，调用点不用各自记得配对
     */
    struct AAssetCloser
    {
        void operator()(AAsset* pAsset) const
        {
            if (pAsset != nullptr)
            {
                AAsset_close(pAsset);
            }
        }
    };

    using AAssetHandle = std::unique_ptr<AAsset, AAssetCloser>;

    /**
     * 打开 APK 里的资产；不存在返回空句柄
     *
     * AAssetManager_open 要的是以 '\0' 结尾的名字，调用方给的是 string_view，
     * 所以这里造一个临时 string。名字按 assets 根解析，前导 '/' 的绝对路径
     * 打不开，会落到文件系统分支 —— 正是那边要处理的情况
     */
    AAssetHandle OpenAsset(AAssetManager* pAssetManager, const std::string_view path)
    {
        return AAssetHandle(
            AAssetManager_open(pAssetManager, std::string(path).c_str(), AASSET_MODE_STREAMING));
    }

    /**
     * 读一个已经打开的资产
     *
     * 长度取的是整个资产的大小（压缩资产会给解压后的长度），按它一次分配。
     * AAsset_read 不保证一次读满，所以循环到长度为止
     */
    std::vector<std::byte> ReadAsset(AAsset& asset, const std::string_view path)
    {
        const off64_t length = AAsset_getLength64(&asset);
        if (length < 0)
        {
            throw std::runtime_error("failed to get APK asset length: " + std::string(path));
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(length));

        std::size_t totalRead = 0;
        while (totalRead < buffer.size())
        {
            const int readSize = AAsset_read(&asset, buffer.data() + totalRead, buffer.size() - totalRead);
            if (readSize < 0)
            {
                throw std::runtime_error("failed to read APK asset: " + std::string(path));
            }

            // 长度是打开时问的，正常不会提前 EOF。真出现就当读失败，
            // 不把半截数据交给 JSON / 网格解析器 —— 那类报错会指向格式，
            // 和真正的根因（资产被截断）对不上
            if (readSize == 0)
            {
                throw std::runtime_error("unexpected end of APK asset: " + std::string(path));
            }

            totalRead += static_cast<std::size_t>(readSize);
        }

        return buffer;
    }
}

namespace Doodle
{
    DoodleAndroidFileSystem::DoodleAndroidFileSystem(AAssetManager* pAssetManager)
        : m_pAssetManager(pAssetManager)
    {
        if (m_pAssetManager == nullptr)
        {
            throw std::runtime_error("DoodleAndroidFileSystem: asset manager is null");
        }
    }

    bool DoodleAndroidFileSystem::Exists(const std::string_view path) const
    {
        // 资产先查：Android 上随 APK 打包的那份才是运行时内容的权威来源
        const AAssetHandle asset = OpenAsset(m_pAssetManager, path);
        if (asset != nullptr)
        {
            return true;
        }

        std::error_code errorCode;
        return std::filesystem::is_regular_file(std::filesystem::path(path), errorCode);
    }

    std::vector<std::byte> DoodleAndroidFileSystem::ReadBinary(const std::string_view path) const
    {
        // 先试 APK assets。路径相对 assets 根，与桌面端的 "Scenes/main.json"
        // 是同一个写法 —— 平台差异只在这一层
        const AAssetHandle asset = OpenAsset(m_pAssetManager, path);
        if (asset != nullptr)
        {
            return ReadAsset(*asset, path);
        }

        // 再试文件系统：给 adb push 进设备的外部文件留一条路
        std::ifstream file(std::string(path), std::ios::ate | std::ios::binary);
        if (file.is_open())
        {
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

        // 两处都没有。把两个来源都列出来：少了 APK 那行，最常见的
        // 「Resources 没打进 APK」就只能靠猜
        std::error_code errorCode;
        const auto absolutePath = std::filesystem::absolute(std::filesystem::path(path), errorCode).string();
        throw std::runtime_error(
            "failed to open file: " + std::string(path) +
            "\n  tried APK asset: " + std::string(path) +
            "\n  tried file: " + (errorCode ? std::string(path) : absolutePath));
    }
}
