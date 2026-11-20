# 02 跨 pass 的帧数据与资源没有归属

类别：结构 ｜ 优先级：高 ｜ 建议时机：做阴影之前 / 同时 ｜ 关联：01、03、06

## 现状

- 现在的约定是「每帧常量归各自的 pass」：`DoodleForwardPass::UpdatePassUbo` 自己读 `DoodleCamera`（view / proj）、`DoodleLight`（方向、颜色 × 强度）和交换链（尺寸、预旋转），拼出 `DoodleForwardPassUbo` 写进本 pass 的 set 0。
- set 0 的契约只允许一个 UBO：`DoodleRenderPass::CreatePassUboResources` 只建这一个绑定，`DoodleShader::ValidatePassInterface` 也只认它。
- 每个 pass 只录自己那一段的屏障，pass 之间没有任何资源交接。

## 问题

阴影一接进来，就有三样东西不属于任何一个 pass：

- **光空间矩阵**：阴影 pass 渲染时要用，前向 pass 采样 shadow map 时也要用，两边必须是同一个值。按现在的约定只能各算一遍，还得保证算得完全一致。
- **shadow map**：阴影 pass 写、前向 pass 读。前向 pass 的 set 0 要多一个采样器绑定，而 set 0 现在只允许一个 UBO。
- **pass 间屏障**：shadow map 要从深度附件转成片元着色器可采样（`DEPTH_STENCIL_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL`）。这条屏障写死在阴影 pass 里不对（它不知道谁来读），写在前向 pass 里也不对（它不知道谁写的）。

另外，前向 pass 因此依赖了场景组件（相机、光源）和交换链的预旋转，pass 不再只是「拿数据录命令」。

## 建议

- `DoodleRenderPipeline` 加一个帧准备步骤，统一算一次本帧的视图数据：相机的 view / proj / viewProj、主光源的方向与颜色、光源的 view / proj。结果放进 `DoodleRenderPassContext`，各 pass 只负责把要用的部分打包进自己的 UBO。裁剪空间修正也在这一步统一做（见 06）。
- shadow map 由 `DoodleRenderPipeline` 持有。它的类注释写的分工就是「管有几个 pass、按什么顺序、它们共用什么资源」，跨 pass 的资源归它正合适。
- pass 之间的布局转换由 `DoodleRenderPipeline` 在两个 pass 的 Render 之间插入。先做一个最小的「资源当前状态 → 目标状态」记录就够，不必一上来做完整的 render graph。
- set 0 从「单个 UBO」改成绑定表：`DoodlePassShaderInterface` 按 pass 列出 set 0 的全部绑定（前向：UBO + shadow map 采样器），建资源和反射校验都按这张表走。

## 解决标志

光空间矩阵每帧只算一次；shadow map 和它的屏障不在任何一个具体 pass 里；`DoodleForwardPass` 不再 include `DoodleCamera.h` / `DoodleLight.h`。
