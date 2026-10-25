//
//  DoodleMeshAssetLoader.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/25.
//

#include "DoodleMeshAssetLoader.h"

#include <stdexcept>
#include <string>

#include "Vulkan/DoodleVulkanDevice.h"

namespace Doodle
{
    DoodleMeshAssetLoader::DoodleMeshAssetLoader(const DoodleVulkanDevice& device)
    {
        // 内建网格与将来的外部网格走同一条上传路径；这里在注册时一次性建好
        m_meshLibrary.Initialize(device);
    }

    const DoodleMesh* DoodleMeshAssetLoader::Load(const DoodleAssetLoadContext&, const std::string_view key)
    {
        const DoodleMesh* pMesh = m_meshLibrary.Find(key);
        if (pMesh == nullptr)
        {
            // 网格名写错是内容错误，必须在建场景之前暴露出来。
            // 将来的文件网格在这里补「查不到再按路径导入」
            throw std::runtime_error("mesh asset not found: " + std::string(key));
        }

        return pMesh;
    }

    void DoodleMeshAssetLoader::Destroy()
    {
        m_meshLibrary.Destroy();
    }
}
