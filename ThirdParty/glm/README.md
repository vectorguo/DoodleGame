# GLM

OpenGL Mathematics —— header-only 的 C++ 数学库（向量、矩阵、四元数等）。
本目录只用到它的头文件，没有需要编译或链接的部分。

## 目录结构

`ThirdParty/glm/` 里**直接就是 GLM 的内容**（`glm.hpp`、`detail/`、`gtc/` …），
不再套一层 `glm/`。这也是第三方库目录的惯例：一个库一个目录。

CMakeLists 里加进 include 路径的是本目录的**父目录** `ThirdParty`，
所以 `#include <glm/glm.hpp>` 解析到 `ThirdParty/glm/glm.hpp`。
将来再引别的库（volk 之类）照同一规矩放在 `ThirdParty/` 下，不必再加路径。

## 为什么放在仓库里，而不是靠 brew

桌面侧 brew 装的 GLM 在 `/opt/homebrew/include`，Android 侧没有对应物 ——
NDK 的 include 路径里不可能出现 Homebrew 的目录，Android 也没有包管理器。
把它收进仓库，两个平台共用同一份、同一个版本，谁 clone 下来都能直接编。

## 版本与来路

- 版本：1.0.3
- 来源：Homebrew `glm` formula，即 `/opt/homebrew/Cellar/glm/1.0.3/`
  （头文件取自 `include/glm/`，许可证取自包根目录）
- 拷入日期：2026-10-11
- 许可证：MIT（见同目录 `copying.txt`）

## 怎么升级

用新版整个覆盖：

    cp -R <来源>/glm/*        ThirdParty/glm/
    cp <来源>/copying.txt     ThirdParty/glm/

`<来源>` 可以是 GLM 官方仓库（头文件在仓库根的 `glm/` 子目录下，许可证在仓库根），
也可以是 brew 解出来的包目录 `/opt/homebrew/Cellar/glm/<版本>/`。

### 从 brew 那份拷的话，多一步

brew 的 `include/glm/` 里带着一份**完整的嵌套副本** `glm/glm/`（433 个文件、3.2 MB），
外加一个 `CMakeLists.txt` —— 是打包时把 GLM 仓库的 `glm/` 子目录一并收进来的产物。
内容与外层逐字节相同，且 GLM 自身没有任何一处真的 `#include <glm/glm/...>`
（有的几处都在注释里），纯死重量。

本目录当初已经删过一次。照上面的步骤覆盖后会再带进来，记得再删：

    rm -rf ThirdParty/glm/glm

## 一个顺序上的注意

CMakeLists 里 `ThirdParty` 排在 `/opt/homebrew/include` 的**前面**。这台机器上 brew
也装了一份 GLM（1.0.3），顺序反了会静默用到系统那份 —— 你以为在用仓库里锁定的版本，
实际不是，换台机器行为就可能不同。
