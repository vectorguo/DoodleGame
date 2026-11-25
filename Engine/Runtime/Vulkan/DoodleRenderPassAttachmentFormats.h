//
// Created by 郭智 on 2026/11/8.
//

#pragma once

#include <vector>

#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * 一个 pass 的附件格式与采样数：这一批绘制往什么样的附件上画
     *
     * 动态渲染之后管线不再绑某个渲染通道对象，而是在创建时声明附件的格式与采样数；
     * 录制时 vkCmdBeginRendering 给的附件必须与这里一致。一个 pass 表里的管线都按
     * 同一份建（见 DoodleRenderPass）
     *
     * 与 DoodleRenderPassLayout 是一对：那份是着色器能读到什么，按 pass 类型静态定死；
     * 这份是画到什么上面，由 pass 按自己的附件现取，交换链重建后可能变
     *
     * 判等用默认的逐成员比较：交换链重建后 pass 拿新旧两份一比，就知道管线要不要重建
     */
    struct DoodleRenderPassAttachmentFormats
    {
        /**
         * 每个颜色附件的格式，按 location 排列
         *
         * 现在只有一个（交换链图像）。做成数组是给多渲染目标留的：延迟渲染的
         * G-buffer、离屏的多个输出；深度预渲染 / 阴影这类只写深度的 pass 是空数组
         */
        std::vector<VkFormat> colorFormats;

        /**
         * 深度附件格式；VK_FORMAT_UNDEFINED 表示这个 pass 没有深度附件
         * （后处理、UI 这类全屏 pass），此时渲染状态里的深度设置不生效
         */
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        /**
         * 光栅化采样数。现在是 1，留给 MSAA：采样数是管线的一部分，
         * 开 MSAA 的 pass 要另一套管线
         */
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        [[nodiscard]] bool operator==(const DoodleRenderPassAttachmentFormats&) const = default;
    };
}
