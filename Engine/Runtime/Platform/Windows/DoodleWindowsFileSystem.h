//
//  DoodleWindowsFileSystem.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once

#include "../DoodleFileSystem.h"

namespace Doodle
{
    /**
     * Windows 文件来源
     *
     * 候选顺序：
     *   1. path 本身 —— 相对进程当前工作目录。VS 里按 F5 时工作目录是可执行文件所在目录
     *      （见 CMakeLists.txt 的 VS_DEBUGGER_WORKING_DIRECTORY），在资源管理器里双击也是；
     *      构建会把运行时数据拷到那里
     *   2. <可执行文件目录>/path —— 从别的目录用命令行启动时走这条
     *
     * 与 macOS 那份的差别只在第二条：那边资源在 .app/Contents/Resources 里，
     * Windows 没有包结构，按惯例与可执行文件放在一起
     *
     * 路径一律按 UTF-8 的窄字符串处理，和另外两个平台一致。Win32 的窄字符 API 本来按
     * 系统代码页（简中是 936）解释字符串，进程在 DoodleGame.manifest 里把活动代码页
     * 声明成了 UTF-8，这条约定在 Windows 上才成立
     */
    class DoodleWindowsFileSystem final : public DoodleFileSystem
    {
    public:
        DoodleWindowsFileSystem() = default;
        ~DoodleWindowsFileSystem() override = default;

        [[nodiscard]] bool Exists(std::string_view path) const override;

        [[nodiscard]] std::vector<std::byte> ReadBinary(std::string_view path) const override;
    };
}
