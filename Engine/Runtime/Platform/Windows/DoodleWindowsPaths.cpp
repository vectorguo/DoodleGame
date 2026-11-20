//
//  DoodleWindowsPaths.cpp
//  Doodle
//
//  Created by 郭智 on 2026/11/16.
//

#include "DoodleWindowsPaths.h"

#include <filesystem>
#include <string>

//<windows.h> 排在本工程的头文件之后，理由见 DoodleWindowsWindow.cpp。这里只为 GetModuleFileNameW
#include <windows.h>

namespace Doodle
{
    std::string GetExecutableDirectory()
    {
        //开了长路径支持的系统上路径可以超过 MAX_PATH。缓冲不够时 GetModuleFileNameW
        //只会截断并返回缓冲长度，不告诉你要多大，所以只能加倍重试。
        //32768 是 Win32 路径长度的上限，到那儿还不够就不再试了
        std::wstring buffer(MAX_PATH, L'\0');
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            if (buffer.size() >= 32768)
            {
                return {};
            }
            buffer.resize(buffer.size() * 2);
        }

        //不像 macOS 那份先 canonical：那边是为了解开 _NSGetExecutablePath 可能带的符号链接，
        //GetModuleFileNameW 给的已经是加载时用的完整路径。在 Windows 上 canonical
        //还会把映射盘符换成 \\server\share 那种形式，报错时反而对不上用户看到的路径
        return std::filesystem::path(buffer).parent_path().string();
    }
}
