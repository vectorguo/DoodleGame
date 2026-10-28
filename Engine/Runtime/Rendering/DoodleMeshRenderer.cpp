//
//  DoodleMeshRenderer.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleMeshRenderer.h"

#include <stdexcept>
#include <string>

#include "Asset/DoodleAssetManager.h"
#include "Core/DoodleComponentConfig.h"
#include "Vulkan/DoodleMesh.h"

namespace Doodle
{
    void DoodleMeshRenderer::Serialize(DoodleComponentConfig& config) const
    {
        //只写反序列化保存的 key；运行时 SetMesh 替换网格不会更新它
        config.SetString("mesh", m_meshKey);
    }

    void DoodleMeshRenderer::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"mesh"});
#endif

        m_meshKey = config.GetString("mesh", "");
        if (m_meshKey.empty())
        {
            throw std::runtime_error(config.GetPath() + ".mesh: must not be empty");
        }

        m_pMesh = DoodleAssetManager::GetInstance().Load<DoodleMesh>(m_meshKey);
        if (m_pMesh == nullptr)
        {
            // Loader 的契约是不返回空指针；这里只做防御
            throw std::runtime_error(config.GetPath() + ".mesh: mesh '" + m_meshKey + "' is missing");
        }
    }
}
