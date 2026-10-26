//
//  DoodleMacFileSystem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include "../DoodleFileSystem.h"

namespace Doodle
{
    /**
     * macOS 文件来源
     *
     * 候选顺序：
     *   1. path 本身 —— 相对进程当前工作目录。开发时（CLion）工作目录是工程根目录
     *   2. <可执行文件目录>/../Resources/path —— .app 包内。Finder 双击启动时走这条
     *
     * 与 DoodleShaderModule 的查找顺序是同一个思路。着色器那边还带一份编进二进制的
     * 副本给 Android 用，这里没有：场景文件在 Android 走 APK assets，不需要内嵌。
     */
    class DoodleMacFileSystem final : public DoodleFileSystem
    {
    public:
        DoodleMacFileSystem() = default;
        ~DoodleMacFileSystem() override = default;

        [[nodiscard]] bool Exists(std::string_view path) const override;

        [[nodiscard]] std::vector<std::byte> ReadBinary(std::string_view path) const override;
    };
}
