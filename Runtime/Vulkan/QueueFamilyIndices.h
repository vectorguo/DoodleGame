//
//  QueueFamilyIndices.h
//  Doodle
//
//  Created by 郭智 on 2026/9/12.
//
#pragma once

#include <optional>

namespace Doodle
{
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamilyIndex;
        std::optional<uint32_t> presentFamilyIndex;

        bool IsComplete() const
        {
            return graphicsFamilyIndex.has_value() &&
                   presentFamilyIndex.has_value();
        }
    };
}
