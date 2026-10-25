//
//  DoodleFileSystem.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleFileSystem.h"

namespace Doodle
{
    std::string DoodleFileSystem::ReadText(const std::string_view path) const
    {
        const std::vector<std::byte> bytes = ReadBinary(path);

        // std::string 的 (const char*, size) 构造不接受空指针，即使 size 为 0。
        // 空 vector 的 data() 可能为空，单独挡掉
        if (bytes.empty())
        {
            return {};
        }

        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}
