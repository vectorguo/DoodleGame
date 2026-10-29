//
//  DoodleSceneLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleSceneLoader.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "DoodleSceneImporter.h"
#include "Platform/DoodleFileSystem.h"

namespace Doodle
{
    DoodleSceneLoader::DoodleSceneLoader(const DoodleFileSystem& fileSystem)
        : DoodleAssetLoader<DoodleSceneData>(fileSystem)
    {
    }

    std::unique_ptr<DoodleSceneData> DoodleSceneLoader::Create(const std::string_view key)
    {
        const std::vector<std::byte> bytes = GetFileSystem().ReadBinary(key);

        // 解析器负责 JSON 语法与字段规则；本层只负责文件来源和缓存边界。
        // 解析失败时异常在这里抛出，基类不会把半份数据放进缓存
        return std::make_unique<DoodleSceneData>(DoodleSceneImporter::Import(bytes, key));
    }
}
