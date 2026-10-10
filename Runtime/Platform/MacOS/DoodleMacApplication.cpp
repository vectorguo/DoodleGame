//
//  DoodleMacApplication.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/9.
//
#include "DoodleMacApplication.h"

#include <memory>

#include "DoodleMacWindow.h"

namespace Doodle
{
    std::unique_ptr<DoodleWindow> DoodleMacApplication::CreateWindow()
    {
        //glfwInit 与 glfwCreateWindow 都在 DoodleMacWindow 的构造函数里。
        //GLFW 建出来的窗口默认就是可见的，不用再单独 show 一次
        //
        //尺寸变化的接线不在这里：窗口会自己找驱动层（见
        //DoodleMacWindow::FramebufferSizeCallback），本函数只回答
        //「这次启动开个多大的窗、叫什么名字」
        return std::make_unique<DoodleMacWindow>(1280, 800, "Doodle");
    }
} // Doodle
