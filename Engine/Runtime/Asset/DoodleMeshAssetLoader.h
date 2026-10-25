//
//  DoodleMeshAssetLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <string_view>

#include "DoodleAssetLoader.h"
#include "Vulkan/DoodleMeshLibrary.h"

namespace Doodle
{
    class DoodleVulkanDevice;

    /**
     * 网格资产 loader，资产管理器内部使用
     *
     * 对外不暴露：调用方只写 assetManager.Load<DoodleMesh>(key)。它持有
     * DoodleMeshLibrary —— 原来挂在 DoodleVulkanManager 下的那层 —— 并按 key
     * 返回库里的网格。
     *
     * 现在只认内建名（如 "builtin:cube"）。将来读 FBX 时，就在同一个 Load 里
     * 先查库、查不到再按路径导入并登记回库，调用方仍然只看到 Load<DoodleMesh>。
     *
     * 生命周期：loader 归资产管理器所有，构造时上传内建网格；Destroy 在 Vulkan
     * 设备销毁之前由资产管理器调用。
     */
    class DoodleMeshAssetLoader final : public DoodleAssetLoader<DoodleMesh>
    {
    public:
        explicit DoodleMeshAssetLoader(const DoodleVulkanDevice& device);
        ~DoodleMeshAssetLoader() override = default;

        DoodleMeshAssetLoader(const DoodleMeshAssetLoader&) = delete;
        DoodleMeshAssetLoader& operator=(const DoodleMeshAssetLoader&) = delete;
        DoodleMeshAssetLoader(DoodleMeshAssetLoader&&) = delete;
        DoodleMeshAssetLoader& operator=(DoodleMeshAssetLoader&&) = delete;

        [[nodiscard]] const DoodleMesh* Load(const DoodleAssetLoadContext& context,
                                             std::string_view key) override;

        void Destroy() override;

    private:
        /**
         * 网格资产的实际拥有者与名字索引
         *
         * 仍是 DoodleMeshLibrary：loader 只是把它从 VulkanManager 的公开面
         * 收到了资产管理器后面
         */
        DoodleMeshLibrary m_meshLibrary;
    };
}
