# 03 交换链的布局转换写在前向 pass 里

类别：结构 ｜ 优先级：中 ｜ 建议时机：加后处理 / UI 之前（可以和 02 一起做）｜ 关联：02

## 现状

- 交换链图像的两条布局转换都在前向 pass 里：`DoodleForwardPass::RecordImageTransitionToColorAttachment`（`UNDEFINED → COLOR_ATTACHMENT_OPTIMAL`）和 `DoodleForwardPass::RecordImageTransitionToPresent`（`→ PRESENT_SRC_KHR`）。
- `DoodleVulkanManager::EndFrame` 里 acquire 信号量的等待阶段（`COLOR_ATTACHMENT_OUTPUT`）必须和前一条转换的 `srcStageMask` 一致，两边靠注释互相提醒 —— 这条约束跨了后端和一个具体的 pass。
- 颜色附件的图像视图也是前向 pass 自己从交换链取的（`m_pSwapChain->GetImageViews()`）。

## 问题

「前向 pass 最后写交换链」是写死的前提。加后处理之后，前向 pass 改画到离屏的 HDR 目标，最后写交换链的变成后处理或 UI pass：两条转换和那条等待阶段的约束都得搬走；以后每换一次「最后一个 pass」，都要再搬一次。

## 建议

- 交换链图像「取到之后 → 颜色附件」和「呈现之前 → PRESENT」这两条转换收回 `DoodleRenderPipeline`，放在第一个 pass 之前、最后一个 pass 之后。等待阶段的约束只剩后端和 RenderPipeline 之间这一对。
- pass 只声明「往哪个目标画」，目标由 RenderPipeline 通过 PassContext 交给它，pass 不再自己去交换链取图像。
- 如果以后最后一步打算用 blit / copy 写交换链，`DoodleSwapChain` 创建时的 `imageUsage` 要补上 `TRANSFER_DST`（现在只有 `COLOR_ATTACHMENT`）。

## 解决标志

`DoodleForwardPass` 不再碰交换链图像的布局；新增一个最后写交换链的 pass，不需要改前向 pass，也不需要改 `DoodleVulkanManager`。
