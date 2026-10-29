//
//  DoodleFbxImporter.cpp
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#include "DoodleFbxImporter.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "ufbx/ufbx.h"

namespace
{
    /**
     * ufbx 场景的 owner：无论成功、异常都走同一个释放路径
     */
    struct FbxSceneDeleter
    {
        void operator()(ufbx_scene* pScene) const
        {
            if (pScene != nullptr)
            {
                ufbx_free_scene(pScene);
            }
        }
    };

    /**
     * ufbx 的矩阵是列主序的 3×4：cols[0..2] 是 3×3 部分，cols[3] 是平移
     */
    glm::mat4 FbxMatrixToGlm(const ufbx_matrix& matrix)
    {
        glm::mat4 result(1.0f);
        result[0] = glm::vec4(matrix.cols[0].x, matrix.cols[0].y, matrix.cols[0].z, 0.0f);
        result[1] = glm::vec4(matrix.cols[1].x, matrix.cols[1].y, matrix.cols[1].z, 0.0f);
        result[2] = glm::vec4(matrix.cols[2].x, matrix.cols[2].y, matrix.cols[2].z, 0.0f);
        result[3] = glm::vec4(matrix.cols[3].x, matrix.cols[3].y, matrix.cols[3].z, 1.0f);
        return result;
    }

    glm::vec3 FbxVec3ToGlm(const ufbx_vec3& value)
    {
        return {value.x, value.y, value.z};
    }

    /**
     * 由世界空间法线算临时明暗色
     *
     * 第一版没有材质/光照管线，顶点色是唯一能带进片元的信息。把法线点乘
     * 固定光向当成 Lambert，至少能看清模型的凹凸和朝向；等做材质时这一段
     * 会被真正的 material 参数替换。
     */
    glm::vec3 ShadeFbxNormal(const glm::vec3& normal, const glm::vec3& lightDirection)
    {
        const float diffuse = std::max(glm::dot(normal, lightDirection), 0.0f);
        return glm::vec3(0.82f) * (0.22f + 0.78f * diffuse);
    }

    std::string MakeFbxErrorMessage(const ufbx_error& error)
    {
        std::string message;
        if (error.description.data != nullptr && error.description.length > 0)
        {
            message.assign(error.description.data, error.description.length);
        }
        else
        {
            message = "unknown FBX error";
        }

        if (error.info_length > 0)
        {
            message += " (";
            message.append(error.info, error.info_length);
            message += ")";
        }
        return message;
    }

    /**
     * FBX 字节 → 拍平后的 CPU 网格数据
     *
     * 目标坐标与单位交给 ufbx 归一化：右手 Z-up、1 单位 = 1 米。节点世界
     * 变换在下面烘焙进顶点，输出直接是引擎世界空间。
     *
     * 按节点遍历而不是按 mesh 数组遍历：同一份 mesh 可能被多个节点实例化，
     * 每个实例的 geometry_to_world 不同；拍平语义下每个实例都复制一份几何。
     */
    Doodle::DoodleFbxMeshData ImportFbxMesh(const std::span<const std::byte> bytes,
                              const std::string_view sourceName)
    {
        if (bytes.empty())
        {
            throw std::runtime_error("FBX is empty: " + std::string(sourceName));
        }

        ufbx_load_opts options{};
        options.target_axes = ufbx_axes_right_handed_z_up;
        options.target_unit_meters = 1.0f;
        options.generate_missing_normals = true;
        // 第一版只要几何：不读外部贴图/缓存文件，避免在没有贴图解析器时
        // 因为缺文件而整份加载失败
        options.load_external_files = false;
        options.filename.data = sourceName.data();
        options.filename.length = sourceName.size();

        ufbx_error error{};
        std::unique_ptr<ufbx_scene, FbxSceneDeleter> scene(
            ufbx_load_memory(bytes.data(), bytes.size(), &options, &error));
        if (scene == nullptr)
        {
            throw std::runtime_error("failed to load FBX '" + std::string(sourceName) +
                                     "': " + MakeFbxErrorMessage(error));
        }

        Doodle::DoodleFbxMeshData result;

        const glm::vec3 lightDirection = glm::normalize(glm::vec3(0.45f, 0.65f, 0.8f));

        for (std::size_t nodeIndex = 0; nodeIndex < scene->nodes.count; ++nodeIndex)
        {
            const ufbx_node& node = *scene->nodes.data[nodeIndex];
            if (node.mesh == nullptr)
            {
                continue;
            }

            const ufbx_mesh& mesh = *node.mesh;
            if (mesh.faces.count == 0)
            {
                continue;
            }

            if (!mesh.vertex_position.exists)
            {
                throw std::runtime_error("FBX mesh has no vertex positions: " +
                                         std::string(sourceName));
            }

            const glm::mat4 model = FbxMatrixToGlm(node.geometry_to_world);
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

            // ufbx_triangulate_face 输出的是 mesh 的 index 域（face.index_begin + k），
            // 不是顶点下标；后面还要经 vertex_position.indices 映射到属性值。
            // max_face_triangles * 3 是文档保证够用的上界
            const std::size_t maxTriangleIndices =
                std::max<std::size_t>(mesh.max_face_triangles * 3, 3);
            std::vector<uint32_t> triangleIndices(maxTriangleIndices);

            for (std::size_t faceIndex = 0; faceIndex < mesh.faces.count; ++faceIndex)
            {
                const ufbx_face face = mesh.faces.data[faceIndex];
                const uint32_t triangleCount = ufbx_triangulate_face(
                    triangleIndices.data(), triangleIndices.size(), &mesh, face);

                for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
                {
                    glm::vec3 positions[3];
                    glm::vec3 normals[3];

                    for (int corner = 0; corner < 3; ++corner)
                    {
                        const uint32_t index = triangleIndices[triangle * 3 + corner];

                        const glm::vec3 localPosition = FbxVec3ToGlm(
                            mesh.vertex_position.values.data[mesh.vertex_position.indices.data[index]]);
                        positions[corner] = glm::vec3(model * glm::vec4(localPosition, 1.0f));

                        if (mesh.vertex_normal.exists)
                        {
                            const glm::vec3 localNormal = FbxVec3ToGlm(
                                mesh.vertex_normal.values.data[mesh.vertex_normal.indices.data[index]]);
                            normals[corner] = normalMatrix * localNormal;
                        }
                        else
                        {
                            normals[corner] = glm::vec3(0.0f);
                        }
                    }

                    glm::vec3 faceNormal = glm::cross(
                        positions[1] - positions[0],
                        positions[2] - positions[0]);
                    const float faceNormalLength = glm::length(faceNormal);
                    if (faceNormalLength > 0.0f)
                    {
                        faceNormal /= faceNormalLength;
                    }

                    if (!mesh.vertex_normal.exists)
                    {
                        // 源文件没有法线：用面法线做平面着色
                        normals[0] = normals[1] = normals[2] = faceNormal;
                    }
                    else
                    {
                        // 法线经过非均匀缩放后可能不再单位长；长度异常时退回面法线，
                        // 避免 NaN 一路传进颜色
                        for (glm::vec3& normal : normals)
                        {
                            const float length = glm::length(normal);
                            normal = length > 0.0f ? normal / length : faceNormal;
                        }
                    }

                    // 让绕序与法线一致：法线朝外时三角形应为 CCW。
                    // 这一步兜住 FBX 源文件绕序与管线 frontFace 不一致的情况
                    if (glm::dot(faceNormal, normals[0] + normals[1] + normals[2]) < 0.0f)
                    {
                        std::swap(positions[1], positions[2]);
                        std::swap(normals[1], normals[2]);
                    }

                    for (int corner = 0; corner < 3; ++corner)
                    {
                        Doodle::Vertex vertex{};
                        vertex.pos = positions[corner];
                        vertex.color = ShadeFbxNormal(normals[corner], lightDirection);

                        result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
                        result.vertices.push_back(vertex);
                    }
                }
            }
        }

        if (result.vertices.empty() || result.indices.empty())
        {
            throw std::runtime_error("FBX contains no triangle geometry: " +
                                     std::string(sourceName));
        }

        return result;
    }
}

namespace Doodle
{
    DoodleFbxMeshData DoodleFbxImporter::Import(const std::span<const std::byte> bytes,
                                                const std::string_view sourceName)
    {
        return ImportFbxMesh(bytes, sourceName);
    }
}
