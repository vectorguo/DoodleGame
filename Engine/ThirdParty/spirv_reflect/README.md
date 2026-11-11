# SPIRV-Reflect

- 版本：tag **vulkan-sdk-1.4.357.0**（与本机 Vulkan SDK 1.4.357 对齐）
- 来源：<https://github.com/KhronosGroup/SPIRV-Reflect>（2026-11-06 取得）
- 许可：Apache-2.0，全文见 `LICENSE`
- 用途：`DoodleShader` 加载时反射两段 SPIR-V，得出 set / binding / 描述符类型 /
  阶段，按结果建 set 1 布局，并核对 set 0 与 push constant 是否符合全工程约定

## 文件

- `spirv_reflect.h`、`spirv_reflect.c`：上游原样拷贝，`.c` 编进引擎静态库。
- `include/spirv/unified1/spirv.h`：上游仓库自带的 SPIR-V 头，`spirv_reflect.h`
  按相对路径 `./include/spirv/unified1/spirv.h` 包含它。不改用 Vulkan SDK 里那份：
  Android 构建不依赖 SDK，两边要是同一份头。

包含路径走 `Engine/ThirdParty`（CMake 已加），写作 `"spirv_reflect/spirv_reflect.h"`。
头文件在 C++ 里默认带一层 `spv_reflect::ShaderModule` 封装（RAII，析构时释放反射结果），
`DoodleShader` 用的就是它。

## 升级

三个文件一起换成同一个 tag 的版本，不要只换其中一个；本地不改上游文件。
