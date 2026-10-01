//
//  SwapChainSupportDetails.h
//  Doodle
//
//  Created by 郭智 on 2026/9/27.
//
#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    struct SwapChainSupportDetails
    {
        /**
         * 基本表面能力
         */
        VkSurfaceCapabilitiesKHR surfaceCapabilities;

        /**
         * 表面格式
         */
        std::vector<VkSurfaceFormatKHR> surfaceFormats;

        /**
         * 呈现模式
         */
        std::vector<VkPresentModeKHR> presentModes;
    };
}