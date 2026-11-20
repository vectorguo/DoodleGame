# 10 贴图：格式写死、没有 mip、每张一个采样器

类别：后端扩展性 ｜ 优先级：中 ｜ 建议时机：贴图种类变多之前 ｜ 关联：08、09

## 现状

- `DoodleTexture` 的格式固定为 `R8G8B8A8_SRGB`（`DoodleTexture::kTextureFormat`）。
- 只有一级 mip：`DoodleImage` 固定 `mipLevels = 1`，采样器的 `maxLod = 0`。
- 每张贴图在 `DoodleTexture::CreateSampler` 里自建一个 `VkSampler`；没有开各向异性。

## 问题

- 法线、粗糙度这类非颜色贴图按 sRGB 解码会算错。
- 没有 mip，远处和斜视角的表面会闪烁、出摩尔纹，采样带宽也浪费。
- 规范只保证 `maxSamplerAllocationCount` ≥ 4000，采样器数量跟着贴图数量涨会撞上限；而实际用到的采样器状态只有寥寥几种。

## 建议

- 采样器按状态（过滤、寻址、各向异性、LOD 范围）缓存共享，贴图只引用。
- 加载时生成 mip（`vkCmdBlitImage` 逐级缩小，或者离线工具预生成），上传路径和 09 一起改。
- 颜色空间按用途决定：材质槽或贴图导入设置声明 sRGB / 线性，非颜色贴图用 `R8G8B8A8_UNORM`。
- 要开各向异性，先在建逻辑设备时打开 `samplerAnisotropy` 特性（现在 `VkPhysicalDeviceFeatures` 全是零）。

## 解决标志

采样器数量与贴图数量无关；非颜色贴图按线性上传；贴图带完整的 mip 链。
