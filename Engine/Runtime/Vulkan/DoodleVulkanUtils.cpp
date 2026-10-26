//
//  DoodleVulkanUtils.cpp
//  Doodle
//

#include "DoodleVulkanUtils.h"

namespace Doodle
{
    bool DoodleVulkanUtils::SwapsExtent(VkSurfaceTransformFlagBitsKHR transform)
    {
        //四个四分之一转里，只有 90 和 270 会把宽高对调。180 只是倒过来，尺寸不变
        switch (transform)
        {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            return true;

        default:
            //IDENTITY、180，以及桌面从不会出现的镜像位：都不对调
            return false;
        }
    }

    float DoodleVulkanUtils::GetDisplayAspect(VkExtent2D imageExtent, VkSurfaceTransformFlagBitsKHR preTransform)
    {
        //不做零除保护：extent 为 0 的交换链根本建不出来，能走到这里的尺寸一定有效
        if (SwapsExtent(preTransform))
        {
            return static_cast<float>(imageExtent.height) / static_cast<float>(imageExtent.width);
        }

        return static_cast<float>(imageExtent.width) / static_cast<float>(imageExtent.height);
    }

    glm::mat4 DoodleVulkanUtils::MakePreRotation(VkSurfaceTransformFlagBitsKHR preTransform)
    {
        glm::mat4 rotation(1.0f);

        //列向量写法：[0] 是第 0 列，[1] 是第 1 列
        switch (preTransform)
        {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
            //(x, y) → (-y, x)。Vulkan 的 NDC y 朝下，屏幕上看着是逆时针 90 度
            rotation[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            rotation[1] = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
            break;

        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            rotation[0] = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
            rotation[1] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            break;

        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
            rotation[0] = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
            rotation[1] = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
            break;

        default:
            //IDENTITY（桌面恒为此值），以及镜像位：不转
            break;
        }

        return rotation;
    }
}
