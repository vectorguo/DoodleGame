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
        //尺寸变化只往基类送一个通知，不在这里处理：回调可能在任意时刻触发，
        //在回调里当场重建会打乱帧的时序（理由见 DoodleVulkanManager::NotifyWindowResized）
        return std::make_unique<DoodleMacWindow>(1280, 800, "Doodle", [this] { this->NotifyWindowResized(); });
    }
} // Doodle
