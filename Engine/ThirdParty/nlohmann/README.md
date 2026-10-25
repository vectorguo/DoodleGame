# nlohmann/json

JSON for Modern C++ —— 单头文件、header-only 的 JSON 解析库。
本目录只用到 `json.hpp`，没有需要编译或链接的部分。

## 目录结构

`ThirdParty/nlohmann/json.hpp` 是官方 release 的 single include 版本。
CMake 里加进 include 路径的是父目录 `ThirdParty`，所以源码写
`#include <nlohmann/json.hpp>`。

## 为什么放在仓库里

Android 侧没有包管理器，系统路径上不会有 nlohmann/json；收进仓库后
桌面与 Android 共用同一份、同一个版本。

## 版本与来路

- 版本：3.12.0
- 来源：<https://github.com/nlohmann/json/releases/tag/v3.12.0>
  （取该 release 的 `json.hpp` 与 `LICENSE.MIT`）
- 拷入日期：2026-10-25
- 许可证：MIT（见同目录 `LICENSE.MIT`）

## 怎么升级

用新版整个覆盖：

    curl -L -o ThirdParty/nlohmann/json.hpp \
        https://github.com/nlohmann/json/releases/download/vX.Y.Z/json.hpp
    curl -L -o ThirdParty/nlohmann/LICENSE.MIT \
        https://raw.githubusercontent.com/nlohmann/json/vX.Y.Z/LICENSE.MIT

同时更新本文件里的版本号与来路。
