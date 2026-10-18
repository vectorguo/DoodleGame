//
//  DoodleImage.h
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#pragma once

#include <vulkan/vulkan_core.h>

#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 一张 GPU 图像：VkImage 对象 + 它占的内存 + 一个视图
     *
     * 与 DoodleGraphicsBuffer 同形：持有句柄、显式 Initialize / Destroy、由宿主持有。
     * 区别只在资源种类 —— 这里是 2D 图像，不是线性字节区间。
     *
     * 它只管「建出来、活得对」：布局转换、当附件还是被采样、要不要上传数据，
     * 都由使用者决定（usage / aspect 通过参数传进来）。那些语义收在这里，
     * 等于让一张图同时变成资源、渲染目标和上传器。
     *
     * mip 与 layer 固定为 1：本项目当前不需要多级/数组图。需要时把这两个字段
     * 变成参数，而不是在调用点各写一次 VkImageCreateInfo。
     */
    class DoodleImage
    {
    public:
        DoodleImage() = default;

        //句柄的成对生死在本类手里：复制一份就是两个对象各自去销毁同一组句柄
        DoodleImage(const DoodleImage&) = delete;
        DoodleImage& operator=(const DoodleImage&) = delete;

        /**
         * 建图、分配内存、绑定、建视图
         *
         * @param device 已初始化的Vulkan设备
         * @param extent 图像尺寸
         * @param format 图像格式
         * @param usage 用途标志。决定驱动怎么摆放它，也决定谁能用它
         * @param aspectFlags 视图的切面 —— 深度图要 DEPTH（+STENCIL），颜色图要 COLOR
         *
         * @note 前置条件：尚未初始化。创建后处于 VK_IMAGE_LAYOUT_UNDEFINED，
         * 布局转换归使用者
         */
        void Initialize(const DoodleVulkanDevice& device, VkExtent2D extent,
                        VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags);

        /**
         * 销毁视图、图与内存
         *
         * @note 前置条件：GPU 已空闲，且没有在途命令引用这张图。
         * 顺序是创建序的反序：视图 → 图 → 内存
         */
        void Destroy();

        [[nodiscard]] VkImage GetImage() const
        {
            return m_image;
        }

        [[nodiscard]] VkImageView GetImageView() const
        {
            return m_imageView;
        }

        [[nodiscard]] VkFormat GetFormat() const
        {
            return m_format;
        }

    private:
        /**
         * 建图并分配、绑定内存
         *
         * 前置条件：m_pDevice 与 m_format 已赋值
         */
        void CreateImage(VkExtent2D extent, VkImageUsageFlags usage);

        /**
         * 建视图
         *
         * 前置条件：m_image 已创建。失败时把图与内存一起收掉再抛
         */
        void CreateImageView(VkImageAspectFlags aspectFlags);

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 图像格式
         */
        VkFormat m_format = VK_FORMAT_UNDEFINED;

        /**
         * 图像对象。不含存储，只是描述
         */
        VkImage m_image = VK_NULL_HANDLE;

        /**
         * 图像占的那块内存
         */
        VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;

        /**
         * 图像视图。目前每张图一个，切面由调用方给
         */
        VkImageView m_imageView = VK_NULL_HANDLE;
    };
}
