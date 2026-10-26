//
//  DoodleVulkanUtils.h
//  Doodle
//

#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

namespace Doodle
{
    /**
     * Vulkan 的纯函数工具箱
     *
     * 收进来的门槛有两条，都满足才放：
     *
     *   1. 不碰任何 Vulkan 句柄、不依赖设备状态。输入是规范里的值类型
     *      （枚举、extent、能力结构），输出是算出来的值，同样的输入永远同样的输出
     *   2. 它陈述的是**Vulkan 规定的事实**，不是本工程的偏好。选 surface format、
     *      选 present mode、选 compositeAlpha 那些"我们要什么"留在各自持有设备的层
     *
     * 第二条不写清楚，这里迟早变成杂物间：一旦开始收"要给设备指针才能算"的
     * 函数，它和 DoodleVulkanDevice 就没区别了，而且没人说得清某段逻辑该去哪
     *
     * 全是静态方法，没有实例也不该有：工具没有生命周期，造一个对象出来只会
     * 让人以为它持有了什么
     */
    class DoodleVulkanUtils final
    {
    public:
        DoodleVulkanUtils() = delete;

        /**
         * 这个变换会不会把图像的宽高对调（90 / 270）
         *
         * 交换链据此决定图像尺寸，投影据此把宽高比换回来 —— 两处必须是同一个判断。
         * 差一个就会得到一张"看着像对的"歪图：比例没错，FOV 错了，很难看出来
         *
         * @param transform 交换链实际填进 preTransform 的那个变换（即表面的 currentTransform）
         */
        [[nodiscard]] static bool SwapsExtent(VkSurfaceTransformFlagBitsKHR transform);

        /**
         * 按**显示方向**算宽高比
         *
         * 交换链图像的宽高和用户看到的宽高在 90/270 时是对调的，所以宽高比要取倒数。
         * 投影必须用这一个：拿图像那一组宽高比去调 perspective，会把 FOV 压到长边上，
         * 转回显示方向后既错 FOV 又两轴不一致
         *
         * @param imageExtent 交换链图像的尺寸（不是 currentExtent 那个显示方向的尺寸）
         * @param preTransform 交换链声明的朝向
         * @return 显示方向的 width / height
         */
        [[nodiscard]] static float GetDisplayAspect(VkExtent2D imageExtent, VkSurfaceTransformFlagBitsKHR preTransform);

        /**
         * 把裁剪空间里的画面转成交换链图像的朝向
         *
         * 交换链声明了 preTransform = currentTransform，也就是"画面已经转好了"，
         * 合成器不会再替我们转。顶点输出是在**显示方向**的相机里算的，
         * 所以这最后一转得在投影里补上 —— 合成器那边的旋转正好把它抵消
         *
         * 只绕裁剪空间的 Z 轴转，不改 x/y 的缩放关系。所以宽高比必须在这之前
         * 就按显示方向算好（见 GetDisplayAspect），不能指望这一步去修
         *
         * @param preTransform 交换链声明的朝向。IDENTITY 时返回单位阵
         */
        [[nodiscard]] static glm::mat4 MakePreRotation(VkSurfaceTransformFlagBitsKHR preTransform);
    };
}
