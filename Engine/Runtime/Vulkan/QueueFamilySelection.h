//
//  QueueFamilySelection.h
//  Doodle
//
//  Created by 郭智 on 2026/9/12.
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace Doodle
{
    /**
     * 为每个队列角色选中的队列族索引
     *
     * 注意区分：这里存的是「队列族的索引」（vkGetDeviceQueue 的 queueFamilyIndex 参数），
     * 不是「族内队列的索引」（vkGetDeviceQueue 的 queueIndex 参数，本工程固定用 0）
     *
     * 某个角色没找到对应的族时保持 nullptr，用 HasAllRequiredFamilies 判断是否全部找到
     */
    struct QueueFamilySelection
    {
        /**
         * 图形队列族索引
         */
        std::optional<uint32_t> graphicsFamilyIndex;

        /**
         * 呈现队列族索引
         */
        std::optional<uint32_t> presentFamilyIndex;

        /**
         * 是否所有必需角色的队列族都已找到
         */
        [[nodiscard]] bool HasAllRequiredFamilies() const
        {
            return graphicsFamilyIndex.has_value() &&
                   presentFamilyIndex.has_value();
        }

        /**
         * 逐字段比较
         *
         * 默认实现即可：成员都是可选索引，比的就是「选中了哪个族」。
         * 用在表面重建后的复查上（见 DoodleVulkanDevice::VerifyQueueFamilySelection）
         */
        bool operator==(const QueueFamilySelection&) const = default;

        /**
         * 去重后的队列族索引列表，供创建逻辑设备时逐个填写 QueueCreateInfo
         *
         * 多个角色经常落在同一个族上（Apple Silicon 只有一个族，四个角色全在 0），
         * 而同一个族只能创建一次 QueueCreateInfo，所以要去重
         *
         * @note 前置条件：HasAllRequiredFamilies 为 true
         */
        [[nodiscard]] std::vector<uint32_t> GetUniqueFamilyIndices() const
        {
            std::vector<uint32_t> families;
            for (const auto index : {graphicsFamilyIndex, presentFamilyIndex})
            {
                const uint32_t familyIndex = index.value();
                if (!std::ranges::contains(families, familyIndex))
                {
                    families.emplace_back(familyIndex);
                }
            }
            return families;
        }
    };
}
