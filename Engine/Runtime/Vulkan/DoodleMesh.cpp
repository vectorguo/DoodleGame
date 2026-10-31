//
//  DoodleMesh.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#include "DoodleMesh.h"

#include <cstdint>
#include <stdexcept>

namespace Doodle
{
    DoodleMesh::DoodleMesh(const std::string_view assetPath)
        : m_assetPath(assetPath)
    {
        if (m_assetPath.empty())
        {
            throw std::invalid_argument("DoodleMesh: assetPath must not be empty");
        }
    }

    void DoodleMesh::Initialize(const DoodleVulkanDevice& device,
                                const std::vector<Vertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                const std::vector<DoodleSubmesh>& submeshes)
    {
        // 先校验，后上传：本类的析构不销毁 GPU 资源，上传到一半抛异常会留下
        // 没人释放的缓冲。子网格是绘制端唯一的分段依据，区间错了画面上会缺块，
        // 校验层却一句话都不会说 —— 在这里当场拦住
        if (submeshes.empty())
        {
            throw std::runtime_error("mesh has no submeshes");
        }

        uint64_t coveredIndices = 0;
        for (const DoodleSubmesh& submesh : submeshes)
        {
            if (submesh.indexCount == 0)
            {
                throw std::runtime_error("mesh has an empty submesh");
            }
            if (submesh.firstIndex != coveredIndices)
            {
                throw std::runtime_error("mesh submeshes must be ordered and contiguous");
            }

            coveredIndices += submesh.indexCount;
            if (coveredIndices > indices.size())
            {
                throw std::runtime_error("mesh submesh range is out of bounds");
            }
        }
        if (coveredIndices != indices.size())
        {
            throw std::runtime_error("mesh submeshes do not cover the index buffer");
        }

        m_vertexBuffer.Initialize(device,
                                  vertices.size() * sizeof(Vertex),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  DoodleGraphicsBuffer::EMemoryType::DeviceLocal,
                                  vertices.data());
        m_indexBuffer.Initialize(device,
                                 indices.size() * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 DoodleGraphicsBuffer::EMemoryType::DeviceLocal,
                                 indices.data());

        // 区间是 CPU 侧数据，拷一份随网格保存：调用方的导入结果
        // 在 Create 返回后就销毁了
        m_submeshes.assign(submeshes.begin(), submeshes.end());
    }

    void DoodleMesh::Destroy()
    {
        m_vertexBuffer.Destroy();
        m_indexBuffer.Destroy();

        // 把 CPU 侧数据也清掉，Destroy 之后对象回到未初始化状态，
        // 不会留下「缓冲没了但区间还在」的半截网格
        m_submeshes.clear();
    }
}
