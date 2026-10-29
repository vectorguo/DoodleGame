//
//  DoodleFbxImporter.h
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "Vulkan/DoodleVertex.h"

namespace Doodle
{
    /**
     * 一份 FBX 拍平后的 CPU 结果
     *
     * 只描述数据，不是资产类型、不注册进 DoodleAssetManager。第一版只产出
     * 几何：所有节点上的所有 mesh 实例都按世界变换烘焙进同一组顶点/索引，
     * 节点层级、材质、UV、骨骼与动画都不保留。
     *
     * 顶点仍用现有的 Vertex（position + color）；颜色是由法线和固定光向算出
     * 的临时明暗，用来在没有材质/光照管线时看清模型形状。DoodleMeshLoader
     * 负责把这份数据上传成 DoodleMesh。
     */
    struct DoodleFbxMeshData
    {
        /**
         * 拍平后的顶点；每个三角形三个独立顶点（不复用索引）
         *
         * 不复用是为了绕开 FBX 的法线/UV 分裂属性：同一个位置在不同面上
         * 可能有不同法线，逐角点复制最直接。第一版先接受这份冗余，
         * 等顶点格式与材质确定后再优化
         */
        std::vector<Vertex> vertices;

        /**
         * 顶点索引，与 vertices 一一对应（0, 1, 2, ...）
         */
        std::vector<uint32_t> indices;
    };

    /**
     * FBX 格式导入器
     *
     * 只做「FBX 字节 → DoodleFbxMeshData」：不读文件、不缓存、不创建 Vulkan
     * 资源、不碰资产管理器。文件来源、缓存与 GPU 上传归 DoodleMeshLoader。
     * 与 DoodleSceneImporter 同一层：格式解析和资产所有权分开，解析可以脱离
     * 设备测试，工作线程化时只搬 importer。
     *
     * ufbx 只在 .cpp 里出现；坐标与单位在加载时归一化为右手 Z-up、1 米。
     */
    class DoodleFbxImporter final
    {
    public:
        DoodleFbxImporter() = delete;

        /**
         * 解析一份 FBX 字节并拍平成单个 mesh
         *
         * @param bytes 完整 FBX 文件内容
         * @param sourceName 只用于错误消息，通常是资产 key / 路径
         * @return 已经归一化到 Z-up、米制的顶点与索引
         * @throw std::runtime_error 文件为空、解析失败、没有三角形几何
         */
        [[nodiscard]] static DoodleFbxMeshData Import(std::span<const std::byte> bytes,
                                                      std::string_view sourceName);
    };
}
