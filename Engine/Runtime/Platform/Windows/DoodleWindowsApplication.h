//
//  DoodleWindowsApplication.h
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#pragma once
#include "../DoodleApplication.h"

namespace Doodle
{
    /**
     * Windows 的驱动层实现：窗口用 Win32 建，文件来源走本地文件系统
     *
     * 与 macOS 那份同构：窗口与程序同寿，三个生命周期函数直接用基类的默认实现，
     * 本类只回答「窗口与文件从哪来」这两件事。建出来的对象立刻交给基类持有，
     * 什么时候释放、按什么顺序释放都在那边
     *
     * 本头文件以及包含它的翻译单元都不能先于本工程的头文件包含 <windows.h>：
     * 它的 CreateWindow 宏会把下面那个虚函数改名，理由见 DoodleWindowsWindow.h
     */
    class DoodleWindowsApplication final : public DoodleApplication
    {
    public:
        DoodleWindowsApplication() = default;
        ~DoodleWindowsApplication() override = default;

    protected:
        /** 建出 Win32 窗口 */
        std::unique_ptr<DoodleWindow> CreateWindow() override;

        /** 建出 Windows 文件来源：工作目录 → 可执行文件所在目录 */
        std::unique_ptr<DoodleFileSystem> CreateFileSystem() override;
    };
} // Doodle
