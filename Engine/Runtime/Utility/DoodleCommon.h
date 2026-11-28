//
// Created by 郭智 on 2026/11/28.
//

#pragma once

/**
 * 公共头：标准库 + Vulkan 核心 + glm，Engine 各头文件需要的常见依赖都在这里
 *
 * 用法：头文件 / 源文件要这些东西时，写一行 #include "Utility/DoodleCommon.h"，
 * 不再逐个列 <vector> / <string> / <vulkan/vulkan_core.h> / <glm/glm.hpp>。
 * 它同时被预编译头包住（见 DoodlePrecompiled.h），每个 TU 只解析一次。
 *
 * 以下三类**不收**，需要的地方仍自己 include：
 * - 重头：<filesystem>、<fstream>、<iostream>、<cstdio>。它们只出现在 .cpp
 *   实现里（平台层、DoodleShaderModule），收进来等于让每个 TU 陪跑
 * - nlohmann/json.hpp：只有组件配置用得上，它一个就比上面这一摞还贵
 * - 平台头：<windows.h>、Android 的 log / input 这类头、<mach-o/dyld.h>。
 *   必须留在平台实现文件里，混进来跨平台就编不过
 *
 * 只收 vulkan_core.h，不收 vulkan.h：后者会按 VK_USE_PLATFORM_* 拉进
 * vulkan_win32.h 这类平台头，那正是工程里刻意挡在平台实现文件之外的东西。
 *
 * 代价要说清：不开预编译头时，每个 include 它的 TU 都会把这些头完整解析一遍，
 * 所以别往里加冷门头 —— 加了就是全工程付账。
 */

// 容器与字符串
#include <array>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 内存、类型、异常与工具
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <utility>

// 算法、数学、时间与并发
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

// Vulkan 核心头：引擎几乎每个头文件都会碰到 Vk 类型
#include <vulkan/vulkan_core.h>

// 数学库：glm.hpp 自带 vec2/3/4 与 core，下面四个 GTC 是工程里另外用到的
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
