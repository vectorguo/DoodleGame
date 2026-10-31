//
//  DoodleMeshLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleMeshLoader.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DoodleFbxImporter.h"
#include "Platform/DoodleFileSystem.h"

namespace Doodle
{
    DoodleMeshLoader::DoodleMeshLoader(const DoodleVulkanDevice& device,const DoodleFileSystem& fileSystem)
        : DoodleAssetLoader(fileSystem), m_pDevice(&device)
    {
        // 这里不做任何上传：loader 注册是初始化链上最不该失败的一步。
        // 真正的资源错误留给 Create，报错时还能带上具体 key
    }

    std::unique_ptr<DoodleMesh> DoodleMeshLoader::Create(const std::string_view key)
    {
        // 第一版只认 FBX。扩展名检查放在读文件之前：路径拼错时
        // 报「不支持这个格式」比导入器报「无法识别文件格式」更直接
        if (!key.ends_with(".fbx"))
        {
            throw std::runtime_error("mesh asset not found: " + std::string(key) + " (only .fbx paths are supported)");
        }

        const std::vector<std::byte> bytes = GetFileSystem().ReadBinary(key);
        const DoodleFbxMeshData meshData = DoodleFbxImporter::Import(bytes, key);

        auto pMesh = std::make_unique<DoodleMesh>(key);
        pMesh->Initialize(*m_pDevice, meshData.vertices, meshData.indices, meshData.submeshes);

        return pMesh;
    }

    void DoodleMeshLoader::DestroyAsset(DoodleMesh& mesh)
    {
        // DoodleMesh 的析构只释放 C++ 对象；VkBuffer / VkDeviceMemory 必须
        // 显式 Destroy。基类的默认 DestroyAsset 是空操作，少了这一行，
        // 资产管理器清缓存之后设备上会留下缓冲与内存，触发 VUID-05137
        mesh.Destroy();
    }
}
