//
//  DoodleWindowsPaths.h
//  Doodle
//
//  Created by 郭智 on 2026/11/16.
//

#pragma once

#include "Utility/DoodleCommon.h"

namespace Doodle
{
    /**
     * 可执行文件所在目录的绝对路径（UTF-8）；取不到返回空串
     *
     * 目前有两个使用者：文件来源按它拼资源候选路径（见 DoodleWindowsFileSystem.cpp），
     * 日志按它把 DoodleGame.log 落在 exe 旁边（见 DoodleWindowsLog.cpp）。
     * 合成一处是因为「exe 在哪儿」只有一个答案，那段长路径的重试逻辑也只该有一份
     */
    [[nodiscard]] std::string GetExecutableDirectory();
}
