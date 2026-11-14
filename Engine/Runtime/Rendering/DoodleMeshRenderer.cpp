//
//  DoodleMeshRenderer.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/27.
//

#include "DoodleMeshRenderer.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

#include "Asset/DoodleAssetManager.h"
#include "Core/DoodleComponentConfig.h"
#include "DoodleMaterial.h"
#include "DoodleMesh.h"

namespace Doodle
{
    void DoodleMeshRenderer::Serialize(DoodleComponentConfig& config) const
    {
        //路径从网格资产取：renderer 不再另存 key。换过网格就写新网格的路径。
        //没有 assetPath（运行时动态创建）或没有网格时写空串；这种状态
        //不能被反序列化回来，序列化端只负责如实写出现状
        if (m_pMesh == nullptr)
        {
            config.SetString("mesh", "");
        }
        else
        {
            const auto& meshAssetPath = m_pMesh->GetAssetPath();
            config.SetString("mesh", meshAssetPath.empty() ? "" : meshAssetPath);
        }

        //材质按槽顺序写成资产路径数组。全部写出来，不省略 ——
        //读回时不需要另一套「缺省等于什么」的拼状态规则。
        //空指针槽写空串：表示该槽没有材质，不画对应的子网格
        std::vector<std::string> materialPaths;
        materialPaths.reserve(m_pMaterials.size());
        for (const DoodleMaterial* pMaterial : m_pMaterials)
        {
            materialPaths.push_back(pMaterial != nullptr ? pMaterial->GetAssetPath() : std::string());
        }
        config.SetStringArray("materials", materialPaths);
    }

    void DoodleMeshRenderer::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"mesh", "materials"});
#endif

        //初始化Mesh
        const std::string meshAssetPath = config.GetString("mesh", "");
        const DoodleMesh* pMesh = nullptr;
        if (!meshAssetPath.empty())
        {
            pMesh = DoodleAssetManager::GetInstance().Load<DoodleMesh>(meshAssetPath);
            if (pMesh == nullptr)
            {
                // Loader 的契约是不返回空指针；这里只做防御
                throw std::runtime_error("mesh: mesh '" + meshAssetPath + "' is missing");
            }
        }

        // 无论有没有网格都走 SetMesh：槽表长度必须始终等于当前网格的槽数，
        // 收集侧才能直接用 materialIndex 索引它。SetMesh 保留同槽号的已有材质、
        // 把新出现的槽留空；下面的 materials 配置再逐槽覆盖
        SetMesh(pMesh);

        const std::vector<std::string> materialPaths = config.GetStringArray("materials");
        for (std::size_t i = 0; i < materialPaths.size() && i < m_pMaterials.size(); ++i)
        {
            //空串表示该槽没有材质（SetMesh 已经留空），不画对应的子网格
            if (materialPaths[i].empty())
            {
                continue;
            }

            // 数组下标由这里补进错误消息，场景层再在外层补组件路径
            try
            {
                const auto* pMaterial = DoodleAssetManager::GetInstance().Load<DoodleMaterial>(materialPaths[i]);
                m_pMaterials[i] = pMaterial;
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error("materials[" + std::to_string(i) + "]: " + error.what());
            }
        }
    }

    void DoodleMeshRenderer::SetMesh(const DoodleMesh* pMesh)
    {
        m_pMesh = pMesh;
        if (m_pMesh != nullptr)
        {
            const auto slotCount = m_pMesh->GetMaterialSlotCount();
            if (slotCount > m_pMaterials.size())
            {
                m_pMaterials.resize(slotCount);
            }
        }
    }
}
