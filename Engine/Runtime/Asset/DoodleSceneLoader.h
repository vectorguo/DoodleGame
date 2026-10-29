//
//  DoodleSceneLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <memory>
#include <string_view>

#include "DoodleAssetLoader.h"
#include "Scene/DoodleSceneData.h"

namespace Doodle
{
    class DoodleFileSystem;

    /**
     * 场景资产 loader，资产管理器内部使用
     *
     * 对外只暴露 assetManager.Load<DoodleSceneData>(path)。文件读取、缓存、
     * 去重与销毁在 DoodleAssetLoader<DoodleSceneData> 基类里；本类只做两件事：
     * 读文件、把字节交给 DoodleSceneImporter 解析。JSON 语法与字段规则不在
     * 本类里，解析器可以脱离文件系统和设备单独测试。
     *
     * 解析失败时 Create 抛异常，基类不会把半份数据放进缓存。
     */
    class DoodleSceneLoader final : public DoodleAssetLoader<DoodleSceneData>
    {
    public:
        explicit DoodleSceneLoader(const DoodleFileSystem& fileSystem);
        ~DoodleSceneLoader() override = default;

        DoodleSceneLoader(const DoodleSceneLoader&) = delete;
        DoodleSceneLoader& operator=(const DoodleSceneLoader&) = delete;
        DoodleSceneLoader(DoodleSceneLoader&&) = delete;
        DoodleSceneLoader& operator=(DoodleSceneLoader&&) = delete;

    protected:
        [[nodiscard]] std::unique_ptr<DoodleSceneData> Create(std::string_view key) override;
    };
}
