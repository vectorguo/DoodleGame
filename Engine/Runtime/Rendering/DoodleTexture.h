//
//  DoodleTexture.h
//  Doodle
//
//  Created by 郭智 on 2026/10/17.
//

#pragma once

#include <string_view>
#include <vulkan/vulkan_core.h>

#include "Asset/DoodleAsset.h"
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
     * 与 Unity 的 Texture 差着几层：这里没有 mip 生成、流式加载、引用计数；
     * 资产导入由 DoodleTextureLoader 负责。它是本项目「被采样的图」的底座：
     * DoodleImage 负责图与视图，本类加上采样器与上传。
     *
     * 现在由场景里的材质按路径加载并引用（非拥有指针）；门面按纹理创建
     * set 1 描述符集，片元着色器采样。没有配置纹理的材质绑定默认 1×1 白图。
     *
     * 格式固定 R8G8B8A8_SRGB、单级 mip、采样器固定线性 + REPEAT：
     * 这些是当前唯一用途的形状。需要别的格式/过滤时再把它们变成参数，
     * 而不是在调用点各猜一次
     */
    class DoodleTexture : public DoodleAsset
    {
    public:
        /**
         * 运行时动态创建：没有资产路径，不能被序列化
         */
        DoodleTexture() = default;

        /**
         * 文件资产：路径交给 DoodleAsset 保存并校验非空
         *
         * @param assetPath 资产的加载路径（当前是相对文件路径）
         */
        explicit DoodleTexture(std::string_view assetPath);

        /**
         * 从 CPU 像素建一张纹理
         *
         * @param device 已初始化的Vulkan设备
         * @param width 宽
         * @param height 高
         * @param pixels RGBA8 像素
         * @param size 像素字节数，必须严格等于 width * height * 4
         *
         * @note 前置条件：DoodleCommandManager 已初始化（上传要用它的传输池），
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

        /**
         * 图像视图：描述符里写入的 view
         *
         * 与 DoodleImage 的视图是同一个句柄，本类不另持有一份。
         * 采样器是独立对象，取用见 GetSampler
         */
        [[nodiscard]] VkImageView GetImageView() const
        {
            return m_image.GetImageView();
        }

        /**
         * 图像格式
         *
         * 当前固定为 R8G8B8A8_SRGB（见 kTextureFormat）。
         * 渲染路径不需要每帧读它；留给调试、断言与将来的格式分支
         */
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
         * buffer→image 拷贝。编排只有四步 —— 建中转、交给命令层录「转 TRANSFER_DST →
         * 拷贝 → 转 SHADER_READ_ONLY」、提交并等待、销毁中转；
         * 三条命令由 DoodleCommandManager::FillTextureTransferCommand 录制
         *
         * 提交后在队列上空闲等待，保证返回时图像已经可被采样
         */
        void UploadPixels(const uint32_t width,
                          const uint32_t height,
                          const void* pixels,
                          const VkDeviceSize size);

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
