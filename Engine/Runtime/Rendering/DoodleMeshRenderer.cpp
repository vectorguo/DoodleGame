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

        //材质按槽顺序写成资产路径数组。全部写出来，不省略默认值 ——
        //读回时不再需要另一套「缺省等于什么」的拼状态规则。
        //默认材质没有 assetPath，写成空串；反序列化时空串重新指向默认材质
        std::vector<std::string> materialPaths;
        materialPaths.reserve(m_materials.size());
        for (const DoodleMaterial* pMaterial : m_materials)
        {
            materialPaths.push_back(pMaterial != nullptr ? pMaterial->GetAssetPath() : std::string{});
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
        if (!meshAssetPath.empty())
        {
            const auto* pMesh = DoodleAssetManager::GetInstance().Load<DoodleMesh>(meshAssetPath);
            if (pMesh == nullptr)
            {
                // Loader 的契约是不返回空指针；这里只做防御
                throw std::runtime_error("mesh: mesh '" + meshAssetPath + "' is missing");
            }

            // SetMesh 负责安装网格并把材质表重置为默认值；
            // 下面的 materials 配置只覆盖前 N 个槽，缺的槽保持默认
            SetMesh(pMesh);
        }

        const std::vector<std::string> materialPaths = config.GetStringArray("materials");
        if (materialPaths.size() > m_materials.size())
        {
            throw std::runtime_error(
                "materials: has " + std::to_string(materialPaths.size()) +
                " entries but mesh '" + meshAssetPath + "' has " + std::to_string(m_materials.size()) +
                " material slots");
        }

        for (std::size_t i = 0; i < materialPaths.size(); ++i)
        {
            //空串表示该槽使用默认材质（SetMesh 已经装好），不需要覆盖
            if (materialPaths[i].empty())
            {
                continue;
            }

            // 数组下标由这里补进错误消息，场景层再在外层补组件路径
            try
            {
                const auto* pMaterial = DoodleAssetManager::GetInstance().Load<DoodleMaterial>(materialPaths[i]);
                m_materials[i] = pMaterial;
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

        // 槽数跟着网格走；旧材质不能沿用，否则会串到别的子网格上。
        // 默认材质由本组件持有，槽表只存指针
        const std::size_t slotCount = m_pMesh != nullptr ? m_pMesh->GetMaterialSlotCount() : 0;
        m_materials.assign(slotCount, &m_defaultMaterial);
    }
}
