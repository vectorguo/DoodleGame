//
//  DoodleMesh.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/24.
//

#include "DoodleMesh.h"

#include <stdexcept>

namespace Doodle
{
    void DoodleMesh::Initialize(const DoodleVulkanDevice& device,
                                std::span<const Vertex> vertices,
                                std::span<const uint32_t> indices)
    {
        m_vertexBuffer.Initialize(device,
                                  vertices.size_bytes(),
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  DoodleGraphicsBuffer::EMemoryType::DeviceLocal,
                                  vertices.data());
        m_indexBuffer.Initialize(device,
                                 indices.size_bytes(),
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 DoodleGraphicsBuffer::EMemoryType::DeviceLocal,
                                 indices.data());
        m_indexCount = static_cast<uint32_t>(indices.size());
    }

    void DoodleMesh::Destroy()
    {
        m_vertexBuffer.Destroy();
        m_indexBuffer.Destroy();
        m_indexCount = 0;
    }
}
