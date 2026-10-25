//
//  DoodleAndroidFileSystem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include "../DoodleFileSystem.h"

namespace Doodle
{
    /**
     * Android 文件来源
     *
     * 当前只支持文件系统路径；APK assets（AAssetManager）按计划在第 5 步接入。
     * 接口不变，届时只改本类实现，场景 loader 与资产管理器不需要动。
     */
    class DoodleAndroidFileSystem final : public DoodleFileSystem
    {
    public:
        DoodleAndroidFileSystem() = default;
        ~DoodleAndroidFileSystem() override = default;

        [[nodiscard]] bool Exists(std::string_view path) const override;

        [[nodiscard]] std::vector<std::byte> ReadBinary(std::string_view path) const override;
    };
}
