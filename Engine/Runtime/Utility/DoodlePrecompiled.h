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
 * 内容 = 公共头 DoodleCommon.h（标准库 + vulkan_core + glm）+ 只在这里收的
 * nlohmann/json.hpp。json 不进公共头：只有组件配置用得上，它一个却比那一摞
 * 加起来还贵；放 PCH 里是零成本，放公共头则是不开 PCH 时每个 TU 都陪跑。
 *
 * 不收**项目自己的头**：改一次工程头就让整份 PCH 失效，连没改过的 TU 也要重编；
 * 工程头本身不贵，预编译它们只会把「改哪个文件重编哪些」变成「改一个全量重编」。
 * 也不收平台头（windows.h / android / mach-o）：PCH 三平台共用，收进来一头都编不过。
 */

#include "Utility/DoodleCommon.h"

// 组件配置的序列化格式：DoodleComponentConfig.h 被组件体系广泛包含
#include <nlohmann/json.hpp>
