//
// Created by 郭智 on 2026/11/28.
//

#pragma once

/**
 * 标准库公共头：Engine 各头文件用到的标准库头全部收在这里
 *
 * 用法：头文件 / 源文件需要标准库时，写一行 #include "Utility/DoodleStd.h"，
 * 不再逐个列 <vector> / <string> / <memory> 那一摞。下面的清单是扫过
 * Engine 所有头文件后取的全集，Engine 里每个用到标准库的头都换成了它。
 *
 * 以下两类**不收**，需要的地方仍自己 include：
 * - 重头：<filesystem>、<fstream>、<iostream>、<cstdio>。扫过一遍，Engine 的
 *   头文件一个都没用到 —— 它们只出现在实现里（平台层、DoodleShaderModule）。
 *   将来某个头文件真需要，再判断是加进来让所有 TU 陪跑，还是让它自己 include
 * - 平台头：<windows.h>、Android 的 log / input 这类头、<mach-o/dyld.h>。
 *   必须留在平台实现文件里，混进来跨平台就编不过
 *
 * 代价要说清：这不是预编译头，每个 include 它的 TU 都会把这些头完整解析一遍，
 * 所以别往里加冷门头 —— 加了就是全工程付账。
 *
 * 第三方（vulkan / glm / nlohmann）也不在这里：它们各自有版本与路径的讲究，
 * 由使用处按原来的方式 include。
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
