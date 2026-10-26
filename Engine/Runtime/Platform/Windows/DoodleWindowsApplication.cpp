//
//  DoodleWindowsApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//
#include "DoodleWindowsApplication.h"

#include <memory>

#include "DoodleWindowsFileSystem.h"
#include "DoodleWindowsWindow.h"

namespace Doodle
{
    std::unique_ptr<DoodleWindow> DoodleWindowsApplication::CreateWindow()
    {
        //注册窗口类、CreateWindowExW 与显示都在 DoodleWindowsWindow 的构造函数里，
        //交出来时窗口已经在屏幕上了
        //
        //1280×800 与 macOS 那份是同一组数。两边都按逻辑尺寸理解（那边是点，这边是 96 DPI 下的
        //像素），所以开出来的窗口一样大；换算成物理像素由窗口按所在显示器的缩放去做
        //
        //尺寸变化的接线不在这里：窗口会自己找驱动层（见 DoodleWindowsWindow 的窗口过程）
        return std::make_unique<DoodleWindowsWindow>(1280, 800, "Doodle");
    }

    std::unique_ptr<DoodleFileSystem> DoodleWindowsApplication::CreateFileSystem()
    {
        return std::make_unique<DoodleWindowsFileSystem>();
    }
} // Doodle
