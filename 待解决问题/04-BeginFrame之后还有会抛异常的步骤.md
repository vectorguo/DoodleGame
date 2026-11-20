# 04 BeginFrame 之后还有会抛异常的步骤

类别：结构 ｜ 优先级：高 ｜ 建议时机：做阴影之前 ｜ 关联：01、12

## 现状

- `DoodleVulkanManager::BeginFrame` 在 acquire 成功之后就 `vkResetFences`，并 begin 本帧的命令缓冲。
- 这之后 `DoodleRenderPipeline::Render` 里还有会抛的步骤：
  - `DoodleDescriptorManager::ResolveMaterialDescriptorSets`：材质没有着色器、集合分配失败；
  - `DoodleForwardPass::Render` 开头的预检查：集合数与绘制项数不一致、材质没有着色器、管线解析或创建失败。
- 前向 pass 为了不在 `vkCmdBeginRendering` 之后才抛，录制前先把所有绘制项的管线解析一遍 ——「录制期间不能抛」靠每个 pass 自己防。

## 问题

- 一旦在 reset fence 之后抛出，本槽位的 fence 再也不会被 signal（已经 reset，却没有提交），命令缓冲也停在 recording 状态。现在异常一律致命（Windows 入口 catch 之后直接退出），所以没出过事；但只要以后想「坏一帧、继续跑」，下次轮到这个槽位就会在 `vkWaitForFences(UINT64_MAX)` 上永远卡住。
- 阴影 pass 遇到「材质的着色器没有 shadow 描述」应该跳过这一项，而不是报错。这种「按 pass 过滤 / 跳过」的语义，塞不进现在「BeginFrame 之后边解析边录」的结构里。
- 每个 pass 都得记得先做一遍预检查，新加 pass 时容易漏。

## 建议

沿用现在把 `WarmupPipelines` 放在 BeginFrame 之前的同一个思路，把一帧拆成两段：

- **Prepare（可以失败，在 BeginFrame 之前）**：各 pass 生成自己的列表（见 01），逐项解析管线和 set 1；解析不了的，按这个 pass 自己的规则跳过或报错；
- **Record（按构造不抛）**：只读 Prepare 的结果录命令。

预检查一次、录制再解析一次的重复（见 12）也随之消失。

## 解决标志

除了设备级失败（end / 提交 / 呈现），BeginFrame 之后不再有会抛异常的步骤；阴影 pass 跳过没有 shadow 描述的材质，不需要特殊处理。
