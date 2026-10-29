//
//  DoodleMeshLoader.h
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#pragma once

#include <memory>
#include <string_view>

#include "DoodleAssetLoader.h"
#include "Vulkan/DoodleMesh.h"

namespace Doodle
{
    class DoodleVulkanDevice;

    /**
     * 网格资产 loader，资产管理器内部使用
     *
     * 对外不暴露：调用方只写 assetManager.Load<DoodleMesh>(key)。缓存、去重
     * 与销毁在 DoodleAssetLoader<DoodleMesh> 基类里；本类只负责把 key 变成
     * 一份网格。
     *
     * 现在只认内建名 "builtin:cube"。将来读 FBX 时，在同一个 Create 里按路径
     * 导入并返回即可 —— 名字索引已经由基类的缓存承担，不需要再维护一份库。
     *
     * 设备在构造时注入，Create 时才用它上传：loader 注册本身不再碰 Vulkan，
     * 失败点从「资产管理器初始化」推迟到「第一次 Load」，报错也能带上 key。
     */
    class DoodleMeshLoader final : public DoodleAssetLoader<DoodleMesh>
    {
    public:
        explicit DoodleMeshLoader(const DoodleVulkanDevice& device);
        ~DoodleMeshLoader() override = default;

        DoodleMeshLoader(const DoodleMeshLoader&) = delete;
        DoodleMeshLoader& operator=(const DoodleMeshLoader&) = delete;
        DoodleMeshLoader(DoodleMeshLoader&&) = delete;
        DoodleMeshLoader& operator=(DoodleMeshLoader&&) = delete;

    protected:
        [[nodiscard]] std::unique_ptr<DoodleMesh> Create(const DoodleAssetLoadContext& context, std::string_view key) override;

    private:
        /**
         * Vulkan 设备，非拥有；寿命由 DoodleApplication 保证
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;
    };
}
