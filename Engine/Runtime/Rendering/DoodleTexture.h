//
//  DoodleTexture.h
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "Vulkan/DoodleImage.h"

namespace Doodle
{
    //只在接口签名与成员指针里出现，完整定义留给源文件
    class DoodleVulkanDevice;

    /**
     * 一张纹理：图像 + 采样器 + 一次性的像素上传
     *
     * 放在 Rendering：它属于「画什么」里的像素内容；Vulkan 侧的 DoodleImage
     * 才是「怎么存」的图像机制。本类按值持有那份机制，对外暴露的仍是纹理语义。
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
        void Initialize(const DoodleVulkanDevice& device,
                        const uint32_t width,
                        const uint32_t height,
                        const void* pixels,
                        const VkDeviceSize size);

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

        [[nodiscard]] VkFormat GetFormat() const
        {
            return m_image.GetFormat();
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

    private:
        /**
         * 把 CPU 像素上传到这张纹理的图像，并把布局转到 SHADER_READ_ONLY_OPTIMAL
         *
         * 名字只说「上传像素」：在 DoodleTexture 里目标只有自己的 m_image，
         * 把 ToImage 写进方法名等于把内部成员抬进接口，读起来也像一次裸的
         * buffer→image 拷贝。实际编排有四步 —— 建中转、录「转 TRANSFER_DST →
         * 拷贝 → 转 SHADER_READ_ONLY」、交给命令层提交并等待、销毁中转；
         * 单条布局转换 / 拷贝命令由下面三个私有录制函数完成
         *
         * 提交后在队列上空闲等待，保证返回时图像已经可被采样
         */
        void UploadPixels(const uint32_t width,
                          const uint32_t height,
                          const void* pixels,
                          const VkDeviceSize size);

        /**
         * 录一次布局转换：刚建出来的图 → TRANSFER_DST
         *
         * 旧内容不要，源 scope 留空；dst access 只写到 TRANSFER_WRITE ——
         * 接下来对这张图的唯一操作就是拷贝
         *
         * subresource 固定单 mip、单层、颜色切面 —— 与 DoodleImage 的单级单层
         * 不变量一致；将来接 mip 时，这里和 CreateSampler 的 LOD 一起改
         */
        void RecordImageTransitionToTransferDst(VkCommandBuffer commandBuffer) const;

        /**
         * 录一次布局转换：TRANSFER_DST → SHADER_READ_ONLY
         *
         * 拷贝的写要在 TRANSFER 阶段完成；采样读发生在片元着色器
         */
        void RecordImageTransitionToShaderRead(VkCommandBuffer commandBuffer) const;

        /**
         * 录「中转缓冲 → 整张图像」的拷贝
         *
         * 名字对齐 vkCmdCopyBufferToImage2：Record + 命令名。
         * bufferRowLength / bufferImageHeight 填 0 表示紧密排列；
         * 区域是整张图从 (0,0) 开始
         */
        void RecordCopyBufferToImage(VkCommandBuffer commandBuffer,
                                     VkBuffer sourceBuffer,
                                     uint32_t width,
                                     uint32_t height) const;

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
         * 当前唯一用途：RGBA8、sRGB。要别的格式时把它变成 Initialize 的参数
         */
        static constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;

        /**
         * RGBA8 每个像素的字节数，校验像素数量与尺寸用
         */
        static constexpr uint32_t kBytesPerPixel = 4;

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
