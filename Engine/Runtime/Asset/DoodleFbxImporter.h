//
//  DoodleFbxImporter.h
//  Doodle
//
//  Created by 郭智 on 2026/10/29.
//

#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "Rendering/DoodleSubmesh.h"
#include "Rendering/DoodleVertex.h"

namespace Doodle
{
    /**
     * 一份 FBX 拍平后的 CPU 结果
     *
     * 只描述数据，不是资产类型、不注册进 DoodleAssetManager。当前产出几何
     * 与材质分区：所有节点上的所有 mesh 实例都按世界变换烘焙进同一组顶点/索引，
     * 节点层级、纹理、骨骼与动画都不保留。
     *
     * 顶点是 position + normal + 第一套 UV。法线来自源文件（没有时按面法线
     * 补齐），UV 来自第一套纹理坐标；源文件缺哪一项就填「安全值」，不会因为
     * 缺一张贴图或一组 UV 而整份加载失败。DoodleMeshLoader 负责把这份数据
     * 上传成 DoodleMesh。
     *
     * 材质只保留「分区」不保留外观：导入器按源文件的材质把索引切成子网格，
     * 给每段分配一个稳定的 materialIndex；基色、纹理等属于 DoodleMeshRenderer
     * 的材质配置，不随网格资产走。这样同一份网格可以被不同对象配成不同材质。
     */
    struct DoodleFbxMeshData
    {
        /**
         * 拍平后的顶点；每个三角形三个独立顶点（不复用索引）
         *
         * 不复用是为了绕开 FBX 的法线/UV 分裂属性：同一个位置在不同面上
         * 可能有不同法线或 UV，逐角点复制最直接。第一版先接受这份冗余，
         * 等顶点格式与材质确定后再优化
         */
        std::vector<Vertex> vertices;

        /**
         * 顶点索引，与 vertices 一一对应（0, 1, 2, ...）
         */
        std::vector<uint32_t> indices;

        /**
         * 按材质切出的索引区间，指向 indices
         *
         * 导入时按 ufbx 的 material_parts 逐段顺序发射三角形，所以每段的索引在
         * indices 里连续，各段首尾相接覆盖整个数组。同一材质被多个节点实例
         * 使用时会产生多段共享同一个 materialIndex —— 区间按「节点 × 材质」切，
         * 不为了合并去重排顶点。
         *
         * materialIndex 从 0 开始按首次使用顺序分配，供 renderer 的材质表定位；
         * 网格本身不保存材质数据。
         */
        std::vector<DoodleSubmesh> submeshes;
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
        [[nodiscard]] static DoodleFbxMeshData Import(std::span<const std::byte> bytes, std::string_view sourceName);
    };
}
