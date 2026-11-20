# 06 裁剪空间修正分散在相机组件与前向 pass

类别：结构 ｜ 优先级：高 ｜ 建议时机：做阴影时 ｜ 关联：02

## 现状

- Vulkan 的 Y 翻转写在场景组件里：`DoodleCamera::GetProjectionMatrix` 末尾的 `proj[1][1] *= -1`。
- 交换链预旋转写在前向 pass 里：`DoodleForwardPass::UpdatePassUbo` 左乘 `DoodleVulkanUtils::MakePreRotation`，宽高比也按预旋转对调（`DoodleVulkanUtils::GetDisplayAspect`）。
- 管线的 `frontFace` 全局固定为逆时针（`DoodleVulkanPipelineObject::CreateGraphicsPipeline`），那里的注释说明了它必须和 Y 翻转、顶点顺序一起看。

## 问题

- 阴影的光源投影矩阵会在别处生成。只要那一处漏了 Y 翻转，三角形绕序就和全局的 `frontFace` 约定相反，背面剔除剔反 —— 画面不一定明显出错（阴影只是变「薄」或者缺一块），很难查。
- 反过来，预旋转只属于画到交换链上的 pass，shadow map 这类离屏目标不该做。现在它和 Y 翻转分在两处，没有一个地方说清楚「哪种目标做哪种修正」。
- 场景组件带上了图形 API 的约定；`DoodleCamera` 本该只回答「这台相机怎么投影」。

## 建议

- `DoodleCamera::GetProjectionMatrix` 只返回与图形 API 无关的投影。
- 渲染侧收成一个函数（随 02 的帧视图数据一起做）：所有投影都做 Y 翻转；画到交换链上的再加预旋转，并按显示方向算宽高比。光源投影也走这个函数。
- 渲染 shadow map 和采样 shadow map 用同一个光空间矩阵时，采样直接用 `ndc.xy * 0.5 + 0.5`，不需要另外翻 V —— Y 翻转影响的只是绕序。

## 解决标志

`DoodleCamera` 里没有 Y 翻转；光源和相机的投影都经过同一个修正函数；阴影 pass 的剔除方向和前向一致。
