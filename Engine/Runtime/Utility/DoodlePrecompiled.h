//
// Created by 郭智 on 2026/11/28.
//

#pragma once

/**
 * 预编译头正文
 *
 * 由 CMake 的 target_precompile_headers 强制包含到每个 TU 的最前面，一个 target
 * 只编译一次。**任何文件都不需要 include 本文件**，也不该 include —— 它已经
 * 在编译命令行里了，写一遍只是多余。
 *
 * 内容只有两类：
 * 1. 标准库统一头 —— 各头文件照常写 #include "Utility/DoodleStd.h"，
 *    这时命中 #pragma once，零成本
 * 2. 几乎每个 TU 都要路过的第三方 —— vulkan_core / glm / nlohmann。它们才是
 *    真正的大头（json.hpp 单独一份就比标准库那摞还贵）
 *
 * 不收**项目自己的头**：改一次工程头就让整份 PCH 失效，连没改过的 TU 也要重编；
 * 工程头本身不贵，预编译它们只会把「改哪个文件重编哪些」变成「改一个全量重编」。
 * 也不收平台头（windows.h / android / mach-o）：PCH 三平台共用，收进来一头都编不过。
 *
 * 往这里加东西的门槛和 DoodleStd.h 一样：几乎每个 TU 都要 include 才值得，
 * 加了就是 PCH 体积涨、全工程陪编译。
 */

#include "Utility/DoodleStd.h"

// Vulkan 核心头：二十多个引擎头直接 include，几乎每个 TU 都经手。
// 注意只收 vulkan_core.h —— vulkan.h 会按 VK_USE_PLATFORM_* 拉进 vulkan_win32.h
// 这类平台头，那正是工程里刻意挡在平台实现文件里的东西
#include <vulkan/vulkan_core.h>

// 数学库：glm.hpp 自带 vec2/3/4 与 core，下面四个 GTC 是工程里另外用到的
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// 组件配置的序列化格式：DoodleComponentConfig.h 被组件体系广泛包含
#include <nlohmann/json.hpp>
