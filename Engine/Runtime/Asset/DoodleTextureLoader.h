//
//  DoodleTextureLoader.h
//  Doodle
//
//  由 DoodleTexture 资产化新增
//

#pragma once

#include <memory>
#include <string_view>

#include "DoodleAssetLoader.h"
#include "Rendering/DoodleTexture.h"

namespace Doodle
{
    class DoodleVulkanDevice;
    class DoodleFileSystem;

    /**
     * 纹理加载器：文件字节 → stb_image 解码 → RGBA8 → DoodleTexture
     *
     * key 是纹理文件的相对路径（例如 "Art/SceneRes/Building/House1/House1_d.tga"）。
     * 解码统一转成 RGBA8、sRGB 格式；DoodleTexture 的 Initialize 会把像素上传到
     * 设备本地并转到可采样布局。缓存与去重由 DoodleAssetLoader 基类按 key 负责。
     *
     * 当前 TGA 的 24 / 32 位未压缩与 RLE 都走 stb_image 的同一入口；
     * PNG / JPG 也一并支持。mip 生成、压缩格式与异步加载都还没做。
     *
     * 线程约定：会创建 Vulkan 资源并等队列空闲，只能在主线程、初始化期调用。
     */
    class DoodleTextureLoader final : public DoodleAssetLoader<DoodleTexture>
    {
    public:
        DoodleTextureLoader(DoodleVulkanDevice& device, const DoodleFileSystem& fileSystem);

        DoodleTextureLoader(const DoodleTextureLoader&) = delete;
        DoodleTextureLoader& operator=(const DoodleTextureLoader&) = delete;

    protected:
        /**
         * 读取并解码一份纹理，再上传成 DoodleTexture
         *
         * @throws std::runtime_error 文件读取失败、解码失败，或像素尺寸不合法。
         *         失败时不会留下已上传的 GPU 资源
         */
        [[nodiscard]] std::unique_ptr<DoodleTexture> Create(std::string_view key) override;

        /**
         * 销毁纹理的采样器、视图、图与内存
         *
         * 前置条件：GPU 已空闲（资产管理器在设备销毁之前统一调用）
         */
        void DestroyAsset(DoodleTexture& texture) override;

    private:
        /**
         * Vulkan 设备，非拥有；寿命由 DoodleApplication 保证
         */
        DoodleVulkanDevice* m_pDevice = nullptr;
    };
}
