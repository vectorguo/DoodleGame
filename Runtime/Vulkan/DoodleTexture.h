//
//  DoodleTexture.h
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "DoodleImage.h"
#include "DoodleVulkanDevice.h"

namespace Doodle
{
    /**
     * 一张纹理：图像 + 采样器 + 一次性的像素上传
     *
     * 与 Unity 的 Texture 差着几层：这里没有资产导入、mip 生成、流式加载、
     * 引用计数，也没有「绑定到材质/描述符」的语义。它是本项目「被采样的图」的底座：
     * DoodleImage 负责图与视图，本类加上采样器与上传。
     *
     * 现在还没有调用者 —— 描述符与着色器采样要等 Uniform buffers / 纹理那几章。
     * 所以本类是「资源就绪、尚未接线」的状态：上传路径可以独立验证，
     * 但画面上还看不到它
     *
     * 格式固定 R8G8B8A8_SRGB、单级 mip、采样器固定线性 + REPEAT：
     * 这些是当前唯一用途的形状。需要别的格式/过滤时再把它们变成参数，
     * 而不是在调用点各猜一次
     */
    class DoodleTexture
    {
    public:
        DoodleTexture() = default;

        //句柄的成对生死在本类手里：复制一份就是两个对象各自去销毁同一组句柄
        DoodleTexture(const DoodleTexture&) = delete;
        DoodleTexture& operator=(const DoodleTexture&) = delete;

        /**
         * 从 CPU 像素建一张纹理
         *
         * @param device 已初始化的Vulkan设备
         * @param width 宽
         * @param height 高
         * @param pixels RGBA8 像素
         * @param size 像素字节数，必须严格等于 width * height * 4
         *
         * @note 前置条件：DoodleCommandBuffer 已初始化（上传要用它的传输池），
         * 且不得与在途的帧并发 —— 上传会提交一次拷贝并等队列空闲，
         * 只在初始化期调用
         */
        void Initialize(const DoodleVulkanDevice& device, uint32_t width, uint32_t height,
                        const void* pixels, VkDeviceSize size);

        /**
         * 销毁采样器、视图、图与内存
         *
         * @note 前置条件：GPU 已空闲，且没有在途命令引用这张图
         */
        void Destroy();

        [[nodiscard]] VkImageView GetImageView() const
        {
            return m_image.GetImageView();
        }

        /**
         * 采样器：过滤与寻址方式
         *
         * 与图像分开 —— 同一张图可以配不同采样器。这是 Vulkan 与 Unity 的一处语义差异：
         * Unity 的过滤/寻址挂在纹理上，Vulkan 里它们是独立对象
         */
        [[nodiscard]] VkSampler GetSampler() const
        {
            return m_sampler;
        }

        [[nodiscard]] VkFormat GetFormat() const
        {
            return m_image.GetFormat();
        }

    private:
        /**
         * 把像素经中转缓冲拷进图像，并把布局转到 SHADER_READ_ONLY_OPTIMAL
         *
         * 三段：UNDEFINED → TRANSFER_DST → 拷贝 → SHADER_READ_ONLY。
         * 提交后在队列上空闲等待，保证返回时图像已经可被采样
         */
        void UploadPixels(uint32_t width, uint32_t height, const void* pixels, VkDeviceSize size);

        /**
         * 建采样器：线性过滤、REPEAT、单级 mip
         */
        void CreateSampler();

        /**
         * 销毁采样器
         */
        void DestroySampler();

    private:
        /**
         * Vulkan设备层，在 Initialize 时绑定
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;

        /**
         * 图像与视图
         */
        DoodleImage m_image;

        /**
         * 采样器
         */
        VkSampler m_sampler = VK_NULL_HANDLE;
    };
}
