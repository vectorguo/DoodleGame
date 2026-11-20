//
//  DoodleWindowsLog.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/16.
//

#include "../DoodleLog.h"
#include "DoodleWindowsPaths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

//<windows.h> 排在本工程的头文件之后，理由见 DoodleWindowsWindow.cpp。这里只为 OutputDebugStringW
#include <windows.h>

namespace Doodle
{
    namespace
    {
        /**
         * 与 exe 同目录的日志文件名
         *
         * 判据是「exe 在哪儿」而不是「工作目录在哪儿」：从资源管理器双击、
         * VS 按 F5、cmd 里从别的目录启动，拿到的都是同一个文件
         */
        constexpr const char* kLogFileName = "DoodleGame.log";

        /**
         * 三条出口共用一把锁
         *
         * 日志本身不保证从哪个线程来（渲染、资源加载都在同一进程里），
         * 不加锁的话文件里的行会互相穿插，调试器输出也会乱序
         */
        std::mutex& LogMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        /**
         * exe 旁边的日志文件；打不开就返回一个未打开的流
         *
         * 每次启动截断（trunc）：文件里永远是最近一次运行的日志，不会一轮轮攒下去。
         * 第一次写日志时才打开，也就是 main 起来之后 —— 这时候进程的清单已经生效，
         * 窄路径按 UTF-8 解释（见 DoodleWindowsPaths.h）
         *
         * 打不开不报错：程序可能装在 Program Files 这类没写权限的地方，
         * 那时日志只剩调试器与（可能的）控制台两条出口，不该为写文件让游戏起不来
         */
        std::ofstream& LogFile()
        {
            static std::ofstream file = [] {
                const std::string directory = GetExecutableDirectory();
                if (directory.empty())
                {
                    return std::ofstream{};
                }

                return std::ofstream(std::filesystem::path(directory) / kLogFileName,
                                     std::ios::out | std::ios::trunc);
            }();
            return file;
        }

        /**
         * UTF-8 转 UTF-16 后送进调试器
         *
         * DoodleGame 按 WINDOWS 子系统链接（见 CMakeLists.txt），没有控制台时
         * OutputDebugString 是开发期最顺手的出口：VS 调试时落在「输出」窗口，
         * 外面开着 DebugView 也能接住
         *
         * 用 W 版本而不是 A 版本：A 版本按 ANSI 代码页解释字节，虽然清单里已经把
         * 活动代码页声明成 UTF-8，与其指望这个前提成立，不如在这里显式转一次
         *
         * 失败不管：日志是尽力而为的东西，送不出去不能反过来影响游戏
         */
        void WriteToDebugger(const char* message)
        {
            //长度传 -1：按 '\0' 结尾处理，返回值把结尾的 '\0' 也算进去了；
            //非法字节序列替换成 U+FFFD，不让整条日志丢掉
            const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
            if (length <= 1)
            {
                return;
            }

            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length);

            //结尾那个 '\0' 换成换行：OutputDebugString 不补行尾，
            //不换行的话 VS「输出」窗口会把一条条日志连成一行
            wide.back() = L'\n';
            OutputDebugStringW(wide.c_str());
        }

        /**
         * 把一条日志同时送到三条出口
         *
         *   1. 控制台：从 cmd / PowerShell 启动时 main 会把标准流接上去（见 DoodleWindowsMain.cpp），
         *      从 VS 或资源管理器启动时没有控制台，写进去静默失败，不影响后面两条
         *   2. 日志文件：exe 同目录的 DoodleGame.log —— 双击启动时唯一能事后翻看的去处
         *   3. 调试器：VS 的「输出」窗口 / DebugView
         */
        void Write(const char* message, const bool isError)
        {
            if (message == nullptr)
            {
                return;
            }

            const std::lock_guard lock(LogMutex());

            if (isError)
            {
                std::cerr << message << std::endl;
            }
            else
            {
                std::cout << message << std::endl;
            }

            std::ofstream& file = LogFile();
            if (file.is_open())
            {
                file << message << '\n';
                //逐行冲刷：崩溃 / 强杀时最后一行也得在文件里。日志量小，这点开销无所谓
                file.flush();
            }

            WriteToDebugger(message);
        }
    }

    void Log::Error(const char* message)
    {
        Write(message, true);
    }

    void Log::Info(const char* message)
    {
        Write(message, false);
    }
}
