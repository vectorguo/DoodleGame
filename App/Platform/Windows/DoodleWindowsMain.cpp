//
//  DoodleWindowsMain.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/26.
//

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "Platform/DoodleLog.h"
#include "Platform/Windows/DoodleWindowsApplication.h"
#include "DoodleGame.h"

//<windows.h> 排在本工程的头文件之后，理由见 DoodleWindowsWindow.cpp。这里只为控制台
#include <windows.h>

using namespace Doodle;

namespace
{
    /**
     * 有现成的控制台就接上去，没有就什么也不做
     *
     * 工程按 WINDOWS 子系统链接（见 CMakeLists.txt），系统不会给进程分配控制台，
     * 所以从 VS 按 F5、从资源管理器双击都不会再冒出那个黑的 cmd 窗口。
     * 但从 cmd / PowerShell 启动时，父进程本来就带着控制台 —— 接上去，
     * 日志仍然打在原来那个窗口里，和改之前一样
     *
     * 接上之后还要把标准流重新绑到 CONOUT$：WINDOWS 子系统的进程即使继承了控制台，
     * CRT 启动时也不会替它打开 stdout / stderr，不重绑的话 std::cerr 照样是空的
     */
    void AttachParentConsole()
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        {
            return;
        }

        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
} // namespace

/**
 * Windows 侧的进程入口
 *
 * 形状与 macOS 那份（App/Platform/MacOS/DoodleMacMain.cpp）一样：建对象、
 * 按顺序调 Initialize / Run / Destroy、把异常翻成退出码，连窗口都不用它建
 * （见 DoodleWindowsApplication::CreateWindow）
 *
 * 用普通的 main 而不是 WinMain：工程按 WINDOWS 子系统链接，靠 CMakeLists.txt 里的
 * /ENTRY:mainCRTStartup 让 CRT 仍然从 main 起 —— 不为一份入口再写个转发。
 * 没有控制台之后，DoodleLog 的日志落在 exe 同目录的 DoodleGame.log 与调试器输出里
 * （见 DoodleWindowsLog.cpp），下面 catch 里的异常消息也跟着走那几条出口，不必在这里另开一支
 */
int main()
{
    //有控制台的话（从 cmd / PowerShell 启动）按 UTF-8 显示。进程里的窄字符串都是
    //UTF-8 —— 源码按 /utf-8 编译，活动代码页在 DoodleGame.manifest 里声明成了 UTF-8 ——
    //而控制台默认按系统的 OEM 代码页（简中是 936）解释输出，不改的话错误消息里
    //一出现中文路径就是乱码
    //
    //退出前还原：控制台是和启动它的那个 shell 共用的，不该把改动留给它。
    //没有控制台时（从 VS 或资源管理器启动）这几个调用都是空操作
    AttachParentConsole();
    const UINT previousOutputCodePage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    //每次启动写一行。日志文件在第一次写时才打开、且按本次运行截断，
    //这一行既标出运行边界，也保证一路无事时 exe 旁边照样有个日志文件
    //（里面至少是这一行，见 DoodleWindowsLog.cpp）。Android 那份入口也有同样的一行
    Log::Info("Doodle: main entered");

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
        // 走 Log 而不是直接 std::cerr：没有控制台时它还会落到日志文件与调试器输出
        Log::Error(e.what());
        exitCode = EXIT_FAILURE;
    }

    SetConsoleOutputCP(previousOutputCodePage);
    return exitCode;
}
