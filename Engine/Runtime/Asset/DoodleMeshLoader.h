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
    class DoodleFileSystem;

    /**
     * 网格资产 loader，资产管理器内部使用
     *
     * 对外不暴露：调用方只写 assetManager.Load<DoodleMesh>(key)。缓存、去重
     * 与销毁在 DoodleAssetLoader<DoodleMesh> 基类里；本类只负责把 key 变成
     * 一份网格。
     *
     * 目前只支持以 .fbx 结尾的相对路径：交给 DoodleFbxImporter 解析并拍平成
     * 单个 DoodleMesh。
     *
     * FBX 第一版按「全部拍平成一个 mesh」处理：节点世界变换烘焙进顶点，
     * 只保留位置、法线与第一套 UV；材质、骨骼与动画都不保留。缓存、去重与销毁
     * 仍由基类按 key 负责；这条限制写在 DoodleFbxImporter 的注释里，
     * 换成模型资产时从那里扩展。
     *
     * 设备在构造时注入，Create 时才用它上传：loader 注册本身不再碰 Vulkan，
     * 失败点从「资产管理器初始化」推迟到「第一次 Load」，报错也能带上 key。
     * 文件系统同样在构造时由基类保存非拥有指针，Load 只接收 key。
     */
    class DoodleMeshLoader final : public DoodleAssetLoader<DoodleMesh>
    {
    public:
        DoodleMeshLoader(const DoodleVulkanDevice& device, const DoodleFileSystem& fileSystem);
        ~DoodleMeshLoader() override = default;

        DoodleMeshLoader(const DoodleMeshLoader&) = delete;
        DoodleMeshLoader& operator=(const DoodleMeshLoader&) = delete;
        DoodleMeshLoader(DoodleMeshLoader&&) = delete;
        DoodleMeshLoader& operator=(DoodleMeshLoader&&) = delete;

    protected:
        [[nodiscard]] std::unique_ptr<DoodleMesh> Create(std::string_view key) override;

        /**
         * 释放一份网格的 GPU 资源
         *
         * 基类的默认实现是空操作；DoodleMesh 的析构器只释放对象内存，
         * 不销毁 VkBuffer / VkDeviceMemory。不重写这里，资产管理器 Destroy
         * 之后设备上会留下这对缓冲，vkDestroyDevice 触发 VUID-05137
         */
        void DestroyAsset(DoodleMesh& mesh) override;

    private:
        /**
         * Vulkan 设备，非拥有；寿命由 DoodleApplication 保证
         */
        const DoodleVulkanDevice* m_pDevice = nullptr;
    };
}
