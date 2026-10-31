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
#include <utility>

#include "Asset/DoodleAssetManager.h"
#include "Core/DoodleComponentConfig.h"
#include "Vulkan/DoodleMesh.h"

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

        //材质按槽顺序写成对象数组。全部写出来，不省略默认值 ——
        //读回时不再需要另一套「缺省等于什么」的拼状态规则
        std::vector<DoodleComponentConfig> materialConfigs;
        materialConfigs.reserve(m_materials.size());
        for (const DoodleMaterial& material : m_materials)
        {
            DoodleComponentConfig materialConfig;
            materialConfig.SetVec4("baseColor", material.GetBaseColor());
            materialConfigs.push_back(std::move(materialConfig));
        }
        config.SetObjectArray("materials", materialConfigs);
    }

    void DoodleMeshRenderer::Deserialize(const DoodleComponentConfig& config)
    {
#ifndef NDEBUG
        config.ExpectOnlyKeys({"mesh", "materials"});
#endif

        const std::string assetPath = config.GetString("mesh", "");
        if (assetPath.empty())
        {
            throw std::runtime_error("mesh: must not be empty");
        }

        const auto* pMesh = DoodleAssetManager::GetInstance().Load<DoodleMesh>(assetPath);
        if (pMesh == nullptr)
        {
            // Loader 的契约是不返回空指针；这里只做防御
            throw std::runtime_error("mesh: mesh '" + assetPath + "' is missing");
        }

        // SetMesh 负责安装网格并把材质表重置为默认值；
        // 下面的 materials 配置只覆盖前 N 个槽，缺的槽保持默认
        SetMesh(pMesh);

        const std::vector<DoodleComponentConfig> materialConfigs = config.GetObjectArray("materials");
        if (materialConfigs.size() > m_materials.size())
        {
            throw std::runtime_error(
                "materials: has " + std::to_string(materialConfigs.size()) +
                " entries but mesh '" + assetPath + "' has " + std::to_string(m_materials.size()) +
                " material slots");
        }

        for (std::size_t i = 0; i < materialConfigs.size(); ++i)
        {
            // 子配置自己不知道数组下标，这里补上 "materials[i]" 前缀，
            // 场景层再在外层补组件路径
            try
            {
#ifndef NDEBUG
                materialConfigs[i].ExpectOnlyKeys({"baseColor"});
#endif
                DoodleMaterial material;
                material.SetBaseColor(materialConfigs[i].GetVec4("baseColor", material.GetBaseColor()));
                m_materials[i] = material;
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

        // 槽数跟着网格走；旧材质不能沿用，否则会串到别的子网格上
        const std::size_t slotCount = m_pMesh != nullptr ? m_pMesh->GetMaterialSlotCount() : 0;
        m_materials.assign(slotCount, DoodleMaterial{});
    }
}
