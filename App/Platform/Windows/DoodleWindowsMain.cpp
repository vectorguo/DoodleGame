//
//  DoodleWindowsMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include <cstdlib>
#include <exception>
#include <iostream>

#include "Platform/Windows/DoodleWindowsApplication.h"
#include "DoodleGame.h"

//<windows.h> 排在本工程的头文件之后，理由见 DoodleWindowsWindow.cpp。这里只为控制台代码页
#include <windows.h>

using namespace Doodle;

/**
 * Windows 侧的进程入口
 *
 * 形状与 macOS 那份（App/Platform/MacOS/DoodleMacMain.cpp）一样：建对象、
 * 按顺序调 Initialize / Run / Destroy、把异常翻成退出码，连窗口都不用它建
 * （见 DoodleWindowsApplication::CreateWindow）
 *
 * 用普通的 main 而不是 WinMain：工程按控制台子系统链接，运行时会带出一个控制台窗口，
 * DoodleLog 与异常消息照常走 std::cout / std::cerr 打在那里 —— 和 macOS 一样，
 * DoodleLog 不必为 Windows 另开一支。将来要出不带控制台的包，把链接子系统换成
 * WINDOWS 并把入口指定为 mainCRTStartup 即可，本函数不用动；但那时 std::cerr
 * 就没人接了，得先给 DoodleLog 加一条 OutputDebugString 的出口
 */
int main()
{
    //控制台按 UTF-8 显示。进程里的窄字符串都是 UTF-8 —— 源码按 /utf-8 编译，
    //活动代码页在 DoodleGame.manifest 里声明成了 UTF-8 —— 而控制台默认按系统的
    //OEM 代码页（简中是 936）解释输出，不改的话错误消息里一出现中文路径就是乱码
    //
    //退出前还原：从 cmd 里启动时控制台是和它共用的，不该把改动留给它
    const UINT previousOutputCodePage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    int exitCode = EXIT_SUCCESS;
    try
    {
        // 场景组件类型必须在场景加载之前登记
        DoodleGame::RegisterComponents();

        DoodleWindowsApplication app;
        app.Initialize();
        app.Run();
        app.Destroy();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }

    SetConsoleOutputCP(previousOutputCodePage);
    return exitCode;
}
